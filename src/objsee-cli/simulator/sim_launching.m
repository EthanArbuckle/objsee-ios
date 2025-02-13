//
//  sim_launching.m
//  objsee
//
//  Created by Ethan Arbuckle on 1/18/25.
//

#include <Foundation/Foundation.h>
#include <objc/message.h>
#include "app_launching.h"


static NSString *run_command(NSArray *command, NSDictionary *environment, BOOL waitUntilExit) {
    NSPipe *pipe = [NSPipe pipe];
    NSFileHandle *file = pipe.fileHandleForReading;
    
    id task = [[objc_getClass("NSTask") alloc] init];
    ((void (*)(id, SEL, id))objc_msgSend)(task, NSSelectorFromString(@"setLaunchPath:"), @"/usr/bin/env");
    ((void (*)(id, SEL, id))objc_msgSend)(task, NSSelectorFromString(@"setArguments:"), command);
    ((void (*)(id, SEL, id))objc_msgSend)(task, NSSelectorFromString(@"setStandardOutput:"), pipe);
    ((void (*)(id, SEL, id))objc_msgSend)(task, NSSelectorFromString(@"setStandardError:"), pipe);
    if (environment) {
        ((void (*)(id, SEL, id))objc_msgSend)(task, NSSelectorFromString(@"setEnvironment:"), environment);
    }
    
    ((void (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"launch"));
    if (waitUntilExit) {
        ((void (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"waitUntilExit"));
        NSData *data = [file readDataToEndOfFile];
        return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    }
    
    return nil;
}

NSString *first_booted_simulator_uuid(void) {
    // Find the first booted simulator and return its UUID
    NSArray *command = @[@"xcrun", @"simctl", @"list", @"--json", @"-e", @"devices"];
    NSString *output = run_command(command, nil, YES);
    NSDictionary *json = [NSJSONSerialization JSONObjectWithData:[output dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
    for (NSArray *simRuntime in json[@"devices"]) {
        for (NSDictionary *simDevice in json[@"devices"][simRuntime]) {
            if ([simDevice[@"state"] isEqualToString:@"Booted"]) {
                return simDevice[@"udid"];
            }
        }
    }
    
    return nil;
}

kern_return_t launch_simulator_app_with_encoded_tracer_config(NSString *simulatorUUID, NSString *bundleID, NSString *configString) {
    NSArray *command = @[@"xcrun", @"simctl", @"launch", simulatorUUID, bundleID];
    NSMutableDictionary *environment = [NSMutableDictionary dictionaryWithDictionary:[[NSProcessInfo processInfo] environment]];
    
    // Inject the tracer library with DYLD_INSERT_LIBRARIES
    environment[@"SIMCTL_CHILD_DYLD_INSERT_LIBRARIES"] = [NSString stringWithUTF8String:OBJSEE_LIBRARY_PATH];
    // Provide the tracing config as an env var
    environment[@"SIMCTL_CHILD_OBJSEE_CONFIG"] = configString;
    
    // Launch the app in the simulator
    run_command(command, environment, NO);
    
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    on_process_launch(bundleID, ^(pid_t pid) {
        if (pid > 0) {
            printf("App launched with PID: %d\n", pid);
        }
        dispatch_semaphore_signal(sem);
    });
    
    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    return KERN_SUCCESS;
}
