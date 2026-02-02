//
//  config_decode.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#include <yyjson.h>
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
        if (a == 255 || b == 255 || c == 255 || d == 255) {
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

    yyjson_doc *doc = yyjson_read((const char *)json_str, json_len, 0);
    free(json_str);
    if (doc == NULL) {
        return TRACER_ERROR_RUNTIME;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    tracer_config_t config_out = {0};
    tracer_format_options_t format = {0};

    yyjson_val *obj;

    obj = yyjson_obj_get(root, "port");
    if (obj != NULL) {
        config_out.transport_config.port = yyjson_get_int(obj);
    }
    
    obj = yyjson_obj_get(root, "host");
    if (obj != NULL) {
        config_out.transport_config.host = strdup(yyjson_get_str(obj));
    }
    

    obj = yyjson_obj_get(root, "file");
    if (obj != NULL) {
        config_out.transport_config.file_path = strdup(yyjson_get_str(obj));
    }

    obj = yyjson_obj_get(root, "transport");
    if (obj != NULL) {
        config_out.transport = yyjson_get_int(obj);
    }

    obj = yyjson_obj_get(root, "format");
    if (obj != NULL) {
        yyjson_val *format_obj;

        format_obj = yyjson_obj_get(obj, "include_formatted_trace");
        if (format_obj != NULL) {
            format.include_formatted_trace = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "include_event_json");
        if (format_obj != NULL) {
            format.include_event_json = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "output_as_json");
        if (format_obj != NULL) {
            format.output_as_json = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "include_colors");
        if (format_obj != NULL) {
            format.include_colors = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "include_thread_id");
        if (format_obj != NULL) {
            format.include_thread_id = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "include_indents");
        if (format_obj != NULL) {
            format.include_indents = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "indent_char");
        if (format_obj != NULL) {
            format.indent_char = strdup(yyjson_get_str(format_obj));
        }

        format_obj = yyjson_obj_get(obj, "include_indent_separators");
        if (format_obj != NULL) {
            format.include_indent_separators = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "indent_separator_char");
        if (format_obj != NULL) {
            format.indent_separator_char = strdup(yyjson_get_str(format_obj));
        }

        format_obj = yyjson_obj_get(obj, "variable_separator_spacing");
        if (format_obj != NULL) {
            format.variable_separator_spacing = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "static_separator_spacing");
        if (format_obj != NULL) {
            format.static_separator_spacing = yyjson_get_int(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "include_newline_in_formatted_trace");
        if (format_obj != NULL) {
            format.include_newline_in_formatted_trace = yyjson_get_bool(format_obj);
        }

        format_obj = yyjson_obj_get(obj, "arg_format");
        if (format_obj != NULL) {
            format.args = yyjson_get_int(format_obj);
        }
        
        format_obj = yyjson_obj_get(obj, "include_caller_info");
        if (format_obj != NULL) {
            format.include_caller_info = yyjson_get_bool(format_obj);
        }
        
        config_out.format = format;
    }
            
    obj = yyjson_obj_get(root, "filters");
    if (obj != NULL) {
        size_t filter_count = yyjson_arr_size(obj);
        if (filter_count > 0) {
            uint32_t valid_filters = 0;
            for (uint32_t i = 0; i < filter_count; i++) {
                yyjson_val *single_filter = yyjson_arr_get(obj, i);
                yyjson_val *single_filter_value;

                config_out.filters[valid_filters].class_pattern = NULL;
                single_filter_value = yyjson_obj_get(single_filter, "class");
                if (single_filter_value != NULL) {
                    config_out.filters[valid_filters].class_pattern = strdup(yyjson_get_str(single_filter_value));
                }
                
                config_out.filters[valid_filters].method_pattern = NULL;
                single_filter_value = yyjson_obj_get(single_filter, "method");
                if (single_filter_value != NULL) {
                    config_out.filters[valid_filters].method_pattern = strdup(yyjson_get_str(single_filter_value));
                }
                
                config_out.filters[valid_filters].image_pattern = NULL;
                single_filter_value = yyjson_obj_get(single_filter, "image");
                if (single_filter_value != NULL) {
                    config_out.filters[valid_filters].image_pattern = strdup(yyjson_get_str(single_filter_value));
                }
                
                config_out.filters[valid_filters].exclude = false;
                single_filter_value = yyjson_obj_get(single_filter, "exclude");
                if (single_filter_value != NULL) {
                    config_out.filters[valid_filters].exclude = yyjson_get_bool(single_filter_value);
                }

                if (config_out.filters[valid_filters].class_pattern != NULL || config_out.filters[valid_filters].method_pattern != NULL || config_out.filters[valid_filters].image_pattern != NULL) {
                    valid_filters++;
                }
            }
            config_out.filter_count = valid_filters;
        }
    }
    
    config_out.tracer_delay_ms = 0;
    obj = yyjson_obj_get(root, "tracer_delay_ms");
    if (obj != NULL) {
        config_out.tracer_delay_ms = yyjson_get_int(obj);
    }
    
    config_out.use_symbol_rebinding = false;
    obj = yyjson_obj_get(root, "use_symbol_rebinding");
    if (obj != NULL) {
        config_out.use_symbol_rebinding = yyjson_get_bool(obj);
    }

    yyjson_doc_free(doc);

    *config = config_out;
    return TRACER_SUCCESS;
}

const char *copy_config_description(tracer_config_t config) {
    const size_t buffer_size = 1024;
    char *formatted = (char *)malloc(buffer_size);
    if (formatted == NULL) {
        return NULL;
    }
    
    int offset = 0;
    int written = 0;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Transport: %d, ", config.transport);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    if (config.transport == TRACER_TRANSPORT_SOCKET) {
        written = snprintf(formatted + offset, buffer_size - offset, "Host: %s, ", config.transport_config.host);
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
        
        written = snprintf(formatted + offset, buffer_size - offset, "Port: %d, ", config.transport_config.port);
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
    }
    else if (config.transport == TRACER_TRANSPORT_FILE) {
        written = snprintf(formatted + offset, buffer_size - offset, "File: %s, ", config.transport_config.file_path);
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
    }
    else if (config.transport == TRACER_TRANSPORT_CUSTOM) {
        written = snprintf(formatted + offset, buffer_size - offset, "Custom transport, ");
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
    }
    else {
        written = snprintf(formatted + offset, buffer_size - offset, "Stdout transport, ");
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
    }
    
    written = snprintf(formatted + offset, buffer_size - offset, "Include formatted trace: %d, ", config.format.include_formatted_trace);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Include event json: %d, ", config.format.include_event_json);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Output as json: %d, ", config.format.output_as_json);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Include colors: %d, ", config.format.include_colors);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Include thread id: %d, ", config.format.include_thread_id);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Include indents: %d, ", config.format.include_indents);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Indent char: %s, ", config.format.indent_char);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Include indent separators: %d, ", config.format.include_indent_separators);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Indent separator: %s, ", config.format.indent_separator_char);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Variable separator spacing: %d, ", config.format.variable_separator_spacing);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Static separator spacing: %d, ", config.format.static_separator_spacing);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Include newline in formatted trace: %d, ", config.format.include_newline_in_formatted_trace);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Arg format: %d, ", config.format.args);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Launched from DYLD_INSERT_LIB: %d, ", config.from_dyld_insert);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Tracer delay ms: %d, ", config.tracer_delay_ms);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    written = snprintf(formatted + offset, buffer_size - offset, "Symbol rebinding: %d\n", config.use_symbol_rebinding);
    if (written < 0 || written >= buffer_size - offset) {
        free(formatted);
        return NULL;
    }
    offset += written;
    
    for (int i = 0; i < config.filter_count; i++) {
        written = snprintf(formatted + offset, buffer_size - offset, "Filter %d Class pattern: %s, ", i, config.filters[i].class_pattern);
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
        
        written = snprintf(formatted + offset, buffer_size - offset, "Filter %d Method pattern: %s, ", i, config.filters[i].method_pattern);
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
        
        written = snprintf(formatted + offset, buffer_size - offset, "Filter %d Image pattern: %s, ", i, config.filters[i].image_pattern);
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
        
        written = snprintf(formatted + offset, buffer_size - offset, "Filter %d Exclude: %d\n", i, config.filters[i].exclude);
        if (written < 0 || written >= buffer_size - offset) {
            free(formatted);
            return NULL;
        }
        offset += written;
    }
    
    return formatted;
}
