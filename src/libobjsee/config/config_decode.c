//
//  config_decode.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#include <json-c/json_tokener.h>
#include "config_decode.h"
#include "format.h"

static unsigned char *base64_decode(const char *input, size_t *out_length) {
    if (input == NULL || out_length == NULL) {
        return NULL;
    }
    
    size_t input_len = strlen(input);
    if (input_len % 4 != 0) {
        return NULL;
    }
    
    *out_length = (input_len / 4) * 3;
    if (input[input_len - 1] == '=') {
        (*out_length)--;
    }
    if (input[input_len - 2] == '=') {
        (*out_length)--;
    }
    
    unsigned char *decoded = malloc(*out_length);
    if (decoded == NULL) {
        return NULL;
    }
    
    static const unsigned char decode_table[256] = {
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,62,255,255,255,63,
        52,53,54,55,56,57,58,59,60,61,255,255,255,64,255,255,
        255, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,255,255,255,255,255,
        255,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
    };

    const unsigned char *encoded = (const unsigned char *)input;
    size_t i = 0;
    size_t j = 0;
    while (i < input_len) {

        unsigned char a = decode_table[encoded[i++]];
        unsigned char b = decode_table[encoded[i++]];
        unsigned char c = decode_table[encoded[i++]];
        unsigned char d = decode_table[encoded[i++]];
        if (a == 255 || b == 255 || (c == 255 && c != 64) || (d == 255 && d != 64)) {
            free(decoded);
            return NULL;
        }
        
        decoded[j++] = (a << 2) | (b >> 4);
        if (c != 64) {
            decoded[j++] = (b << 4) | (c >> 2);
            if (d != 64) {
                decoded[j++] = (c << 6) | d;
            }
        }
    }
    
    return decoded;
}

tracer_result_t decode_tracer_config(const char *config_str, tracer_config_t *config) {
    if (config_str == NULL || config == NULL) {
        return TRACER_ERROR_INVALID_ARGUMENT;
    }
    
    size_t json_len;
    unsigned char *json_str = base64_decode(config_str, &json_len);
    if (json_str == NULL) {
        return TRACER_ERROR_RUNTIME;
    }
    
    struct json_object *root = json_tokener_parse((const char *)json_str);
    free(json_str);
    if (root == NULL) {
        return TRACER_ERROR_RUNTIME;
    }

    tracer_config_t config_out = {0};
    tracer_format_options_t format = {0};

    json_object *obj;
    if (json_object_object_get_ex(root, "port", &obj)) {
        config_out.transport_config.port = json_object_get_int(obj);
    }
    
    if (json_object_object_get_ex(root, "host", &obj)) {
        config_out.transport_config.host = strdup(json_object_get_string(obj));
    }
    
    if (json_object_object_get_ex(root, "file", &obj)) {
        config_out.transport_config.file_path = strdup(json_object_get_string(obj));
    }
    
    if (json_object_object_get_ex(root, "transport", &obj)) {
        config_out.transport = json_object_get_int(obj);
    }
    
    if (json_object_object_get_ex(root, "format", &obj)) {
        json_object *format_obj = obj;
        if (json_object_object_get_ex(obj, "include_formatted_trace", &format_obj)) {
            format.include_formatted_trace = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "include_event_json", &format_obj)) {
            format.include_event_json = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "output_as_json", &format_obj)) {
            format.output_as_json = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "include_colors", &format_obj)) {
            format.include_colors = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "include_thread_id", &format_obj)) {
            format.include_thread_id = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "include_indents", &format_obj)) {
            format.include_indents = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "indent_char", &format_obj)) {
            format.indent_char = strdup(json_object_get_string(format_obj));
        }
        
        if (json_object_object_get_ex(obj, "include_indent_separators", &format_obj)) {
            format.include_indent_separators = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "indent_separator_char", &format_obj)) {
            format.indent_separator_char = strdup(json_object_get_string(format_obj));
        }
        
        if (json_object_object_get_ex(obj, "variable_separator_spacing", &format_obj)) {
            format.variable_separator_spacing = json_object_get_boolean(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "static_separator_spacing", &format_obj)) {
            format.static_separator_spacing = json_object_get_int(format_obj);
        }
        
        if (json_object_object_get_ex(obj, "include_newline_in_formatted_trace", &format_obj)) {
            format.include_newline_in_formatted_trace = json_object_get_boolean(format_obj);
        }

        if (json_object_object_get_ex(obj, "arg_format", &format_obj)) {
            format.args = json_object_get_int(format_obj);
        }
        config_out.format = format;
    }
    
    if (json_object_object_get_ex(root, "filters", &obj)) {
        size_t filter_count = json_object_array_length(obj);
            
        if (filter_count > 0) {
            uint32_t valid_filters = 0;
            for (uint32_t i = 0; i < filter_count; i++) {
                
                json_object *single_filter = json_object_array_get_idx(obj, i);
                json_object *single_filter_value;
                
                config_out.filters[valid_filters].class_pattern = NULL;
                if (json_object_object_get_ex(single_filter, "class", &single_filter_value)) {
                    const char *class_pattern = json_object_get_string(single_filter_value);
                    config_out.filters[valid_filters].class_pattern = strdup(class_pattern);
                }
                
                config_out.filters[valid_filters].method_pattern = NULL;
                if (json_object_object_get_ex(single_filter, "method", &single_filter_value)) {
                    const char *method_pattern = json_object_get_string(single_filter_value);
                    config_out.filters[valid_filters].method_pattern = strdup(method_pattern);
                }
                
                config_out.filters[valid_filters].image_pattern = NULL;
                if (json_object_object_get_ex(single_filter, "image", &single_filter_value)) {
                    const char *image_pattern = json_object_get_string(single_filter_value);
                    config_out.filters[valid_filters].image_pattern = strdup(image_pattern);
                }
                
                config_out.filters[valid_filters].exclude = false;
                if (json_object_object_get_ex(single_filter, "exclude", &single_filter_value)) {
                    config_out.filters[valid_filters].exclude = json_object_get_boolean(single_filter_value);
                }
                
                if (config_out.filters[valid_filters].class_pattern || config_out.filters[valid_filters].method_pattern || config_out.filters[valid_filters].image_pattern) {
                    valid_filters++;
                }
            }
            config_out.filter_count = valid_filters;
        }
    }
    
    json_object_put(root);

    *config = config_out;
    return TRACER_SUCCESS;
}

