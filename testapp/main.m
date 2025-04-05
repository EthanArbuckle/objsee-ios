//
//  main.m
//  testapp
//
//  Created by Ethan Arbuckle on 2/8/25.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"

#import <libobjsee/tracer.h>

#include <os/log.h>

static tracer_config_t config;

void objsee_main(const char *encoded_config_string) {
    os_log(OS_LOG_DEFAULT, "Loading libobjsee tracer with encoded config");
    
    config = (tracer_config_t) {
        .transport = TRACER_TRANSPORT_SOCKET,
        .format = (tracer_format_options_t) {
            .include_colors = false,
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
            .args = TRACER_ARG_FORMAT_DESCRIPTIVE,
        },
    };
    
    tracer_error_t *error = NULL;
    tracer_t *tracer = tracer_create_with_config(config, &error);
    if (tracer == NULL) {
        if (error != NULL) {
            os_log(OS_LOG_DEFAULT, "Failed to create tracer: %s", error->message);
            free_error(error);
        }
        return;
    }

    tracer_include_class(tracer, "*");

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


int main(int argc, char * argv[]) {
    
    objsee_main(NULL);

    NSString * appDelegateClassName;
    @autoreleasepool {
        // Setup code that might create autoreleased objects goes here.
        appDelegateClassName = NSStringFromClass([AppDelegate class]);
    }
    return UIApplicationMain(argc, argv, nil, appDelegateClassName);
}
