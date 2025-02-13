//
//  encoding_description.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 1/4/25.
//

#ifndef encoding_description_h
#define encoding_description_h

#include <CoreFoundation/CoreFoundation.h>


/**
 * @brief Parse a struct encoding and return a human-readable description
 *
 * @param encoding The encoding to parse
 * @return The description of the struct
 * @note The returned string is malloc'd and must be freed by the caller
 */
char *get_struct_description_from_type_encoding(const char *encoding);


/**
 * @brief Get the name of a type from its type encoding
 *
 * @param type_encoding The type encoding
 * @return The name of the type
 */
const char *get_name_of_type_from_type_encoding(const char *type_encoding);


#endif /* encoding_description_h */
