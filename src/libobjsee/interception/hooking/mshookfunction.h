//
//  mshookfunction.h
//  objsee
//
//  Created by Ethan Arbuckle on 2/1/26.
//

#include <CoreFoundation/CoreFoundation.h>

/*
 * @brief Hooks a function using MSHookFunction. It will search common paths for the library if needed
    * @param symbol The symbol to hook
    * @param replace The replacement function
    * @param old Pointer to store the original function
 */
kern_return_t objsee_MSHookFunction(const void *symbol, const void *replace, void **old);
