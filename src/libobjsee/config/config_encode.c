//
//  config_encode.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#include <CoreFoundation/CoreFoundation.h>
#include <json-c/json_object.h>
#include "config_encode.h"

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *input, size_t length) {
    size_t output_len = 4 * ((length + 2) / 3);
    char *encoded = malloc(output_len + 1);
    if (encoded == NULL) {
        return NULL;
    }
    
    size_t i = 0;
    size_t j = 0;
    size_t remaining = length;
    while (remaining >= 3) {
        encoded[j++] = base64_table[input[i] >> 2];
        encoded[j++] = base64_table[((input[i] & 0x03) << 4) | (input[i + 1] >> 4)];
        encoded[j++] = base64_table[((input[i + 1] & 0x0f) << 2) | (input[i + 2] >> 6)];
        encoded[j++] = base64_table[input[i + 2] & 0x3f];
        i += 3;
        remaining -= 3;
    }
    
    if (remaining) {
        encoded[j++] = base64_table[input[i] >> 2];
        if (remaining == 1) {
            encoded[j++] = base64_table[(input[i] & 0x03) << 4];
            encoded[j++] = '=';
        }
        else {
            encoded[j++] = base64_table[((input[i] & 0x03) << 4) | (input[i + 1] >> 4)];
            encoded[j++] = base64_table[(input[i + 1] & 0x0f) << 2];
        }
        encoded[j++] = '=';
    }
    
    encoded[j] = '\0';
    return encoded;
}

tracer_result_t encode_tracer_config(tracer_config_t *config, char **out_str) {
    if (config == NULL || out_str == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    json_object *root = json_object_new_object();
    if (root == NULL) {
        return TRACER_ERROR_MEMORY;
    }
    
    if (config->transport_config.host) {
        json_object_object_add(root, "host", json_object_new_string(config->transport_config.host));
    }
    if (config->transport_config.port) {
        json_object_object_add(root, "port", json_object_new_int(config->transport_config.port));
    }
    if (config->transport_config.file_path) {
        json_object_object_add(root, "file", json_object_new_string(config->transport_config.file_path));
    }
    json_object_object_add(root, "transport", json_object_new_int(config->transport));
    
    json_object *format = json_object_new_object();
    if (format == NULL) {
        json_object_put(root);
        return TRACER_ERROR_MEMORY;
    }
    
    json_object_object_add(format, "include_formatted_trace", json_object_new_boolean(config->format.include_formatted_trace));
    json_object_object_add(format, "include_event_json", json_object_new_boolean(config->format.include_event_json));
    json_object_object_add(format, "output_as_json", json_object_new_boolean(config->format.output_as_json));
    json_object_object_add(format, "include_colors", json_object_new_boolean(config->format.include_colors));
    json_object_object_add(format, "include_thread_id", json_object_new_boolean(config->format.include_thread_id));
    json_object_object_add(format, "include_indents", json_object_new_boolean(config->format.include_indents));
    json_object_object_add(format, "indent_char", json_object_new_string(config->format.indent_char));
    json_object_object_add(format, "include_indent_separators", json_object_new_boolean(config->format.include_indent_separators));
    json_object_object_add(format, "indent_separator_char", json_object_new_string(config->format.indent_separator_char));
    json_object_object_add(format, "variable_separator_spacing", json_object_new_boolean(config->format.variable_separator_spacing));
    json_object_object_add(format, "static_separator_spacing", json_object_new_int(config->format.static_separator_spacing));
    json_object_object_add(format, "include_newline_in_formatted_trace", json_object_new_boolean(config->format.include_newline_in_formatted_trace));
    json_object_object_add(format, "arg_format", json_object_new_int(config->format.args));
    json_object_object_add(root, "format", format);
    
    if (config->filter_count > 0) {
        json_object *filters_array = json_object_new_array();
        if (filters_array == NULL) {
            json_object_put(root);
            return TRACER_ERROR_MEMORY;
        }
        
        for (size_t i = 0; i < config->filter_count; i++) {
            json_object *filter = json_object_new_object();
            if (filter == NULL) {
                continue;
            }
            
            if (config->filters[i].class_pattern) {
                json_object_object_add(filter, "class", json_object_new_string(config->filters[i].class_pattern));
            }
            if (config->filters[i].method_pattern) {
                json_object_object_add(filter, "method", json_object_new_string(config->filters[i].method_pattern));
            }
            if (config->filters[i].image_pattern) {
                json_object_object_add(filter, "image", json_object_new_string(config->filters[i].image_pattern));
            }
            
            json_object_object_add(filter, "exclude", json_object_new_boolean(config->filters[i].exclude));
            json_object_array_add(filters_array, filter);
        }
        
        json_object_object_add(root, "filters", filters_array);
    }

    const char *json_str = json_object_to_json_string(root);
    if (json_str == NULL) {
        json_object_put(root);
        return TRACER_ERROR_MEMORY;
    }
    
    *out_str = base64_encode((const unsigned char *)json_str, strlen(json_str));

    json_object_put(root);
    return *out_str ? TRACER_SUCCESS : TRACER_ERROR_INVALID_ARGUMENT;
}
