//
//  main.m
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#include <Foundation/Foundation.h>
#include <dlfcn.h>
#include "config_encode.h"
#include "trace_server.h"
#include "tui_trace_server.h"
#include "crash_handler.h"
#include "dylib_injector.h"
#include "app_launching.h"
#include "cli_args.h"
#include "sim_launching.h"
#include "tmpfs_overlay.h"

#define OBJSEE_CLI_VERSION "0.0.1"

const char *OBJSEE_LIBRARY_PATH = "/var/jb/usr/lib/libobjsee.dylib";

static void print_version(void) {
    printf("objsee-cli version %s\n", OBJSEE_CLI_VERSION);
    printf("libobjsee version %s (%s)\n\n", OBJSEE_LIB_VERSION, OBJSEE_LIBRARY_PATH);
}

static void print_usage(void) {
    printf("Usage: objsee [options] <bundle id>\n\n");
    printf("Options:\n");
    printf("  -h, --help                    Show this help message\n");
    printf("  -v, --version                 Show version information\n");
    printf("  -T                            Run in TUI mode\n");
    printf("  -c <pattern>                  Include class pattern\n");
    printf("  -C <pattern>                  Exclude class pattern\n");
    printf("  -m <pattern>                  Include method pattern\n");
    printf("  -M <pattern>                  Exclude method pattern\n");
    printf("  -i <pattern>                  Image path pattern\n\n");
    printf("  -p <process hint>             Attach to an existing process\n");
    printf("  --nocolor                     Disable color output\n");
    printf("  --sim                         Run the app in iOS Simulator\n\n");
    printf("  -A0                           Include no arguments\n");
    printf("  -A1                           Include basic argument detail\n");
    printf("  -A2                           Include class names in argument detail\n");
    printf("  -A3                           Include full argument detail\n\n");
    printf("Example: objsee -c \"UIView*\" -m \"*\" com.apple.mobilesafari\n\n");
}

static kern_return_t locate_objsee_library(void) {
    char *possible_paths[] = {
        // Prioritize /tmp/ over paths that are more likely to have sandbox restrictions
        "/tmp/libobjsee.dylib",
        "/var/jb/usr/lib/libobjsee.dylib",
        "/usr/lib/libobjsee.dylib",
        "/var/jb/tmp/libobjsee.dylib",
        NULL,
    };
    
    for (int i = 0; possible_paths[i] != NULL; i++) {
        if (access(possible_paths[i], F_OK) == 0) {
            OBJSEE_LIBRARY_PATH = possible_paths[i];
            return KERN_SUCCESS;
        }
    }
    
    const char *jbroot_path = getenv("JBROOT");
    if (jbroot_path != NULL) {
        char *path = malloc(strlen(jbroot_path) + strlen("/usr/lib/libobjsee.dylib") + 1);
        strcpy(path, jbroot_path);
        strcat(path, "/usr/lib/libobjsee.dylib");
        
        if (access(path, F_OK) == 0) {
            OBJSEE_LIBRARY_PATH = path;
            printf("Found libobjsee at path: %s\n", OBJSEE_LIBRARY_PATH);
            return KERN_SUCCESS;
        }
    }
    
    return KERN_FAILURE;
}

