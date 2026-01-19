#include <Foundation/Foundation.h>

#if !defined(__arm64__)
#warning "dylib_injector.m is only supported on arm64 architecture."
kern_return_t inject_dylib_into_pid(const char *dylib_path, int pid) {
    return -1;
}

uint64_t get_function_address_in_pid(const char *function_name, const char *image_filter, int pid) {
    return 0;
}

kern_return_t call_remote_function_with_string(uint64_t function_address, const char *string_arg, pid_t pid) {
    return -1;
}

#else

#include <mach/mach.h>
#include <dlfcn.h>
#include "symbolication.h"

extern kern_return_t mach_vm_allocate(vm_map_t target, mach_vm_address_t *address, mach_vm_size_t size, int flags);
extern kern_return_t mach_vm_deallocate(vm_map_t target, mach_vm_address_t address, mach_vm_size_t size);
extern kern_return_t mach_vm_protect(vm_map_t target_task, mach_vm_address_t address, mach_vm_size_t size, boolean_t set_maximum, vm_prot_t new_protection);
extern kern_return_t mach_vm_write(vm_map_t target_task, mach_vm_address_t address, vm_offset_t data, mach_msg_type_number_t dataCnt);

kern_return_t call_remote_function_with_string(uint64_t function_address, const char *string_arg, uint64_t second_arg, pid_t pid) {
    mach_port_t task = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS || task == MACH_PORT_NULL) {
        printf("task_for_pid(%d) failed: %s\n", pid, mach_error_string(kr));
        return -1;
    }

    mach_vm_size_t stack_size = 0x10000;
    
    mach_vm_address_t remote_stack = 0;
    kr = mach_vm_allocate(task, &remote_stack, stack_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_allocate(task=0x%x, size=0x%llx) failed: %s\n", task, stack_size, mach_error_string(kr));
        return -1;
    }
    
    kr = mach_vm_protect(task, remote_stack, stack_size, 0, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_protect(task=0x%x, addr=0x%llx, size=0x%llx) failed: %s\n", task, remote_stack, stack_size, mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        return -1;
    }

    size_t string_len = strlen(string_arg) + 1;
    size_t string_alloc_size = (string_len + 0xFFF) & ~0xFFF;
    mach_vm_address_t remote_string = 0;
    kr = mach_vm_allocate(task, &remote_string, string_alloc_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_allocate for remote string failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        return -1;
    }
    
    kr = mach_vm_write(task, remote_string, (vm_offset_t)string_arg, (mach_msg_type_number_t)string_len);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_write(task=0x%x, addr=0x%llx) for remote string failed: %s\n", task, remote_string, mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        return -1;
    }

    void *pthread_create_addr = dlsym(RTLD_DEFAULT, "pthread_create_from_mach_thread");
    void *pthread_exit_addr = dlsym(RTLD_DEFAULT, "pthread_exit");
    if (pthread_create_addr == NULL || pthread_exit_addr == NULL) {
        printf("failed to resolve pthread symbols\n");
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        return -1;
    }

    uint32_t payload_code[] = {
        0x58000100,  // 0x00: ldr x0, pc+32  -> 0x20 (string_addr)
        0x58000121,  // 0x04: ldr x1, pc+36  -> 0x24 (second_arg)
        0x58000148,  // 0x08: ldr x8, pc+40  -> 0x28 (function_addr)
        0xD63F0100,  // 0x0C: blr x8
        0xD2800000,  // 0x10: mov x0, #0
        0x58000128,  // 0x14: ldr x8, pc+36  -> 0x38 (pthread_create_ptr)
        0xD63F0100,  // 0x18: blr x8
        0x14000000,  // 0x1C: b #0
    };

    size_t code_size = sizeof(payload_code);
    size_t payload_total_size = code_size + (4 * sizeof(uint64_t));
    payload_total_size = (payload_total_size + 0xFFF) & ~0xFFF;
    
    mach_vm_address_t remote_payload = 0;
    kr = mach_vm_allocate(task, &remote_payload, payload_total_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_allocate for payload failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        return -1;
    }
    
    uint8_t *payload_buf = calloc(1, payload_total_size);
    memcpy(payload_buf, payload_code, code_size);
    memcpy(payload_buf + 0x20, &remote_string, sizeof(uint64_t));
    memcpy(payload_buf + 0x28, &second_arg, sizeof(uint64_t));
    memcpy(payload_buf + 0x30, &function_address, sizeof(uint64_t));
    memcpy(payload_buf + 0x38, &pthread_exit_addr, sizeof(uint64_t));
    
    kr = mach_vm_write(task, remote_payload, (vm_offset_t)payload_buf, (mach_msg_type_number_t)payload_total_size);
    free(payload_buf);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_write for payload failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        return -1;
    }
    
    kr = mach_vm_protect(task, remote_payload, payload_total_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_protect for payload failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        return -1;
    }

    uint32_t bootstrap_code[] = {
        0x910023E0,  // 0x00: add x0, sp, #8
        0xD2800001,  // 0x04: mov x1, #0
        0xD2800003,  // 0x08: mov x3, #0
        0x580000A8,  // 0x0C: ldr x8, pc+20 -> 0x20
        0xD63F0100,  // 0x10: blr x8
        0xD2800849,  // 0x14: mov x9, #0x42
        0x14000000,  // 0x18: b #0
        0xD503201F,  // 0x1C: nop
    };
    
    size_t bootstrap_size = sizeof(bootstrap_code) + sizeof(uint64_t);
    bootstrap_size = (bootstrap_size + 0xFFF) & ~0xFFF;
    
    mach_vm_address_t remote_bootstrap = 0;
    kr = mach_vm_allocate(task, &remote_bootstrap, bootstrap_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_allocate for bootstrap failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        return -1;
    }
    
    uint8_t *bootstrap_buf = calloc(1, bootstrap_size);
    memcpy(bootstrap_buf, bootstrap_code, sizeof(bootstrap_code));
    memcpy(bootstrap_buf + 0x20, &pthread_create_addr, sizeof(uint64_t));
    
    kr = mach_vm_write(task, remote_bootstrap, (vm_offset_t)bootstrap_buf, (mach_msg_type_number_t)bootstrap_size);
    free(bootstrap_buf);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_write for bootstrap failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        mach_vm_deallocate(task, remote_bootstrap, bootstrap_size);
        return -1;
    }
    
    kr = mach_vm_protect(task, remote_bootstrap, bootstrap_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_protect for bootstrap failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        mach_vm_deallocate(task, remote_bootstrap, bootstrap_size);
        return -1;
    }

    arm_thread_state64_t state;
    bzero(&state, sizeof(arm_thread_state64_t));
    state.__x[2] = remote_payload;
    __darwin_arm_thread_state64_set_pc_fptr(state, (void *)remote_bootstrap);
    __darwin_arm_thread_state64_set_sp(state, (void *)(remote_stack + stack_size - 0x10));
        
    mach_port_t remote_thread;
    kr = thread_create_running(task, ARM_THREAD_STATE64, (thread_state_t)&state, ARM_THREAD_STATE64_COUNT, &remote_thread);
    if (kr != KERN_SUCCESS) {
        printf("thread_create_running failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_string, string_alloc_size);
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        mach_vm_deallocate(task, remote_bootstrap, bootstrap_size);
        return -1;
    }

    arm_thread_state64_t poll_state;
    mach_msg_type_number_t state_count;
    int completed = 0;
    
    for (int i = 0; i < 5000; i++) {
        usleep(1000);
        
        state_count = ARM_THREAD_STATE64_COUNT;
        kr = thread_get_state(remote_thread, ARM_THREAD_STATE64, (thread_state_t)&poll_state, &state_count);
        if (kr != KERN_SUCCESS) {
            printf("thread_get_state failed: %s\n", mach_error_string(kr));
            break;
        }
        
        if (poll_state.__x[9] == 0x42) {
            completed = 1;
            break;
        }
    }
    
    thread_terminate(remote_thread);
    
    if (completed) {
        usleep(100000);
    }
    
    return completed ? KERN_SUCCESS : -1;
}

