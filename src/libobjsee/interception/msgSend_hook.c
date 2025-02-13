//
//  msgSend_hook.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <objc/runtime.h>
#include <mach/mach.h>
#include <os/log.h>
#include <dlfcn.h>
#include "realized_class_tracking.h"
#include "selector_deny_list.h"
#include "event_handler.h"
#include "signal_guard.h"
#include "arg_capture.h"
#include "tracer.h"
#include "rebind.h"

#define USE_JAILBREAK_HOOKER 0

void *original_objc_msgSend = NULL;
static pthread_key_t interception_stacktrace_thread_key;
// TODO: remove
static tracer_t *g_tracer_ctx = NULL;

__attribute__((aligned(16), always_inline, hot))
static inline struct tracer_thread_context_t *get_thread_context(void) {
    
    struct tracer_thread_context_t *ctx = (struct tracer_thread_context_t *)pthread_getspecific(interception_stacktrace_thread_key);
    if (__builtin_expect(ctx == NULL, 0)) {
        ctx = (struct tracer_thread_context_t *)malloc(sizeof(tracer_thread_context_t));
        if (ctx == NULL) {
            tracer_set_error(g_tracer_ctx, "get_thread_context: Failed to allocate thread context");
            return NULL;
        }
        
        ctx->stack_depth = -1;
        ctx->trace_depth = 0;
        ctx->capture_arguments = g_tracer_ctx->config.format.args != TRACER_ARG_FORMAT_NONE;
        
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
    if (__builtin_expect(!class_isMetaClass(cls), 1)) {
        return false;
    }
    return true;
}

void free_event_arguments(tracer_event_t *event) {
    if (event == NULL) {
        return;
    }
    
    FREE_IF_NOT_NULL(event->method_signature);
    if (event->arguments) {
        for (size_t i = 0; i < event->argument_count; i++) {
            
            FREE_IF_NOT_NULL(event->arguments[i].type_encoding);
            FREE_IF_NOT_NULL(event->arguments[i].objc_class_name);
            FREE_IF_NOT_NULL(event->arguments[i].block_signature);
            FREE_IF_NOT_NULL(event->arguments[i].description);
            
            event->arguments[i].address = NULL;
            event->arguments[i].size = 0;
            event->arguments[i].objc_class = NULL;
        }
        FREE_IF_NOT_NULL(event->arguments);
    }
    event->argument_count = 0;
}

static kern_return_t stack_make_local_copy(void *stack_ptr, vm_address_t *stack_copy, size_t stack_size) {
    if (stack_ptr == NULL || stack_copy == NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    
    vm_address_t copy = 0;
    kern_return_t kr = vm_allocate(mach_task_self(), &copy, stack_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    
    kr = vm_write(mach_task_self(), copy, (vm_offset_t)stack_ptr, (mach_msg_type_number_t)stack_size);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    
    kr = vm_protect(mach_task_self(), copy, stack_size, false, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    
    *stack_copy = copy;
    return KERN_SUCCESS;
}

__attribute__((aligned(16), always_inline, hot))
SEL pre_objc_msgSend_callback(__unsafe_unretained id self, SEL _cmd, uintptr_t lr, void *stack_ptr) {
    
    struct tracer_thread_context_t *ctx = get_thread_context();
    
    bool should_trace = true;
    if (__builtin_expect(ctx->stack_depth + 1 >= INITIAL_STACK_FRAMES, 0)) {
        tracer_set_error(g_tracer_ctx, "stack depth exceeded limit");
        should_trace = false;
    }
    
    ctx->stack_depth += 1;
    struct tracer_thread_context_frame_t *frame = &ctx->frames[ctx->stack_depth];
    // LR is the minimal info needed for stuff that'a not being traced
    frame->lr = lr;
    if (!should_trace || !self || (uintptr_t)self <= 0x100 || selector_is_denylisted(_cmd)) {
        frame->traced = false;
        return _cmd;
    }
    
    frame->_cmd = _cmd;
    frame->traced = true;
    // Defer image resolution until it's needed by a filter)
    frame->image_path = NULL;
    
    // Attempt to resolve an objc class for the object `self`.
    // This has been observed to crash in some cases
    Class self_class = NULL;
    WHILE_IGNORING_SIGNALS({
        self_class = object_getClass(self);
    });
        
    // object_getClass() failed
    if (self_class == NULL) {
        frame->traced = false;
        return _cmd;
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
        ctx->last_class_cache.name = object_getClassName((id)self_class);
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
    
    frame->traced = tracer_should_trace(g_tracer_ctx, frame);
    if (frame->traced == false) {
        return _cmd;
    }
    
    // Create trace event
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
    
    vm_address_t stack_copy = 0;
    size_t stack_size = 1024 * 2;
    bool capture_args = ctx->capture_arguments && ctx->stack_depth <= 32 && strstr(frame->selector_name, ":") != NULL;
    if (__builtin_expect(capture_args, 1)) {
        if (stack_make_local_copy(stack_ptr, &stack_copy, stack_size) == KERN_SUCCESS) {
            capture_arguments(g_tracer_ctx, frame, (void *)stack_copy, &event);
        }
    }
    
    tracer_handle_event(g_tracer_ctx, &event);
    
//    if (__builtin_expect(event.arguments != NULL, 1)) {
//         // TODO: events now uses vm_allocate, this needs to use vm_deallocate
//         free_event_arguments(&event);
//    }

    if (stack_copy) {
        vm_deallocate(mach_task_self(), stack_copy, stack_size);
    }
    
    ctx->trace_depth += 1;
    return _cmd;
}

__attribute__((aligned(16), always_inline, hot))
uintptr_t post_objc_msgSend_callback(void) {
    struct tracer_thread_context_t *ctx = (struct tracer_thread_context_t *)pthread_getspecific(interception_stacktrace_thread_key);
    size_t current_depth = ctx->stack_depth;
    if (current_depth < 0) {
        tracer_set_error(g_tracer_ctx, "attempted to pop a record with index < 0. this is not expected.");
        abort();
    }
    
    ctx->stack_depth -= 1;
    if (ctx->trace_depth > 0) {
        ctx->trace_depth -= 1;
    }
    
    struct tracer_thread_context_frame_t *frame = &ctx->frames[current_depth];
    return frame->lr;
}

__attribute__((naked, always_inline, hot, aligned(16)))
id new_objc_msgSend(id self, SEL _cmd, ...) {
    __asm__ volatile(
                     "sub sp, sp, #512\n"
                     "stp x0, x1, [sp, #0]\n"
                     "stp x2, x3, [sp, #16]\n"
                     "stp x4, x5, [sp, #32]\n"
                     "stp x6, x7, [sp, #48]\n"
                     "stp x8, x9, [sp, #64]\n"
                     "stp q0, q1, [sp, #80]\n"
                     "stp q2, q3, [sp, #144]\n"
                     
                     "mov x2, x30\n"
                     "mov x3, sp\n"
                     "bl _pre_objc_msgSend_callback\n"
                     "mov x17, x0\n"
                     
                     "ldp q2, q3, [sp, #144]\n"
                     "ldp q0, q1, [sp, #80]\n"
                     "ldp x8, x9, [sp, #64]\n"
                     "ldp x6, x7, [sp, #48]\n"
                     "ldp x4, x5, [sp, #32]\n"
                     "ldp x2, x3, [sp, #16]\n"
                     "ldp x0, x1, [sp, #0]\n"
                     
                     "add sp, sp, #512\n"
                     
                     "adrp x16, _original_objc_msgSend@PAGE\n"
                     "add  x16, x16, _original_objc_msgSend@PAGEOFF\n"
                     "ldr  x16, [x16]\n"
                     "mov x1, x17\n"
                     "blr x16\n"
                     
                     "sub sp, sp, #512\n"
                     "stp x0, x1, [sp, #0]\n"
                     "stp x2, x3, [sp, #16]\n"
                     "stp x4, x5, [sp, #32]\n"
                     "stp x6, x7, [sp, #48]\n"
                     "stp x8, x9, [sp, #64]\n"
                     "stp q0, q1, [sp, #80]\n"
                     "stp q2, q3, [sp, #144]\n"
                     
                     "bl _post_objc_msgSend_callback\n"
                     "mov x30, x0\n"
                     
                     "ldp q2, q3, [sp, #144]\n"
                     "ldp q0, q1, [sp, #80]\n"
                     "ldp x8, x9, [sp, #64]\n"
                     "ldp x6, x7, [sp, #48]\n"
                     "ldp x4, x5, [sp, #32]\n"
                     "ldp x2, x3, [sp, #16]\n"
                     "ldp x0, x1, [sp, #0]\n"
                     
                     "add sp, sp, #512\n"
                     "ret"
                     );
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
    
    // To combat unrealized classes during objc_msgSend argument capturing at process launch, before enabling interception
    // run through all objc classes to ensure they're realized
    unsigned int class_count = 0;
    Class *classes = objc_copyClassList(&class_count);
    if (classes == NULL) {
        tracer_set_error(g_tracer_ctx, "init_message_interception: Failed to get class list");
        return TRACER_ERROR_INITIALIZATION;
    }

    for (unsigned int i = 0; i < class_count; i++) {
        Class cls = classes[i];
        if (cls == NULL) {
            continue;
        }

        class_isMetaClass(cls);
        record_class_encounter(object_getClass((id)cls));
    }
    free(classes);
    
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
    
    void *_objc_msgSend = get_original_objc_msgSend();
    if (_objc_msgSend == NULL) {
        tracer_set_error(g_tracer_ctx, "Failed to locate objc_msgSend");
        return TRACER_ERROR_INITIALIZATION;
    }

#if USE_JAILBREAK_HOOKER
    void *jbhooker_handle = dlopen("/var/jb/usr/lib/libellekit.dylib", 0);
    void *_MSHookFunction = dlsym(jbhooker_handle, "MSHookFunction");
    if (_MSHookFunction) {
        ((void (*)(void *, void *, void **))_MSHookFunction)(_objc_msgSend, new_objc_msgSend, (void **)&original_objc_msgSend);
    }
#else
    {
        original_objc_msgSend = _objc_msgSend;
        if (original_objc_msgSend == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to locate objc_msgSend");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        struct symbol_rebinding_t *rebinding = hook_function("objc_msgSend", new_objc_msgSend);
        if (rebinding == NULL) {
            tracer_set_error(g_tracer_ctx, "Failed to hook objc_msgSend");
            return TRACER_ERROR_INITIALIZATION;
        }
        
        free(rebinding);
    }
#endif
    return TRACER_SUCCESS;
}
