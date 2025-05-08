//
//  tracer_core.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <os/log.h>
#include <arm_neon.h>
#include "tracer_internal.h"

typedef struct {
    Class isa;
} _nsobject;

extern uint64_t objc_debug_isa_magic_mask;
extern uint64_t objc_debug_isa_magic_value;

static void tracer_thread_destructor(void *ctx) {
    if (ctx) {
        free(ctx);
    }
}

tracer_result_t tracer_context_init(tracer_t *tracer) {
    if (tracer == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    tracer_context_t *internal_ctx = (tracer_context_t *)tracer;
    if (internal_ctx->initialized || internal_ctx->running) {
        tracer_set_error(tracer, "Tracer already initialized");
        return TRACER_ERROR_ALREADY_INITIALIZED;
    }
    
    if (pthread_rwlock_init(&internal_ctx->filter_lock, NULL) != 0) {
        tracer_set_error(tracer, "Failed to initialize filter lock");
        return TRACER_ERROR_INITIALIZATION;
    }
    
    if (pthread_mutex_init(&internal_ctx->transport_lock, NULL) != 0) {
        tracer_set_error(tracer, "Failed to initialize transport lock");
        pthread_rwlock_destroy(&internal_ctx->filter_lock);
        return TRACER_ERROR_INITIALIZATION;
    }
    
    if (pthread_mutex_init(&internal_ctx->error_lock, NULL) != 0) {
        tracer_set_error(tracer, "Failed to initialize error lock");
        pthread_mutex_destroy(&internal_ctx->transport_lock);
        pthread_rwlock_destroy(&internal_ctx->filter_lock);
        return TRACER_ERROR_INITIALIZATION;
    }
    
    if (pthread_key_create(&internal_ctx->thread_key, tracer_thread_destructor) != 0) {
        tracer_set_error(tracer, "Failed to create thread key");
        pthread_mutex_destroy(&internal_ctx->error_lock);
        pthread_mutex_destroy(&internal_ctx->transport_lock);
        pthread_rwlock_destroy(&internal_ctx->filter_lock);
        return TRACER_ERROR_INITIALIZATION;
    }
        
    internal_ctx->initialized = true;
    return TRACER_SUCCESS;
}

tracer_thread_context_t *tracer_get_thread_context(tracer_t *tracer) {
    if (tracer == NULL ) {
        return NULL;
    }
    
    if (!tracer->initialized) {
        tracer_set_error(tracer, "Cannot get thread context: tracer not initialized");
        return NULL;
    }
    
    tracer_thread_context_t *ctx = pthread_getspecific(tracer->thread_key);
    if (ctx == NULL) {
        ctx = calloc(1, sizeof(tracer_thread_context_t));
        if (ctx == NULL) {
            tracer_set_error(tracer, "Failed to allocate thread context");
            return NULL;
        }
        
        uint64_t thread_id;
        pthread_threadid_np(NULL, &thread_id);
        ctx->thread_id = (uint16_t)(thread_id ^ (thread_id >> 32));
        
        pthread_setspecific(tracer->thread_key, ctx);
    }
    return ctx;
}

void tracer_set_error(tracer_t *tracer, const char *format, ...) {
    if (tracer == NULL || format == NULL) {
        return;
    }

    pthread_mutex_lock(&tracer->error_lock);
    
    va_list args;
    va_start(args, format);
    vsnprintf(tracer->last_error, sizeof(tracer->last_error), format, args);
    va_end(args);
    
    printf("Error: %s\n", tracer->last_error);
    os_log(OS_LOG_DEFAULT, "Error:  %s", tracer->last_error);
    pthread_mutex_unlock(&tracer->error_lock);
}

static bool match_wildcard(const char *pattern, const char *str) {
    if (pattern == NULL || str == NULL) {
        return false;
    }

    if (!*pattern || strcmp(pattern, "*") == 0) {
        return true;
    }
    
    const char *str_ptr = str;
    const char *pat_ptr = pattern;
    const char *str_star = NULL;
    const char *pat_star = NULL;
    
    while (*str_ptr) {
        if (*pat_ptr == '*') {
            // Wildcard - remember position
            pat_star = pat_ptr++;
            str_star = str_ptr;
        }
        else if (*pat_ptr == *str_ptr) {
            // Matching character - advance both
            pat_ptr++;
            str_ptr++;
        }
        else if (pat_star) {
            // Mismatch with previous wildcard - reset pattern and advance string
            pat_ptr = pat_star + 1;
            str_ptr = ++str_star;
        }
        else {
            return false;
        }
    }
    
    while (*pat_ptr == '*') {
        pat_ptr++;
    }
    
    return !*pat_ptr;
}

bool tracer_should_trace(tracer_t *tracer, tracer_thread_context_frame_t *frame) {
    if (tracer == NULL || frame == NULL || frame->self_class_name == NULL || frame->selector_name == NULL) {
        return false;
    }
    
    pthread_rwlock_rdlock(&tracer->filter_lock);
    
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        // This pass only considers exclusion filters
        const tracer_filter_t *filter = &tracer->config.filters[i];
        if (filter->exclude == false) {
            continue;
        }
        
        // If the image path has not been resolved
        if (frame->image_path == NULL) {
            // And the filter calls for the image path
            if (filter->image_pattern != NULL || filter->custom_filter != NULL) {
                // Then fetch the image path
                frame->image_path = class_getImageName(frame->self_class);
            }
        }
        
        if (filter->image_pattern != NULL && frame->image_path != NULL) {
            if (strstr(frame->image_path, filter->image_pattern)) {
                pthread_rwlock_unlock(&tracer->filter_lock);
                return false;
            }
        }
        
        if (filter->class_pattern != NULL) {
            if (match_wildcard(filter->class_pattern, frame->self_class_name)) {
                pthread_rwlock_unlock(&tracer->filter_lock);
                return false;
            }
        }
            
        if (filter->method_pattern != NULL) {
            if (match_wildcard(filter->method_pattern, frame->selector_name)) {
                pthread_rwlock_unlock(&tracer->filter_lock);
                return false;
            }
        }
    }
    
    bool should_trace = false;
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        // This pass only considers inclusion filters
        const tracer_filter_t *filter = &tracer->config.filters[i];
        if (filter->exclude) {
            continue;
        }
        
        if (should_trace) {
            break;
        }
        
        // If the image path has not been resolved
        if (frame->image_path == NULL) {
            // And the filter calls for the image path
            if (filter->image_pattern != NULL || filter->custom_filter != NULL) {
                // Then fetch the image path
                frame->image_path = class_getImageName(frame->self_class);
            }
        }
        
        if (filter->custom_filter != NULL) {
            tracer_event_t event = {
                .class_name = frame->self_class_name,
                .method_name = frame->selector_name,
                .image_path = frame->image_path,
                .thread_id = (uint64_t)pthread_self(),
                .is_class_method = false,
                .trace_depth = 0,
                .real_depth = 0,
                .arguments = NULL,
                .argument_count = 0,
                .method_signature = NULL
            };
            
            should_trace = filter->custom_filter((struct tracer_event_t *)&event, filter->custom_filter_context);
            continue;
        }
        
        // If an image filter is specified
        if (filter->image_pattern != NULL) {
            // And the current image path is NULL
            if (frame->image_path == NULL) {
                // Then do not trace
                continue;
            }
            
            // If both image paths are not NULL
            // And the current image path does not match
            if (strstr(frame->image_path, filter->image_pattern) == NULL) {
                // Then do not trace
                continue;
            }
        }
        
        // If a class filter is specified
        if (filter->class_pattern != NULL) {
            // And the current class name matches
            if (!match_wildcard(filter->class_pattern, frame->self_class_name)) {
                // Then do not trace
                continue;
            }
        }
        
        // If a method filter is specified
        if (filter->method_pattern != NULL) {
            // And the current method name does not match
            if (!match_wildcard(filter->method_pattern, frame->selector_name)) {
                // Then do not trace
                continue;
            }
        }
        
        should_trace = true;
    }
    
    pthread_rwlock_unlock(&tracer->filter_lock);
    return should_trace;
}

