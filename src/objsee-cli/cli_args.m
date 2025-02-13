//
//  cli_args.m
//  cli
//
//  Created by Ethan Arbuckle on 1/17/25.
//

#include "cli_args.h"
#include <Foundation/Foundation.h>
#include <dlfcn.h>
#include "app_launching.h"

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

int parse_cli_arguments(int argc, char *argv[], cli_options_t *options, tracer_config_t *config) {
    memset(options, 0, sizeof(cli_options_t));
    
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
            options->pid = pid_from_hint(argv[i + 1]);
            i++;
            continue;
        }
        
        if (strcmp(argv[i], "--sim") == 0) {
            options->run_in_simulator = true;
            continue;
        }
        
        if (argv[i][0] == '-' && i + 1 < argc) {
            if (config->filter_count >= TRACER_MAX_FILTERS) {
                printf("Error: Too many filters (max is %d)\n", TRACER_MAX_FILTERS);
                return -1;
            }
            
            int current = config->filter_count;
            const char *pattern = argv[i + 1];
            
            switch (argv[i][1]) {
                case 'c':
                    config->filters[current].class_pattern = pattern;
                    config->filters[current].exclude = false;
                    break;
                case 'C':
                    config->filters[current].class_pattern = pattern;
                    config->filters[current].exclude = true;
                    break;
                case 'm':
                    config->filters[current].method_pattern = pattern;
                    config->filters[current].exclude = false;
                    break;
                case 'M':
                    config->filters[current].method_pattern = pattern;
                    config->filters[current].exclude = true;
                    break;
                case 'i':
                    config->filters[current].image_pattern = pattern;
                    config->filters[current].exclude = false;
                    break;
                default:
                    printf("Error: Unknown option '%s'\n", argv[i]);
                    return -1;
            }
            
            config->filter_count++;
            i++;
            continue;
        }
        
        if (!options->bundle_id && argv[i][0] != '-') {
            options->bundle_id = argv[i];
        }
        else {
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
        .output_as_json = true,
        .include_colors = true,
        .include_thread_id = true,
        .include_indents = true,
        .indent_char = " ",
        .include_indent_separators = true,
        .indent_separator_char = "|",
        .variable_separator_spacing = true,
        .static_separator_spacing = 2,
        .include_newline_in_formatted_trace = false,
        .args = TRACER_ARG_FORMAT_DESCRIPTIVE,
    };
    
    return 0;
}
