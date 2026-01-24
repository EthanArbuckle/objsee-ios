//
//  cli_args.m
//  cli
//
//  Created by Ethan Arbuckle on 1/17/25.
//

#include <Foundation/Foundation.h>
#include "app_launching.h"
#include "cli_args.h"


int parse_cli_arguments(int argc, char *argv[], cli_options_t *options, tracer_config_t *config) {
    memset(options, 0, sizeof(cli_options_t));
    options->argc = argc;
    options->argv = argv;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            options->show_help = true;
            return 0;
        }
        
        if (strcmp(argv[i], "--help") == 0) {
            options->show_help = true;
            options->show_version = true;
            return 0;
        }
        
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            options->show_version = true;
            return 0;
        }
        
        if (strcmp(argv[i], "--nocolor") == 0) {
            config->format.include_colors = false;
            continue;
        }
        
        if (strcmp(argv[i], "-T") == 0) {
            options->tui_mode = true;
            continue;
        }
        
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            // Arg may be either a PID or the name of a running process.
            // If it's a number, use that as the PID directly. Otherwise, store the value so it can be resolved to a PID (later, after cli args are fully parsed)
            const char *process_arg = argv[i + 1];
            if (isdigit(process_arg[0])) {
                char *endptr;
                long pid_value = strtol(process_arg, &endptr, 10);
                if (*endptr != '\0' || pid_value <= 0) {
                    printf("Error: Invalid PID value '%s'\n", process_arg);
                    return -1;
                }
                options->target_process.pid = (pid_t)pid_value;
            }
            else if (process_arg[0] != '\0') {
                options->target_process.process_hint = process_arg;
            }
            else {
                printf("Error: Invalid process identifier '%s'\n", process_arg);
                return -1;
            }
            
            i++;
            continue;
        }
        
        if (strcmp(argv[i], "--sim") == 0) {
            options->run_in_simulator = true;
            continue;
        }
        
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            char *endptr;
            long delay = strtol(argv[i + 1], &endptr, 10);
            if (*endptr != '\0' || delay < 0) {
                printf("Error: Invalid delay value '%s'\n", argv[i + 1]);
                return -1;
            }
            config->tracer_delay_ms = (int)delay;
            i++;
            continue;
        }
        
        if (strcmp(argv[i], "-R") == 0) {
            config->use_symbol_rebinding = true;
            continue;
        }
        
        // arg verbosity: -A0, -A1, -A2, -A3
        if (argv[i][0] == '-' && argv[i][1] == 'A' && argv[i][2] >= '0' && argv[i][2] <= '3') {
            config->format.args = argv[i][2] - '0';
            continue;
        }
        
        if (argv[i][0] == '-' && i + 1 < argc) {
            const char *pattern = argv[i + 1];
            bool is_exclude = false;
            char filter_type;
            
            switch (argv[i][1]) {
                case 'c':
                    filter_type = 'c';
                    is_exclude = false;
                    break;
                case 'C':
                    filter_type = 'c';
                    is_exclude = true;
                    break;
                case 'm':
                    filter_type = 'm';
                    is_exclude = false;
                    break;
                case 'M':
                    filter_type = 'm';
                    is_exclude = true;
                    break;
                case 'i':
                    filter_type = 'i';
                    is_exclude = false;
                    break;
                default:
                    printf("Error: Unknown option '%s'\n", argv[i]);
                    return -1;
            }
            
            // Find existing filter to merge with, or create new one
            int filter_index = -1;
            
            // Look for most recent filter with same exclude flag
            for (int j = config->filter_count - 1; j >= 0; j--) {
                if (config->filters[j].exclude == is_exclude) {
                    // Check if this pattern type already exists in this filter
                    bool pattern_already_set = false;
                    switch (filter_type) {
                        case 'c':
                            pattern_already_set = (config->filters[j].class_pattern != NULL);
                            break;
                        case 'm':
                            pattern_already_set = (config->filters[j].method_pattern != NULL);
                            break;
                        case 'i':
                            pattern_already_set = (config->filters[j].image_pattern != NULL);
                            break;
                    }
                    
                    if (!pattern_already_set) {
                        // Can merge into this filter
                        filter_index = j;
                        break;
                    }
                }
            }
            
            if (filter_index == -1) {
                if (config->filter_count >= TRACER_MAX_FILTERS) {
                    printf("Error: Too many filters (max is %d)\n", TRACER_MAX_FILTERS);
                    return -1;
                }
                filter_index = config->filter_count++;
                memset(&config->filters[filter_index], 0, sizeof(tracer_filter_t));
                config->filters[filter_index].exclude = is_exclude;
            }
            
            switch (filter_type) {
                case 'c':
                    config->filters[filter_index].class_pattern = pattern;
                    break;
                case 'm':
                    config->filters[filter_index].method_pattern = pattern;
                    break;
                case 'i':
                    config->filters[filter_index].image_pattern = pattern;
                    break;
            }
            
            i++;
            continue;
        }
        
        // If no target process specified yet, treat arg as a potential target
        target_process_options_t *target = &options->target_process;
        if ((target->file_path == NULL || target->bundle_id == NULL) && argv[i][0] != '-') {
            // If the arg is a valid file path, treat it as such
            if (access(argv[i], F_OK) != -1) {
                target->file_path = argv[i];
                continue;
            }
            
            // Otherwise, treat it as a bundle id
            target->bundle_id = argv[i];
        }
        else {
            // Allow arbitrary args if we're launching an executable
            if (target->file_path != NULL) {
                continue;
            }
            
            printf("Error: Unexpected argument '%s'\n", argv[i]);
            return -1;
        }
    }
    
    return 0;
}

int apply_defaults_to_config(tracer_config_t *config) {
    config->transport_config.host = "127.0.0.1";
    config->transport_config.port = find_free_socket_port();
    config->transport = TRACER_TRANSPORT_SOCKET;
    if (config->transport_config.port == -1) {
        return -1;
    }
    
    config->format = (tracer_format_options_t){
        .include_formatted_trace = true,
        .include_event_json = false,
        .output_as_json = false,
        .include_colors = true,
        .include_thread_id = true,
        .include_indents = true,
        .indent_char = " ",
        .include_indent_separators = true,
        .indent_separator_char = "|",
        .variable_separator_spacing = false,
        .static_separator_spacing = 2,
        .include_newline_in_formatted_trace = false,
        .args = TRACER_ARG_FORMAT_CLASS,
    };
    
    config->tracer_delay_ms = 0;
    config->use_symbol_rebinding = false;

    return 0;
}
