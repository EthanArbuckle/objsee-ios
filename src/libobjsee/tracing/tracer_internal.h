//
//  tracer_internal.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//
#ifndef TRACER_INTERNAL_H
#define TRACER_INTERNAL_H

#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <pthread.h>
#include "tracer_types.h"
#include "tracer.h"

#define TRACER_MAX_STACK_DEPTH 256
#define TRACER_BUFFER_SIZE 2048
#define INITIAL_STACK_FRAMES 256

extern BOOL objc_opt_isKindOfClass(id _Nullable obj, Class _Nullable cls);
extern size_t malloc_size(const void * _Nonnull);
extern const char * _Nullable _Block_signature(void * _Nonnull aBlock);
extern kern_return_t mach_vm_read_overwrite(vm_map_t target_task, mach_vm_address_t address, mach_vm_size_t size, mach_vm_address_t data, mach_vm_size_t * _Nonnull outsize);

struct BlockDescriptor {
    unsigned long reserved;
    unsigned long size;
    void (* _Nullable copy)(void * _Nonnull dst, void * _Nonnull src);
    void (* _Nonnull dispose)(void *_Nonnull);
    const char * _Nullable signature;
};

struct BlockLiteral {
    void * _Nonnull isa;
    int flags;
    int reserved;
    void (* _Nonnull invoke)(void);
    struct BlockDescriptor * _Nonnull descriptor;
};

typedef struct tracer_thread_context_frame_t {
    SEL _Nonnull _cmd;
    const char * _Nonnull selector_name;
    bool selector_is_class_method;
    const char * _Nullable image_path;
    Class _Nonnull self_class;
    const char * _Nonnull self_class_name;
    uintptr_t lr;
    bool traced;
} __attribute__((packed)) tracer_thread_context_frame_t;

typedef struct tracer_thread_context_t {
    uint16_t thread_id;
    
    uint32_t stack_depth;
    uint32_t trace_depth;
    
    // Initial value is INITIAL_STACK_FRAMES, grows as needed
    uint32_t frame_capacity;
    struct tracer_thread_context_frame_t frames[INITIAL_STACK_FRAMES];
    
    // Simple inline caches for last class
    // to reduce calls into objc runtime for repeated calls
    struct {
        Class _Nullable cls;
        const char * _Nullable name;
        bool is_meta;
    } last_class_cache;

    // Whether to capture arguments for traced calls
    bool capture_arguments;
    
    // Base of the stack for the current message send (for argument capture)
    void * _Nullable stack_base;
} __attribute__((aligned(64))) tracer_thread_context_t;

typedef struct tracer_context_t {
    bool initialized;
    bool running;
    tracer_config_t config;
    pthread_rwlock_t filter_lock;
    void * _Nullable transport_context;
    char last_error[256];
    pthread_mutex_t error_lock;
} tracer_context_t;

tracer_result_t tracer_context_init(tracer_t * _Nonnull tracer);

bool tracer_should_trace(tracer_t * _Nonnull tracer, tracer_thread_context_frame_t * _Nonnull frame);
void tracer_set_error(tracer_t * _Nonnull tracer, const char * _Nonnull format, ...);

__attribute__((always_inline, hot))
static inline uint32_t fnv1a_hash(const char * _Nonnull str) {
    if (str == NULL || *str == '\0') {
        return 0;
    }

    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

#endif // TRACER_INTERNAL_H
