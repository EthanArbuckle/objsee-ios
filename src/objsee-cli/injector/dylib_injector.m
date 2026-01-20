#include <Foundation/Foundation.h>
#include <mach/mach.h>
#include <dlfcn.h>
#include "dylib_injector.h"
#include "symbolication.h"

mach_port_t get_task_for_pid(int pid) {
    mach_port_t task = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS || task == MACH_PORT_NULL) {
        printf("%s: task_for_pid(%d) failed: %s\n", __func__, pid, mach_error_string(kr));
        return MACH_PORT_NULL;
    }

    return task;
}

kern_return_t write_string_to_remote_task(mach_port_t task, const char *string, mach_vm_address_t *remote_address, size_t *string_alloc_size) {
    size_t string_len = strlen(string) + 1;
    size_t alloc_size = (string_len + 0xFFF) & ~0xFFF;
    mach_vm_address_t remote_string = 0;
    kern_return_t kr = mach_vm_allocate(task, &remote_string, alloc_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        printf("%s: remote_string mach_vm_allocate(task=0x%x, size=0x%zx) failed: %s\n", __func__, task, alloc_size, mach_error_string(kr));
        return -1;
    }
    
    kr = mach_vm_write(task, remote_string, (vm_offset_t)string, (mach_msg_type_number_t)string_len);
    if (kr != KERN_SUCCESS) {
        printf("%s: remote_string mach_vm_write(task=0x%x, addr=0x%llx) failed: %s\n", __func__, task, remote_string, mach_error_string(kr));
        mach_vm_deallocate(task, remote_string, alloc_size);
        return -1;
    }
    
    *remote_address = remote_string;
    *string_alloc_size = alloc_size;
    return KERN_SUCCESS;
}

#if defined(__arm64__)

