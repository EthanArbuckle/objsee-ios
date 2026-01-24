//
//  transport.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <sys/socket.h>
#include <sys/param.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include "tracer_internal.h"
#include "transport.h"
#include "logging.h"

#define MAX_RETRIES 3
#define RETRY_BASE_DELAY_MS 100

static void make_abs_timespec_from_now(struct timespec *ts, int seconds_from_now) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    ts->tv_sec = tv.tv_sec + seconds_from_now;
    ts->tv_nsec = (long)tv.tv_usec * 1000L;
}

static void *transport_thread(void *tracer_arg) {
    tracer_t *tracer = (tracer_t *)tracer_arg;
    transport_context_t *ctx = (transport_context_t *)tracer->transport_context;
    
    while (ctx->running) {
        
        struct timespec timeout;
        make_abs_timespec_from_now(&timeout, 1);
        
        pthread_mutex_lock(&ctx->queue.lock);

        while (ctx->queue.count == 0 && ctx->running) {
            int rc = pthread_cond_timedwait(&ctx->queue.not_empty, &ctx->queue.lock, &timeout);
            if (rc == ETIMEDOUT) {
                break;
            }
        }
        
        if (!ctx->running) {
            pthread_mutex_unlock(&ctx->queue.lock);
            break;
        }
        
        // No messages to process
        if (ctx->queue.count == 0) {
            pthread_mutex_unlock(&ctx->queue.lock);
            continue;
        }
        
        // Get next message
        size_t idx = ctx->queue.head;
        queued_message_t msg = ctx->queue.messages[idx];
        if (msg.is_inline) {
            msg.data = msg.inline_data;
        }
        memset(&ctx->queue.messages[idx], 0, sizeof(queued_message_t));

        ctx->queue.head = (ctx->queue.head + 1) % ctx->queue.capacity;
        ctx->queue.count--;

        pthread_cond_signal(&ctx->queue.not_full);
        pthread_mutex_unlock(&ctx->queue.lock);
        
        char *send_buffer = NULL;
        size_t send_length = msg.length;
        if (msg.length > 0 && msg.data[msg.length - 1] != '\n') {
            send_buffer = malloc(msg.length + 1);
            if (send_buffer) {
                memcpy(send_buffer, msg.data, msg.length);
                send_buffer[msg.length] = '\n';
                send_length = msg.length + 1;
            }
        }
        
        size_t total_sent = 0;
        int retry_count = 0;
        int consecutive_wouldblock = 0;
        while (total_sent < send_length) {
            
            const void *curr_buf = send_buffer ? send_buffer : msg.data;
            ssize_t sent = send(ctx->fd, curr_buf + total_sent, send_length - total_sent, 0);
            if (sent > 0) {
                total_sent += sent;
                consecutive_wouldblock = 0;
                continue;
            }
            
            if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    consecutive_wouldblock++;
                    
                    // If buffer is consistently full, increase backoff
                    unsigned int sleep_ms = RETRY_BASE_DELAY_MS * (1 << MIN(consecutive_wouldblock, 6));
                    usleep(sleep_ms * 1000);
                    
                    if (consecutive_wouldblock > 3) {
                        retry_count++;
                    }
                    
                    if (retry_count >= MAX_RETRIES) {
                        tracer_set_error(tracer, "Max retries exceeded while sending");
                        break;
                    }
                    continue;
                }
                
                tracer_set_error(tracer, "Send failed: %s", strerror(errno));
                
                free(send_buffer);
                if (!msg.is_inline) {
                    free(msg.data);
                }
                return NULL;
            }
        }
        
        free(send_buffer);
        if (!msg.is_inline) {
            free(msg.data);
        }
    }
    
    return NULL;
}

static tracer_result_t init_socket_transport(tracer_t *tracer, const tracer_transport_config_t *config) {
    bool connected = false;
    int sockfd = -1;
    
    for (int i = 0; i < MAX_RETRIES; i++) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            objsee_log("Failed to create socket on attempt %d", i + 1);
            continue;
        }
        
        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(config->port),
        };
        
        if (inet_pton(AF_INET, config->host, &server_addr.sin_addr) <= 0) {
            close(sockfd);
            tracer_set_error(tracer, "Invalid address");
            return TRACER_ERROR_INITIALIZATION;
        }
                
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            objsee_log("Connection attempt %d failed: %s (host: %s, port: %d)", i + 1, strerror(errno), config->host, config->port);
            close(sockfd);
            sleep(1);
            continue;
        }
        
        connected = true;
        if (i > 0) {
            objsee_log("Successfully connected on attempt %d", i + 1);
        }
        break;
    }
    
    if (!connected) {
        if (sockfd >= 0) {
            close(sockfd);
        }
        tracer_set_error(tracer, "Failed to connect after %d attempts", MAX_RETRIES);
        return TRACER_ERROR_INITIALIZATION;
    }
    
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    transport_context_t *ctx = tracer->transport_context;
    ctx->fd = sockfd;
    return TRACER_SUCCESS;
}

static tracer_result_t init_file_transport(tracer_t *tracer, const tracer_transport_config_t *config) {
    transport_context_t *ctx = tracer->transport_context;
    if (ctx->type == TRACER_TRANSPORT_STDOUT || config->file_path == NULL) {
        ctx->fd = STDOUT_FILENO;
        return TRACER_SUCCESS;
    }

    int fd = open(config->file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        tracer_set_error(tracer, "Failed to open output file");
        return TRACER_ERROR_INITIALIZATION;
    }
    
    ctx->fd = fd;
    return TRACER_SUCCESS;
}

