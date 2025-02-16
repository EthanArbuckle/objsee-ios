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

typedef struct {
    char **buffers;
    bool *in_use;
    size_t size;
    size_t capacity;
    pthread_mutex_t lock;
} event_buffer_pool_t;

static event_buffer_pool_t *buffer_pool = NULL;

static event_buffer_pool_t *get_buffer_pool(void) {
    if (buffer_pool == NULL) {
        buffer_pool = calloc(1, sizeof(event_buffer_pool_t));
        if (buffer_pool == NULL) {
            return NULL;
        }
        
        buffer_pool->capacity = 2048;
        buffer_pool->buffers = calloc(buffer_pool->capacity, sizeof(char *));
        buffer_pool->in_use = calloc(buffer_pool->capacity, sizeof(bool));
        
        if (buffer_pool->buffers == NULL || !buffer_pool->in_use) {
            free(buffer_pool->buffers);
            free(buffer_pool->in_use);
            free(buffer_pool);
            buffer_pool = NULL;
            return NULL;
        }
        
        pthread_mutex_init(&buffer_pool->lock, NULL);
    }

    return buffer_pool;
}

static char *get_buffer_from_pool(void) {
    event_buffer_pool_t *pool = get_buffer_pool();
    if (pool == NULL) {
        return NULL;
    }
    
    pthread_mutex_lock(&pool->lock);
    
    char *buffer = NULL;
    for (size_t i = 0; i < pool->size; i++) {
        if (!pool->in_use[i]) {
            pool->in_use[i] = true;
            buffer = pool->buffers[i];
            break;
        }
    }
    
    if (buffer == NULL && pool->size < pool->capacity) {
        buffer = malloc(1024 * 4);
        if (buffer) {
            pool->buffers[pool->size] = buffer;
            pool->in_use[pool->size] = true;
            pool->size++;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);
    return buffer;
}

static void return_buffer_to_pool(char *buffer) {
    event_buffer_pool_t *pool = get_buffer_pool();
    if (pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&pool->lock);
    
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->buffers[i] == buffer) {
            pool->in_use[i] = false;
            break;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);
}

void tracer_handle_event(tracer_t *tracer, tracer_event_t *event) {
    if (tracer == NULL) {
        return;
    }
    
    if (event == NULL) {
        tracer_set_error(tracer, "Event is NULL");
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
    
    const char *event_transport_output = NULL;
    if (!format.include_event_json && format.include_formatted_trace && !format.output_as_json) {
        // Json is disabled, formatted trace is enabled.
        // Build the string then write it directly to the transport
        event_transport_output = build_formatted_event_str(event, format);
        if (event_transport_output == NULL) {
            tracer_set_error(tracer, "Failed to build formatted string for an event");
            return;
        }
        
        event->formatted_output = strdup(event_transport_output);
    }
    else if (format.output_as_json) {
        // Json is enabled. Build the json string for the event, then write it to the transport.
        // It may include a formatted string field depending on format options
        WHILE_IGNORING_SIGNALS({
            event_transport_output = build_json_event_str(tracer, event);
        });
        
        if (event_transport_output == NULL) {
            tracer_set_error(tracer, "Failed to build json string for an event");
            return;
        }
    }
    
    if (event_transport_output == NULL) {
        tracer_set_error(tracer, "Failed to build event output. No data to send to transport");
        return;
    }
    
    char *buffer = get_buffer_from_pool();
    if (buffer == NULL) {
        tracer_set_error(tracer, "Event buffer pool exhausted");
        return;
    }
    
    size_t output_len = strlen(event_transport_output);
    if (output_len > 0 && event_transport_output[output_len - 1] != '\n') {
        snprintf(buffer, 4096, "%s\n", event_transport_output);
    }
    else {
        strncpy(buffer, event_transport_output, 4096);
    }
    
    free((void *)event_transport_output);
    
    transport_send(tracer, buffer, output_len + 1);
    return_buffer_to_pool(buffer);
}

void cleanup_event_handler(void) {
    if (buffer_pool == NULL) {
        return;
    }
    
    pthread_mutex_lock(&buffer_pool->lock);
    if (buffer_pool->buffers) {
        for (size_t i = 0; i < buffer_pool->size; i++) {
            free(buffer_pool->buffers[i]);
        }
        free(buffer_pool->buffers);
    }
    
    if (buffer_pool->in_use) {
        free(buffer_pool->in_use);
    }
    
    pthread_mutex_unlock(&buffer_pool->lock);
    pthread_mutex_destroy(&buffer_pool->lock);
    free(buffer_pool);
    buffer_pool = NULL;
}

tracer_result_t init_event_handler(tracer_t *tracer) {
    event_buffer_pool_t *pool = get_buffer_pool();
    if (pool == NULL) {
        tracer_set_error(tracer, "Failed to initialize event buffer pool");
        return TRACER_ERROR_INITIALIZATION;
    }

    return TRACER_SUCCESS;
}
