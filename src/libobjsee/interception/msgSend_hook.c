//
//  msgSend_hook.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <objc/runtime.h>
#include <mach/mach.h>
#include <execinfo.h>
#include <dlfcn.h>
#include "selector_deny_list.h"
#include "class_name_cache.h"
#include "substrate_apis.h"
#include "event_handler.h"
#include "objc-internal.h"
#include "arg_capture.h"
#include "tracer.h"
#include "rebind.h"
#include "tsd.h"

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

extern void new_objc_msgSend(void);

static tracer_t *g_tracer_ctx = NULL;

void *g_original_objc_msgSend = NULL;
static int g_objc_msgSend_hooked = 0;

// Copy of objc_msgSend's instructions, pre- and post-hooking. Used for unhooking
static uint32_t prehook_msgSend_instructions[4];
static uint32_t hooked_msgSend_instructions[4];

__attribute__((aligned(16), always_inline, hot))
static inline struct tracer_thread_context_t *get_thread_context(void) {
    struct tracer_thread_context_t *ctx = (struct tracer_thread_context_t *)_get_tracer_thread_context();
    if (UNLIKELY(ctx == NULL)) {
        ctx = (struct tracer_thread_context_t *)calloc(1, sizeof(struct tracer_thread_context_t));
        if (UNLIKELY(ctx == NULL)) {
            tracer_set_error(g_tracer_ctx, "get_thread_context: Failed to allocate thread context");
            return NULL;
        }
        
        ctx->stack_depth = 0;
        ctx->trace_depth = 0;
        ctx->capture_arguments = g_tracer_ctx->config.format.args != TRACER_ARG_FORMAT_NONE;
        ctx->stack_base = NULL;
        if (ctx->capture_arguments) {
            ctx->stack_base = pthread_get_stackaddr_np(pthread_self());
        }
        
        uint64_t thread_id = 0;
        pthread_threadid_np(pthread_self(), &thread_id);
        ctx->thread_id = (uint16_t)(thread_id ^ (thread_id >> 32));
        
        set_tracer_thread_context((void *)ctx);
    }
    
    return ctx;
}

__attribute__((always_inline))
static inline bool selector_has_arguments(const char *sel_name) {
    while (*sel_name) {
        if (*sel_name++ == ':') {
            return true;
        }
    }
    return false;
}

static void free_event_arguments(tracer_event_t *event) {
    if (LIKELY(event->method_signature != NULL)) {
        free((void *)event->method_signature);
        event->method_signature = NULL;
    }
    
    if (LIKELY(event->arguments != NULL)) {
        for (size_t i = 0; i < event->argument_count; i++) {
            tracer_argument_t *arg = &event->arguments[i];
            if (LIKELY(arg->type_encoding != NULL)) {
                free((void *)arg->type_encoding);
                arg->type_encoding = NULL;
            }
            
            if (LIKELY(arg->description != NULL)) {
                free((void *)arg->description);
                arg->description = NULL;
            }
            
            if (UNLIKELY(arg->block_signature != NULL)) {
                free((void *)arg->block_signature);
                arg->block_signature = NULL;
            }
            
            if (LIKELY(arg->objc_class_name != NULL)) {
                free((void *)arg->objc_class_name);
                arg->objc_class_name = NULL;
            }
        }
        
        free(event->arguments);
        event->arguments = NULL;
    }
    
    event->argument_count = 0;
    event->formatted_output = NULL;
    event->class_name = NULL;
    event->method_name = NULL;
    event->image_path = NULL;
    event->thread_id = 0;
    event->trace_depth = 0;
    event->real_depth = 0;
    event->is_class_method = false;
}

