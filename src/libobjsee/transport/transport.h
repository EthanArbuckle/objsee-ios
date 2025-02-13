//
//  transport.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include "tracer_internal.h"
#include <pthread.h>

typedef struct {
    char *data;
    size_t length;
} queued_message_t;


typedef struct {
    union {
        int fd;
        void *custom_handle;
    };
    
    struct {
        queued_message_t *messages;
        size_t capacity;
        size_t count;
        pthread_mutex_t lock;
        pthread_cond_t not_full;
        pthread_cond_t not_empty;
    } queue;
    
    bool running;
    pthread_t transport_thread;
    
    tracer_transport_type_t type;
    pthread_mutex_t write_lock;
} transport_context_t;

tracer_result_t transport_init(tracer_t *tracer, const tracer_transport_config_t *config);
tracer_result_t transport_send(tracer_t *tracer, const void *data, size_t length);
