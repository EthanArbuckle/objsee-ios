//
//  tracer_core.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <arm_neon.h>
#include "tracer_internal.h"
#include "logging.h"

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
    
    if (pthread_mutex_init(&internal_ctx->error_lock, NULL) != 0) {
        tracer_set_error(tracer, "Failed to initialize error lock");
        pthread_rwlock_destroy(&internal_ctx->filter_lock);
        return TRACER_ERROR_INITIALIZATION;
    }
        
    internal_ctx->initialized = true;
    return TRACER_SUCCESS;
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
    objsee_log("Error: %s", tracer->last_error);
    
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

typedef enum {
    NOT_NEEDED,
    NEEDED_FOR_INCLUDE,
    NEEDED_FOR_EXCLUDE
} image_path_needed_t;

bool tracer_should_trace(tracer_t *tracer, tracer_thread_context_frame_t *frame) {
    if (tracer == NULL || frame == NULL || frame->self_class_name == NULL || frame->selector_name == NULL) {
        return false;
    }
        
    pthread_rwlock_rdlock(&tracer->filter_lock);
    
    // Image path resolution is deferred until/if needed by a filter
    image_path_needed_t image_path_needed = NOT_NEEDED;
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        const tracer_filter_t *filter = &tracer->config.filters[i];
        if (filter->image_pattern == NULL) {
            image_path_needed = NOT_NEEDED;
        }
        else if (filter->image_pattern && filter->custom_filter) {
            // Always need image path for custom filters
            image_path_needed = NEEDED_FOR_EXCLUDE;
        }
        else if (filter->image_pattern != NULL && filter->exclude) {
            image_path_needed = NEEDED_FOR_EXCLUDE;
        }
        else if (filter->image_pattern != NULL && !filter->exclude) {
            image_path_needed = NEEDED_FOR_INCLUDE;
        }
    }
    
    void (^resolve_image_path)(void) = ^{
        if (frame->image_path == NULL) {
            frame->image_path = class_getImageName(frame->self_class);
        }
    };
    
    // If the image path is needed for any EXCLUDE filter.
    // INCLUDE is still deferred because trace decision may be decided during the EXCLUDE pass
    if (image_path_needed == NEEDED_FOR_EXCLUDE) {
        resolve_image_path();
    }
    
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        // This pass only considers exclusion filters
        const tracer_filter_t *filter = &tracer->config.filters[i];
        if (filter->exclude == false) {
            continue;
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
    
    if (image_path_needed == NEEDED_FOR_INCLUDE) {
        resolve_image_path();
    }

    // If the user did not specify inclusion patterns, trace everything.
    // This makes it such that if the user only specifies exclusion patterns, everything
    // not captured by those exclusions will be traced
    bool has_inclusion_filters = false;
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        if (!tracer->config.filters[i].exclude) {
            has_inclusion_filters = true;
            break;
        }
    }
    
    bool should_trace = !has_inclusion_filters;
    
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        // This pass only considers inclusion filters
        const tracer_filter_t *filter = &tracer->config.filters[i];
        if (filter->exclude) {
            continue;
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
        
        bool filter_matches = true;

        // If an image filter is specified
        if (filter->image_pattern != NULL) {
            // And the current image path is NULL
            if (frame->image_path == NULL) {
                // Then do not trace
                filter_matches = false;
            }
            // If both image paths are not NULL
            // And the current image path does not match
            else if (strstr(frame->image_path, filter->image_pattern) == NULL) {
                // Then do not trace
                filter_matches = false;
            }
        }
        
        // If a class filter is specified
        if (filter->class_pattern != NULL && filter_matches) {
            // And the current class name matches
            filter_matches = match_wildcard(filter->class_pattern, frame->self_class_name);
        }
        
        // If a method filter is specified
        if (filter->method_pattern != NULL && filter_matches) {
            // And the current method name does not match
            filter_matches = match_wildcard(filter->method_pattern, frame->selector_name);
        }
        
        if (filter_matches) {
            should_trace = true;
        }
    }
    
    pthread_rwlock_unlock(&tracer->filter_lock);
    return should_trace;
}

__attribute__((aligned(16), always_inline, hot)) bool is_valid_pointer(void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;
#if defined(__LP64__)
    const uintptr_t min_addr = 0x4000;
    const uintptr_t max_addr = 0x800000000000;
    const uintptr_t tag_mask = 0xFULL << 60;
    const uintptr_t high_bit = 1ULL << 63;
    const uintptr_t objc_tag_bit = 1ULL << 60;
#else
    const uintptr_t min_addr = 0x4000;
    const uintptr_t max_addr = UINTPTR_MAX;
#endif

    if (addr < min_addr || addr > max_addr || (addr & (sizeof(void *) - 1)) != 0) {
        return false;
    }

#if defined(__LP64__)
    if ((addr & high_bit) != 0 || (addr & objc_tag_bit) != 0) {
        return true;
    }

    uint64_t isa = (uint64_t)((_nsobject *)ptr)->isa;
    if ((isa & objc_debug_isa_magic_mask) != objc_debug_isa_magic_value) {
        return false;
    }

    uintptr_t untagged = addr & ~tag_mask;
    if (untagged < 0x100000000 || untagged > 0x2000000000 || (untagged & 0x7) != 0) {
        return false;
    }
#endif

    return true;
}