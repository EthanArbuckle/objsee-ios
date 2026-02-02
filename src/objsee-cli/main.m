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
#include "highlight.h"

#define OBJSEE_CLI_VERSION "0.0.1"
const char *_objsee_cli_version(void) {
    return OBJSEE_CLI_VERSION;
}

const char *OBJSEE_LIBRARY_PATH = "/var/jb/Library/Frameworks/libobjsee.framework/libobjsee";

static void print_version(void) {
    printf("objsee-cli version %s\n", OBJSEE_CLI_VERSION);
    printf("libobjsee version %s (%s)\n\n", OBJSEE_LIB_VERSION, OBJSEE_LIBRARY_PATH);
}

static void print_usage(void) {
    printf("\033[1m\033[33mUSAGE\033[0m\n");
    printf("    \033[36mobjsee\033[0m \033[90m[\033[37moptions\033[90m]\033[0m \033[32m<bundle_id>\033[0m\n\n");
    
    printf("\033[1m\033[33mFILTERING\033[0m\n");
    printf("    \033[36m-c\033[0m \033[32m<pattern>\033[0m     \033[90m│\033[0m Include class pattern (wildcards supported)\n");
    printf("    \033[36m-C\033[0m \033[32m<pattern>\033[0m     \033[90m│\033[0m Exclude class pattern (wildcards supported)\n");
    printf("    \033[36m-m\033[0m \033[32m<pattern>\033[0m     \033[90m│\033[0m Include method pattern (wildcards supported)\n");
    printf("    \033[36m-M\033[0m \033[32m<pattern>\033[0m     \033[90m│\033[0m Exclude method pattern (wildcards supported)\n");
    printf("    \033[36m-i\033[0m \033[32m<pattern>\033[0m     \033[90m│\033[0m Filter by image path pattern\n\n");
    
    printf("\033[1m\033[33mPROCESS CONTROL\033[0m\n");
    printf("    \033[36m-p\033[0m \033[32m<hint>\033[0m        \033[90m│\033[0m Attach to existing process by hint\n");
    printf("    \033[36m--sim\033[0m            \033[90m│\033[0m Launch target in iOS Simulator\n");
    printf("    \033[36m-T\033[0m               \033[90m│\033[0m Enable interactive TUI mode\n\n");
    
    printf("\033[1m\033[33mARGUMENT DETAIL LEVELS\033[0m\n");
    printf("    \033[36m-A0\033[0m              \033[90m│\033[0m No argument details \033[90m(fastest)\033[0m\n");
    printf("    \033[36m-A1\033[0m              \033[90m│\033[0m Basic argument information\n");
    printf("    \033[36m-A2\033[0m              \033[90m│\033[0m Include argument class names\n");
    printf("    \033[36m-A3\033[0m              \033[90m│\033[0m Full argument introspection \033[90m(slowest)\033[0m\n\n");
    
    printf("\033[1m\033[33mADVANCED OPTIONS\033[0m\n");
    printf("    \033[36m-d\033[0m \033[32m<delay>\033[0m       \033[90m│\033[0m Tracer init delay in ms \033[90m(default: 0)\033[0m\n");
    printf("    \033[36m-R\033[0m               \033[90m│\033[0m Use symbol rebinding \033[90m(vs MSHookFunction)\033[0m\n");
    printf("    \033[36m--nocolor\033[0m        \033[90m│\033[0m Disable colored output\n\n");
    
    printf("\033[1m\033[33mSYSTEM\033[0m\n");
    printf("    \033[36m-h\033[0m, \033[36m--help\033[0m       \033[90m│\033[0m Show this help message\n");
    printf("    \033[36m-v\033[0m, \033[36m--version\033[0m    \033[90m│\033[0m Show version information\n\n");
    
    printf("\033[1m\033[33mEXAMPLES\033[0m\n");
    printf("    \033[90m# Trace all UIView methods in Safari\033[0m\n");
    printf("    \033[36mobjsee\033[0m \033[37m-c\033[0m \033[32m\"UIView*\"\033[0m \033[37m-m\033[0m \033[32m\"*\"\033[0m \033[33mcom.apple.mobilesafari\033[0m\n\n");
    printf("    \033[90m# Attach to running process with full argument detail\033[0m\n");
    printf("    \033[36mobjsee\033[0m \033[37m-p\033[0m \033[32m\"SpringBoard\"\033[0m \033[37m-A3\033[0m\n\n");
    printf("    \033[90m# Exclude system framework noise, focus on app logic\033[0m\n");
    printf("    \033[36mobjsee\033[0m \033[37m-C\033[0m \033[32m\"*NS*\"\033[0m \033[37m-C\033[0m \033[32m\"UI*\"\033[0m \033[37m-A2\033[0m \033[33mcom.example.app\033[0m\n\n");
}

