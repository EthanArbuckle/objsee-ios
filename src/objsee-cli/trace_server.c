//
//  trace_server.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#include <CoreFoundation/CoreFoundation.h>
#include <json-c/json_tokener.h>
#include <netinet/in.h>
#include "format.h"

// Max time to wait for a client (the process being traced) to connect
#define ACCEPT_TIMEOUT_SECONDS 20

static volatile int running = 1;
static int server_fd = -1;
static int client_fd = -1;

static void handle_signal(int sig) {
    running = 0;
}

static void print_json_event_formatted_output(const char *json_str, int len) {
    struct json_tokener *tokener = json_tokener_new();
    if (tokener == NULL) {
        printf("Failed to create JSON tokener\n");
        return;
    }
    
    json_object *trace = json_tokener_parse_ex(tokener, json_str, len);
    enum json_tokener_error jerr = json_tokener_get_error(tokener);
    if (jerr != json_tokener_success) {
        if (jerr != json_tokener_continue) {
            printf("Failed to parse JSON: %s\n%s\n", json_tokener_error_desc(jerr), json_str);
        }
        
        json_tokener_free(tokener);
        return;
    }

    json_object *formatted_obj;
    if (json_object_object_get_ex(trace, "formatted_output", &formatted_obj)) {
        const char *formatted = json_object_get_string(formatted_obj);
        printf("%s\n", formatted);
    }
    else {
        // Fall back to printing the entire JSON object
        printf("%s\n", json_str);
    }
    
    json_object_put(trace);
    json_tokener_free(tokener);
}

static bool pid_exists(pid_t pid) {
    if (pid == 0) {
        return false;
    }
    
    if ((kill(pid, 0) == -1) && (errno == ESRCH)) {
        return false;
    }
    else {
        return true;
    }
}

static int setup_socket(tracer_transport_config_t config) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("Socket creation failed\n");
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("setsockopt(SO_REUSEADDR) failed\n");
        close(fd);
        return -1;
    }
    
    int buffer_size = 1024 * 1024 * 2;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) < 0) {
        printf("setsockopt(SO_RCVBUF) failed\n");
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) < 0) {
        printf("setsockopt(SO_SNDBUF) failed\n");
    }
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(config.port),
    };
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("Bind failed\n");
        close(fd);
        return -1;
    }
    
    if (listen(fd, 0) < 0) {
        printf("Listen failed\n");
        close(fd);
        return -1;
    }
    
    return fd;
}

int run_trace_server(tracer_config_t *config, pid_t traced_pid) {
    setbuf(stdout, NULL);
    
    struct sigaction sa = {
        .sa_handler = handle_signal,
        .sa_flags = 0,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    server_fd = setup_socket(config->transport_config);
    if (server_fd < 0) {
        return 1;
    }
    
    // Accept client connection
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    time_t start_time = time(NULL);
    while (pid_exists(traced_pid) && (time(NULL) - start_time) < ACCEPT_TIMEOUT_SECONDS) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd >= 0) {
            printf("Client connected successfully\n");
            break;
        }
        
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("Accept failed with error: %s\n", strerror(errno));
            break;
        }
        usleep(10000);
    }
    
    if (client_fd < 0) {
        if (!pid_exists(traced_pid)) {
            printf("Target process %d terminated before connection could be established\n", traced_pid);
        }
        else {
            printf("Target process %d is running but a connection could not be established\n", traced_pid);
        }
        close(server_fd);
        return 1;
    }
    
    // Set non-blocking mode
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    char buffer[8192] = {0};
    size_t buffer_pos = 0;

    while (running && pid_exists(traced_pid)) {

        ssize_t bytes_read = recv(client_fd, buffer + buffer_pos, sizeof(buffer) - buffer_pos - 1, 0);
        if (bytes_read > 0) {
            
            buffer_pos += bytes_read;
            buffer[buffer_pos] = '\0';
            
            char *json_start = buffer;
            char *json_end = buffer;
            while ((json_end = strchr(json_start, '\n')) != NULL) {
                size_t json_len = json_end - json_start;
                if (json_len > 0) {
                    *json_end = '\0';
                    print_json_event_formatted_output(json_start, (int)json_len);
                    *json_end = '\n';
                }
                json_start = json_end + 1;
            }
            
            size_t remaining = buffer + buffer_pos - json_start;
            if (remaining > 0 && remaining < sizeof(buffer)) {
                memmove(buffer, json_start, remaining);
                buffer_pos = remaining;
            }
            else {
                buffer_pos = 0;
            }
        }
        else if (bytes_read == 0) {
            printf("Traced process disconnected\n");
            break;
        }
        else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("recv failed\n");
            break;
        }
        
        usleep(1000);
    }
    
    if (client_fd >= 0) {
        close(client_fd);
    }

    if (server_fd >= 0) {
        close(server_fd);
    }
    
    return 0;
}
