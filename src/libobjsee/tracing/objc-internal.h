//
//  objc-internal.h
//  objsee
//
//  Created by Ethan Arbuckle on 2/13/25.
//

#ifndef objc_internal_h
#define objc_internal_h

#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>

#define _OBJC_TAG_MASK (1UL<<63)
#define _OBJC_TAG_INDEX_SHIFT 0
#define _OBJC_TAG_SLOT_SHIFT 0
#define _OBJC_TAG_PAYLOAD_LSHIFT 1
#define _OBJC_TAG_PAYLOAD_RSHIFT 4
#define _OBJC_TAG_EXT_MASK (_OBJC_TAG_MASK | 0x7UL)
#define _OBJC_TAG_NO_OBFUSCATION_MASK ((1UL<<62) | _OBJC_TAG_EXT_MASK)
#define _OBJC_TAG_CONSTANT_POINTER_MASK ~(_OBJC_TAG_EXT_MASK | ((uintptr_t)_OBJC_TAG_EXT_SLOT_MASK << _OBJC_TAG_EXT_SLOT_SHIFT))
#define _OBJC_TAG_EXT_INDEX_SHIFT 55
#define _OBJC_TAG_EXT_SLOT_SHIFT 55
#define _OBJC_TAG_EXT_PAYLOAD_LSHIFT 9
#define _OBJC_TAG_EXT_PAYLOAD_RSHIFT 12

static inline bool _objc_isTaggedPointer(const void * _Nullable ptr) {
#if __LP64__
    return ((uintptr_t)ptr & _OBJC_TAG_MASK) == _OBJC_TAG_MASK;
#endif
    return false;
}

// From https://github.com/apple-oss-distributions/objc4/blob/fb265098298302243cd7eeaa1f63f0ba7786dd9a/runtime/objc-runtime-new.h#L76
#define RW_REALIZED           (1<<31)
#if defined(__arm__)
#define FAST_DATA_MASK        0xfffffffcUL
#define CLASS_BITS_OFFSET     16
#elif defined(__aarch64__)
#define FAST_DATA_MASK        0x00007ffffffffff8UL
#define CLASS_BITS_OFFSET     32
#endif

/**
 * @brief Checks if a class is realized
 * @param cls The class to check
 * @return true if the class is realized
 */
__attribute__((always_inline))
static inline bool is_class_realized(Class _Nonnull cls) {
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

#endif /* objc_internal_h */