int main(int argc, char *argv[]) {
    dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", 9);
    
    @autoreleasepool {
        __block cli_options_t options;
        tracer_config_t config = {0};
        apply_defaults_to_config(&config);
        
        if (locate_objsee_library() != KERN_SUCCESS) {
            printf("Failed to find libobjsee\n");
            return 1;
        }
        
        if (parse_cli_arguments(argc, argv, &options, &config) < 0) {
            print_usage();
            return 1;
        }
        
        if (options.show_help) {
            print_usage();
            if (options.show_version) {
                print_version();
            }
            return 0;
        }
        
        if (options.show_version) {
            print_version();
            return 0;
        }
        
        if (options.tui_mode) {
            // TUI mode requires some overrrides
            config.format.include_colors = false;
            config.format.output_as_json = true;
            config.format.include_indents = false;
            config.format.include_event_json = true;
            config.format.include_formatted_trace = true;
            config.format.include_thread_id = false;
            config.format.variable_separator_spacing = false;
            config.format.static_separator_spacing = 0;
            config.format.include_indent_separators = false;
            config.format.include_newline_in_formatted_trace = true;
        }
        else if (options.file_path) {
            config.transport = TRACER_TRANSPORT_STDOUT;
            config.transport_config.host = NULL;
            config.transport_config.port = 0;
            config.format.output_as_json = false;
        }
        
        NSString *bundleID = nil;
        if (options.file_path == NULL && (options.bundle_id == NULL || (bundleID = [NSString stringWithUTF8String:options.bundle_id]) == nil) && options.pid == 0) {
            printf("Error: No bundle ID or PID specified\n");
            return 1;
        }
        
        if (options.file_path) {
            return spawn_process(&options, config);
        }
        
        // Prepare the encoded config. This will be used for both process launches and attachments.
        // When launching apps, it's provided via an env var. When attaching, it's passed as an arg to the entrypoint function objsee_main()
        char *b64_encoded_config = NULL;
        if (encode_tracer_config(&config, &b64_encoded_config) != TRACER_SUCCESS) {
            printf("Failed to encode libobjsee config\n");
            return KERN_FAILURE;
        }
        NSString *configString = [NSString stringWithUTF8String:b64_encoded_config];
        free(b64_encoded_config);
        
        if (options.file_path == NULL && options.pid != 0) {
            if (options.run_in_simulator) {
                printf("Cannot attach to running process in simulator\n");
                return 1;
            }
            // If attaching to an existing pid:
            // 1. Inject the library dylib into the running process
            // 2. Lookup the address of the entry point function objsee_main()
            // 3. Call the entry point function with the encoded config string as an argument
            if (inject_dylib_into_pid(OBJSEE_LIBRARY_PATH, options.pid) != 0) {
                printf("Failed to inject libobjsee into process\n");
                return 1;
            }
            
            // Find address of entry point
            uint64_t objsee_main_addr = get_function_address_in_pid("objsee_main", "libobjsee", options.pid);
            if (objsee_main_addr <= 0) {
                printf("Failed to find objsee_main in process\n");
                return 1;
            }
            
            // Invoke entry point with the config
            if (call_remote_function_with_string(objsee_main_addr, (char *)[configString UTF8String], options.pid) != KERN_SUCCESS) {
                printf("Failed to start objsee_main in process\n");
                return 1;
            }
            
            setup_exception_handler_on_process(options.pid);
            printf("Attached to process with PID: %d\n", options.pid);
        }
        else if (bundleID && options.run_in_simulator) {
            
            NSString *bootedSimulatorUUID = first_booted_simulator_uuid();
            if (!bootedSimulatorUUID) {
                printf("No booted simulator found\n");
                return 1;
            }
            
#if !TARGET_OS_IPHONE
            make_running_simulator_runtime_readwrite();
#endif
            if (launch_simulator_app_with_encoded_tracer_config(bootedSimulatorUUID, bundleID, configString) != KERN_SUCCESS) {
                printf("Failed to launch app in simulator\n");
                return 1;
            }
        }
        else if (bundleID) {
            // The app will be launched. If it's already running, terminate it
            terminate_app_if_running(bundleID);
            
            // Setup an launch-finished listener (the process should be fully spawned before continuing)
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            on_process_launch(bundleID, ^(pid_t pid) {
                if (pid > 0) {
                    options.pid = pid;
                    printf("App launched with PID: %d\n", options.pid);
                    setup_exception_handler_on_process(options.pid);
                }
                dispatch_semaphore_signal(sem);
            });
            
            // Begin app launch -- config provided via env var
            if (launch_app_with_encoded_tracer_config(bundleID, configString) != KERN_SUCCESS) {
                printf("Failed to launch app\n");
                return 1;
            }
            
            // Wait for the launch-listener to trigger or timeout
            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
            if (options.pid <= 0) {
                printf("Failed to launch app\n");
                return 1;
            }
        }
        
        // The target app is running (either spawned new or attached to existing), and the library is injected.
        // Connect to the transport socket and start listening for incoming trace events
        int status = 0;
        if (options.tui_mode) {
            status = run_tui_trace_server(&config);
        }
        else {
            status = run_trace_server(&config, options.pid);
        }
        
        return status;
    }
    
    return 0;
}
