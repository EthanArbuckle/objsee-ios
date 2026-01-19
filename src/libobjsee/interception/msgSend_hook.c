//
//  msgSend_hook.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <objc/runtime.h>
#include <mach/mach.h>
#include <dlfcn.h>
#include "realized_class_tracking.h"
#include "selector_deny_list.h"
#include "event_handler.h"
#include "signal_guard.h"
#include "arg_capture.h"
#include "tracer.h"
#include "rebind.h"

extern void new_objc_msgSend(void);

void *original_objc_msgSend = NULL;
static pthread_key_t interception_stacktrace_thread_key;
// TODO: remove
static tracer_t *g_tracer_ctx = NULL;

__attribute__((aligned(16), always_inline, hot))
static inline struct tracer_thread_context_t *get_thread_context(void) {
    
    struct tracer_thread_context_t *ctx = (struct tracer_thread_context_t *)pthread_getspecific(interception_stacktrace_thread_key);
    if (__builtin_expect(ctx == NULL, 0)) {
        ctx = (struct tracer_thread_context_t *)calloc(1, sizeof(struct tracer_thread_context_t));
        if (ctx == NULL) {
            tracer_set_error(g_tracer_ctx, "get_thread_context: Failed to allocate thread context");
            return NULL;
        }
        
        ctx->stack_depth = -1;
        ctx->trace_depth = 0;
        ctx->capture_arguments = g_tracer_ctx->config.format.args != TRACER_ARG_FORMAT_NONE;
        
        ctx->stack_base = NULL;
        if (ctx->capture_arguments) {
            ctx->stack_base = pthread_get_stackaddr_np(pthread_self());
        }
        
        uint64_t thread_id = 0;
        pthread_threadid_np(pthread_self(), &thread_id);
        ctx->thread_id = (uint16_t)(thread_id ^ (thread_id >> 32));
        
        pthread_setspecific(interception_stacktrace_thread_key, ctx);
    }
    
    return ctx;
}

