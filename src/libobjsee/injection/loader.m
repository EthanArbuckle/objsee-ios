//
//  loader.m
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#ifndef BUILDING_CLI_TOOL

#include <os/log.h>
#include <dlfcn.h>
#include "config_decode.h"

OBJC_EXPORT void objsee_main(const char *encoded_config_string, bool from_dyld_insert);
static bool determine_if_from_dyld_insert(const char *dyld_insert_libraries);

// If enabled, delays the tracer's activation by 1 second to reduce early-injection crashes.
// Some processes may crash if objsee_main() runs too early in launch (e.g., before critical
// classes or subsystems are realized).
// Disable this if early instrumentation is required and the target process tolerates it
#define DELAY_INJECTION 1

#if DELAY_INJECTION
static uint64_t delay_period_ms = 1000;
#define DELAY_WRAPPED(block) \
    if (delay_period_ms > 0) { \
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay_period_ms * NSEC_PER_MSEC)), dispatch_get_main_queue(), block); \
    } else { \
        block(); \
    }
#else
#define DELAY_WRAPPED(block) block()
#endif

// Problems arise when the tracer is activated too early in the process launch, when some classes
// may still be unrealized. To mitigate this, an objc +load method is used to initialize tracing.
// The runtime is likely to be closer to "fully loaded" at this point compared to when constructor
// functions are fired
@interface RuntimeEntryShim : NSObject
@end

__attribute__((constructor)) void libobjsee_init(void) {
    write(STDOUT_FILENO, "libobjsee loaded\n", 17);
}
    
@implementation RuntimeEntryShim

+ (void)load {
    char log_message[64];
    snprintf(log_message, sizeof(log_message), "objsee loaded with delay %llu ms\n", delay_period_ms);
    write(STDOUT_FILENO, log_message, strlen(log_message));
    
    // The cli tool provides the configuration for the tracer via an environment variable
    const char *encoded_config_string = getenv(CONFIG_ENV_VAR);
    
    // Check if this library is being loaded via DYLD_INSERT_LIBRARIES. This determines if tracing should be
    // inmediately activated or not when it gets loaded into a process *without* a config envvar set.
    // 1. When libobjsee is dlopen'd for programmatic use, tracing should not be enabled automatically.
    // 2. When loaded via DYLD_INSERT_LIBRARIES, tracing should be enabled automatically and output to stdout.
    // 3. When a config is provided via env var, tracing should be enabled automatically and adhere to the config.
    const char *dyld_insert_libraries = getenv("DYLD_INSERT_LIBRARIES");
    bool from_dyld_insert = false;

    if (encoded_config_string == NULL && dyld_insert_libraries != NULL) {
        from_dyld_insert = determine_if_from_dyld_insert(dyld_insert_libraries);
#if DELAY_INJECTION
        if (from_dyld_insert) {
            // Don't delay if the tracer is being injected via DYLD_INSERT_LIBRARIES
            delay_period_ms = 0;
        }
#endif
    }

    if (encoded_config_string || from_dyld_insert) {
        DELAY_WRAPPED(^{
            objsee_main(encoded_config_string, from_dyld_insert);
        });
    }
}

static bool determine_if_from_dyld_insert(const char *dyld_insert_libraries) {
    bool from_dyld_insert = false;
    Dl_info info;
    if (dladdr((void *)objsee_main, &info) == 0) {
        os_log(OS_LOG_DEFAULT, "Failed to get dl_info for objsee_main");
        return false;
    }
    
    char self_path[PATH_MAX];
    if (realpath(info.dli_fname, self_path) == NULL) {
        strlcpy(self_path, info.dli_fname, sizeof(self_path));
    }

    for (const char *dyld_ptr = dyld_insert_libraries; dyld_ptr && *dyld_ptr; ) {
        const char *path_end = strchr(dyld_ptr, ':');
        
        size_t len = path_end ? (size_t)(path_end - dyld_ptr) : strlen(dyld_ptr);
        if (len < PATH_MAX) {
            char entry[PATH_MAX];
            memcpy(entry, dyld_ptr, len);
            entry[len] = '\0';
            
            char resolved_entry[PATH_MAX];
            if (strcmp(entry, self_path) == 0 || (realpath(entry, resolved_entry) != NULL && strcmp(resolved_entry, self_path) == 0)) {
                from_dyld_insert = true;
                break;
            }
            
        }
        dyld_ptr = path_end ? path_end + 1 : NULL;
    }
    
    return from_dyld_insert;
}

@end

void objsee_main(const char *encoded_config_string, bool from_dyld_insert) {
    tracer_config_t config = {0};
    if (encoded_config_string) {
        if (decode_tracer_config(encoded_config_string, &config) != TRACER_SUCCESS) {
            os_log(OS_LOG_DEFAULT, "Failed to decode tracer configuration");
            return;
        }
        
        config.from_dyld_insert = from_dyld_insert;
    }
    else {
        os_log(OS_LOG_DEFAULT, "No config provided, using defaults");
        config = (tracer_config_t) {
            .transport = TRACER_TRANSPORT_SOCKET,
            .from_dyld_insert = from_dyld_insert,
        };
        
        config.format = (tracer_format_options_t) {
            .include_colors = true,
            .include_formatted_trace = true,
            .include_event_json = false,
            .output_as_json = false,
            .include_thread_id = false,
            .include_indents = true,
            .indent_char = " ",
            .include_indent_separators = true,
            .indent_separator_char = "|",
            .variable_separator_spacing = false,
            .static_separator_spacing = 2,
            .include_newline_in_formatted_trace = false,
            .args = TRACER_ARG_FORMAT_CLASS,
        };
    }
    
    const char *config_description = copy_config_description(config);
    if (config_description != NULL) {
        os_log(OS_LOG_DEFAULT, "libobjsee config: %{PUBLIC}s", config_description);
        free((void *)config_description);
    }
    
    tracer_error_t *error = NULL;
    tracer_t *tracer = tracer_create_with_config(config, &error);
    if (tracer == NULL) {
        os_log(OS_LOG_DEFAULT, "Failed to create tracer: %s", error->message);
        free_error(error);
        return;
    }
    
    // Unless the config specifies a transport method, output to stdout when loaded
    // via DYLD_INSERT_LIBRARIES, unless a config is provided which takes precedence
    if (config.from_dyld_insert && encoded_config_string == NULL) {
        tracer_set_output_stdout(tracer);
    }
    
    for (size_t i = 0; i < config.filter_count; i++) {
        tracer_add_filter(tracer, &config.filters[i]);
    }

    tracer_result_t ret = -1;
    for (int attempt = 0; attempt < 3; attempt++) {
        if ((ret = tracer_start(tracer)) == TRACER_SUCCESS) {
            os_log(OS_LOG_DEFAULT, "Tracer started");
            break;
        }
        else {
            os_log(OS_LOG_DEFAULT, "Failed to start tracer: %d (attempt %d)", ret, attempt);
            sleep(1);
        }
    }
    
    if (ret != TRACER_SUCCESS) {
        tracer_cleanup(tracer);
    }
}

#endif
