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


kern_return_t launch_traced_app(const char *bundle_id, const char *encoded_config) {
    void *_SBServices_handle = dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", RTLD_NOW);
    void *_SBSLaunchApplicationForDebugging = dlsym(_SBServices_handle, "SBSLaunchApplicationForDebugging");
    if (_SBSLaunchApplicationForDebugging == NULL) {
        NSLog(@"Failed to resolve SBSLaunchApplicationForDebugging(). Cannot launch app");
        return KERN_FAILURE;
    }

    NSDictionary *environment = @{
        @"DYLD_INSERT_LIBRARIES": [NSString stringWithUTF8String:OBJSEE_LIBRARY_PATH],
        @"OBJSEE_CONFIG": [NSString stringWithUTF8String:encoded_config]
    };
    
    NSString *bundleID = [NSString stringWithUTF8String:bundle_id];
    int launchResult = ((int (*)(NSString *, CFURLRef, NSArray *, NSDictionary *, NSString *, NSString *, uint32_t))_SBSLaunchApplicationForDebugging)(bundleID, NULL, NULL, environment, NULL, NULL, 0);
    return (launchResult == 0) ? KERN_SUCCESS : KERN_FAILURE;
}

kern_return_t terminate_app_if_running(const char *bundle_id) {
    void *_SBServices_handle = dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", RTLD_NOW);
    void *_SBSProcessIDForDisplayIdentifier = dlsym(_SBServices_handle, "SBSProcessIDForDisplayIdentifier");
    void *_SBSApplicationTerminationAssertionCreateWithError = dlsym(_SBServices_handle, "SBSApplicationTerminationAssertionCreateWithError");
    void *_SBSApplicationTerminationAssertionInvalidate = dlsym(_SBServices_handle, "SBSApplicationTerminationAssertionInvalidate");
    
    if (_SBSProcessIDForDisplayIdentifier == NULL || _SBSApplicationTerminationAssertionCreateWithError == NULL || _SBSApplicationTerminationAssertionInvalidate == NULL) {
        printf("Failed to resolve SpringBoardServices functions. Cannot terminate app\n");
        return KERN_FAILURE;
    }
    
    NSString *bundleID = [NSString stringWithUTF8String:bundle_id];

    // Check if the app is running, skip assertion creation if not
    pid_t pid = -1;
    Boolean found = ((Boolean (*)(NSString *, pid_t *))_SBSProcessIDForDisplayIdentifier)(bundleID, &pid);
    if (!found || pid < 1) {
        // App not running
        return KERN_SUCCESS;
    }
    
    // Create termination assertion
    uint8_t error_code = 0;
    void *assertion = ((void *(*)(void *, NSString *, uint8_t, uint8_t *))_SBSApplicationTerminationAssertionCreateWithError)(NULL, bundleID, UINT8_MAX, &error_code);
    if (assertion != NULL) {
        ((void (*)(void *))_SBSApplicationTerminationAssertionInvalidate)(assertion);
    }
    
    if (error_code == 0) {
        // Success. Wait a moment for the app to terminate
        usleep(500000);
        return KERN_SUCCESS;
    }

    return KERN_FAILURE;
}

void on_process_launch(const char *bundle_id, void (^completion)(pid_t pid)) {
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
            if ([launchedAppBundleId isEqualToString:[NSString stringWithUTF8String:bundle_id]] == NO) {
                return;
            }
/*
             SBApplicationStateGetDescription(0) => Unknown
             SBApplicationStateGetDescription(1) => Terminated
             SBApplicationStateGetDescription(2) => Background Task Suspended
             SBApplicationStateGetDescription(4) => Background Running
             SBApplicationStateGetDescription(8) => Foreground Running
             SBApplicationStateGetDescription(16) => Process Server
             SBApplicationStateGetDescription(32) => Foreground Running Obscured
 */         int state = [info[@"SBApplicationStateKey"] intValue];
            if (state < 4) {
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
    
    // [monitor setHandler:handler];
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

kern_return_t spawn_traced_process(cli_options_t *options, const char *encoded_config, pid_t *out_pid) {
    if (options == NULL || options->target_process.file_path == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    const char *file_path = options->target_process.file_path;
    pid_t pid = -1;

    /* Locate file_path index in original argv */
    int file_idx = -1;
    for (int i = 0; i < options->argc; i++) {
        if (options->argv[i] == file_path ||
            (options->argv[i] && strcmp(options->argv[i], file_path) == 0)) {
            file_idx = i;
            break;
        }
    }
    if (file_idx < 0) {
        return KERN_INVALID_ARGUMENT;
    }

    size_t config_len = strlen(encoded_config);
    size_t dylib_len = strlen(OBJSEE_LIBRARY_PATH);

    char *dyld_insert_env = malloc(dylib_len + sizeof("DYLD_INSERT_LIBRARIES="));
    char *objsee_config_env = malloc(config_len + sizeof("OBJSEE_CONFIG="));
    if (dyld_insert_env == NULL || objsee_config_env == NULL) {
        free(dyld_insert_env);
        free(objsee_config_env);
        return KERN_RESOURCE_SHORTAGE;
    }
    
    sprintf(dyld_insert_env, "DYLD_INSERT_LIBRARIES=%s", OBJSEE_LIBRARY_PATH);
    sprintf(objsee_config_env, "OBJSEE_CONFIG=%s", encoded_config);

    char *envp[] = { dyld_insert_env, objsee_config_env, NULL };

    /* Child argv = file_path + args strictly after file_path */
    int child_argc = options->argc - (file_idx + 1);
    char **child_argv = calloc((size_t)child_argc + 2, sizeof(char *));
    if (child_argv == NULL) {
        free(dyld_insert_env);
        free(objsee_config_env);
        return KERN_RESOURCE_SHORTAGE;
    }

    child_argv[0] = (char *)file_path;
    for (int i = 0; i < child_argc; i++) {
        child_argv[i + 1] = options->argv[file_idx + 1 + i];
    }
    child_argv[child_argc + 1] = NULL;

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED);

    int ret = posix_spawn(&pid, file_path, NULL, &attr, child_argv, envp);

    posix_spawnattr_destroy(&attr);
    free(dyld_insert_env);
    free(objsee_config_env);
    free(child_argv);
    
    if (ret != 0) {
        return KERN_FAILURE;
    }

    kill(pid, SIGCONT);

    int status;
    waitpid(pid, &status, 0);

    if (out_pid != NULL) {
        *out_pid = pid;
    }

    return KERN_SUCCESS;
}

pid_t pid_from_hint(const char *hint) {
    if (hint == NULL) {
        return -1;
    }
    
    static dispatch_once_t onceToken;
    static void *pidFromHint = NULL;
    dispatch_once(&onceToken, ^{
        void *symbolication_handle = dlopen("/System/Library/PrivateFrameworks/Symbolication.framework/Symbolication", 9);
        if (symbolication_handle) {
            pidFromHint = dlsym(symbolication_handle, "pidFromHint");
        }
    });
    
    if (pidFromHint == NULL) {
        printf("Failed to resolve pidFromHint()\n");
        return -1;
    }
    
    return ((pid_t (*)(NSString *))pidFromHint)([NSString stringWithUTF8String:hint]);
}
