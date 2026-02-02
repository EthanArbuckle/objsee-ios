//
//  mshookfuntion.c
//  objsee
//
//  Created by Ethan Arbuckle on 2/1/26.
//

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include "logging.h"

static void *_find_MSHookFunction(void);

kern_return_t objsee_MSHookFunction(const void *symbol, const void *replace, void **old) {
    void *_MSHookFunction = _find_MSHookFunction();
    if (_MSHookFunction == NULL) {
        objsee_log("MSHookFunction not found. Is a hooking library installed?");
        return KERN_FAILURE;
    }
        
    ((void (*)(const void *, const void *, void **))_MSHookFunction)(symbol, replace, old);

    return KERN_SUCCESS;
}

static void *_find_MSHookFunction(void) {
    static void *_MSHookFunction = NULL;
    if (_MSHookFunction == NULL) {
        const char *possible_lib_paths[4 ] = {
            "/var/jb/usr/lib/libsubstrate.dylib",
            "/usr/lib/libsubstrate.dylib",
            "/cores/binpack/usr/lib/libellekit.dylib",
            "/var/jb/usr/lib/libellekit.dylib"
        };
        
        void *jbhooker_handle = NULL;
        for (size_t i = 0; i < sizeof(possible_lib_paths) / sizeof(possible_lib_paths[0]); i++) {
            jbhooker_handle = dlopen(possible_lib_paths[i], RTLD_LAZY);
            if (jbhooker_handle != NULL) {
                break;
            }
        }
        
        if (jbhooker_handle != NULL) {
            _MSHookFunction = dlsym(jbhooker_handle, "MSHookFunction");
        }
    }
    
    return _MSHookFunction;
}
