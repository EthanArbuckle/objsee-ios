//
//  transport.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "tracer_internal.h"
#include "transport.h"
#include <os/log.h>

#define MAX_RETRIES 3
#define RETRY_BASE_DELAY_MS 100

static void *transport_thread(void *tracer_arg) {
    tracer_t *tracer = (tracer_t *)tracer_arg;
    transport_context_t *ctx = (transport_context_t *)tracer->transport_context;
    
    while (ctx->running) {
        pthread_mutex_lock(&ctx->queue.lock);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1;
        
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
        queued_message_t msg = ctx->queue.messages[0];
        memmove(&ctx->queue.messages[0], &ctx->queue.messages[1], (ctx->queue.count - 1) * sizeof(queued_message_t));
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
                free(msg.data);
                return NULL;
            }
        }
        
        free(send_buffer);
        free(msg.data);
    }
    
    return NULL;
}

static tracer_result_t init_socket_transport(tracer_t *tracer, const tracer_transport_config_t *config) {
    bool connected = false;
    int sockfd = -1;
    
    for (int i = 0; i < MAX_RETRIES; i++) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            os_log(OS_LOG_DEFAULT, "Failed to create socket on attempt %d", i + 1);
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
            os_log(OS_LOG_DEFAULT, "Connection attempt %d failed: %s", i + 1, strerror(errno));
            close(sockfd);
            usleep((RETRY_BASE_DELAY_MS * 1000) * (1 << i));
            continue;
        }
        
        connected = true;
        if (i > 0) {
            os_log(OS_LOG_DEFAULT, "Successfully connected on attempt %d", i + 1);
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
    if (pthread_mutex_init(&ctx->write_lock, NULL) != 0) {
        os_log(OS_LOG_DEFAULT, "Failed to create write lock");
        free(ctx);
        return TRACER_ERROR_INITIALIZATION;
    }
    
    ctx->type = tracer->config.transport;
    ctx->queue.capacity = 10000;
    ctx->queue.messages = calloc(ctx->queue.capacity, sizeof(queued_message_t));
    if (ctx->queue.messages == NULL) {
        pthread_mutex_destroy(&ctx->write_lock);
        free(ctx);
        return TRACER_ERROR_MEMORY;
    }

    pthread_mutex_init(&ctx->queue.lock, NULL);
    pthread_cond_init(&ctx->queue.not_full, NULL);
    pthread_cond_init(&ctx->queue.not_empty, NULL);
    
    ctx->running = true;
    int thread_err = pthread_create(&ctx->transport_thread, NULL, transport_thread, tracer);
    if (thread_err != 0) {
        os_log(OS_LOG_DEFAULT, "Failed to create transport thread: %s", strerror(thread_err));
        free(ctx->queue.messages);
        free(ctx);
        return TRACER_ERROR_INITIALIZATION;
    }
    pthread_detach(ctx->transport_thread);
    
    tracer_result_t result;
    switch (ctx->type) {
        case TRACER_TRANSPORT_SOCKET: {
            result = init_socket_transport(tracer, config);
            break;
        }
        case TRACER_TRANSPORT_STDOUT:
        case TRACER_TRANSPORT_FILE:
            result = init_file_transport(tracer, config);
            break;
        case TRACER_TRANSPORT_CUSTOM: {
            ctx->custom_handle = config->custom_context;
            result = TRACER_SUCCESS;
            break;
        }
        default:
            result = TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (result != TRACER_SUCCESS) {
        pthread_mutex_destroy(&ctx->write_lock);
        free(ctx->queue.messages);
        free(ctx);
        return result;
    }
    
    return TRACER_SUCCESS;
}

tracer_result_t transport_send(tracer_t *tracer, const void *data, size_t length) {
    transport_context_t *ctx = tracer->transport_context;
    if (ctx == NULL || data == NULL || length == 0) {
        return TRACER_ERROR_INITIALIZATION;
    }
    
    pthread_mutex_lock(&ctx->write_lock);
    tracer_result_t result = TRACER_SUCCESS;
    
    switch (ctx->type) {
        case TRACER_TRANSPORT_SOCKET:
        case TRACER_TRANSPORT_FILE: {
            char *data_copy = malloc(length);
            if (data_copy == NULL) {
                
                pthread_mutex_unlock(&ctx->write_lock);
                tracer_set_error(tracer, "Failed to allocate memory");
                return TRACER_ERROR_MEMORY;
            }
            memcpy(data_copy, data, length);
            
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 2;
            pthread_mutex_lock(&ctx->queue.lock);
            
            while (ctx->queue.count >= ctx->queue.capacity) {
                int rc = pthread_cond_timedwait(&ctx->queue.not_full, &ctx->queue.lock, &timeout);
                if (rc == ETIMEDOUT) {
                    pthread_mutex_unlock(&ctx->queue.lock);
                    pthread_mutex_unlock(&ctx->write_lock);
                    free(data_copy);
                    return TRACER_ERROR_TIMEOUT;
                }
            }
            
            ctx->queue.messages[ctx->queue.count++] = (queued_message_t){
                .data = data_copy,
                .length = length
            };
            
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
                os_log(OS_LOG_DEFAULT, "%s", (const char *)data);
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&ctx->write_lock);
    return result;
}