kern_return_t call_remote_function_with_string(mach_port_t task, mach_vm_address_t function_address, mach_vm_address_t remote_string, uint32_t second_arg) {
    mach_vm_size_t stack_size = 0x10000;
    mach_vm_address_t remote_stack = 0;
    kern_return_t kr = mach_vm_allocate(task, &remote_stack, stack_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        printf("%s: remote_stack mach_vm_allocate(task=0x%x, size=0x%llx) failed: %s\n", __func__, task, stack_size, mach_error_string(kr));
        return -1;
    }
    
    kr = mach_vm_protect(task, remote_stack, stack_size, 0, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_protect(task=0x%x, addr=0x%llx, size=0x%llx) failed: %s\n", task, remote_stack, stack_size, mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        return -1;
    }

    void *pthread_create_addr = dlsym(RTLD_DEFAULT, "pthread_create_from_mach_thread");
    void *pthread_exit_addr = dlsym(RTLD_DEFAULT, "pthread_exit");
    if (pthread_create_addr == NULL || pthread_exit_addr == NULL) {
        printf("%s: failed to resolve pthread symbols\n", __func__);
        mach_vm_deallocate(task, remote_stack, stack_size);
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
        printf("%s: remote_payload mach_vm_allocate(task=0x%x, size=0x%zx) failed: %s\n,", __func__, task, payload_total_size, mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
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
        printf("%s: remote_payload mach_vm_write(task=0x%x, addr=0x%llx) failed: %s\n", __func__, task, remote_payload, mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        return -1;
    }
    
    kr = mach_vm_protect(task, remote_payload, payload_total_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        printf("%s: remote_payload mach_vm_protect(task=0x%x, addr=0x%llx) failed: %s\n", __func__, task, remote_payload, mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
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
        mach_vm_deallocate(task, remote_payload, payload_total_size);
        mach_vm_deallocate(task, remote_bootstrap, bootstrap_size);
        return -1;
    }
    
    kr = mach_vm_protect(task, remote_bootstrap, bootstrap_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        printf("mach_vm_protect for bootstrap failed: %s\n", mach_error_string(kr));
        mach_vm_deallocate(task, remote_stack, stack_size);
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

#else

kern_return_t call_remote_function_with_string(mach_port_t task, mach_vm_address_t function_address, mach_vm_address_t remote_string, uint32_t second_arg) {
    thread_act_array_t thread_list;
    mach_msg_type_number_t thread_count;
    kern_return_t kr = task_threads(task, &thread_list, &thread_count);
    if (kr != KERN_SUCCESS) {
        printf("%s: task_threads failed: %s\n", __func__, mach_error_string(kr));
        return kr;
    }

    if (thread_count == 0) {
        printf("%s: No threads found in target task\n", __func__);
        return KERN_FAILURE;
    }

    mach_port_t target_thread = thread_list[0];
    if (thread_count > 2) {
        target_thread = thread_list[2];
    }

    mach_vm_address_t remote_page = 0;
    kr = mach_vm_allocate(task, &remote_page, 4096, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        printf("%s: mach_vm_allocate failed: %s\n", __func__, mach_error_string(kr));
        vm_deallocate(mach_task_self(), (vm_address_t)thread_list, thread_count * sizeof(thread_t));
        return kr;
    }

    struct {
        uint16_t code[6];
        uint32_t data[3];
    } trampoline;

    trampoline.code[0] = 0x4802; // ldr r0, [pc, #8]  -> data[0]
    trampoline.code[1] = 0x4903; // ldr r1, [pc, #12] -> data[1]
    trampoline.code[2] = 0x4b03; // ldr r3, [pc, #12] -> data[2]
    trampoline.code[3] = 0x4798; // blx r3
    trampoline.code[4] = 0xe7fe; // b . (trap)
    trampoline.code[5] = 0xbf00; // nop

    trampoline.data[0] = (uint32_t)remote_string;
    trampoline.data[1] = second_arg;
    
    if ((function_address & 1) == 0 && (function_address & 3) != 0) {
        function_address |= 1;
    }
    trampoline.data[2] = function_address;

    mach_vm_write(task, remote_page, (vm_offset_t)&trampoline, sizeof(trampoline));
    mach_vm_protect(task, remote_page, 4096, 0, VM_PROT_READ | VM_PROT_EXECUTE);

    if (thread_suspend(target_thread) != KERN_SUCCESS) {
        printf("%s: thread_suspend failed\n", __func__);
        mach_vm_deallocate(task, remote_page, 4096);
        vm_deallocate(mach_task_self(), (vm_address_t)thread_list, thread_count * sizeof(thread_t));
        return KERN_FAILURE;
    }
    thread_abort(target_thread);

    arm_thread_state_t original_state;
    mach_msg_type_number_t state_count = ARM_THREAD_STATE_COUNT;
    thread_get_state(target_thread, ARM_THREAD_STATE, (thread_state_t)&original_state, &state_count);

    arm_vfp_state_t original_vfp_state;
    mach_msg_type_number_t vfp_count = ARM_VFP_STATE_COUNT;
    thread_get_state(target_thread, ARM_VFP_STATE, (thread_state_t)&original_vfp_state, &vfp_count);

    arm_thread_state_t new_state = original_state;
    new_state.__pc = (uint32_t)remote_page | 1;
    new_state.__cpsr |= 0x20;
    new_state.__lr = (uint32_t)remote_page + 0x08 + 1;
    new_state.__sp = (new_state.__sp - 128) & ~0xF;

    thread_set_state(target_thread, ARM_THREAD_STATE, (thread_state_t)&new_state, ARM_THREAD_STATE_COUNT);
    thread_resume(target_thread);

    int completed = 0;
    arm_thread_state_t cur_state;
    uint32_t trap_addr = (uint32_t)remote_page + 0x08;

    for (int i = 0; i < 2000; i++) { // 2 seconds timeout
        usleep(1000);
        state_count = ARM_THREAD_STATE_COUNT;
        if (thread_get_state(target_thread, ARM_THREAD_STATE, (thread_state_t)&cur_state, &state_count) == KERN_SUCCESS) {
            if ((cur_state.__pc & ~1) == trap_addr) {
                completed = 1;
                break;
            }
        }
    }

    thread_suspend(target_thread);
    thread_set_state(target_thread, ARM_VFP_STATE, (thread_state_t)&original_vfp_state, ARM_VFP_STATE_COUNT);
    thread_set_state(target_thread, ARM_THREAD_STATE, (thread_state_t)&original_state, ARM_THREAD_STATE_COUNT);
    thread_resume(target_thread);

    vm_deallocate(mach_task_self(), (vm_address_t)thread_list, thread_count * sizeof(thread_t));

    return completed ? KERN_SUCCESS : KERN_FAILURE;
}

#endif

kern_return_t remote_dlopen(int pid, const char *dylib_path, int flags) {
    mach_port_t task = get_task_for_pid(pid);

    mach_vm_address_t remote_string = 0;
    size_t string_alloc_size = 0;
    if (write_string_to_remote_task(task, dylib_path, &remote_string, &string_alloc_size) != KERN_SUCCESS) {
        printf("Failed to write remote string.\n");
        return KERN_FAILURE;
    }

    mach_vm_address_t dlopen_address = (mach_vm_address_t)dlsym(RTLD_DEFAULT, "dlopen");
    kern_return_t kr = call_remote_function_with_string(task, (mach_vm_address_t)dlopen_address, remote_string, flags);

    mach_vm_deallocate(task, remote_string, string_alloc_size);

    return kr;
}

mach_vm_address_t remote_dlsym(int pid, const char *image_name, const char *symbol_name) {
    mach_port_t task = get_task_for_pid(pid);
    if (task == MACH_PORT_NULL) {
        printf("%s: Failed to get task for pid %d\n", __func__, pid);
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