static void populate_event_caller_info(struct tracer_thread_context_t *ctx, int stack_depth_on_entry, tracer_event_t *event) {
    if (stack_depth_on_entry > 0) {
        // If current depth is > 0, the caller is the previous frame
        struct tracer_thread_context_frame_t *caller_frame = &ctx->frames[stack_depth_on_entry - 1];
        event->caller.class_name = caller_frame->self_class_name;
        event->caller.method_name = caller_frame->selector_name;
    }
    else {
        // This is the first captured call in this thread.
        // Use backtrace() and dladdr() to get caller info
        const char *bt_frames[5];
        int bt_size = backtrace((void **)bt_frames, 5);
        
        if (bt_size >= 3) {
            Dl_info info;
            if (dladdr(bt_frames[2], &info) != 0) {
                if (info.dli_sname != NULL) {
                    event->caller.class_name = info.dli_sname;
                }
                else {
                    // Fallback to file offset
                    static __thread char addr_buf[32];
                    uintptr_t offset = (uintptr_t)bt_frames[2] - (uintptr_t)info.dli_fbase;
                    snprintf(addr_buf, sizeof(addr_buf), "0x%lx", offset);
                    event->caller.class_name = addr_buf;
                }
                
                if (info.dli_fname != NULL) {
                    event->caller.method_name = info.dli_fname;
                }
            }
        }
    }
}

__attribute__((aligned(16), always_inline, hot))
bool pre_objc_msgSend_callback(__unsafe_unretained id self, SEL _cmd, uintptr_t lr, void *stack_ptr) {
    struct tracer_thread_context_t *ctx = get_thread_context();
    
    int stack_depth_on_entry = ctx->stack_depth;
    if (UNLIKELY(stack_depth_on_entry >= INITIAL_STACK_FRAMES)) {
        tracer_set_error(g_tracer_ctx, "stack depth exceeded limit");
        return false;
    }
    
    if (!PTR_PLAUSIBLE(self) || !PTR_PLAUSIBLE(_cmd)) {
        return false;
    }
    
    const char *selector_name = sel_getName(_cmd);
    if (selector_name == NULL || should_skip_selector_name(selector_name)) {
        return false;
    }

    Class self_class = object_getClass(self);
    bool invalid_or_unrealized = !PTR_PLAUSIBLE(self_class) || !is_class_realized(self_class);
    if (UNLIKELY(invalid_or_unrealized)) {
        return false;
    }

    struct tracer_thread_context_frame_t *frame = &ctx->frames[stack_depth_on_entry];
    frame->_cmd = _cmd;
    frame->self_class = self_class;
    frame->selector_name = selector_name;

    // Resolve and cache class info
    // These details will be needed by filters later on and could have interest by an API user.
    if (UNLIKELY(ctx->last_class_cache.cls == self_class)) {
        frame->self_class_name = ctx->last_class_cache.name;
        frame->selector_is_class_method = ctx->last_class_cache.is_meta;
    }
    else {
        const char *class_name = class_name_cache_get(self_class);
        frame->self_class_name = class_name;

        bool class_is_meta = class_isMetaClass(self_class);
        frame->selector_is_class_method = class_is_meta;
        
        ctx->last_class_cache.cls = self_class;
        ctx->last_class_cache.name = class_name;
        ctx->last_class_cache.is_meta = class_is_meta;
    }
    
    bool should_trace = tracer_evaluate_trace_policy(g_tracer_ctx, frame);
    bool tracking_callers = g_tracer_ctx->config.format.include_caller_info;
    if (!should_trace) {
        // When caller-tracking is enabled, all frames need to be recorded regardless of whether they are traced
        if (tracking_callers) {
            frame->lr = lr;
            ctx->stack_depth++;

            // true will make the trampoline call post_objc_msgSend_callback() (stack depth decrement)
            return true;
        }
        
        // Not tracing this call, skip the stack decrement callback
        return false;
    }

    tracer_event_t event = {
        .class_name = frame->self_class_name,
        .method_name = selector_name,
        .is_class_method = frame->selector_is_class_method,
        .image_path = frame->image_path,
        .thread_id = ctx->thread_id,
        .trace_depth = ctx->trace_depth,
        .real_depth = ctx->stack_depth,
        .arguments = NULL,
        .argument_count = 0,
        .method_signature = NULL,
    };
    
    if (tracking_callers) {
        populate_event_caller_info(ctx, stack_depth_on_entry, &event);
    }
    
    bool should_capture_args = ctx->capture_arguments && ctx->stack_depth <= 32 && selector_has_arguments(selector_name);
    if (LIKELY(should_capture_args)) {
        // Make a copy of the stack so that memory doesn't change out from under us while interpreting argument values
        char local_stack_copy_buffer[1024];
        size_t stack_buffer_size = sizeof(local_stack_copy_buffer);
        size_t bytes_to_copy = stack_buffer_size;
        
        uintptr_t current_sp = (uintptr_t)stack_ptr;
        uintptr_t stack_base = (uintptr_t)ctx->stack_base;
        if (LIKELY(current_sp < stack_base)) {
            // Don't read past the end of the stack (primarily applicable to armv7)
            size_t available_bytes = stack_base - current_sp;
            if (UNLIKELY(bytes_to_copy > available_bytes)) {
                bytes_to_copy = available_bytes;
            }
        }
        
        if (LIKELY(bytes_to_copy > 0)) {
            memcpy(local_stack_copy_buffer, stack_ptr, bytes_to_copy);
        }
        
        if (UNLIKELY(bytes_to_copy < stack_buffer_size)) {
            memset(local_stack_copy_buffer + bytes_to_copy, 0, stack_buffer_size - bytes_to_copy);
        }

        capture_arguments(g_tracer_ctx, frame, local_stack_copy_buffer, &event);
    }
    
    tracer_handle_event(g_tracer_ctx, &event);
    
    if (LIKELY(should_capture_args)) {
        if (LIKELY(event.arguments != NULL)) {
            free_event_arguments(&event);
        }
    }
    
    ctx->trace_depth++;
    ctx->stack_depth++;
    frame->traced = true;
    frame->lr = lr;
    
    return true;
}

