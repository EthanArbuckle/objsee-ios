//
//  config_encode.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#include <CoreFoundation/CoreFoundation.h>
#include <yyjson.h>
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

const char *encode_tracer_config(tracer_config_t *config) {
    if (config == NULL) {
        return NULL;
    }

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NULL;
    }

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    if (root == NULL) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    yyjson_mut_doc_set_root(doc, root);

    if (config->transport_config.host != NULL) {
        yyjson_mut_obj_add_strcpy(doc, root, "host", config->transport_config.host);
    }
    if (config->transport_config.port) {
        yyjson_mut_obj_add_int(doc, root, "port", config->transport_config.port);
    }
    if (config->transport_config.file_path != NULL) {
        yyjson_mut_obj_add_strcpy(doc, root, "file", config->transport_config.file_path);
    }
    yyjson_mut_obj_add_int(doc, root, "transport", config->transport);

    yyjson_mut_val *format = yyjson_mut_obj(doc);
    if (format == NULL) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    
    yyjson_mut_obj_add_bool(doc, format, "include_formatted_trace", config->format.include_formatted_trace);
    yyjson_mut_obj_add_bool(doc, format, "include_event_json", config->format.include_event_json);
    yyjson_mut_obj_add_bool(doc, format, "output_as_json", config->format.output_as_json);
    yyjson_mut_obj_add_bool(doc, format, "include_colors", config->format.include_colors);
    yyjson_mut_obj_add_bool(doc, format, "include_thread_id", config->format.include_thread_id);
    yyjson_mut_obj_add_bool(doc, format, "include_indents", config->format.include_indents);
    yyjson_mut_obj_add_strcpy(doc, format, "indent_char", config->format.indent_char);
    yyjson_mut_obj_add_bool(doc, format, "include_indent_separators", config->format.include_indent_separators);
    yyjson_mut_obj_add_strcpy(doc, format, "indent_separator_char", config->format.indent_separator_char);
    yyjson_mut_obj_add_bool(doc, format, "variable_separator_spacing", config->format.variable_separator_spacing);
    yyjson_mut_obj_add_int(doc, format, "static_separator_spacing", config->format.static_separator_spacing);
    yyjson_mut_obj_add_bool(doc, format, "include_newline_in_formatted_trace", config->format.include_newline_in_formatted_trace);
    yyjson_mut_obj_add_int(doc, format, "arg_format", config->format.args);
    yyjson_mut_obj_add_val(doc, root, "format", format);

    if (config->filter_count > 0) {
        yyjson_mut_val *filters_array = yyjson_mut_arr(doc);
        if (filters_array == NULL) {
            yyjson_mut_doc_free(doc);
            return NULL;
        }
        
        for (size_t i = 0; i < config->filter_count; i++) {
            yyjson_mut_val *filter = yyjson_mut_obj(doc);
            if (filter == NULL) {
                continue;
            }

            if (config->filters[i].class_pattern != NULL) {
                yyjson_mut_obj_add_strcpy(doc, filter, "class", config->filters[i].class_pattern);
            }
            if (config->filters[i].method_pattern != NULL) {
                yyjson_mut_obj_add_strcpy(doc, filter, "method", config->filters[i].method_pattern);
            }
            if (config->filters[i].image_pattern != NULL) {
                yyjson_mut_obj_add_strcpy(doc, filter, "image", config->filters[i].image_pattern);
            }

            yyjson_mut_obj_add_bool(doc, filter, "exclude", config->filters[i].exclude);
            yyjson_mut_arr_append(filters_array, filter);
        }

        yyjson_mut_obj_add_val(doc, root, "filters", filters_array);
    }

    yyjson_mut_obj_add_int(doc, root, "tracer_delay_ms", config->tracer_delay_ms);
    yyjson_mut_obj_add_bool(doc, root, "use_symbol_rebinding", config->use_symbol_rebinding);

    size_t json_len;
    char *json_str = yyjson_mut_write(doc, 0, &json_len);
    if (json_str == NULL) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    
    const char *encoded_config = base64_encode((const unsigned char *)json_str, json_len);

    free(json_str);
    yyjson_mut_doc_free(doc);
    
    return encoded_config;
}
