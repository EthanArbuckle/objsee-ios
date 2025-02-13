//
//  realized_class_tracking.c
//  objsee
//
//  Created by Ethan Arbuckle on 1/30/25.
//

#include <stdatomic.h>
#include "tracer_internal.h"
#include <os/lock.h>

struct class_tracking_state {
    _Atomic bool initialized;
    os_unfair_lock lock;
    _Atomic(Class) *seen_classes;
    size_t seen_classes_count;
    size_t capacity;
};

static struct class_tracking_state g_class_tracking = {
    .initialized = false,
    .lock = OS_UNFAIR_LOCK_INIT,
    .seen_classes = NULL,
    .seen_classes_count = 0,
    .capacity = 1024
};

static kern_return_t init_class_tracking(void) {
    if (atomic_load(&g_class_tracking.initialized) == true) {
        return KERN_SUCCESS;
    }
    
    os_unfair_lock_lock(&g_class_tracking.lock);
    
    if (atomic_load(&g_class_tracking.initialized) == true) {
        os_unfair_lock_unlock(&g_class_tracking.lock);
        return KERN_SUCCESS;
    }
    
    size_t initial_size = 0;
    if (__builtin_mul_overflow(g_class_tracking.capacity, sizeof(_Atomic(Class)), &initial_size)) {
        os_unfair_lock_unlock(&g_class_tracking.lock);
        return KERN_FAILURE;
    }
    
    g_class_tracking.seen_classes = (_Atomic(Class) *)calloc(g_class_tracking.capacity, sizeof(_Atomic(Class)));
    if (g_class_tracking.seen_classes == NULL) {
        os_unfair_lock_unlock(&g_class_tracking.lock);
        return KERN_FAILURE;
    }
    
    atomic_thread_fence(memory_order_release);
    atomic_store(&g_class_tracking.initialized, true);
    os_unfair_lock_unlock(&g_class_tracking.lock);
    return KERN_SUCCESS;
}

kern_return_t has_seen_class(Class cls) {
    if (cls == NULL) {
        return KERN_FAILURE;
    }
    
    if (atomic_load(&g_class_tracking.initialized) == false) {
        kern_return_t init_result = init_class_tracking();
        if (init_result != KERN_SUCCESS) {
            return KERN_FAILURE;
        }
    }
    
    os_unfair_lock_lock(&g_class_tracking.lock);
    _Atomic(Class) *classes = g_class_tracking.seen_classes;
    size_t count = g_class_tracking.seen_classes_count;
    bool found = false;
    
    if (classes != NULL) {
        for (size_t i = 0; i < count && i < g_class_tracking.capacity; i++) {
            Class stored_class = atomic_load(&classes[i]);
            if (stored_class == cls) {
                found = true;
                break;
            }
        }
    }
    
    os_unfair_lock_unlock(&g_class_tracking.lock);
    return found ? KERN_SUCCESS : KERN_FAILURE;
}

kern_return_t record_class_encounter(Class cls) {
    if (cls == NULL) {
        return KERN_FAILURE;
    }
    
    if (atomic_load(&g_class_tracking.initialized) == false) {
        if (init_class_tracking() != KERN_SUCCESS) {
            return KERN_FAILURE;
        }
    }
    
    os_unfair_lock_lock(&g_class_tracking.lock);
    
    if (g_class_tracking.seen_classes == NULL) {
        os_unfair_lock_unlock(&g_class_tracking.lock);
        return KERN_FAILURE;
    }
    
    size_t current_count = g_class_tracking.seen_classes_count;
    if (current_count == SIZE_MAX) {
        os_unfair_lock_unlock(&g_class_tracking.lock);
        return KERN_FAILURE;
    }
    
    if (current_count >= g_class_tracking.capacity) {
        size_t new_capacity = g_class_tracking.capacity;
        if (__builtin_mul_overflow(new_capacity, 2, &new_capacity)) {
            os_unfair_lock_unlock(&g_class_tracking.lock);
            return KERN_FAILURE;
        }
        
        size_t new_size = 0;
        if (__builtin_mul_overflow(new_capacity, sizeof(_Atomic(Class)), &new_size)) {
            os_unfair_lock_unlock(&g_class_tracking.lock);
            return KERN_FAILURE;
        }
        
        _Atomic(Class) *new_array = (_Atomic(Class) *)realloc(g_class_tracking.seen_classes, new_size);
        if (new_array == NULL) {
            os_unfair_lock_unlock(&g_class_tracking.lock);
            return KERN_FAILURE;
        }
        memset(new_array + g_class_tracking.capacity, 0, new_size - (g_class_tracking.capacity * sizeof(_Atomic(Class))));
        
        g_class_tracking.seen_classes = new_array;
        g_class_tracking.capacity = new_capacity;
    }
    
    _Atomic(Class) *target = &g_class_tracking.seen_classes[current_count];
    
    atomic_store(target, cls);
    g_class_tracking.seen_classes_count++;
    
    os_unfair_lock_unlock(&g_class_tracking.lock);
    return KERN_SUCCESS;
}
