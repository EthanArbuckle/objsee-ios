//
//  tracer.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include "tracer_internal.h"
#include "tracer.h"
#include "tracer_types.h"
#include "transport.h"
#include "msgSend_hook.h"
#include "event_handler.h"

void free_error(tracer_error_t *error) {
    if (error) {
        free(error);
    }
}

static tracer_error_t *create_error(const char *format, ...) {
    tracer_error_t *error = calloc(1, sizeof(tracer_error_t));
    if (error) {
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return error;
}

tracer_t *tracer_create_with_config(tracer_config_t config, tracer_error_t **error) {
    tracer_t *tracer = calloc(1, sizeof(tracer_t));
    if (tracer == NULL) {
        if (error) {
            *error = create_error("Failed to allocate tracer");
        }
        return NULL;
    }
    
    tracer->config = config;
    return tracer;
}

tracer_t *tracer_create_with_error(tracer_error_t **error) {
    tracer_t *tracer = calloc(1, sizeof(tracer_t));
    if (tracer == NULL) {
        if (error) {
            *error = create_error("Failed to allocate tracer");
        }
        return NULL;
    }
    
    tracer_format_options_t format = {
        .include_formatted_trace = true,
        .include_event_json = true,
        .include_colors = true,
        .include_thread_id = true,
        .include_indents = true,
        .indent_char = " ",
        .include_indent_separators = true,
        .indent_separator_char = "|",
        .variable_separator_spacing = true,
        .static_separator_spacing = 2,
        .include_newline_in_formatted_trace = true,
        .args = TRACER_ARG_FORMAT_DESCRIPTIVE
    };
    tracer->config.transport = TRACER_TRANSPORT_STDOUT;
    tracer->config.format = format;
    
    return tracer;
}

tracer_t *tracer_create(void) {
    return tracer_create_with_error(NULL);
}

static void add_filter_pattern(tracer_t *tracer, const char *class_pattern, const char *method_pattern, bool exclude) {
    if (tracer == NULL || tracer->config.filter_count >= TRACER_MAX_FILTERS) {
        return;
    }

    tracer_filter_t *filter = &tracer->config.filters[tracer->config.filter_count++];
    filter->class_pattern = class_pattern ? strdup(class_pattern) : NULL;
    filter->method_pattern = method_pattern ? strdup(method_pattern) : NULL;
    filter->exclude = exclude;
}

void tracer_include_pattern(tracer_t *tracer, const char *class_pattern, const char *method_pattern) {
    if (tracer) {
        add_filter_pattern(tracer, class_pattern, method_pattern, false);
    }
}

void tracer_exclude_pattern(tracer_t *tracer, const char *class_pattern, const char *method_pattern) {
    if (tracer) {
        add_filter_pattern(tracer, class_pattern, method_pattern, true);
    }
}

void tracer_include_class(tracer_t *tracer, const char *class_pattern) {
    tracer_include_pattern(tracer, class_pattern, "*");
}

void tracer_exclude_class(tracer_t *tracer, const char *class_pattern) {
    tracer_exclude_pattern(tracer, class_pattern, "*");
}

void tracer_include_method(tracer_t *tracer, const char *method_pattern) {
    tracer_include_pattern(tracer, "*", method_pattern);
}

void tracer_exclude_method(tracer_t *tracer, const char *method_pattern) {
    tracer_exclude_pattern(tracer, "*", method_pattern);
}

void tracer_include_image(tracer_t *tracer, const char *image_pattern) {
    if (tracer->config.filter_count >= TRACER_MAX_FILTERS) {
        return;
    }

    tracer_filter_t *filter = &tracer->config.filters[tracer->config.filter_count++];
    filter->image_pattern = image_pattern ? strdup(image_pattern) : NULL;
    filter->class_pattern = NULL;
    filter->method_pattern = NULL;
    filter->exclude = false;
}

void tracer_set_output(tracer_t *tracer, tracer_transport_type_t output) {
    if (tracer) {
        tracer->config.transport = output;
    }
}

void tracer_set_output_stdout(tracer_t *tracer) {
    if (tracer) {
        tracer->config.transport = TRACER_TRANSPORT_STDOUT;
    }
}

void tracer_set_output_file(tracer_t *tracer, const char *path) {
    if (tracer) {
        tracer->config.transport = TRACER_TRANSPORT_FILE;
        tracer->config.transport_config.file_path = path;
    }
}

void tracer_set_output_socket(tracer_t *tracer, const char *host, uint16_t port) {
    if (tracer) {
        tracer->config.transport = TRACER_TRANSPORT_SOCKET;
        tracer->config.transport_config.host = strdup(host);
        tracer->config.transport_config.port = port;
    }
}

void tracer_set_output_handler(tracer_t *tracer, tracer_event_handler_t *handler, void *context) {
    if (tracer == NULL || handler == NULL) {
        return;
    }

    tracer->config.transport = TRACER_TRANSPORT_CUSTOM;
    tracer->config.event_handler = handler;
    tracer->config.event_handler_context = context;
}

void tracer_set_format_options(tracer_t *tracer, tracer_format_options_t format) {
    if (tracer) {
        tracer->config.format = format;
    }
}

void tracer_set_arg_detail(tracer_t *tracer, tracer_argument_format_t arg_format) {
    if (tracer) {
        tracer->config.format.args = arg_format;
    }
}

void tracer_format_enable_color(tracer_t *tracer, bool enable) {
    if (tracer) {
        tracer->config.format.include_colors = enable;
    }
}

void tracer_format_enable_indent(tracer_t *tracer, bool enable) {
    if (tracer) {
        tracer->config.format.include_indents = enable;
    }
}

void tracer_format_enable_thread_id(tracer_t *tracer, bool enable) {
    if (tracer) {
        tracer->config.format.include_thread_id = enable;
    }
}

tracer_result_t tracer_internal_init(tracer_t *tracer) {

    if (tracer == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (tracer->initialized) {
        tracer_set_error(tracer, "Cannot initialize tracer: already initialized");
        return TRACER_SUCCESS;
    }
    
    tracer_config_t *config = &tracer->config;
    if (config == NULL) {
        tracer_set_error(tracer, "Cannot initialize tracer: invalid configuration");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (!config->format.include_formatted_trace && !config->format.include_event_json) {
        tracer_set_error(tracer, "Invalid format options");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    if (config->transport == TRACER_TRANSPORT_CUSTOM && config->event_handler == NULL) {
        tracer_set_error(tracer, "Invalid configuration values");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    tracer_result_t result = tracer_context_init(tracer);
    if (result != TRACER_SUCCESS) {
        tracer_set_error(tracer, "Failed to initialize tracer context: %d", result);
        return result;
    }
    
    result = init_event_handler(tracer);
    if (result != TRACER_SUCCESS) {
        tracer_set_error(tracer, "Failed to initialize event handler: %d", result);
        return result;
    }

    result = transport_init(tracer, &config->transport_config);
    if (result != TRACER_SUCCESS) {
        tracer_set_error(tracer, "Failed to initialize transport with result %d", result);
        return result;
    }
    
    return TRACER_SUCCESS;
}

tracer_result_t tracer_add_filter(tracer_t *tracer, const tracer_filter_t *filter) {
    if (tracer == NULL || filter == NULL) {
        tracer_set_error(tracer, "Cannot add filter: tracer not initialized");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_rwlock_wrlock(&tracer->filter_lock);
    
    if (tracer->config.filter_count >= TRACER_MAX_FILTERS) {
        tracer_set_error(tracer, "Cannot add filter: filter limit reached");
        pthread_rwlock_unlock(&tracer->filter_lock);
        return TRACER_ERROR_RUNTIME;
    }
    
    tracer->config.filters[tracer->config.filter_count++] = *filter;
    pthread_rwlock_unlock(&tracer->filter_lock);
    
    return TRACER_SUCCESS;
}

tracer_result_t tracer_start(tracer_t *tracer) {
    if (tracer == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }

    if (tracer->running) {
        return TRACER_ERROR_ALREADY_INITIALIZED;
    }

    // Initialize the tracer if it hasn't been done yet.
    // This does not enable interception
    if (!tracer->initialized) {
        if (tracer_internal_init(tracer) != TRACER_SUCCESS) {
            tracer_set_error(tracer, "tracer_init_custom_config failed");
            return TRACER_ERROR_INITIALIZATION;
        }
    }
    
    // If tracing is started without any filters, log a warning
    // and assume the user wants to trace everything
    if (tracer->config.filter_count == 0) {
        tracer_set_error(tracer, "No filters added, tracing all classes/methods");
        
        tracer_filter_t filter = {
            .class_pattern = "*",
            .method_pattern = "*",
            .exclude = false
        };
        tracer_add_filter(tracer, &filter);
    }
    
    // Start tracing
    tracer_result_t result = init_message_interception(tracer);
    if (result != TRACER_SUCCESS && result != TRACER_ERROR_ALREADY_INITIALIZED) {
        tracer_set_error(tracer, "Failed to initialize message interception: %d", result);
        return result;
    }
        
    tracer->running = true;
    return TRACER_SUCCESS;
}

tracer_result_t tracer_stop(tracer_t *tracer) {
    if (tracer == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }

    if (!tracer->initialized || !tracer->running) {
        tracer_set_error(tracer, "Tracer not running");
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    tracer->running = false;
    return TRACER_SUCCESS;
}

tracer_result_t tracer_cleanup(tracer_t *tracer) {
    if (tracer == NULL) {
        return TRACER_SUCCESS;
    }
    
    if (tracer->transport_context) {
        transport_context_t *transport_ctx = tracer->transport_context;
        pthread_mutex_destroy(&transport_ctx->write_lock);
        if (transport_ctx->fd >= 0) {
            close(transport_ctx->fd);
            transport_ctx->fd = -1;
        }
        free(transport_ctx);
        tracer->transport_context = NULL;
    }
    
    cleanup_event_handler();
    
    pthread_rwlock_destroy(&tracer->filter_lock);
    pthread_mutex_destroy(&tracer->transport_lock);
    pthread_mutex_destroy(&tracer->error_lock);
    pthread_key_delete(tracer->thread_key);
    
    free(tracer);
    tracer = NULL;
    
    return TRACER_SUCCESS;
}

const char *tracer_get_last_error(tracer_t *tracer) {
    if (tracer == NULL) {
        return "Tracer not initialized";
    }
    return tracer->last_error;
}