static kern_return_t locate_objsee_library(void) {
    char *possible_paths[] = {
        "/tmp/libobjsee.dylib",
        "/var/jb/Library/Frameworks/libobjsee.framework/libobjsee",
        "/Library/Frameworks/libobjsee.framework/libobjsee",
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
        char *path = malloc(strlen(jbroot_path) + strlen("/Library/Frameworks/libobjsee.framework/libobjsee") + 1);
        strcpy(path, jbroot_path);
        strcat(path, "/Library/Frameworks/libobjsee.framework/libobjsee");
        
        if (access(path, F_OK) != 0) {
            free(path);
            return KERN_FAILURE;
        }
        
        OBJSEE_LIBRARY_PATH = path;
        return KERN_SUCCESS;
    }
    
    return KERN_FAILURE;
}

static kern_return_t setup_springboard_watchdog_policy_hook(void) {
    pid_t springboard_pid = pid_from_hint("SpringBoard");
    if (springboard_pid <= 0) {
        printf("SpringBoard not running?\n");
        return KERN_FAILURE;
    }
    
    if (remote_dlopen(springboard_pid, OBJSEE_LIBRARY_PATH, RTLD_NOW) != KERN_SUCCESS) {
        printf("Failed to inject libobjsee into SpringBoard\n");
        return KERN_FAILURE;
    }
    
    mach_port_t springboard_task = get_task_for_pid(springboard_pid);
    if (springboard_task == MACH_PORT_NULL) {
        printf("Failed to get task for SpringBoard\n");
        return KERN_FAILURE;
    }

    mach_vm_address_t setup_watchdog_hook_addr = remote_dlsym(springboard_pid, "libobjsee", "setup_objsee_watchdog_policy_hook");
    if (setup_watchdog_hook_addr == 0) {
        printf("Failed to locate setup_objsee_watchdog_policy_hook in SpringBoard\n");
        return KERN_FAILURE;
    }
    
    return call_remote_function_with_string(springboard_task, setup_watchdog_hook_addr, 0, 0);
}

