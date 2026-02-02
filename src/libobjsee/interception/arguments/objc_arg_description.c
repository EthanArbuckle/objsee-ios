//
//  objc_arg_description.c
//  objsee
//
//  Created by Ethan Arbuckle on 2/7/25.
//

#include <libkern/OSAtomic.h>
#include <objc/runtime.h>
#include <string.h>
#include "msgSend_hook.h"

// Cache the result of -description calls to avoid repeated calls
static char *g_description_cache[1024] = {0};
static size_t g_description_cache_count = 0;
static OSSpinLock g_description_cache_lock = OS_SPINLOCK_INIT;

// Cache the description IMP for classes to avoid repeated lookups
static void *g_imp_cache[1024] = {0};
static size_t g_imp_cache_count = 0;
static OSSpinLock g_imp_cache_lock = OS_SPINLOCK_INIT;

// Cache these to avoid repeated lookups
static SEL sel_description = NULL;
static SEL sel_UTF8String = NULL;
static SEL sel_isKindOfClass = NULL;
static Class class_NSString = NULL;

__attribute__((constructor)) static void init(void) {
    sel_description = sel_registerName("description");
    sel_UTF8String = sel_registerName("UTF8String");
    sel_isKindOfClass = sel_registerName("isKindOfClass:");
    class_NSString = objc_getClass("NSString");
}

static IMP get_description_imp_for_class(Class cls) {
    OSSpinLockLock(&g_imp_cache_lock);
    
    for (size_t i = 0; i < g_imp_cache_count; i += 2) {
        if (g_imp_cache[i] == cls) {
            IMP existingImp = (IMP)g_imp_cache[i + 1];
            OSSpinLockUnlock(&g_imp_cache_lock);
            return existingImp;
        }
    }
    
    IMP descriptionImp = NULL;
    if (class_respondsToSelector(cls, sel_description)) {
        descriptionImp = class_getMethodImplementation(cls, sel_description);
    }
    
    if (descriptionImp != NULL && g_imp_cache_count < 1022) {
        g_imp_cache[g_imp_cache_count] = cls;
        g_imp_cache[g_imp_cache_count + 1] = descriptionImp;
        g_imp_cache_count += 2;
    }

    OSSpinLockUnlock(&g_imp_cache_lock);
    return descriptionImp;
}

static bool is_kind_of_class(id object, Class cls) {
    if (((bool (*)(id, SEL, Class))g_original_objc_msgSend)(object, sel_isKindOfClass, cls)) {
        return true;
    }

    return false;
}

static bool should_object_be_skipped(id object) {
     // Calling -description on these classes will cause a crash
    /*
     Thread 2 Crashed:
     0   libsystem_platform.dylib       0x1e7b83584          _os_unfair_lock_recursive_abort + 36
     1   libsystem_platform.dylib       0x1e7b82894          _os_unfair_lock_lock_slow + 336
     2   CoreFoundation                 0x189d396dc          -[CFPrefsSource description] + 76
     3   libobjsee                      0x1045b5ef4          build_objc_description_for_object + 140
     4   libobjsee                      0x1045b5c00          lookup_description_for_address + 248
     5   libobjsee                      0x1045b3ba4          _description_for_id + 924
     6   libobjsee                      0x1045b3424          description_for_argument + 372
     7   libobjsee                      0x1045b271c          capture_arguments + 2604
     8   libobjsee                      0x1045b7ce0          pre_objc_msgSend_callback + 1456
     9   libobjsee                      0x1045c36f4          new_objc_msgSend + 52
     */
    static Class CFPrefsSearchListSource = NULL;
    static Class CFPrefsSource = NULL;

    if (CFPrefsSearchListSource == NULL) {
        CFPrefsSearchListSource = objc_getClass("CFPrefsSearchListSource");
        CFPrefsSource = objc_getClass("CFPrefsSource");
    }
    
    if (is_kind_of_class(object, CFPrefsSearchListSource) || is_kind_of_class(object, CFPrefsSource)) {
        return true;
    }

    return false;
}

static const char *copy_objc_object_description(void *address, Class obj_class) {
    id object = (id)address;
    if (should_object_be_skipped(object)) {
        return NULL;
    }

    IMP descriptionImp = get_description_imp_for_class(obj_class);
    if (descriptionImp == NULL) {
        return NULL;
    }
    
    id descriptionString = ((id (*)(id, SEL))descriptionImp)(object, sel_description);
    if (descriptionString == NULL) {
        return NULL;
    }
    
    const char *utf8String = ((const char * (*)(id, SEL))g_original_objc_msgSend)(descriptionString, sel_UTF8String);
    if (utf8String == NULL) {
        return NULL;
    }
    
    // For string types, use objc style quoting (@"string")
    if (is_kind_of_class(object, class_NSString)) {
        size_t original_len = strlen(utf8String);
        const char *newline_pos = strchr(utf8String, '\n');
        size_t content_len = newline_pos ? (newline_pos - utf8String) : original_len;
        
        char *quoted_string = malloc(content_len + 4);
        if (quoted_string == NULL) {
            return NULL;
        }
        
        quoted_string[0] = '@';
        quoted_string[1] = '"';
        memcpy(quoted_string + 2, utf8String, content_len);
        quoted_string[2 + content_len] = '"';
        quoted_string[2 + content_len + 1] = '\0';
        
        return quoted_string;
    }
    
    return strdup(utf8String);
}

const char *lookup_description_for_address(void *address, Class obj_class) {
    if (address == NULL || obj_class == NULL) {
        return NULL;
    }

    const char *result_desc = NULL;

    OSSpinLockLock(&g_description_cache_lock);

    for (size_t i = 0; i < g_description_cache_count; i += 2) {
        if (g_description_cache[i] == address) {
            result_desc = (const char *)g_description_cache[i + 1];
            OSSpinLockUnlock(&g_description_cache_lock);
            return result_desc;
        }
    }
    OSSpinLockUnlock(&g_description_cache_lock);

    const char *built_desc = copy_objc_object_description(address, obj_class);
    if (built_desc == NULL) {
        return NULL;
    }

    OSSpinLockLock(&g_description_cache_lock);

    for (size_t i = 0; i < g_description_cache_count; i += 2) {
        if (g_description_cache[i] == address) {
            result_desc = (const char *)g_description_cache[i + 1];
            free((void*)built_desc);
            OSSpinLockUnlock(&g_description_cache_lock);
            return result_desc;
        }
    }

    if (g_description_cache_count < 1022) {
        size_t len = strnlen(built_desc, 1023);
        char *desc_buffer_copy = (char *)malloc(len + 1);

        if (desc_buffer_copy) {
            strncpy(desc_buffer_copy, built_desc, len);
            desc_buffer_copy[len] = '\0';

            g_description_cache[g_description_cache_count] = address;
            g_description_cache[g_description_cache_count + 1] = desc_buffer_copy;
            g_description_cache_count += 2;

            result_desc = desc_buffer_copy;
            free((void *)built_desc);
        }
        else {
            result_desc = built_desc;
        }
    }
    else {
        result_desc = built_desc;
    }

    OSSpinLockUnlock(&g_description_cache_lock);

    return result_desc;
}
