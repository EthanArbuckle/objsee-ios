//
//  dylib_injector.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/16/25.
//

#ifndef dylib_injector_h
#define dylib_injector_h

#include <Foundation/Foundation.h>

extern kern_return_t mach_vm_allocate(vm_map_t target, mach_vm_address_t *address, mach_vm_size_t size, int flags);
extern kern_return_t mach_vm_deallocate(vm_map_t target, mach_vm_address_t address, mach_vm_size_t size);
extern kern_return_t mach_vm_protect(vm_map_t target_task, mach_vm_address_t address, mach_vm_size_t size, boolean_t set_maximum, vm_prot_t new_protection);
extern kern_return_t mach_vm_write(vm_map_t target_task, mach_vm_address_t address, vm_offset_t data, mach_msg_type_number_t dataCnt);


/**
 * Calls a function in a remote process
 * @param task The mach port of the remote task
 * @param function_address The address of the function to call in the remote task
 * @param remote_string The address of the string argument in the remote task
 * @param second_arg A second argument to pass to the function
 * @return KERN_SUCCESS on success, -1 on failure
 */
kern_return_t call_remote_function_with_string(mach_port_t task, mach_vm_address_t function_address, mach_vm_address_t remote_string, uint32_t second_arg);


/**
 * Gets the mach port for a given PID
 * @param pid The process ID to get the mach port for
 * @return The mach port of the task, or MACH_PORT_NULL on failure
 */
mach_port_t get_task_for_pid(int pid);


/**
 * Writes a string to the remote task's memory
 * @param task The mach port of the remote task
 * @param string The string to write
 * @param remote_address Output parameter for the address of the string in the remote task
 * @param string_alloc_size Output parameter for the size of the allocated string
 * @return KERN_SUCCESS on success, -1 on failure
 */
kern_return_t write_string_to_remote_task(mach_port_t task, const char *string, mach_vm_address_t *remote_address, size_t *string_alloc_size);


/**
 * Injects a dylib into a remote process
 * @param pid The process ID to inject into
 * @param dylib_path The path to the dylib to inject
 * @param flags Flags for dlopen
 * @return KERN_SUCCESS on success, -1 on failure
 */
kern_return_t remote_dlopen(int pid, const char *dylib_path, int flags);


/**
 * Get the address of a function in a remote process
 * @param pid The process ID to query
 * @param image_name The name of the image (dylib) containing the symbol
 * @param symbol_name The name of the symbol to look up
 * @return The address of the symbol in the remote process, or 0 on failure
 */
mach_vm_address_t remote_dlsym(int pid, const char *image_name, const char *symbol_name);

#endif /* dylib_injector_h */
