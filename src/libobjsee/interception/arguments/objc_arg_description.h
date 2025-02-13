//
//  objc_arg_description.h
//  objsee
//
//  Created by Ethan Arbuckle on 2/7/25.
//

#ifndef OBJC_ARG_DESCRIPTION_H
#define OBJC_ARG_DESCRIPTION_H


/**
 * @brief Lookup the description of an objective-c object at a given address
 * @param address The address of the object
 * @param obj_class The objective-c class of the object at address
 * @return The description of the object, or NULL if it could not be determined
 *
 * @note The returned string is owned by a cache and should not be freed
 */
const char *lookup_description_for_address(void *address, Class obj_class);


#endif /* OBJC_ARG_DESCRIPTION_H */
