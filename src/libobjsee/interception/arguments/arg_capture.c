//
//  arg_capture.c
//  objsee
//
//  Created by Ethan Arbuckle on 1/30/25.
//

#include <mach/mach.h>
#include "objc_arg_description.h"
#include "tracer_internal.h"
#include "arg_description.h"
#include "encoding_size.h"
#include "objc-internal.h"
#include "logging.h"
#include "class_name_cache.h"

__attribute__((aligned(16), always_inline, hot))
void capture_arguments(tracer_t *g_tracer_ctx, struct tracer_thread_context_frame_t *frame, void *stack_base, tracer_event_t *event) {
    Method method;
    if (frame->selector_is_class_method) {
        method = class_getClassMethod(frame->self_class, frame->_cmd);
    }
    else {
        method = class_getInstanceMethod(frame->self_class, frame->_cmd);
    }
    
    if (method == NULL) {
        return;
    }
    
    unsigned int arg_count = method_getNumberOfArguments(method);
    if (arg_count <= 2 || arg_count >= 32) {
        return;
    }
    
    const char *method_signature = method_getTypeEncoding(method);
    if (method_signature == NULL) {
        tracer_set_error(g_tracer_ctx, "Failed to locate method type encoding");
        return;
    }
    
    event->method_signature = strdup(method_signature);
    
    size_t offsets[32] = {0};
    memset(offsets, 0, sizeof(offsets));
    if (get_offsets_of_args_using_type_encoding(method_signature, offsets, arg_count) != KERN_SUCCESS) {
        tracer_set_error(g_tracer_ctx, "Failed to get offsets of arguments");
        return;
    }
    
    event->argument_count = arg_count - 2;
    event->arguments = (tracer_argument_t *)calloc(event->argument_count, sizeof(tracer_argument_t));
    
    for (unsigned int i = 2; i < arg_count; i++) {
        tracer_argument_t *event_arg = &event->arguments[i - 2];
        
        size_t arg_stack_offset = offsets[i];
        if (arg_stack_offset < 0 || arg_stack_offset >= 512) {
            continue;
        }
        
        event_arg->address = (char *)stack_base + arg_stack_offset;
        event_arg->objc_class = NULL;
        event_arg->objc_class_name = NULL;
        event_arg->block_signature = NULL;
        event_arg->description = NULL;
        
        if (_objc_isTaggedPointer(*(void **)event_arg->address)) {
            continue;
        }
        
        char arg_type[256];
        method_getArgumentType(method, i, arg_type, sizeof(arg_type));
        if (arg_type[0] == '\0') {
            objsee_log("Failed to get type encoding for argument %d\n", i);
            continue;
        }

        event_arg->type_encoding = strdup(arg_type);
        if (event_arg->type_encoding == NULL) {
            continue;
        }
        
        event_arg->size = get_size_of_type_from_type_encoding(event_arg->type_encoding);
        if (event_arg->size == 0) {
            objsee_log("Failed to get size of arg %d of type %s\n", i, event_arg->type_encoding);
            continue;
        }
        
        if (event_arg->type_encoding[0] == '@') {
            __unsafe_unretained id objc_object = *(id *)event_arg->address;
            if (objc_object == nil || (uintptr_t)objc_object < 0x1000) {
                continue;
            }

            size_t malloc_sz = malloc_size(objc_object);
            if (malloc_sz <= 0) {
                continue;
            }
            
            Class object_class = object_getClass(objc_object);
            if (object_class == nil || (uintptr_t)object_class < 0x10000) {
                continue;
            }
            
            // Skip classes that are not yet realized. Interacting with them is dangerous
            if (!is_class_realized(object_class)) {
                continue;
            }

            const char *class_name = class_name_cache_get(object_class);
            if (class_name == NULL) {
                return;
            }

            event_arg->objc_class_name = class_name;
            event_arg->objc_class = object_class;
            
            char description_buf[1024];
            if (description_for_argument(event_arg, g_tracer_ctx->config.format.args, description_buf, sizeof(description_buf)) != KERN_SUCCESS) {
                const char *class_name = event_arg->objc_class_name ? event_arg->objc_class_name : "unknown";
                const char *sel_name = event->method_name ? event->method_name : "unknown";
                objsee_log("Failed to get description for objc argument %d of type %s, class: %s, sel: %s, sig: %s\n", i, event_arg->type_encoding, class_name, sel_name, event->method_signature);
                continue;
            }

            event_arg->description = strdup(description_buf);
        }
        else if (event_arg->type_encoding[0] == '#') {
            Class cls = *(Class *)event_arg->address;
            if (cls == NULL || (uintptr_t)cls < 0x10000) {
                continue;
            }
            
            // Skip classes that are not yet realized. Interacting with them is dangerous
            if (!is_class_realized(cls)) {
                continue;
            }

            char description_buf[1024];
            if (description_for_argument(event_arg, g_tracer_ctx->config.format.args, description_buf, sizeof(description_buf)) != KERN_SUCCESS) {
                const char *class_name = event_arg->objc_class_name ? event_arg->objc_class_name : "unknown";
                const char *sel_name = event->method_name ? event->method_name : "unknown";
                objsee_log("Failed to get description for objc argument %d of type %s, class: %s, sel: %s, sig: %s\n", i, event_arg->type_encoding, class_name, sel_name, event->method_signature);
                continue;
            }
            
            event_arg->description = strdup(description_buf);
        }
            
        else {
            // Make a copy of the argument value. The real one is vulnerable to external modification / deallocation,
            // which could cause crashes when passing it to runtime functions like object_getClass()
            if ((uintptr_t)event_arg->address < 0x1000) {
                objsee_log("Invalid argument address: %p\n", event_arg->address);
                continue;
            }

            char arg_value_buf[512];
            if (event_arg->size <= sizeof(arg_value_buf)) {
                memcpy(arg_value_buf, event_arg->address, event_arg->size);
            }
            else {
                objsee_log("Argument size %d exceeds local buffer size %d for argument %d of type %s\n", (int)event_arg->size, (int)sizeof(arg_value_buf), i, event_arg->type_encoding);
                continue;
            }
            
            uint64_t original_arg_address = (uint64_t)event_arg->address;
            event_arg->address = (void *)arg_value_buf;
            
            char description_buf[1024];
            if (description_for_argument(event_arg, g_tracer_ctx->config.format.args, description_buf, sizeof(description_buf)) != KERN_SUCCESS) {
                objsee_log("Failed to get description for basic argument %d of type %s. class: %s, method: %s, method signature: %s\n", i, event_arg->type_encoding, event->class_name, event->method_name, event->method_signature);
                event_arg->address = (void *)original_arg_address;
                continue;
            }

            event_arg->description = strdup(description_buf);
            event_arg->address = (void *)original_arg_address;
        }
    }
}
