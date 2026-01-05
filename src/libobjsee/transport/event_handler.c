//
//  event_handler.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include "tracer_internal.h"
#include "signal_guard.h"
#include "transport.h"
#include "format.h"
#include "tracer.h"

#define EVENT_BUFFER_SIZE 2048
#define POOL_CAPACITY 64

typedef struct {
    char **buffers;
    size_t *free_stack;
    size_t free_count;
    size_t total_allocated;
    pthread_mutex_t lock;
} event_buffer_pool_t;

static event_buffer_pool_t *buffer_pool = NULL;
static _Thread_local char *tls_buffer = NULL;
static _Thread_local bool tls_buffer_in_use = false;

static event_buffer_pool_t *get_buffer_pool(void) {
    if (buffer_pool != NULL) {
        return buffer_pool;
    }

    event_buffer_pool_t *pool = calloc(1, sizeof(event_buffer_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->buffers = calloc(POOL_CAPACITY, sizeof(char *));
    pool->free_stack = calloc(POOL_CAPACITY, sizeof(size_t));
    if (pool->buffers == NULL || pool->free_stack == NULL) {
        free(pool->buffers);
        free(pool->free_stack);
        free(pool);
        return NULL;
    }

    pthread_mutex_init(&pool->lock, NULL);
    buffer_pool = pool;
    return pool;
}

static char *get_buffer_from_pool(void) {
    if (tls_buffer != NULL && !tls_buffer_in_use) {
        tls_buffer_in_use = true;
        return tls_buffer;
    }

    event_buffer_pool_t *pool = get_buffer_pool();
    if (pool == NULL) {
        return NULL;
    }
    
    char *buffer = NULL;
    pthread_mutex_lock(&pool->lock);

    if (pool->free_count > 0) {
        pool->free_count--;
        size_t idx = pool->free_stack[pool->free_count];
        buffer = pool->buffers[idx];
    }
    else if (pool->total_allocated < POOL_CAPACITY) {
        buffer = malloc(EVENT_BUFFER_SIZE);
        if (buffer != NULL) {
            pool->buffers[pool->total_allocated] = buffer;
            pool->total_allocated++;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);

    if (buffer != NULL && tls_buffer == NULL) {
        tls_buffer = buffer;
        tls_buffer_in_use = true;
    }

    return buffer;
}

static void return_buffer_to_pool(char *buffer) {
    if (buffer == tls_buffer) {
        tls_buffer_in_use = false;
        return;
    }

    event_buffer_pool_t *pool = get_buffer_pool();
    if (pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&pool->lock);

    for (size_t i = 0; i < pool->total_allocated; i++) {
        if (pool->buffers[i] == buffer) {
            pool->free_stack[pool->free_count] = i;
            pool->free_count++;
            break;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);
}

void tracer_handle_event(tracer_t *tracer, tracer_event_t *event) {
    if (tracer == NULL || event == NULL) {
        return;
    }
        
    tracer_thread_context_t *thread_ctx = tracer_get_thread_context(tracer);
    if (thread_ctx == NULL) {
        tracer_set_error(tracer, "Failed to get thread context");
        return;
    }
    
    if (tracer->config.transport == TRACER_TRANSPORT_CUSTOM && tracer->config.event_handler) {
        tracer->config.event_handler(event, tracer->config.event_handler_context);
        return;
    }
    
    tracer_format_options_t format = tracer->config.format;
    if (format.include_event_json && format.include_formatted_trace && !format.output_as_json) {
        // Including both formatted trace and event data is only supported with json output format
        tracer_set_error(tracer, "Cannot include both formatted trace and event data without json output format");
        format.include_formatted_trace = false;
    }

    const char *event_output = NULL;
    if (!format.include_event_json && format.include_formatted_trace && !format.output_as_json) {
        // Json is disabled, formatted trace is enabled.
        // Build the string then write it directly to the transport
        event_output = build_formatted_event_str(event, format);
        if (event_output == NULL) {
            tracer_set_error(tracer, "Failed to build formatted string for an event");
            return;
        }
        event->formatted_output = strdup(event_output);
    }
    else if (format.output_as_json) {
        // Json is enabled. Build the json string for the event, then write it to the transport.
        // It may include a formatted string field depending on format options
        WHILE_IGNORING_SIGNALS({
            event_output = build_json_event_str(tracer, event);
        });

        if (event_output == NULL) {
            tracer_set_error(tracer, "Failed to build json string for an event");
            return;
        }
    }

    if (event_output == NULL) {
        tracer_set_error(tracer, "Failed to build event output. No data to send to transport");
        return;
    }

    size_t output_len = strlen(event_output);
    bool needs_newline = (output_len > 0 && event_output[output_len - 1] != '\n');
    size_t send_len = output_len + (needs_newline ? 1 : 0);

    char *buffer = NULL;
    if (send_len < EVENT_BUFFER_SIZE) {
        buffer = get_buffer_from_pool();
    }

    if (buffer != NULL) {
        memcpy(buffer, event_output, output_len);
        if (needs_newline) {
            buffer[output_len] = '\n';
        }
        buffer[send_len] = '\0';
        free((void *)event_output);
        transport_send(tracer, buffer, send_len);
        return_buffer_to_pool(buffer);
    }
    else {
        transport_send(tracer, event_output, output_len);
        if (needs_newline) {
            transport_send(tracer, "\n", 1);
        }
        free((void *)event_output);
    }
}

void cleanup_event_handler(void) {
    if (buffer_pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&buffer_pool->lock);

    for (size_t i = 0; i < buffer_pool->total_allocated; i++) {
        free(buffer_pool->buffers[i]);
    }
    free(buffer_pool->buffers);
    free(buffer_pool->free_stack);

    pthread_mutex_unlock(&buffer_pool->lock);
    pthread_mutex_destroy(&buffer_pool->lock);
    free(buffer_pool);
    buffer_pool = NULL;
    tls_buffer = NULL;
}

tracer_result_t init_event_handler(tracer_t *tracer) {
    event_buffer_pool_t *pool = get_buffer_pool();
    if (pool == NULL) {
        tracer_set_error(tracer, "Failed to initialize event buffer pool");
        return TRACER_ERROR_INITIALIZATION;
    }

    return TRACER_SUCCESS;
}