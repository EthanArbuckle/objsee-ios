#include <Foundation/Foundation.h>
#include <mach/mach.h>
#include <dlfcn.h>
#include "symbolication.h"

extern kern_return_t mach_vm_allocate(vm_map_t target, mach_vm_address_t *address, mach_vm_size_t size, int flags);
extern kern_return_t mach_vm_deallocate(vm_map_t target, mach_vm_address_t address, mach_vm_size_t size);
extern kern_return_t mach_vm_protect(vm_map_t target_task, mach_vm_address_t address, mach_vm_size_t size, boolean_t set_maximum, vm_prot_t new_protection);
extern kern_return_t mach_vm_write(vm_map_t target_task, mach_vm_address_t address, vm_offset_t data, mach_msg_type_number_t dataCnt);


kern_return_t call_remote_function_with_string(uint64_t function_address, const char *string_arg, pid_t pid) {
    mach_port_t task;
    if (task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS || task == MACH_PORT_NULL) {
        printf("failed to get task for pid\n");
        return -1;
    }

    mach_vm_size_t stack_size = 0x4000;
    mach_port_insert_right(mach_task_self(), task, task, MACH_MSG_TYPE_COPY_SEND);
    
    mach_vm_address_t remote_stack;
    mach_vm_allocate(task, &remote_stack, stack_size, VM_FLAGS_ANYWHERE);
    mach_vm_protect(task, remote_stack, stack_size, 1, VM_PROT_READ | VM_PROT_WRITE);
    
    mach_vm_address_t remote_string;
    mach_vm_allocate(task, &remote_string, 0x100 + strlen(string_arg) + 1, VM_FLAGS_ANYWHERE);
    mach_vm_write(task, 0x100 + remote_string, (vm_offset_t)string_arg, (mach_msg_type_number_t)strlen(string_arg) + 1);
    
    uint64_t *stack = malloc(stack_size);
    size_t sp = (stack_size / 8) - 2;
    
    mach_vm_write(task, remote_stack, (vm_offset_t)stack, (mach_msg_type_number_t)stack_size);
    
    arm_thread_state64_t state = {};
    bzero(&state, sizeof(arm_thread_state64_t));
    
    state.__x[0] = (uint64_t)remote_stack;
    state.__x[2] = (uint64_t)function_address;
    state.__x[3] = (uint64_t)(remote_string + 0x100);
    __darwin_arm_thread_state64_set_lr_fptr(state, (void *)0x7171717171717171);
    __darwin_arm_thread_state64_set_pc_fptr(state, dlsym(RTLD_NEXT, "pthread_create_from_mach_thread"));
    __darwin_arm_thread_state64_set_sp(state, (void *)(remote_stack + (sp * sizeof(uint64_t))));
    
    mach_port_t exc_handler;
    mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &exc_handler);
    mach_port_insert_right(mach_task_self(), exc_handler, exc_handler, MACH_MSG_TYPE_MAKE_SEND);
    
    mach_port_t remote_thread;
    kern_return_t (*_thread_create_running)(task_t, thread_state_flavor_t, thread_state_t, mach_msg_type_number_t, thread_act_t *) = dlsym(RTLD_DEFAULT, "thread_create_running");
    if (_thread_create_running == NULL) {
        printf("failed to resolve thread_create_running\n");
        return -1;
    }
    if (_thread_create_running(task, ARM_THREAD_STATE64, (thread_state_t)&state, ARM_THREAD_STATE64_COUNT, &remote_thread) != KERN_SUCCESS) {
        free(stack);
        printf("failed to create remote thread\n");
        return -1;
    }
    
    kern_return_t (*_thread_set_exception_ports)(thread_act_t, exception_mask_t, mach_port_t, exception_behavior_t, thread_state_flavor_t) = dlsym(RTLD_DEFAULT, "thread_set_exception_ports");
    if (_thread_set_exception_ports == NULL) {
        free(stack);
        printf("failed to resolve thread_set_exception_ports\n");
        return -1;
    }
    if (_thread_set_exception_ports(remote_thread, EXC_MASK_BAD_ACCESS, exc_handler, EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, ARM_THREAD_STATE64) != KERN_SUCCESS) {
        free(stack);
        printf("failed to set remote exception port\n");
        return -1;
    }
    
    thread_resume(remote_thread);
    usleep(1000);
    
    void (*_mach_msg)(mach_msg_header_t *, mach_msg_option_t, mach_msg_size_t, mach_msg_size_t, mach_port_t, mach_msg_timeout_t, mach_port_t) = dlsym(RTLD_DEFAULT, "mach_msg");
    if (_mach_msg == NULL) {
        free(stack);
        printf("failed to resolve mach_msg\n");
        return -1;
    }
    
    mach_msg_header_t *msg = malloc(0x4000);
    _mach_msg(msg, MACH_RCV_MSG | MACH_RCV_LARGE, 0, 0x4000, exc_handler, 0, MACH_PORT_NULL);
    free(msg);
    
    kern_return_t (*_thread_terminate)(thread_act_t) = dlsym(RTLD_DEFAULT, "thread_terminate");
    if (_thread_terminate == NULL) {
        free(stack);
        printf("failed to resolve thread_terminate\n");
        return -1;
    }
    
    _thread_terminate(remote_thread);
    free(stack);
    
    mach_vm_deallocate(task, remote_stack, stack_size);
    mach_vm_deallocate(task, remote_string, strlen(string_arg) + 1);
    return KERN_SUCCESS;
}

kern_return_t inject_dylib_into_pid(const char *dylib_path, int pid) {
    uint64_t dlopen_address = (uint64_t)dlsym(RTLD_DEFAULT, "dlopen");
    if (call_remote_function_with_string(dlopen_address, dylib_path, pid) != KERN_SUCCESS) {
        printf("Failed to load dylib\n");
        return -1;
    }
    
    return KERN_SUCCESS;
}

uint64_t get_function_address_in_pid(const char *function_name, const char *image_filter, int pid) {
    mach_port_t task;
    if (task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS) {
        printf("failed to get task for pid\n");
        return 0;
    }
    
    CSSymbolicatorRef symbolicator = create_symbolicator_with_task(task);
    if (cs_isnull(symbolicator)) {
        printf("Failed to create symbolicator\n");
        return -1;
    }

    __block CSSymbolRef resolved_symbol = (CSSymbolRef){};
    if (image_filter != NULL) {
        // Search all symbols owners looking for one with a path that matches image_filter
        for_each_symbol_owner(symbolicator, ^(CSSymbolOwnerRef owner) {
            if (!cs_isnull(resolved_symbol)) {
                // Already found the symbol
                return;
            }
            
            if (cs_isnull(owner)) {
                return;
            }
            
            const char *image_path = get_image_path_for_symbol_owner(owner);
            if (image_path == NULL) {
                return;
            }
            
            // Found a matching symbol owner. Try to resolve the symbol from it
            resolved_symbol = get_symbol_from_owner_with_name(owner, function_name);
        });
    }
    else {
        // Search all owners for the symbol. This is slower
        for_each_symbol(symbolicator, ^(CSSymbolRef current_symbol) {
            if (!cs_isnull(resolved_symbol)) {
                // Already found the symbol
                return;
            }

            const char *name = get_name_for_symbol(current_symbol);
            if (name == NULL || strcmp(name, function_name) != 0) {
                return;
            }
            resolved_symbol = current_symbol;
        });
    }
    
    if (cs_isnull(resolved_symbol)) {
        printf("Failed to find symbol %s in pid %d\n", function_name, pid);
        return 0;
    }
    
    return get_range_for_symbol(resolved_symbol).location;
}
