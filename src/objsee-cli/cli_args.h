//
//  cli_args.h
//  cli
//
//  Created by Ethan Arbuckle on 1/17/25.
//

#ifndef cli_args_h
#define cli_args_h

#include <CoreFoundation/CoreFoundation.h>
#include "tracer_types.h"

typedef struct {
    const char *bundle_id;
    const char *file_path;
    pid_t pid;
    bool tui_mode;
    bool show_help;
    bool show_version;
    bool no_color;
    bool run_in_simulator;
    int argc;
    char **argv;
} cli_options_t;

/**
 * Parses CLI arguments into a struct
 * @param argc The number of arguments
 * @param argv The arguments
 * @param options The options struct to populate
 * @param config The config struct to populate
 * @return 0 on success, an error code on failure
 */
int parse_cli_arguments(int argc, char *argv[], cli_options_t *options, tracer_config_t *config);

/**
 * Applies default values to a config struct
 * @param config The config struct to populate
 * @return 0 on success, an error code on failure
 */
int apply_defaults_to_config(tracer_config_t *config);

#endif /* cli_args_h */
