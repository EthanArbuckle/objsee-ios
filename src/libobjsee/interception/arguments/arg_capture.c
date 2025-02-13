//
//  arg_capture.c
//  objsee
//
//  Created by Ethan Arbuckle on 1/30/25.
//

#include <mach/mach.h>
#include <os/log.h>
#include "realized_class_tracking.h"
#include "objc_arg_description.h"
#include "tracer_internal.h"
#include "arg_description.h"
#include "signal_guard.h"
#include "encoding_size.h"
#include "objc-internal.h"

#define printf(...) os_log(OS_LOG_DEFAULT, __VA_ARGS__)

__attribute__((aligned(16), always_inline, hot))
void capture_arguments(tracer_t *g_tracer_ctx, struct tracer_thread_context_frame_t *frame, void *stack_base, tracer_event_t *event) {
    if (stack_base == NULL || event == NULL) {
        return;
    }
    
    Method method = class_getInstanceMethod(frame->self_class, frame->_cmd);
    if (method == NULL) {
        return;
    }
    
    unsigned int arg_count = method_getNumberOfArguments(method);
    if (arg_count <= 2 || arg_count >= 32) {
        return;
    }
    
    event->method_signature = method_getTypeEncoding(method);
    if (event->method_signature == NULL) {
        tracer_set_error(g_tracer_ctx, "Failed to locate method type encoding");
        return;
    }
    
    vm_address_t signature_copy;
    size_t sig_len = strlen(event->method_signature) + 1;
    if (vm_allocate(mach_task_self(), &signature_copy, sig_len, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        tracer_set_error(g_tracer_ctx, "Failed to allocate memory for signature copy");
        return;
    }
    memcpy((void *)signature_copy, event->method_signature, sig_len);
    event->method_signature = (const char *)signature_copy;
    
    size_t offsets[32] = {0};
    memset(offsets, 0, sizeof(offsets));
    if (get_offsets_of_args_using_type_encoding(event->method_signature, offsets, arg_count) != KERN_SUCCESS) {
        tracer_set_error(g_tracer_ctx, "Failed to get offsets of arguments");
        vm_deallocate(mach_task_self(), signature_copy, sig_len);
        return;
    }
    
    event->argument_count = arg_count - 2;
    vm_address_t args_buf;
    size_t args_size = event->argument_count * sizeof(tracer_argument_t);
    if (vm_allocate(mach_task_self(), &args_buf, args_size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        tracer_set_error(g_tracer_ctx, "Failed to allocate memory for arguments");
        vm_deallocate(mach_task_self(), signature_copy, sig_len);
        return;
    }
    memset((void *)args_buf, 0, args_size);
    event->arguments = (tracer_argument_t *)args_buf;
    
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
        
        if (_objc_isTaggedPointer(event_arg->address)) {
            os_log(OS_LOG_DEFAULT, "Tagged pointer at address %p\n", event_arg->address);
            continue;
        }
        
        char *arg_type = method_copyArgumentType(method, i);
        if (arg_type == NULL) {
            printf("Failed to get type encoding for arg %d\n", i);
            continue;
        }
        
        vm_address_t type_copy;
        size_t type_len = strlen(arg_type) + 1;
        if (vm_allocate(mach_task_self(), &type_copy, type_len, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
            memcpy((void *)type_copy, arg_type, type_len);
            event_arg->type_encoding = (const char *)type_copy;
        }
        free(arg_type);
        
        if (event_arg->type_encoding == NULL) {
            continue;
        }
        
        event_arg->size = get_size_of_type_from_type_encoding(event_arg->type_encoding);
        if (event_arg->size == 0) {
            printf("Failed to get size of arg %d of type %s\n", i, event_arg->type_encoding);
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
            bool isClass = object_isClass((id)object_class);
            if (isClass == false) {
                printf("Object at address %p is not a class\n", objc_object);
                return;
            }
            
            size_t instance_size = class_getInstanceSize(object_class);
            if (malloc_sz > 0 && malloc_sz < instance_size) {
                return;
            }
            
            WHILE_IGNORING_SIGNALS({
                const char *class_name = object_getClassName(objc_object);
                if (class_name == NULL) {
                    return;
                }
                
                vm_address_t name_copy;
                size_t name_len = strlen(class_name) + 1;
                if (vm_allocate(mach_task_self(), &name_copy, name_len, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
                    memcpy((void *)name_copy, class_name, name_len);
                    event_arg->objc_class_name = (const char *)name_copy;
                }
                event_arg->objc_class = object_class;
            });
            
            if (event_arg->objc_class_name == NULL) {
                continue;
            }
            
            // Interacting with unrealized classes is dangerous.
            // Record the encounter so that it's captured on the next occurrence
            if (has_seen_class(event_arg->objc_class) == false) {
                record_class_encounter(event_arg->objc_class);
                continue;
            }
            
            char description_buf[1024];
            if (description_for_argument(event_arg, g_tracer_ctx->config.format.args, description_buf, sizeof(description_buf)) != KERN_SUCCESS) {
                printf("Failed to get description for objc argument %d of type %s\n", i, event_arg->type_encoding);
                continue;
            }
            
            vm_address_t desc_copy;
            size_t desc_len = strlen(description_buf) + 1;
            if (vm_allocate(mach_task_self(), &desc_copy, desc_len, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
                memcpy((void *)desc_copy, description_buf, desc_len);
                event_arg->description = (const char *)desc_copy;
            }
        }
        else {
            // Make a copy of the argument value. The real one is vulnerable to external modification / deallocation,
            // which could cause crashes when passing it to runtime functions like object_getClass()
            if (event_arg->address == NULL) {
                vm_deallocate(mach_task_self(), type_copy, type_len);
                continue;
            }
            
            if ((uintptr_t)event_arg->address < 0x1000) {
                printf("Invalid argument address: %p\n", event_arg->address);
                vm_deallocate(mach_task_self(), type_copy, type_len);
                continue;
            }
            
            vm_address_t arg_value_buf;
            if (vm_allocate(mach_task_self(), &arg_value_buf, event_arg->size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
                printf("Failed to allocate memory for argument value with size %zu\n", event_arg->size);
                vm_deallocate(mach_task_self(), type_copy, type_len);
                continue;
            }
            
            kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)event_arg->address, event_arg->size, (vm_address_t)arg_value_buf, &event_arg->size);
            if (kr != KERN_SUCCESS) {
                printf("Failed to read argument value at address %p: %s\n", event_arg->address, mach_error_string(kr));
                vm_deallocate(mach_task_self(), arg_value_buf, event_arg->size);
                vm_deallocate(mach_task_self(), type_copy, type_len);
                continue;
            }
            
            uint64_t original_arg_address = (uint64_t)event_arg->address;
            event_arg->address = (void *)arg_value_buf;
            
            char description_buf[1024];
            if (description_for_argument(event_arg, g_tracer_ctx->config.format.args, description_buf, sizeof(description_buf)) != KERN_SUCCESS) {
                printf("Failed to get description for basic argument %d of type %s\n", i, event_arg->type_encoding);
                vm_deallocate(mach_task_self(), arg_value_buf, event_arg->size);
                event_arg->address = (void *)original_arg_address;
                continue;
            }
            
            vm_address_t desc_copy;
            size_t desc_len = strlen(description_buf) + 1;
            if (vm_allocate(mach_task_self(), &desc_copy, desc_len, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
                memcpy((void *)desc_copy, description_buf, desc_len);
                event_arg->description = (const char *)desc_copy;
            }
            
            vm_deallocate(mach_task_self(), arg_value_buf, event_arg->size);
            event_arg->address = (void *)original_arg_address;
        }
    }
}
