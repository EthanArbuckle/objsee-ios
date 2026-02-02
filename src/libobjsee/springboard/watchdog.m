//
//  watchdog.m
//  objsee
//
//  Created by Ethan Arbuckle on 2/1/26.
//

#import <Foundation/Foundation.h>
#import <objc/message.h>
#import "logging.h"

// This is used by the cli tool to setup a watchdog policy hook in SpringBoard
__attribute__((visibility("default")))
void setup_objsee_watchdog_policy_hook(void) {
    // Disable the watchdog scaling factor for apps with OBJSEE_CONFIG in their environment.
    // This prevents them from being killed due to slow launch
    Class FBApplicationProcessWatchdogPolicy = objc_getClass("FBApplicationProcessWatchdogPolicy");
    SEL scalingFactorSel = sel_registerName("_queue_watchdogScalingFactorForAppInfo:isResume:");
    Method scalingFactorMethod = class_getInstanceMethod(FBApplicationProcessWatchdogPolicy, scalingFactorSel);
    if (scalingFactorMethod == NULL) {
        objsee_log("Failed to find FBApplicationProcessWatchdogPolicy scaling factor method");
        return;
    }

    IMP origScalingFactorImp = method_getImplementation(scalingFactorMethod);
    IMP newScalingFactorImp = imp_implementationWithBlock(^double(id _self, id info, BOOL isResume) {
        // Bundle ID of the app needing a scaling factor (it's likely being launched)
        NSString *bundleId = ((NSString * (*)(id, SEL))objc_msgSend)(info, sel_registerName("bundleIdentifier"));
        
        // Get the app's process info from FrontBoard
        id processManager = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("FBProcessManager"), sel_registerName("sharedInstance"));
        id processArray = ((id (*)(id, SEL, id))objc_msgSend)(processManager, sel_registerName("applicationProcessesForBundleIdentifier:"), bundleId);
        id appProcess = ((id (*)(id, SEL))objc_msgSend)(processArray, sel_registerName("firstObject"));
        
        // Get the app's environment variables
        id processExecutionContext = ((id (*)(id, SEL))objc_msgSend)(appProcess, sel_registerName("executionContext"));
        NSDictionary *appEnvironmentVars = ((NSDictionary * (*)(id, SEL))objc_msgSend)(processExecutionContext, sel_registerName("environment"));
        
        // If the app is being launched by objsee (has OBJSEE_CONFIG set), return a high scaling factor
        NSDictionary *objseeConfig = ((NSDictionary * (*)(id, SEL, id))objc_msgSend)(appEnvironmentVars, sel_registerName("valueForKey:"), @"OBJSEE_CONFIG");
        if (objseeConfig) {
            return 100.0;
        }

        return ((double (*)(id, SEL, id, BOOL))origScalingFactorImp)(FBApplicationProcessWatchdogPolicy, scalingFactorSel, info, isResume);
    });
    
    method_setImplementation(scalingFactorMethod, newScalingFactorImp);
}