tracer_result_t transport_init(tracer_t *tracer, const tracer_transport_config_t *config) {
    if (tracer == NULL || config == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (tracer->transport_context != NULL) {
        return TRACER_ERROR_INITIALIZATION;
    }
    
    tracer->transport_context = calloc(1, sizeof(transport_context_t));
    if (tracer->transport_context  == NULL) {
        return TRACER_ERROR_MEMORY;
    }
    
    transport_context_t *ctx = tracer->transport_context;
    ctx->type = tracer->config.transport;
    ctx->fd = -1;
    
    if (pthread_mutex_init(&ctx->queue.lock, NULL) != 0) {
        objsee_log("Failed to create queue.lock");
        free(ctx);
        tracer->transport_context = NULL;
        return TRACER_ERROR_INITIALIZATION;
    }
    
    if (pthread_cond_init(&ctx->queue.not_empty, NULL) != 0) {
        objsee_log("Failed to create queue.not_empty");
        pthread_mutex_destroy(&ctx->queue.lock);
        free(ctx);
        tracer->transport_context = NULL;
        return TRACER_ERROR_INITIALIZATION;
    }
    
    if (pthread_cond_init(&ctx->queue.not_full, NULL) != 0) {
        objsee_log("Failed to create queue.not_full");
        pthread_cond_destroy(&ctx->queue.not_empty);
        pthread_mutex_destroy(&ctx->queue.lock);
        free(ctx);
        tracer->transport_context = NULL;
        return TRACER_ERROR_INITIALIZATION;
    }
    
    ctx->queue.capacity = 60000;
    ctx->queue.messages = calloc(ctx->queue.capacity, sizeof(queued_message_t));
    if (ctx->queue.messages == NULL) {
        pthread_cond_destroy(&ctx->queue.not_full);
        pthread_cond_destroy(&ctx->queue.not_empty);
        pthread_mutex_destroy(&ctx->queue.lock);
        free(ctx);
        tracer->transport_context = NULL;
        return TRACER_ERROR_MEMORY;
    }
    ctx->queue.head = 0;
    ctx->queue.tail = 0;
    
    ctx->running = true;
    int thread_err = pthread_create(&ctx->transport_thread, NULL, transport_thread, tracer);
    if (thread_err != 0) {
        objsee_log("Failed to create transport thread: %s", strerror(thread_err));
        free(ctx->queue.messages);
        pthread_cond_destroy(&ctx->queue.not_full);
        pthread_cond_destroy(&ctx->queue.not_empty);
        pthread_mutex_destroy(&ctx->queue.lock);
        free(ctx);
         tracer->transport_context = NULL;
         return TRACER_ERROR_INITIALIZATION;
    }
    
    tracer_result_t result;
    switch (ctx->type) {
        case TRACER_TRANSPORT_SOCKET: {
            if (config->host == NULL || config->port == 0) {
                result = TRACER_ERROR_INVALID_ARGUMENT;
                break;
            }
    
            result = init_socket_transport(tracer, config);
            break;
        }
        case TRACER_TRANSPORT_STDOUT: {
            ctx->fd = STDOUT_FILENO;
            result = TRACER_SUCCESS;
            break;
        }
        case TRACER_TRANSPORT_FILE: {
            result = init_file_transport(tracer, config);
            break;
        }
        case TRACER_TRANSPORT_CUSTOM: {
            ctx->custom_handle = config->custom_context;
            result = TRACER_SUCCESS;
            break;
        }
        default:
            result = TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (result != TRACER_SUCCESS) {
        free(ctx);
        tracer->transport_context = NULL;
        return result;
    }
    
    pthread_detach(ctx->transport_thread);
    return TRACER_SUCCESS;
}

tracer_result_t transport_send(tracer_t *tracer, const void *data, size_t length) {
    transport_context_t *ctx = tracer->transport_context;
    if (ctx == NULL || data == NULL || length == 0) {
        return TRACER_ERROR_INITIALIZATION;
    }
    
    tracer_result_t result = TRACER_SUCCESS;
    
    switch (ctx->type) {
        case TRACER_TRANSPORT_SOCKET:
        case TRACER_TRANSPORT_FILE: {
            
            pthread_mutex_lock(&ctx->queue.lock);

            struct timespec timeout;
            make_abs_timespec_from_now(&timeout, 2);

            while (ctx->queue.count >= ctx->queue.capacity) {
                int rc = pthread_cond_timedwait(&ctx->queue.not_full, &ctx->queue.lock, &timeout);
                if (rc == ETIMEDOUT) {
                    pthread_mutex_unlock(&ctx->queue.lock);
                    return TRACER_ERROR_TIMEOUT;
                }
            }

            queued_message_t *msg = &ctx->queue.messages[ctx->queue.tail];

            msg->length = length;
            
            if (length > sizeof(msg->inline_data)) {
                msg->data = malloc(length);
                if (msg->data == NULL) {
                    pthread_mutex_unlock(&ctx->queue.lock);
                    return TRACER_ERROR_MEMORY;
                }
                memcpy(msg->data, data, length);
                msg->is_inline = false;
            } else {
                msg->is_inline = true;
                memcpy(msg->inline_data, data, length);
                msg->data = msg->inline_data;
            }

            ctx->queue.tail = (ctx->queue.tail + 1) % ctx->queue.capacity;
            ctx->queue.count++;

            pthread_cond_signal(&ctx->queue.not_empty);
            pthread_mutex_unlock(&ctx->queue.lock);
            break;
        }
        case TRACER_TRANSPORT_CUSTOM: {
            if (tracer->config.event_handler) {
                tracer->config.event_handler(data, tracer->config.event_handler_context);
            }
            break;
        }
        
        case TRACER_TRANSPORT_STDOUT: {
            if (tracer->transport_context) {
                transport_context_t *transport = tracer->transport_context;
                write(transport->fd, data, length);
                objsee_log("%s", (const char *)data);
            }
            break;
        }
    }
    
    return result;
}