int main(int argc, char *argv[]) {
    dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", 9);

    highlight_init(NULL);

    @autoreleasepool {
        __block cli_options_t options;
        tracer_config_t config = {0};
        apply_defaults_to_config(&config);
        
        if (locate_objsee_library() != KERN_SUCCESS) {
            printf("Failed to find libobjsee library\n");
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
            config.format.include_indents = true;
            config.format.include_event_json = true;
            config.format.include_formatted_trace = true;
            config.format.include_thread_id = false;
            config.format.variable_separator_spacing = false;
            config.format.static_separator_spacing = 0;
            config.format.include_indent_separators = false;
            config.format.include_newline_in_formatted_trace = true;
        }
        else if (options.target_process.file_path) {
            config.transport = TRACER_TRANSPORT_STDOUT;
            config.transport_config.host = NULL;
            config.transport_config.port = 0;
            config.format.output_as_json = false;
        }
        
        // Resolve PID from hint if provided
        target_process_options_t *target = &options.target_process;
        if (target->process_hint) {
            pid_t hinted_pid = pid_from_hint(target->process_hint);
            if (hinted_pid <= 0) {
                printf("Failed to find a process matching hint: %s\n", target->process_hint);
                return 1;
            }
            target->pid = hinted_pid;
        }
        
        bool valid_target = (target->file_path != NULL || target->bundle_id != NULL || target->pid > 0);
        if (!valid_target) {
            printf("Error: No target process specified\n");
            return 1;
        }

        // Prepare the encoded config string. All launch paths will use this
        const char *encoded_config = encode_tracer_config(&config);
        if (encoded_config == NULL) {
            printf("Failed to encode libobjsee config\n");
            return 1;
        }

        if (target->file_path) {
            // A filepath was provided -- spawn the process directly. Dyld env vars are used to inject libobjsee
            pid_t spawned_pid = -1;
            kern_return_t spawn_status = spawn_traced_process(&options, encoded_config, &spawned_pid);
            if (spawn_status != KERN_SUCCESS || spawned_pid <= 0) {
                printf("Failed to spawn process: %s\n", target->file_path);
                free((void *)encoded_config);
                return 1;
            }
            
            target->pid = spawned_pid;
        }
        else if (target->pid > 0) {
            // A PID was provided (or resolved from a hint). Inject libobjsee into the running process.
            // Not supported for simulator processes
            if (options.run_in_simulator) {
                printf("Cannot attach to running process in simulator\n");
                free((void *)encoded_config);
                return 1;
            }
            
            // If attaching to an existing pid:
            // 1. Attempt to lookup the address of libobjsee's entrypoint function in the running process. If the library is already loaded, skip to step 4.
            // 2. If the library is not already loaded (entrypoint lookup failed), perform dylib injection.
            // 3. Repeat step 1 to find the entrypoint address now that the library should be loaded.
            // 4. Call the entry point function with the encoded config string as an argument.
            
            // Find the address of libobjsee's objsee_remote_entrypoint() function in the running process
            mach_vm_address_t objsee_remote_entrypoint_addr = remote_dlsym(target->pid, "libobjsee", "objsee_remote_entrypoint");
            if (objsee_remote_entrypoint_addr == 0) {
                // Library not already loaded -- inject it
                if (remote_dlopen(target->pid, OBJSEE_LIBRARY_PATH, RTLD_NOW) != KERN_SUCCESS) {
                    printf("Failed to inject %s into process %d\n", OBJSEE_LIBRARY_PATH, target->pid);
                    free((void *)encoded_config);
                    return 1;
                }
                
                // Try to find the entrypoint address again
                objsee_remote_entrypoint_addr = remote_dlsym(target->pid, "libobjsee", "objsee_remote_entrypoint");
                if (objsee_remote_entrypoint_addr == 0) {
                    printf("Failed to locate objsee_remote_entrypoint() in process %d after injection\n", target->pid);
                    free((void *)encoded_config);
                    return 1;
                }
            }

            // Invoke entry point with the encoded config
            mach_port_t task = get_task_for_pid(target->pid);
            mach_vm_address_t remote_string = 0;
            size_t string_alloc_size = 0;
            if (write_string_to_remote_task(task, encoded_config, &remote_string, &string_alloc_size) != KERN_SUCCESS) {
                printf("Failed to write config string to process with PID %d\n", target->pid);
                return -1;
            }

            kern_return_t call_result = call_remote_function_with_string(task, objsee_remote_entrypoint_addr, remote_string, 0);
            mach_vm_deallocate(task, remote_string, string_alloc_size);
            if (call_result != KERN_SUCCESS) {
                printf("Failed to invoke objsee_main() in process with PID %d\n", target->pid);
                free((void *)encoded_config);
                return 1;
            }
        }
        else if (target->bundle_id) {
            // A bundle ID was provided -- launch the app (in simulator or on device)
            if (options.run_in_simulator) {
                const char *sim_uuid = first_booted_simulator_uuid();
                if (sim_uuid == NULL) {
                    printf("No booted simulator found\n");
                    free((void *)encoded_config);
                    return 1;
                }
                
#if !TARGET_OS_IPHONE
                make_running_simulator_runtime_readwrite();
#endif
                if (simulator_launch_traced_app(sim_uuid, target->bundle_id, encoded_config) != KERN_SUCCESS) {
                    printf("Failed to launch app in simulator\n");
                    free((void *)encoded_config);
                    return 1;
                }
            }
            else {
                // Non-simulator app launch.
                // Terminate if already running, then launch with tracing config
                terminate_app_if_running(target->bundle_id);
                
                // Setup a launch observer, to confirm the app starts and get its PID
                dispatch_semaphore_t sem = dispatch_semaphore_create(0);
                on_process_launch(target->bundle_id, ^(pid_t launched_pid) {
                    if (launched_pid > 0) {
                        target->pid = launched_pid;
                    }
                    dispatch_semaphore_signal(sem);
                });
                
                // Begin app launch -- config provided via env var
                if (launch_traced_app(target->bundle_id, encoded_config) != KERN_SUCCESS) {
                    printf("Failed to launch app\n");
                    free((void *)encoded_config);
                    return 1;
                }
                
                // Wait for the launch-observer to trigger or timeout
                if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0) {
                    printf("Timed out waiting for app launch\n");
                    free((void *)encoded_config);
                    return 1;
                }
                
                if (target->pid <= 0) {
                    printf("Failed to get launched app PID\n");
                    free((void *)encoded_config);
                    return 1;
                }
                
                if (setup_springboard_watchdog_policy_hook() != KERN_SUCCESS) {
                    printf("Warning: Failed to setup SpringBoard watchdog policy hook\n");
                }
            }
        }
        
        free((void *)encoded_config);
        
        // Attach an exception handler to the target process so that crash reports can be captured
        if (setup_exception_handler_on_process(target->pid) != KERN_SUCCESS) {
            printf("Warning: Failed to setup exception handler on target process\n");
        }
        
        printf("Tracing process with PID: %d\n", target->pid);
        
        // The target app is running (either spawned new or attached to existing), and the library is injected.
        // Connect to the transport socket and start listening for incoming trace events
        
        bool tui_supported = false;
#if defined(__arm64__)
        tui_supported = true;
#endif
        if (options.tui_mode) {
            if (!tui_supported) {
                printf("TUI mode is not supported on this architecture\n");
                return 1;
            }
            
            return run_tui_trace_server(&config);
        }
        else {
            return run_trace_server(&config, target->pid);
        }
    }
    
    return 0;
}
