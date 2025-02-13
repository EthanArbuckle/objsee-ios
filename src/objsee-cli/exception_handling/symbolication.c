//
//  symbolication.c
//  cli
//
//  Created by Ethan Arbuckle on 1/17/25.
//

#include <os/log.h>
#include <dlfcn.h>
#include "symbolication.h"

static struct {
    bool initialized;
    CSSymbolicatorRef (*CreateWithTaskFlagsAndNotification)(task_t, uint32_t, void *);
    CSSymbolOwnerRef (*GetSymbolOwnerWithAddressAtTime)(CSSymbolicatorRef, vm_address_t, uint64_t);
    CSSymbolOwnerRef (*GetSymbolOwnerWithNameAtTime)(CSSymbolicatorRef, const char *, uint64_t);
    CSSymbolRef (*GetSymbolWithName)(CSSymbolicatorRef, const char *, uint64_t);
    CSSymbolRef (*GetSymbolWithAddress)(CSSymbolOwnerRef, vm_address_t);
    CSSymbolRef (*GetSymbolFromOwnerWithName)(CSSymbolOwnerRef, const char *);
    Boolean (*IsNull)(CSTypeRef);
    const char *(*GetSymbolName)(CSSymbolRef);
    const char *(*GetSymbolOwnerPath)(CSSymbolRef);
    CSRange (*GetSymbolRange)(CSSymbolRef);
    int (*GetSymbolOwnerCountAtTime)(CSSymbolicatorRef, uint64_t);
    int (*ForEachSymbolAtTime)(CSSymbolicatorRef, uint64_t, void (^)(CSSymbolRef));
    int (*ForEachSymbolOwnerAtTime)(CSSymbolicatorRef, uint64_t, void (^)(CSSymbolOwnerRef));
} CS;

CSSymbolicatorRef create_symbolicator_with_task(task_t task) {
    if (task == TASK_NULL) {
        return CSNULL;
    }
    init_core_symbolication();
    return CS.CreateWithTaskFlagsAndNotification(task, 1, NULL);
}

CSSymbolOwnerRef get_symbol_owner(CSSymbolicatorRef symbolicator, uint64_t address) {
    return CS.GetSymbolOwnerWithAddressAtTime(symbolicator, address, 0x80000000u);
}

CSSymbolOwnerRef get_symbol_owner_for_name(CSSymbolicatorRef symbolicator, const char *name) {
    if (name == NULL) {
        return CSNULL;
    }
    return CS.GetSymbolOwnerWithNameAtTime(symbolicator, name, CS_NOW);
}

CSSymbolRef get_symbol_for_name(CSSymbolicatorRef symbolicator, const char *name) {
    return CS.GetSymbolWithName(symbolicator, name, CS_NOW);
}

CSSymbolRef get_symbol_at_address(CSSymbolOwnerRef symbol_owner, uint64_t address) {
    if (CS.IsNull(symbol_owner)) {
        return CSNULL;
    }
    return CS.GetSymbolWithAddress(symbol_owner, address);
}

CSSymbolRef get_symbol_from_owner_with_name(CSSymbolOwnerRef symbol_owner, const char *name) {
    if (CS.IsNull(symbol_owner)) {
        return CSNULL;
    }
    return CS.GetSymbolFromOwnerWithName(symbol_owner, name);
}

CSRange get_range_for_symbol(CSSymbolRef symbol) {
    return CS.GetSymbolRange(symbol);
}

const char *get_image_path_for_symbol_owner(CSSymbolOwnerRef symbol_owner) {
    return CS.GetSymbolOwnerPath(symbol_owner);
}

const char *get_name_for_symbol(CSSymbolRef symbol) {
    return CS.GetSymbolName(symbol);
}

const char *get_name_for_symbol_at_address(CSSymbolicatorRef symbolicator, uint64_t address) {
    CSSymbolOwnerRef symbol_owner = get_symbol_owner(symbolicator, address);
    if (CS.IsNull(symbol_owner)) {
        return NULL;
    }
    
    CSSymbolRef symbol = get_symbol_at_address(symbol_owner, address);
    if (CS.IsNull(symbol)) {
        return NULL;
    }
    
    return get_name_for_symbol(symbol);
}

bool cs_isnull(CSTypeRef ref) {
    return CS.IsNull(ref);
}

int get_symbol_owner_count(CSSymbolicatorRef symbolicator) {
    return CS.GetSymbolOwnerCountAtTime(symbolicator, CS_NOW);
}

void for_each_symbol(CSSymbolicatorRef symbolicator, void (^handler)(CSSymbolRef)) {
    CS.ForEachSymbolAtTime(symbolicator, CS_NOW, ^(CSSymbolRef symbol) {
        handler(symbol);
    });
}

void for_each_symbol_owner(CSSymbolicatorRef symbolicator, void (^handler)(CSSymbolOwnerRef)) {
    CS.ForEachSymbolOwnerAtTime(symbolicator, CS_NOW, ^(CSSymbolOwnerRef owner) {
        handler(owner);
    });
}

kern_return_t init_core_symbolication(void) {
    
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        CS.initialized = false;
        void *core_symbolication_handle = dlopen("/System/Library/PrivateFrameworks/CoreSymbolication.framework/CoreSymbolication", RTLD_NOW);
        CS.CreateWithTaskFlagsAndNotification = dlsym(core_symbolication_handle, "CSSymbolicatorCreateWithTaskFlagsAndNotification");
        CS.GetSymbolOwnerWithAddressAtTime = dlsym(core_symbolication_handle, "CSSymbolicatorGetSymbolOwnerWithAddressAtTime");
        CS.GetSymbolOwnerWithNameAtTime = dlsym(core_symbolication_handle, "CSSymbolicatorGetSymbolOwnerWithNameAtTime");
        CS.GetSymbolWithName = dlsym(core_symbolication_handle, "CSSymbolicatorGetSymbolWithNameAtTime");
        CS.GetSymbolWithAddress = dlsym(core_symbolication_handle, "CSSymbolOwnerGetSymbolWithAddress");
        CS.IsNull = dlsym(core_symbolication_handle, "CSIsNull");
        CS.GetSymbolName = dlsym(core_symbolication_handle, "CSSymbolGetName");
        CS.GetSymbolOwnerPath = dlsym(core_symbolication_handle, "CSSymbolOwnerGetPath");
        CS.GetSymbolRange = dlsym(core_symbolication_handle, "CSSymbolGetRange");
        CS.GetSymbolOwnerCountAtTime = dlsym(core_symbolication_handle, "CSSymbolicatorGetSymbolOwnerCountAtTime");
        CS.ForEachSymbolAtTime = dlsym(core_symbolication_handle, "CSSymbolicatorForeachSymbolAtTime");
        CS.ForEachSymbolOwnerAtTime = dlsym(core_symbolication_handle, "CSSymbolicatorForeachSymbolOwnerAtTime");
        CS.GetSymbolFromOwnerWithName = dlsym(core_symbolication_handle, "CSSymbolOwnerGetSymbolWithName");
        
        for (size_t i = 1; i < sizeof(CS) / sizeof(void *); i++) {
            if (((void **)(&CS))[i] == NULL) {
                CS.initialized = false;
                os_log(OS_LOG_DEFAULT, "Failed to locate symbol %lu\n", i);
                return;
            }
        }

        CS.initialized = true;
    });
    
    return CS.initialized ? KERN_SUCCESS : KERN_FAILURE;
}
