//
//  class_name_cache.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 1/31/26.
//

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <objc/runtime.h>
#include "objc-internal.h"

#ifndef CLASS_NAME_CACHE_SIZE
#define CLASS_NAME_CACHE_SIZE 8192
#endif

typedef struct {
    _Atomic(uintptr_t) cls;
    _Atomic(uintptr_t) name;
} class_name_cache_entry_t;

static class_name_cache_entry_t g_class_name_cache[CLASS_NAME_CACHE_SIZE];

static inline uint32_t hash_ptr_uintptr(uintptr_t p) {
#if __LP64__
    uint64_t x = (uint64_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (uint32_t)x;
#else
    uint32_t x = (uint32_t)p;
    x ^= x >> 16;
    x *= 0x85ebca6bU;
    x ^= x >> 13;
    x *= 0xc2b2ae35U;
    x ^= x >> 16;
    return x;
#endif
}

extern const char *objc_debug_class_getNameRaw(Class cls);

const char *class_name_cache_get(Class cls) {
    uintptr_t key = (uintptr_t)cls;
    uint32_t h = hash_ptr_uintptr(key);
    uint32_t mask = (CLASS_NAME_CACHE_SIZE - 1);
    uint32_t idx = (h & mask);

    for (uint32_t probe = 0; probe < CLASS_NAME_CACHE_SIZE; probe++) {
        class_name_cache_entry_t *e = &g_class_name_cache[idx];
        uintptr_t seen = atomic_load_explicit(&e->cls, memory_order_acquire);
        if (seen == key) {
            return (const char *)atomic_load_explicit(&e->name, memory_order_acquire);
        }

        if (seen == 0) {
            if (!is_class_realized(cls)) {
                return NULL;
            }
            
            const char *nm = objc_debug_class_getNameRaw(cls);
            if (nm == NULL) { 
                return NULL;
            }

            uintptr_t expected = 0;
            if (atomic_compare_exchange_strong_explicit(&e->cls, &expected, key, memory_order_acq_rel, memory_order_acquire)) {
                atomic_store_explicit(&e->name, (uintptr_t)nm, memory_order_release);
                return nm;
            }
        }

        idx = (idx + 1) & mask;
    }

    return objc_debug_class_getNameRaw(cls);
}

