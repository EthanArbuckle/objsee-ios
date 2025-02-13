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
 * @param pid The process ID to call the function in
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t call_remote_function_with_string(uint64_t function_address, const char *string_arg, pid_t pid);

/**
 * Injects a dylib into a remote process
 * @param dylib_path The path to the dylib to inject
 * @param pid The process ID to inject into
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t inject_dylib_into_pid(const char *dylib_path, int pid);

/**
 * Get the address of a function in a remote process
 * @param function_name The name of the function to find
 * @param image_filter The name of the image to search in. If NULL, all images will be searched
 * @param pid The process ID to search in
 * @return The address of the function in the remote process
 */
uint64_t get_function_address_in_pid(const char *function_name, const char *image_filter, int pid);

#endif /* dylib_injector_h */