kern_return_t inject_dylib_into_pid(const char *dylib_path, int pid) {
    uint64_t dlopen_address = (uint64_t)dlsym(RTLD_DEFAULT, "dlopen");
    return call_remote_function_with_string(dlopen_address, dylib_path, RTLD_NOW, pid);
}

uint64_t remote_dlsym(int pid, const char *image_name, const char *symbol_name) {
    mach_port_t task;
    if (task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS) {
        printf("%s: task_for_pid(%d) failed\n", __func__, pid);
        return 0;
    }
    
    CSSymbolicatorRef symbolicator = create_symbolicator_with_task(task);
    if (cs_isnull(symbolicator)) {
        printf("%s: Failed to create symbolicator for pid %d\n", __func__, pid);
        return 0;
    }

    __block CSSymbolRef resolved_symbol = (CSSymbolRef){};
    if (image_name != NULL) {
        // Search all symbols owners looking for one with a path that matches/contains image_name
        for_each_symbol_owner(symbolicator, ^(CSSymbolOwnerRef owner) {
            if (!cs_isnull(resolved_symbol)) {
                // Already found the symbol
                return;
            }
            
            if (cs_isnull(owner)) {
                return;
            }
            
            const char *image_path = get_image_path_for_symbol_owner(owner);
            if (image_path == NULL || strstr(image_path, image_name) == NULL) {
                // Not a match
                return;
            }
            
            // Found a matching symbol owner. Try to resolve the symbol from it
            resolved_symbol = get_symbol_from_owner_with_name(owner, symbol_name);
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
            if (name == NULL || strcmp(name, symbol_name) != 0) {
                return;
            }
            resolved_symbol = current_symbol;
        });
    }
    
    if (cs_isnull(resolved_symbol)) {
        return 0;
    }
    
    return get_range_for_symbol(resolved_symbol).location;
}

#endif // !defined(__arm64__)
