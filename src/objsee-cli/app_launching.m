//
//  app_launching.m
//  cli
//
//  Created by Ethan Arbuckle on 1/17/25.
//

#include <Foundation/Foundation.h>
#include <objc/message.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include "app_launching.h"


kern_return_t launch_app_with_encoded_tracer_config(NSString *bundleID, NSString *configString) {
    NSDictionary *debugOptions = @{@"__Environment": @{@"DYLD_INSERT_LIBRARIES": [NSString stringWithUTF8String:OBJSEE_LIBRARY_PATH], @"OBJSEE_CONFIG": configString}};
    NSDictionary *options = @{@"__ActivateSuspended" : @(NO), @"__UnlockDevice": @(YES), @"__DebugOptions": debugOptions};
    
    __block int launchStatus = 0;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    void (^completionHandler)(NSError *) = ^(NSError *error) {
        
        launchStatus = error ? 0 : 1;
        if (error) {
            NSLog(@"app launch error: %@", error);
            launchStatus = KERN_FAILURE;
        }
        else {
            launchStatus = KERN_SUCCESS;
        }
        
        dispatch_semaphore_signal(sem);
    };
    
    id systemService = ((id (*)(id, SEL))objc_msgSend)(NSClassFromString(@"FBSSystemService"), NSSelectorFromString(@"sharedService"));
    mach_port_t clientPort = ((mach_port_t (*)(void))dlsym(RTLD_DEFAULT, "SBSCreateClientEntitlementEnforcementPort"))();
    ((void (*)(id, SEL, NSString *, NSDictionary *, mach_port_t, void (^)(NSError *)))objc_msgSend)(systemService, NSSelectorFromString(@"openApplication:options:clientPort:withResult:"), bundleID, options, clientPort, completionHandler);
    
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    return launchStatus;
}

kern_return_t terminate_app_if_running(NSString *bundleID) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    
    void (^completionHandler)(void *) = ^void(void *_something) {
        dispatch_semaphore_signal(sem);
    };
    
    id systemService = ((id (*)(id, SEL))objc_msgSend)(NSClassFromString(@"FBSSystemService"), NSSelectorFromString(@"sharedService"));
    ((void (*)(id, SEL, id, long long, BOOL, id, void (^)(void *)))objc_msgSend)(systemService, NSSelectorFromString(@"terminateApplication:forReason:andReport:withDescription:completion:"), bundleID, 1, 0, nil, completionHandler);
    
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    return KERN_SUCCESS;
}

void on_process_launch(NSString *bundleID, void (^completion)(pid_t pid)) {
    
    static dispatch_once_t onceToken;
    static __strong void (^handler)(NSDictionary *);
    static __strong id monitor;
    static NSMutableSet *seenPids;
    
    dispatch_once(&onceToken, ^{
        seenPids = [NSMutableSet set];
        monitor = [[objc_getClass("BKSApplicationStateMonitor") alloc] init];
        if (!monitor) {
            NSLog(@"Failed to create application process monitor");
            return;
        }
        
        handler = ^(NSDictionary *info) {
            if (![info objectForKey:@"SBApplicationStateProcessIDKey"]) {
                return;
            }
            
            NSString *launchedAppBundleId = [info objectForKey:@"SBApplicationStateDisplayIDKey"];
            if ([launchedAppBundleId isEqualToString:bundleID] == NO) {
                return;
            }
            
            int currentPid = [info[@"SBApplicationStateProcessIDKey"] intValue];
            if (currentPid < 1) {
                return;
            }
            
            NSNumber *pidNum = @(currentPid);
            if ([seenPids containsObject:pidNum]) {
                return;
            }
            
            [seenPids addObject:pidNum];
            completion(currentPid);
        };
    });
    ((void (*)(id, SEL, void (^)(NSDictionary *)))objc_msgSend)(monitor, sel_registerName("setHandler:"), handler);
}

int find_free_socket_port(void) {
    int free_port = -1;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        return -1;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    
    for (int port = 22445; port < 65535; port++) {
        addr.sin_port = htons(port);
        if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            free_port = port;
            break;
        }
    }
    
    close(socket_fd);
    return free_port;
}
