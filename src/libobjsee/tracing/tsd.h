//
//  tsd.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/24/26.
//

#ifndef tsd_h
#define tsd_h

#define TRACER_CTX_TSD_SLOT 0xA0

// This is 2x faster than using pthread_getspecific / pthread_setspecific
__attribute__((always_inline, const))
static __inline__ void **_os_tsd_get_base(void) {
#if defined(__arm__)
    uintptr_t tsd;
    __asm__("mrc p15, 0, %0, c13, c0, 3\n"
            "bic %0, %0, #0x3\n" : "=r" (tsd));
#elif defined(__aarch64__)
    uint64_t tsd;
    __asm__ ("mrs %0, TPIDRRO_EL0" : "=r" (tsd));
#endif
    return (void **)(uintptr_t)tsd;
}

__attribute__((always_inline, const))
static inline void *_get_tracer_thread_context(void) {
    return _os_tsd_get_base()[TRACER_CTX_TSD_SLOT];
}

static inline void set_tracer_thread_context(void *val) {
    _os_tsd_get_base()[TRACER_CTX_TSD_SLOT] = val;
}

#endif /* tsd_h */
