//
//  realized_class_tracking.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/30/25.
//

#ifndef realized_class_tracking_h
#define realized_class_tracking_h

#include <objc/runtime.h>
#include <mach/mach.h>

#if defined(__aarch64__)

// From https://github.com/apple-oss-distributions/objc4/blob/fb265098298302243cd7eeaa1f63f0ba7786dd9a/runtime/objc-runtime-new.h#L76
#define RW_REALIZED           (1<<31)
#define FAST_DATA_MASK        0x00007ffffffffff8UL
#define CLASS_BITS_OFFSET     32

/**
 * @brief Checks if a class is realized
 * @param cls The class to check
 * @return true if the class is realized
 */
__attribute__((always_inline))
static inline bool is_class_realized(Class cls) {
    if (!cls) {
        return false;
    }
    
    vm_address_t bits_ptr = (vm_address_t)cls + CLASS_BITS_OFFSET;
    uintptr_t bits_value = 0;
    vm_size_t read_size = 0;
    if (vm_read_overwrite(mach_task_self(), bits_ptr, sizeof(uintptr_t), (vm_address_t)&bits_value, &read_size) != KERN_SUCCESS) {
        return false;
    }
        
    uintptr_t data_ptr = bits_value & FAST_DATA_MASK;
    if (data_ptr == 0) {
        return false;
    }

    uint32_t flags = 0;
    if (vm_read_overwrite(mach_task_self(), (vm_address_t)data_ptr, sizeof(uint32_t), (vm_address_t)&flags, &read_size) != KERN_SUCCESS) {
        return false;
    }
    
    return (flags & RW_REALIZED) != 0;
}

#else

__attribute__((always_inline))
static inline bool is_class_realized(Class cls) {
    return true;
}

#endif


#endif /* realized_class_tracking_h */
