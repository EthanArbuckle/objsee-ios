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
    p ^= p >> 33;
    p *= 0xff51afd7ed558ccdULL;
    p ^= p >> 33;
    p *= 0xc4ceb9fe1a85ec53ULL;
    p ^= p >> 33;
    return (uint32_t)p;
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

    return objc_debug_class_getNameRaw(cls);// class_getName(cls);
}

