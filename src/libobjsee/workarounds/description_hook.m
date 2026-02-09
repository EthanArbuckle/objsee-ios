//
//  description_hook.m
//  objsee
//
//  Created by Ethan Arbuckle on 2/8/26.
//
 
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

static NSString * (^basic_description)(id) = ^NSString *(id obj) {
    return [NSString stringWithFormat:@"<%@: %p>", NSStringFromClass([obj class]), obj];
};

static void install_description_hook_for_class(Class cls) {
    SEL descriptionSel = sel_registerName("description");
    Method descriptionMethod = class_getInstanceMethod(cls, descriptionSel);
    if (descriptionMethod == NULL) {
        return;
    }
    
    IMP newDescriptionImp = imp_implementationWithBlock(^NSString *(id _self) {
        return basic_description(_self);
    });
    method_setImplementation(descriptionMethod, newDescriptionImp);
}

void install_description_hook(void) {
    // Calling -description on some classes will cause a crash due to a recursive lock issue.
    // For classes known to have this issue, replace their -description with a basic implementation
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
    
    install_description_hook_for_class(objc_getClass("CFPrefsSearchListSource"));
    install_description_hook_for_class(objc_getClass("CFPrefsSource"));
    install_description_hook_for_class(objc_getClass("BSServiceConnection"));
}
