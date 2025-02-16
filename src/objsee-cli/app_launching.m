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
#include <spawn.h>
#include "dylib_injector.h"
#include "app_launching.h"
#include "config_encode.h"


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

kern_return_t spawn_process(cli_options_t *options, tracer_config_t config) {
    if (options == NULL || options->file_path == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    char *config_string = NULL;
    if (encode_tracer_config(&config, &config_string) != TRACER_SUCCESS) {
        printf("Failed to encode libobjsee config\n");
        return KERN_FAILURE;
    }

    size_t config_len = strlen(config_string);
    size_t dylib_len = strlen(OBJSEE_LIBRARY_PATH);
    char *dyld_insert_env = malloc(dylib_len + sizeof("DYLD_INSERT_LIBRARIES=") + 1);
    char *objsee_config_env = malloc(config_len + sizeof("OBJSEE_CONFIG=") + 1);
    if (dyld_insert_env == NULL || objsee_config_env == NULL) {
        free(dyld_insert_env);
        free(objsee_config_env);
        return KERN_RESOURCE_SHORTAGE;
    }
    
    sprintf(dyld_insert_env, "DYLD_INSERT_LIBRARIES=%s", OBJSEE_LIBRARY_PATH);
    sprintf(objsee_config_env, "OBJSEE_CONFIG=%s", config_string);
    char **envp = (char *[]){dyld_insert_env, objsee_config_env, NULL};
    char **child_argv = malloc(sizeof(char *) * (options->argc));
    if (child_argv == NULL) {
        free(dyld_insert_env);
        free(objsee_config_env);
        return KERN_RESOURCE_SHORTAGE;
    }
    
    child_argv[0] = (char *)options->file_path;
    size_t arg_idx = 1;
    for (int i = 2; i < options->argc && arg_idx < options->argc - 1; i++) {
        child_argv[arg_idx++] = options->argv[i];
    }
    child_argv[arg_idx] = NULL;
    
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED);
    int ret = posix_spawn(&options->pid, options->file_path, NULL, &attr, child_argv, envp);
    
    posix_spawnattr_destroy(&attr);
    free(dyld_insert_env);
    free(objsee_config_env);
    free(child_argv);
    
    if (ret != 0) {
        return KERN_FAILURE;
    }
    
    kill(options->pid, SIGCONT);
    
    int status;
    waitpid(options->pid, &status, 0);

    return KERN_SUCCESS;
}