__attribute__((aligned(16), always_inline, hot))
uintptr_t post_objc_msgSend_callback(void) {
    struct tracer_thread_context_t *ctx = get_thread_context();
    size_t stack_depth_on_entry = ctx->stack_depth - 1;
    
    ctx->stack_depth -= 1;
    if (LIKELY(ctx->trace_depth > 0)) {
        ctx->trace_depth -= 1;
    }
    
    struct tracer_thread_context_frame_t *frame = &ctx->frames[stack_depth_on_entry];
    return frame->lr;
}

static void copy_function_instructions(void *src, uint32_t *dest, size_t instr_count) {
    uint32_t *src_ptr = (uint32_t *)src;
    for (size_t i = 0; i < instr_count; i++) {
        dest[i] = src_ptr[i];
    }
}

kern_return_t restore_instructions_from_cache(void *func_ptr, uint32_t *cached_instrs) {
    if (objsee_MSHookMemory(func_ptr, cached_instrs, 16) != KERN_SUCCESS) {
        return KERN_FAILURE;
    }
    
    return KERN_SUCCESS;
}

tracer_result_t init_message_interception(tracer_t *tracer) {
    if (tracer == NULL) {
        tracer_set_error(g_tracer_ctx, "tracer is NULL");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (g_tracer_ctx != NULL) {
        if (g_tracer_ctx == tracer) {
            // Cannot initialize the same tracer twice (without cleaning up first)
            tracer_set_error(g_tracer_ctx, "tracer_cleanup() must be called before reinitializing the same tracer");
            return TRACER_ERROR_ALREADY_INITIALIZED;
        }
        
        if (g_tracer_ctx->running) {
            // A different tracer is already running
            tracer_set_error(g_tracer_ctx, "A tracer is already running");
            return TRACER_ERROR_ALREADY_INITIALIZED;
        }
        
        // Tear down the previous tracer
        if (tracer_cleanup(g_tracer_ctx) != TRACER_SUCCESS) {
            tracer_set_error(g_tracer_ctx, "Failed to clean up previous tracer");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        tracer_set_error(tracer, "Previous tracer cleaned up");
    }

    g_tracer_ctx = tracer;
    
    if (tracer->config.use_symbol_rebinding) {
        // Hook objc_msgSend using symbol rebinding (works without a jailbreak)
        g_original_objc_msgSend = dlsym(RTLD_DEFAULT, "objc_msgSend");
        struct symbol_rebinding_t *rebinding = hook_function("objc_msgSend", new_objc_msgSend);
        if (rebinding == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to hook objc_msgSend");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        free(rebinding);
    }
    else {
        void *objc_msgSend = dlsym(RTLD_DEFAULT, "objc_msgSend");
        if (g_objc_msgSend_hooked == 0) {
            // If objc_msgSend has not been hooked yet, cache its first 4 instructions.
            // They are used for unhooking
            copy_function_instructions(objc_msgSend, prehook_msgSend_instructions, 4);
            
            // Hook objc_msgSend using MSHookFunction
            kern_return_t ret = objsee_MSHookFunction(objc_msgSend, new_objc_msgSend, (void **)&g_original_objc_msgSend);
            if (ret != KERN_SUCCESS) {
                tracer_set_error(g_tracer_ctx, "Failed to hook objc_msgSend using MSHookFunction");
                return TRACER_ERROR_INITIALIZATION;
            }
            
            // Cache the hooked instructions. MSHookFunction (from ellekit) fails to apply a hook to a function more than once.
            // If we hook, unhook, then want to rehook, it has to be done by writing these instructions back to objc_msgSend
            copy_function_instructions(objc_msgSend, hooked_msgSend_instructions, 4);
            g_objc_msgSend_hooked = 1;
        }
        else {
            // objc_msgSend has already been hooked once before, and presumably restored back to its original state.
            // MSHookFunction fails to hook a function that's already been hooked (even with the function's original instructions restored).
            // To rehook, write the instructions that were cached after installing the first hook
            restore_instructions_from_cache(objc_msgSend, hooked_msgSend_instructions);
        }
    }

    return TRACER_SUCCESS;
}

tracer_result_t disable_message_interception(tracer_t *tracer) {
    if (tracer == NULL || g_tracer_ctx != tracer) {
        tracer_set_error(g_tracer_ctx, "disable_message_interception: tracer not active");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
        
    if (tracer->config.use_symbol_rebinding) {
        struct symbol_rebinding_t *rebinding = hook_function("objc_msgSend", g_original_objc_msgSend);
        if (rebinding == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to bind objc_msgSend back to original implementation");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        free(rebinding);
    }
    else {
        // Suspend threads, unhook objc_msgSend by restoring its original instructions, then resume threads
        kern_return_t kr;
        mach_port_t task = mach_task_self();
        thread_act_array_t threads;
        mach_msg_type_number_t thread_count;
        
        if ((kr = task_threads(task, &threads, &thread_count)) != KERN_SUCCESS) {
            return kr;
        }
        
        thread_t self = mach_thread_self();
        for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
            if (threads[i] != self) {
                thread_suspend(threads[i]);
            }
        }
        
        // Restore the instructions that were cached before the hook was installed
        void *objc_msgSend = dlsym(RTLD_DEFAULT, "objc_msgSend");
        restore_instructions_from_cache(objc_msgSend, prehook_msgSend_instructions);
        g_original_objc_msgSend = objc_msgSend;
        
        for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
            if (threads[i] != self) {
                thread_resume(threads[i]);
            }
            mach_port_deallocate(task, threads[i]);
        }
        vm_deallocate(task, (vm_address_t)threads, thread_count * sizeof(thread_act_t));
        mach_port_deallocate(task, self);
    }
    
    return TRACER_SUCCESS;
}
