//
//  dylib_injector.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/16/25.
//

#ifndef dylib_injector_h
#define dylib_injector_h

#include <Foundation/Foundation.h>

/**
 * Calls a function in a remote process
 * @param function_address The address of the function to call
 * @param string_arg A string argument to pass to the function
 * @param second_arg A second argument to pass to the function, or 0
 * @param pid The process ID to call the function in
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t call_remote_function_with_string(uint64_t function_address, const char *string_arg, uint64_t second_arg, pid_t pid);

/**
 * Injects a dylib into a remote process
 * @param dylib_path The path to the dylib to inject
 * @param pid The process ID to inject into
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t inject_dylib_into_pid(const char *dylib_path, int pid);

/**
 * Get the address of a function in a remote process
 * @param pid The process ID to query
 * @param image_name The name of the image (dylib) containing the symbol
 * @param symbol_name The name of the symbol to look up
 * @return The address of the symbol in the remote process, or 0 on failure
 */
uint64_t remote_dlsym(int pid, const char *image_name, const char *symbol_name);

#endif /* dylib_injector_h */
