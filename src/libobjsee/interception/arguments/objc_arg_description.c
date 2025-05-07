//
//  objc_arg_description.c
//  objsee
//
//  Created by Ethan Arbuckle on 2/7/25.
//

#include <objc/runtime.h>
#include <os/lock.h>
#include <string.h>
#include "msgSend_hook.h"

// Cache the result of -description calls to avoid repeated calls
static char *g_description_cache[1024] = {0};
static size_t g_description_cache_count = 0;
static os_unfair_lock g_description_cache_lock = OS_UNFAIR_LOCK_INIT;

// Cache the description IMP for classes to avoid repeated lookups
static void *g_imp_cache[1024] = {0};
static size_t g_imp_cache_count = 0;
static os_unfair_lock g_imp_cache_lock = OS_UNFAIR_LOCK_INIT;


static SEL description_selector(void) {
    static SEL descriptionSel = NULL;
    if (descriptionSel == NULL) {
        descriptionSel = sel_registerName("description");
    }
    return descriptionSel;
}

static IMP get_description_imp_for_class(Class cls) {
    if (cls == NULL) {
        return NULL;
    }
    
    os_unfair_lock_lock(&g_imp_cache_lock);
    
    for (size_t i = 0; i < g_imp_cache_count; i += 2) {
        if (g_imp_cache[i] == cls) {
            IMP existingImp = (IMP)g_imp_cache[i + 1];
            os_unfair_lock_unlock(&g_imp_cache_lock);
            return existingImp;
        }
    }
    
    SEL descriptionSel = description_selector();
    IMP descriptionImp = NULL;
    if (class_respondsToSelector(cls, descriptionSel)) {
        descriptionImp = class_getMethodImplementation(cls, descriptionSel);
    }
    
    if (descriptionImp != NULL && g_imp_cache_count < 1022) {
        g_imp_cache[g_imp_cache_count] = cls;
        g_imp_cache[g_imp_cache_count + 1] = descriptionImp;
        g_imp_cache_count += 2;
    }
    
    os_unfair_lock_unlock(&g_imp_cache_lock);
    return descriptionImp;
}

static const char *build_objc_description_for_object(void *address, Class obj_class) {
    if (address == NULL || obj_class == NULL) {
        return NULL;
    }
    
    IMP descriptionImp = get_description_imp_for_class(obj_class);
    if (descriptionImp == NULL) {
        return NULL;
    }
    
    id object = (id)address;
    SEL descriptionSel = description_selector();
    id descriptionString = ((id (*)(id, SEL))descriptionImp)(object, descriptionSel);
    if (descriptionString == NULL) {
        return NULL;
    }
    
    void *orig_objc_msgSend = get_original_objc_msgSend();
    if (orig_objc_msgSend == NULL) {
        return NULL;
    }
    
    const char *utf8String = ((const char *(*)(id, SEL))orig_objc_msgSend)(descriptionString, sel_registerName("UTF8String"));
    if (utf8String == NULL) {
        return NULL;
    }
    
    // For string types, use objc style quoting (@"string")
    if (objc_opt_isKindOfClass(object, objc_getClass("NSString"))) {
        char *quoted_string = malloc(strlen(utf8String) + 3);
        if (quoted_string == NULL) {
            return NULL;
        }
        
        quoted_string[0] = '@';
        quoted_string[1] = '"';
        strcpy(quoted_string + 2, utf8String);
        char *newline = strchr(quoted_string, '\n');
        if (newline != NULL) {
            newline[0] = '\0';
        }
        
        size_t final_len = strlen(quoted_string);
        quoted_string[final_len] = '"';
        quoted_string[final_len + 1] = '\0';
        return quoted_string;
    }
    
    return strdup(utf8String);
}

const char *lookup_description_for_address(void *address, Class obj_class) {
    if (address == NULL || obj_class == NULL) {
        return NULL;
    }

    const char *result_desc = NULL;

    os_unfair_lock_lock(&g_description_cache_lock);
    for (size_t i = 0; i < g_description_cache_count; i += 2) {
        if (g_description_cache[i] == address) {
            result_desc = (const char *)g_description_cache[i + 1];
            os_unfair_lock_unlock(&g_description_cache_lock);
            return result_desc;
        }
    }
    os_unfair_lock_unlock(&g_description_cache_lock);

    const char *built_desc = build_objc_description_for_object(address, obj_class);
    if (built_desc == NULL) {
        return NULL;
    }

    os_unfair_lock_lock(&g_description_cache_lock);

    for (size_t i = 0; i < g_description_cache_count; i += 2) {
        if (g_description_cache[i] == address) {
            result_desc = (const char *)g_description_cache[i + 1];
            free((void*)built_desc);
            os_unfair_lock_unlock(&g_description_cache_lock);
            return result_desc;
        }
    }

    if (g_description_cache_count < 1022) {
        size_t len = strnlen(built_desc, 1023);
        char *desc_buffer_copy = (char *)malloc(len + 1);

        if (desc_buffer_copy) {
            strncpy(desc_buffer_copy, built_desc, len);
            desc_buffer_copy[len] = '\0';

            g_description_cache[g_description_cache_count]     = address;
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

    os_unfair_lock_unlock(&g_description_cache_lock);
    return result_desc;
}
