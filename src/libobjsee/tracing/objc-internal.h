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
#include <stdatomic.h>
#include <mach/mach.h>

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

#if UINTPTR_MAX == 0xffffffffffffffffULL
    #define PTR_USER_MAX 0x00007FFFFFFFFFFFULL
#else
    #define PTR_USER_MAX UINTPTR_MAX
#endif

#define PTR_PLAUSIBLE(p) ({                  \
    uintptr_t __p = (uintptr_t)(p);          \
    (__p != 0 &&                             \
     __p >  0x0000000000010000ULL &&         \
     __p <= (uintptr_t)PTR_USER_MAX);        \
})

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
static inline bool is_class_realized_uncached(Class _Nonnull cls) {
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

#define REALIZED_CACHE_SIZE (1u << 16)
#define REALIZED_CACHE_MASK (REALIZED_CACHE_SIZE - 1u)

typedef struct {
    _Atomic(uintptr_t) key;
} realized_cache_entry_t;

static realized_cache_entry_t g_realized_cache[REALIZED_CACHE_SIZE];

__attribute__((always_inline))
static inline uint32_t hash_ptr(uintptr_t x) {
#if __LP64__
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
#else
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
#endif
    return (uint32_t)x;
}

__attribute__((always_inline))
static inline bool realized_cache_contains(uintptr_t cls_ptr) {
    uint32_t h = hash_ptr(cls_ptr);
    uint32_t idx = h & REALIZED_CACHE_MASK;

    for (uint32_t probe = 0; probe < 8; probe++) {
        uintptr_t k = atomic_load_explicit(&g_realized_cache[idx].key, memory_order_relaxed);
        if (k == cls_ptr) {
            return true;
        }
        
        if (k == 0) {
            return false;
        }

        idx = (idx + 1) & REALIZED_CACHE_MASK;
    }

    return false;
}

__attribute__((always_inline))
static inline void realized_cache_insert(uintptr_t cls_ptr) {
    uint32_t h = hash_ptr(cls_ptr);
    uint32_t idx = h & REALIZED_CACHE_MASK;

    for (uint32_t probe = 0; probe < 8; probe++) {
        uintptr_t expected = 0;
        if (atomic_compare_exchange_strong_explicit(&g_realized_cache[idx].key, &expected, cls_ptr, memory_order_relaxed, memory_order_relaxed)) {
            return;
        }

        uintptr_t k = atomic_load_explicit(&g_realized_cache[idx].key, memory_order_relaxed);
        if (k == cls_ptr) {
            return;
        }

        idx = (idx + 1) & REALIZED_CACHE_MASK;
    }
}

__attribute__((always_inline))
static inline bool is_class_realized(Class _Nonnull cls) {
    uintptr_t cls_ptr = (uintptr_t)cls;
    if (realized_cache_contains(cls_ptr)) {
        return true;
    }

    if (!is_class_realized_uncached(cls)) {
        return false;
    }

    realized_cache_insert(cls_ptr);
    return true;
}

#endif /* objc_internal_h */
