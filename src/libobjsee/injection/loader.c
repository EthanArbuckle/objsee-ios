//
//  loader.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#include <CoreFoundation/CoreFoundation.h>
#include <os/log.h>
#include "config_decode.h"

OBJC_EXPORT void objsee_main(const char *encoded_config_string) {
    
    tracer_config_t config;
    if (encoded_config_string) {
        if (decode_tracer_config(encoded_config_string, &config) != TRACER_SUCCESS) {
            os_log(OS_LOG_DEFAULT, "Failed to decode tracer configuration");
            return;
        }
        
        const char *config_description = copy_human_readable_config(config);
        if (config_description == NULL) {
            os_log(OS_LOG_DEFAULT, "Failed to encode config description");
            return;
        }
        
        os_log(OS_LOG_DEFAULT, "Using config: %{PUBLIC}s", config_description);
        free((void *)config_description);
    }
    else {
        os_log(OS_LOG_DEFAULT, "No config provided, using defaults");
        config = (tracer_config_t) {
            .transport = TRACER_TRANSPORT_STDOUT,
        };
        
        config.format = (tracer_format_options_t) {
            .include_colors = false,
            .include_formatted_trace = true,
            .include_event_json = false,
            .output_as_json = false,
            .include_thread_id = true,
            .include_indents = true,
            .indent_char = " ",
            .include_indent_separators = true,
            .indent_separator_char = "|",
            .variable_separator_spacing = false,
            .static_separator_spacing = 2,
            .include_newline_in_formatted_trace = true,
            .args = TRACER_ARG_FORMAT_NONE
        };
    }
    
    tracer_error_t *error = NULL;
    tracer_t *tracer = tracer_create_with_config(config, &error);
    if (tracer == NULL) {
        os_log(OS_LOG_DEFAULT, "Failed to create tracer: %s", error->message);
        free_error(error);
        return;
    }
    
    if (config.transport_config.host && config.transport_config.port) {
        tracer_set_output_socket(tracer, config.transport_config.host, config.transport_config.port);
    }
    else if (config.transport_config.file_path) {
        tracer_set_output_file(tracer, config.transport_config.file_path);
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

__attribute__((constructor)) static void loader_init(void) {
    // The cli tool provides the configuration for the tracer via an environment variable
    const char *encoded_config_string = getenv(CONFIG_ENV_VAR);
    if (encoded_config_string) {
        objsee_main(encoded_config_string);
    }
}
