//
//  substrate_apis.h
//  objsee
//
//  Created by Ethan Arbuckle on 2/1/26.
//

#include <CoreFoundation/CoreFoundation.h>

/*
 * @brief Hooks a function using a MSHookFunction-like API. It will search common paths for the library if needed
    * @param symbol The symbol to hook
    * @param replace The replacement function
    * @param old Pointer to store the original function
 */
kern_return_t objsee_MSHookFunction(const void *symbol, const void *replace, void **old);


/*
 * @brief Hooks a memory region using a MSHookMemory-like API. It will search common paths for the library if needed
    * @param target The target memory region to hook
    * @param data The data to write
    * @param size The size of the data
 */
kern_return_t objsee_MSHookMemory(const void *target, const void *data, size_t size);
