//
//  substrate_apis.c
//  objsee
//
//  Created by Ethan Arbuckle on 2/1/26.
//

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include "logging.h"

static void *_find_MSHookFunction(void);
static void *_find_MSHookMemory(void);

kern_return_t objsee_MSHookFunction(const void *symbol, const void *replace, void **old) {
    void *_MSHookFunction = _find_MSHookFunction();
    if (_MSHookFunction == NULL) {
        objsee_log("MSHookFunction not found. Is a hooking library installed?");
        return KERN_FAILURE;
    }
        
    ((void (*)(const void *, const void *, void **))_MSHookFunction)(symbol, replace, old);

    return KERN_SUCCESS;
}

kern_return_t objsee_MSHookMemory(const void *target, const void *data, size_t size) {
    void *_MSHookMemory = _find_MSHookMemory();
    if (_MSHookMemory == NULL) {
        objsee_log("MSHookMemory not found. Is a hooking library installed?");
        return KERN_FAILURE;
    }
        
    ((void (*)(const void *, const void *, size_t))_MSHookMemory)(target, data, size);
    return KERN_SUCCESS;
}

static void *_find_hooking_library(void) {
    static void *jbhooker_handle = NULL;
    if (jbhooker_handle == NULL) {
        const char *possible_lib_paths[4] = {
            "/var/jb/usr/lib/libsubstrate.dylib",
            "/usr/lib/libsubstrate.dylib",
            "/cores/binpack/usr/lib/libellekit.dylib",
            "/var/jb/usr/lib/libellekit.dylib"
        };
        
        for (size_t i = 0; i < sizeof(possible_lib_paths) / sizeof(possible_lib_paths[0]); i++) {
            jbhooker_handle = dlopen(possible_lib_paths[i], RTLD_LAZY);
            if (jbhooker_handle != NULL) {
                break;
            }
        }
    }
    
    return jbhooker_handle;
}

static void *_find_MSHookFunction(void) {
    static void *_MSHookFunction = NULL;
    if (_MSHookFunction == NULL) {
        _MSHookFunction = dlsym(_find_hooking_library(), "MSHookFunction");
    }
    
    return _MSHookFunction;
}

static void *_find_MSHookMemory(void) {
    static void *_MSHookMemory = NULL;
    if (_MSHookMemory == NULL) {
        
        const char *symbol_names[2] = {
            "EKHookMemoryRaw_impl"
            "MSHookMemory",
        };
        for (size_t i = 0; i < sizeof(symbol_names) / sizeof(symbol_names[0]); i++) {
            _MSHookMemory = dlsym(_find_hooking_library(), symbol_names[i]);
            if (_MSHookMemory != NULL) {
                break;
            }
        }
    }
    
    return _MSHookMemory;
}