__attribute__((always_inline)) static inline
bool is_class_method_fast(Class cls, SEL cmd) {
    // Most methods are instance methods
    return false;
    if (__builtin_expect(!class_isMetaClass(cls), 1)) {
        return false;
    }
    return true;
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

void free_event_arguments(tracer_event_t *event) {
    if (event == NULL) {
        return;
    }
    
    if (event->method_signature) {
        vm_deallocate(mach_task_self(), (vm_address_t)event->method_signature, strlen(event->method_signature) + 1);
        event->method_signature = NULL;
    }
    
    if (event->arguments) {
        for (size_t i = 0; i < event->argument_count; i++) {
            tracer_argument_t *arg = &event->arguments[i];
            if (arg->type_encoding) {
                vm_deallocate(mach_task_self(), (vm_address_t)arg->type_encoding, strlen(arg->type_encoding) + 1);
                arg->type_encoding = NULL;
            }
            
            if (arg->objc_class_name) {
                vm_deallocate(mach_task_self(), (vm_address_t)arg->objc_class_name, strlen(arg->objc_class_name) + 1);
                arg->objc_class_name = NULL;
            }
            
            if (arg->description) {
                vm_deallocate(mach_task_self(), (vm_address_t)arg->description, strlen(arg->description) + 1);
                arg->description = NULL;
            }
            
            if (arg->block_signature) {
                vm_deallocate(mach_task_self(), (vm_address_t)arg->block_signature, strlen(arg->block_signature) + 1);
                arg->block_signature = NULL;
            }
        }
        
        vm_deallocate(mach_task_self(), (vm_address_t)event->arguments, event->argument_count * sizeof(tracer_argument_t));
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

__attribute__((aligned(16), always_inline, hot))
bool pre_objc_msgSend_callback(__unsafe_unretained id self, SEL _cmd, uintptr_t lr, void *stack_ptr) {
    struct tracer_thread_context_t *ctx = get_thread_context();
    
    int scratch_depth = ctx->stack_depth + 1;
    if (__builtin_expect(scratch_depth >= INITIAL_STACK_FRAMES, 0)) {
        tracer_set_error(g_tracer_ctx, "stack depth exceeded limit");
        return false;
    }
    
    if (!self || (uintptr_t)self <= 0x100 || selector_is_denylisted(_cmd)) {
        return false;
    }
    
    struct tracer_thread_context_frame_t *frame = &ctx->frames[scratch_depth];
    frame->lr = lr;
    frame->_cmd = _cmd;
    frame->image_path = NULL;
    
    Class self_class = object_getClass(self);
    if (self_class == NULL || (uintptr_t)self_class < 0x10000 || !is_class_realized(self_class)) {
        return false;
    }

    // Resolve and cache class name, selector name, and whether the selector is a class method.
    // These details will be needed by filters later on and could have interest by an API user.
    if (ctx->last_class_cache.cls == self_class) {
        frame->self_class = self_class;
        frame->self_class_name = ctx->last_class_cache.name;
        frame->selector_is_class_method = ctx->last_class_cache.is_meta;
    }
    else {
        ctx->last_class_cache.cls = self_class;
        ctx->last_class_cache.name = class_getName(self_class);
        ctx->last_class_cache.is_meta = is_class_method_fast(self_class, _cmd);

        frame->self_class = self_class;
        frame->self_class_name = ctx->last_class_cache.name;
        frame->selector_is_class_method = ctx->last_class_cache.is_meta;
    }
    
    if (ctx->last_sel_cache.sel == _cmd) {
        frame->selector_name = ctx->last_sel_cache.name;
    }
    else {
        const char *sel_name = sel_getName(_cmd);
        ctx->last_sel_cache.sel = _cmd;
        ctx->last_sel_cache.name = sel_name;
        frame->selector_name = ctx->last_sel_cache.name;
    }
    
    if (!tracer_should_trace(g_tracer_ctx, frame)) {
        return false;
    }
    
    ctx->stack_depth++;
    frame->traced = true;
    
    tracer_event_t event = {
        .class_name = frame->self_class_name,
        .method_name = frame->selector_name,
        .is_class_method = frame->selector_is_class_method,
        .image_path = frame->image_path,
        .thread_id = ctx->thread_id,
        .trace_depth = ctx->trace_depth,
        .real_depth = ctx->stack_depth,
        .arguments = NULL,
        .argument_count = 0,
        .method_signature = NULL,
    };
    
    if (ctx->capture_arguments && ctx->stack_depth <= 32 && selector_has_arguments(frame->selector_name)) {
        // Make a copy of the stack so that memory doesn't change out from under us while interpreting argument values
        char local_stack_copy_buffer[1024];
        size_t stack_buffer_size = sizeof(local_stack_copy_buffer);
        size_t bytes_to_copy = stack_buffer_size;
        
        uintptr_t current_sp = (uintptr_t)stack_ptr;
        uintptr_t stack_base = (uintptr_t)ctx->stack_base;
        if (current_sp < stack_base) {
            // Don't read past the end of the stack (primarily applicable to armv7)
            size_t available_bytes = stack_base - current_sp;
            if (bytes_to_copy > available_bytes) {
                bytes_to_copy = available_bytes;
            }
        }
        
        if (bytes_to_copy > 0) {
            memcpy(local_stack_copy_buffer, stack_ptr, bytes_to_copy);
        }
        
        if (bytes_to_copy < stack_buffer_size) {
            memset(local_stack_copy_buffer + bytes_to_copy, 0, stack_buffer_size - bytes_to_copy);
        }

        capture_arguments(g_tracer_ctx, frame, local_stack_copy_buffer, &event);
    }
    
    tracer_handle_event(g_tracer_ctx, &event);
    
    if (__builtin_expect(event.arguments != NULL, 0)) {
        free_event_arguments(&event);
    }
    
    ctx->trace_depth += 1;
    
    return true;
}

__attribute__((aligned(16), always_inline, hot))
uintptr_t post_objc_msgSend_callback(void) {
    struct tracer_thread_context_t *ctx = (struct tracer_thread_context_t *)pthread_getspecific(interception_stacktrace_thread_key);
    size_t current_depth = ctx->stack_depth;
    
    ctx->stack_depth -= 1;
    if (ctx->trace_depth > 0) {
        ctx->trace_depth -= 1;
    }
    
    struct tracer_thread_context_frame_t *frame = &ctx->frames[current_depth];
    return frame->lr;
}

void *get_original_objc_msgSend(void) {
    if (original_objc_msgSend == NULL) {
        original_objc_msgSend = dlsym(RTLD_DEFAULT, "objc_msgSend");
        if (original_objc_msgSend == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to locate objc_msgSend");;
        }
    }
    
    return original_objc_msgSend;
}

tracer_result_t init_message_interception(tracer_t *tracer) {

    if (tracer == NULL) {
        tracer_set_error(g_tracer_ctx, "init_message_interception: Invalid tracer context");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (g_tracer_ctx != NULL) {
        tracer_set_error(g_tracer_ctx, "init_message_interception: Tracer already initialized");
        return TRACER_ERROR_ALREADY_INITIALIZED;
    }
    
    g_tracer_ctx = tracer;
    
    if (pthread_key_create(&interception_stacktrace_thread_key, NULL) != 0) {
        tracer_set_error(g_tracer_ctx, "Failed to create thread-local storage");
        return TRACER_ERROR_MEMORY;
    }
    
    original_objc_msgSend = get_original_objc_msgSend();
    if (original_objc_msgSend == NULL) {
        tracer_set_error(g_tracer_ctx, "Failed to locate objc_msgSend");
        return TRACER_ERROR_INITIALIZATION;
    }

    if (tracer->config.use_symbol_rebinding) {
        // Hook objc_msgSend using symbol rebinding (works without a jailbreak)
        struct symbol_rebinding_t *rebinding = hook_function("objc_msgSend", new_objc_msgSend);
        if (rebinding == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to hook objc_msgSend");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        free(rebinding);
    }
    else {
        // Hook objc_msgSend using a jailbreak hooking library
        const char *possible_lib_paths[3] = {
            "/var/jb/usr/lib/libsubstrate.dylib",
            "/usr/lib/libsubstrate.dylib",
            "/cores/binpack/usr/lib/libellekit.dylib"
        };
        
        void *jbhooker_handle = NULL;
        for (size_t i = 0; i < sizeof(possible_lib_paths) / sizeof(possible_lib_paths[0]); i++) {
            jbhooker_handle = dlopen(possible_lib_paths[i], RTLD_LAZY);
            if (jbhooker_handle != NULL) {
                break;
            }
        }
        
        if (jbhooker_handle == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to find or load jailbreak hooker library");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        void *_MSHookFunction = dlsym(jbhooker_handle, "MSHookFunction");
        if (_MSHookFunction == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to locate MSHookFunction in jailbreak hooker library");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        ((void (*)(void *, void *, void **))_MSHookFunction)(original_objc_msgSend, new_objc_msgSend, (void **)&original_objc_msgSend);
    }

    return TRACER_SUCCESS;
}