const char *copy_human_readable_config(tracer_config_t config) {    
    char *formatted = (char *)malloc(1024);
    if (formatted == NULL) {
        return NULL;
    }
    
    int offset = 0;
    offset += snprintf(formatted + offset, 1024 - offset, "Transport: %d\n", config.transport);
    if (config.transport == TRACER_TRANSPORT_SOCKET) {
        offset += snprintf(formatted + offset, 1024 - offset, "Host: %s\n", config.transport_config.host);
        offset += snprintf(formatted + offset, 1024 - offset, "Port: %d\n", config.transport_config.port);
    }
    else if (config.transport == TRACER_TRANSPORT_FILE) {
        offset += snprintf(formatted + offset, 1024 - offset, "File: %s\n", config.transport_config.file_path);
    }
    else if (config.transport == TRACER_TRANSPORT_CUSTOM) {
        offset += snprintf(formatted + offset, 1024 - offset, "Custom transport\n");
    }
    else {
        offset += snprintf(formatted + offset, 1024 - offset, "Stdout transport\n");
    }
    
    offset += snprintf(formatted + offset, 1024 - offset, "Format:\n");
    offset += snprintf(formatted + offset, 1024 - offset, "Include formatted trace: %d\n", config.format.include_formatted_trace);
    offset += snprintf(formatted + offset, 1024 - offset, "Include event json: %d\n", config.format.include_event_json);
    offset += snprintf(formatted + offset, 1024 - offset, "Output as json: %d\n", config.format.output_as_json);
    offset += snprintf(formatted + offset, 1024 - offset, "Include colors: %d\n", config.format.include_colors);
    offset += snprintf(formatted + offset, 1024 - offset, "Include thread id: %d\n", config.format.include_thread_id);
    offset += snprintf(formatted + offset, 1024 - offset, "Include indents: %d\n", config.format.include_indents);
    offset += snprintf(formatted + offset, 1024 - offset, "Indent char: %s\n", config.format.indent_char);
    offset += snprintf(formatted + offset, 1024 - offset, "Include indent separators: %d\n", config.format.include_indent_separators);
    offset += snprintf(formatted + offset, 1024 - offset, "Indent separator char: %s\n", config.format.indent_separator_char);
    offset += snprintf(formatted + offset, 1024 - offset, "Variable separator spacing: %d\n", config.format.variable_separator_spacing);
    offset += snprintf(formatted + offset, 1024 - offset, "Static separator spacing: %d\n", config.format.static_separator_spacing);
    offset += snprintf(formatted + offset, 1024 - offset, "Include newline in formatted trace: %d\n", config.format.include_newline_in_formatted_trace);
    offset += snprintf(formatted + offset, 1024 - offset, "Arg format: %d\n", config.format.args);
    
    offset += snprintf(formatted + offset, 1024 - offset, "Filters:\n");
    for (int i = 0; i < config.filter_count; i++) {
        offset += snprintf(formatted + offset, 1024 - offset, "Filter %d:\n", i);
        offset += snprintf(formatted + offset, 1024 - offset, "Class pattern: %s\n", config.filters[i].class_pattern);
        offset += snprintf(formatted + offset, 1024 - offset, "Method pattern: %s\n", config.filters[i].method_pattern);
        offset += snprintf(formatted + offset, 1024 - offset, "Image pattern: %s\n", config.filters[i].image_pattern);
        offset += snprintf(formatted + offset, 1024 - offset, "Exclude: %d\n", config.filters[i].exclude);
    }
    
    return formatted;
}
