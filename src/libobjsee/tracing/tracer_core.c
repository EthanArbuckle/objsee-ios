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
    
//    printf("Error: %s\n", tracer->last_error);
    os_log(OS_LOG_DEFAULT, "Error:  %s", tracer->last_error);
    pthread_mutex_unlock(&tracer->error_lock);
}

static bool match_wildcard(const char *pattern, const char *str) {
    if (pattern == NULL || str == NULL) {
        return false;
    }

    if (!*pattern|| strcmp(pattern, "*") == 0) {
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


static bool match_wildcard_simd(const char *pattern, const char *str) {
    return match_wildcard(pattern, str);
    if (!pattern || !str) {
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
            // Handle wildcard
            pat_star = pat_ptr++;
            str_star = str_ptr;
            continue;
        }
        
        // If we have a previous wildcard match point and current match fails,
        // rewind to just after the last wildcard
        if (*pat_ptr != *str_ptr && pat_star) {
            pat_ptr = pat_star + 1;
            str_ptr = ++str_star;
            continue;
        }
        
        // No wildcard, try SIMD matching of next chunk
        if (*pat_ptr == *str_ptr && *(str_ptr + 1) && *(pat_ptr + 1)) {
            size_t remaining_str = strlen(str_ptr + 1);
            size_t remaining_pat = strlen(pat_ptr + 1);
            size_t chunk_size = remaining_str < remaining_pat ? remaining_str : remaining_pat;
            if (chunk_size > 15) chunk_size = 15;
            
            // Load and compare chunks
            uint8x16_t str_vec = vld1q_u8((const uint8_t *)(str_ptr + 1));
            uint8x16_t pat_vec = vld1q_u8((const uint8_t *)(pat_ptr + 1));
            uint8x16_t asterisk_vec = vceqq_u8(pat_vec, vdupq_n_u8('*'));
            uint8x16_t equal = vceqq_u8(str_vec, pat_vec);
            
            // Find first mismatch or wildcard
            uint16x8_t sum = vpaddlq_u8(vorrq_u8(equal, asterisk_vec));
            uint32x4_t sum2 = vpaddlq_u16(sum);
            uint64x2_t sum3 = vpaddlq_u32(sum2);
            uint64_t match_run = vgetq_lane_u64(sum3, 0) + vgetq_lane_u64(sum3, 1);
            
            if (match_run > 0) {
                str_ptr += match_run;
                pat_ptr += match_run;
                continue;
            }
        }
        
        if (*pat_ptr != *str_ptr && !pat_star) {
            return false;
        }
        
        pat_ptr++;
        str_ptr++;
    }
    
    while (*str_ptr) {
        if (*pat_ptr == '*') {
            pat_star = pat_ptr++;
            str_star = str_ptr;
        }
        else if (*pat_ptr == *str_ptr) {
            pat_ptr++;
            str_ptr++;
        }
        else if (pat_star) {
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
    
    bool should_fetch_image_path = false;
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        const tracer_filter_t *filter = &tracer->config.filters[i];
        
        if (!should_fetch_image_path && (filter->image_pattern != NULL || filter->custom_filter != NULL)) {
            should_fetch_image_path = true;
        }
        
        if (!filter->exclude) {
            continue;
        }
        
        if (filter->class_pattern && match_wildcard_simd(filter->class_pattern, frame->self_class_name)) {
            if (filter->method_pattern == NULL || match_wildcard_simd(filter->method_pattern, frame->selector_name)) {
                pthread_rwlock_unlock(&tracer->filter_lock);
                return false;
            }
        }
    }
    
    if (should_fetch_image_path && frame->image_path == NULL) {
        frame->image_path = class_getImageName(frame->self_class);
    }
    
    bool should_trace = false;
    bool class_match = false;
    for (size_t i = 0; i < tracer->config.filter_count; i++) {
        const tracer_filter_t *filter = &tracer->config.filters[i];
        if (filter->exclude) {
            continue;
        }

        if (!should_trace && ((filter->class_pattern && match_wildcard_simd(filter->class_pattern, frame->self_class_name)) || (filter->method_pattern && match_wildcard_simd(filter->method_pattern, frame->selector_name)))) {
                              
            class_match = true;
            if (filter->method_pattern == NULL || match_wildcard(filter->method_pattern, frame->selector_name)) {
                should_trace = true;

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
                }
                
                if (should_trace) {
                    break;
                }
            }
        }
        
        // Class patterns take precedence over image patterns
        if (!class_match && filter->image_pattern != NULL && frame->image_path != NULL) {
            if (strstr(frame->image_path, filter->image_pattern) != NULL) {
                should_trace = true;
                break;
            }
        }
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
