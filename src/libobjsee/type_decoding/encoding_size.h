//
//  encoding_size.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 1/11/25.
//

#ifndef encoding_size_h
#define encoding_size_h

#include <objc/runtime.h>


/**
 * @brief Get the size of a type from its type encoding
 *
 * @param type_encoding The type encoding
 * @return The size of the type
 */
size_t get_size_of_type_from_type_encoding(const char *type_encoding);


/**
 * @brief Get the offsets of arguments in a method signature
 *
 * @param type_encoding The method signature
 * @param offsets The array to store the offsets in
 * @param arg_count The number of arguments
 *
 * @return KERN_SUCCESS on success, or an error code on failure
 */
kern_return_t get_offsets_of_args_using_type_encoding(const char *type_encoding, size_t *offsets, size_t arg_count);

#endif /* encoding_size_h */