__attribute__((aligned(16), always_inline, hot))
bool is_valid_pointer(void *ptr) {
    if (ptr == NULL || ((uintptr_t)ptr % sizeof(void *)) != 0) {
        return false;
    }
    
    // Userspace shouldn't exceed 0x800000000000
    if ((uintptr_t)ptr < 0x4000 || (uintptr_t)ptr > 0x800000000000) {
        return false;
    }
    
    // Check for tagged pointers
    if (((uintptr_t)ptr & (0x1UL << 63UL)) == (0x1UL << 63UL) ||
        ((uintptr_t)ptr & (0x1UL << 60UL)) == (0x1UL << 60UL)) {
        return true;
    }
    
    uint64_t isa = (uint64_t)((_nsobject *)ptr)->isa;
    if ((isa & objc_debug_isa_magic_mask) != objc_debug_isa_magic_value) {
        return false;
    }

    // Check for class pointers
    if (((uintptr_t)ptr & 0xFFFF800000000000) != 0) {
        return false;
    }
    
    uintptr_t addr = ((uintptr_t)ptr & ~(0xFULL << 60));
    if (addr < 0x100000000 || addr > 0x2000000000) {
        return false;
    }
    
    if (((uintptr_t)ptr & ~(0xFULL << 60) & 0x7) != 0) {
        return false;
    }
    
    return true;
}
