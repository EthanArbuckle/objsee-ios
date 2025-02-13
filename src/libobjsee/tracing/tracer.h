//
//  tracer.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#ifndef TRACER_H
#define TRACER_H

#include "tracer_types.h"

#define OBJSEE_LIB_VERSION "0.0.1"

struct tracer_error_s {
    char message[256];
};

typedef struct tracer_context_t tracer_t;
typedef struct tracer_error_s tracer_error_t;

tracer_t *tracer_create(void);
tracer_t *tracer_create_with_error(tracer_error_t **error);
tracer_t *tracer_create_with_config(tracer_config_t config, tracer_error_t **error);

void tracer_set_output(tracer_t *tracer, tracer_transport_type_t output);
void tracer_set_output_stdout(tracer_t *tracer);
void tracer_set_output_file(tracer_t *tracer, const char *path);
void tracer_set_output_socket(tracer_t *tracer, const char *host, uint16_t port);
void tracer_set_output_handler(tracer_t *tracer, tracer_event_handler_t *handler, void *context);

void tracer_set_format_options(tracer_t *tracer, tracer_format_options_t format);
void tracer_set_arg_detail(tracer_t *tracer, tracer_argument_format_t arg_format);

void tracer_format_enable_color(tracer_t *tracer, bool enable);
void tracer_format_enable_indent(tracer_t *tracer, bool enable);
void tracer_format_enable_thread_id(tracer_t *tracer, bool enable);

void tracer_include_pattern(tracer_t *tracer, const char *class_pattern, const char *method_pattern);
void tracer_exclude_pattern(tracer_t *tracer, const char *class_pattern, const char *method_pattern);
void tracer_include_class(tracer_t *tracer, const char *class_pattern);
void tracer_exclude_class(tracer_t *tracer, const char *class_pattern);
void tracer_include_method(tracer_t *tracer, const char *method_pattern);
void tracer_exclude_method(tracer_t *tracer, const char *method_pattern);
void tracer_include_image(tracer_t *tracer, const char *image_pattern);

tracer_result_t tracer_add_filter(tracer_t *tracer, const tracer_filter_t *filter);
tracer_result_t tracer_start(tracer_t *tracer);
tracer_result_t tracer_stop(tracer_t *tracer);
tracer_result_t tracer_cleanup(tracer_t *tracer);

const char *tracer_get_last_error(tracer_t *tracer);
void free_error(tracer_error_t *error);

#endif // TRACER_H
