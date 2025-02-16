//
//  format.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <CoreFoundation/CoreFoundation.h>
#include <json-c/json_object.h>
#include "tracer_internal.h"
#include "encoding_description.h"
#include "color_utils.h"
#include <dlfcn.h>

#define STATIC_BUFFER_SIZE 1024
#define FORMATTED_EVENT_BUF_SIZE 1024
#define ASSEMBLED_METHOD_BUF_SIZE 1024


static inline kern_return_t fast_append(char **ptr, const char *end, const char *format, ...) {
    if (!ptr || !*ptr || !end || format == NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    
    ptrdiff_t remaining = end - *ptr;
    if (remaining < 0 || remaining > FORMATTED_EVENT_BUF_SIZE) {
        return KERN_NO_SPACE;
    }
    
    va_list args;
    va_start(args, format);
    
    va_list args_copy;
    va_copy(args_copy, args);
    
    int written = vsnprintf(*ptr, remaining, format, args);
    va_end(args);
    
    if (written < 0 || written >= remaining) {
        va_end(args_copy);
        return KERN_NO_SPACE;
    }
    
    if (written > 0 && *ptr + written <= end) {
        *ptr += written;
    }
    
    va_end(args_copy);
    return KERN_SUCCESS;
}

static inline kern_return_t fast_write_color(char **ptr, const char *end, uint8_t color) {
    return fast_append(ptr, end, "\033[38;5;%dm", color);
}

__unused static char *format_binary_data(const char *data, size_t size) {
    static __thread char hex_buffer[STATIC_BUFFER_SIZE];
    size_t display_size = size > 16 ? 16 : size;
    
    int written = snprintf(hex_buffer, STATIC_BUFFER_SIZE, "<binary:%zu bytes: ", size);
    if (written < 0 || written >= STATIC_BUFFER_SIZE) {
        return strdup("<format error>");
    }
    
    char *hex_ptr = hex_buffer + written;
    int remaining = STATIC_BUFFER_SIZE - written;
    
    for (size_t i = 0; i < display_size && remaining > 2; i++) {
        written = snprintf(hex_ptr, remaining, "%02x", (unsigned char)data[i]);
        if (written < 0 || written >= remaining) {
            return strdup("<format error>");
        }
        hex_ptr += written;
        remaining -= written;
    }
    
    if (size > 16 && remaining > 3) {
        strncat(hex_ptr, "...>", remaining);
    }
    else if (remaining > 1) {
        strncat(hex_ptr, ">", remaining);
    }
    
    return strdup(hex_buffer);
}

static uint32_t spaces_between_indent_level(uint32_t indent_level) {
    switch (indent_level) {
        case 0:
        case 1:
        case 2:
        case 3:
            return 3;
        case 4:
        case 5:
        case 6:
        case 7:
            return 2;
        default:
            return 1;
    }
    return 0;
}

static const char *demangle_swift(const char *name) {
    typedef char *(*swift_demangle_ft)(const char *mangledName, size_t mangledNameLength, char *outputBuffer, size_t *outputBufferSize, uint32_t flags);
    static swift_demangle_ft swift_demangle_f;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        swift_demangle_f = (swift_demangle_ft) dlsym(RTLD_DEFAULT, "swift_demangle");
    });
    
    if (swift_demangle_f) {
        return swift_demangle_f(name, strlen(name), 0, 0, 0);
    }
    return name;
}

const char *build_formatted_event_str(const tracer_event_t *event, tracer_format_options_t format) {
    if (event == NULL || event->class_name == NULL || event->method_name == NULL) {
        return NULL;
    }
    
    char formatted_event_buf[FORMATTED_EVENT_BUF_SIZE] = {0};
    char assembled_method_name_buf[ASSEMBLED_METHOD_BUF_SIZE] = {0};
    char *ptr = formatted_event_buf;
    const char *const end = formatted_event_buf + FORMATTED_EVENT_BUF_SIZE;
    
    // Thread ID formatting
    if (format.include_thread_id) {
        if (format.include_colors) {
            uint8_t thread_color = COLOR_THREAD_START + (event->thread_id % (COLOR_THREAD_END - COLOR_THREAD_START));
            if (fast_write_color(&ptr, end, thread_color) != KERN_SUCCESS) {
                return NULL;
            }
        }
        
        if (fast_append(&ptr, end, "[0x%x] ", event->thread_id) != KERN_SUCCESS) {
            return NULL;
        }
        
        if (format.include_colors) {
            if (fast_append(&ptr, end, COLOR_RESET) != KERN_SUCCESS) {
                return NULL;
            }
        }
    }
    
    // Indentation
    if (format.include_indents) {
        uint8_t depth_color = format.include_colors ? COLOR_DEPTH_START + (event->trace_depth % (COLOR_DEPTH_END - COLOR_DEPTH_START)) : 0;
        
        for (uint32_t i = 0; i < event->trace_depth; i++) {
            uint32_t spaces = format.variable_separator_spacing ? spaces_between_indent_level(i) : format.static_separator_spacing;
            
            for (uint32_t j = 0; j < spaces; j++) {
                if (fast_append(&ptr, end, format.indent_char) != KERN_SUCCESS) {
                    return NULL;
                }
            }
            
            if (format.include_indent_separators) {
                if (format.include_colors) {
                    if (fast_write_color(&ptr, end, depth_color) != KERN_SUCCESS) {
                        return NULL;
                    }
                }
                
                if (fast_append(&ptr, end, format.indent_separator_char) != KERN_SUCCESS) {
                    return NULL;
                }
                
                if (format.include_colors) {
                    if (fast_append(&ptr, end, COLOR_RESET) != KERN_SUCCESS) {
                        return NULL;
                    }
                }
            }
        }
        
        if (event->trace_depth > 0) {
            if (fast_append(&ptr, end, format.indent_char) != KERN_SUCCESS) {
                return NULL;
            }
        }
    }
    
    // Class name and method type
    const char *class_name = event->class_name;
    if (class_name && strncmp(class_name, "_Tt", 3) == 0) {
        // Demangle Swift class names
        const char *demangled = demangle_swift(class_name);
        if (demangled) {
            free((void *)class_name);
            class_name = demangled;
        }
    }
    
    if (format.include_colors && class_name) {
        uint8_t class_color = get_consistent_color(class_name, COLOR_CLASS_START, COLOR_CLASS_RANGE);
        if (fast_write_color(&ptr, end, class_color) != KERN_SUCCESS) {
            return NULL;
        }
    }
    
    if (fast_append(&ptr, end, "%s[%s ", event->is_class_method ? "+" : "-", class_name) != KERN_SUCCESS) {
        return NULL;
    }
    
    if (strlcpy(assembled_method_name_buf, event->method_name, ASSEMBLED_METHOD_BUF_SIZE) >= ASSEMBLED_METHOD_BUF_SIZE) {
        return NULL;
    }
    
    // Process method parts and arguments
    size_t arg_index = 0;
    char *method_part = strtok(assembled_method_name_buf, ":");
    while (method_part != NULL) {
        if (format.include_colors) {
            uint8_t method_color = get_consistent_color(event->method_name, COLOR_METHOD_START, COLOR_METHOD_RANGE);
            if (fast_write_color(&ptr, end, method_color) != KERN_SUCCESS) {
                return NULL;
            }
        }
        
        if (fast_append(&ptr, end, "%s", method_part) != KERN_SUCCESS) {
            return NULL;
        }
        
        const char *remaining = event->method_name + (method_part - assembled_method_name_buf);
        if (remaining && strchr(remaining, ':')) {
            if (fast_append(&ptr, end, ":") != KERN_SUCCESS) {
                return NULL;
            }
        }
        
        if (arg_index < event->argument_count) {
            const tracer_argument_t *arg = &event->arguments[arg_index];
            
            if (format.include_colors) {
                if (fast_append(&ptr, end, COLOR_RESET) != KERN_SUCCESS) {
                    return NULL;
                }
            }
            
            const char *type = arg->objc_class_name ? arg->objc_class_name : arg->type_encoding;
            uint8_t arg_color = format.include_colors ? get_consistent_color(type, COLOR_METHOD_START, COLOR_METHOD_RANGE) : 0;
            
            if (arg->block_signature) {
                if (fast_append(&ptr, end, " ") != KERN_SUCCESS) {
                    return NULL;
                }
                if (format.include_colors && fast_write_color(&ptr, end, arg_color) != KERN_SUCCESS) {
                    return NULL;
                }
                if (fast_append(&ptr, end, "(%s)", arg->block_signature) != KERN_SUCCESS) {
                    return NULL;
                }
                if (format.include_colors && fast_append(&ptr, end, COLOR_RESET) != KERN_SUCCESS) {
                    return NULL;
                }
            }
            else {
                if (format.include_colors) {
                    if (fast_write_color(&ptr, end, arg_color) != KERN_SUCCESS) {
                        return NULL;
                    }
                }
                
                if (arg->description) {
                    if (fast_append(&ptr, end, "%s",  arg->description) != KERN_SUCCESS) {
                        return NULL;
                    }
                }
                else {
                    if (fast_append(&ptr, end, "nil") != KERN_SUCCESS) {
                        return NULL;
                    }
                }
                
                if (format.include_colors) {
                    if (fast_append(&ptr, end, COLOR_RESET) != KERN_SUCCESS) {
                        return NULL;
                    }
                }
            }
            
            if (arg_index + 1 < event->argument_count) {
                if (fast_append(&ptr, end, " ") != KERN_SUCCESS) {
                    return NULL;
                }
            }
            
            arg_index++;
        }
        
        method_part = strtok(NULL, ":");
    }
    
    if (format.include_colors) {
        uint8_t class_color = get_consistent_color(event->class_name, COLOR_CLASS_START, COLOR_CLASS_RANGE);
        if (fast_write_color(&ptr, end, class_color) != KERN_SUCCESS) {
            return NULL;
        }
    }
    
    if (fast_append(&ptr, end, "]") != KERN_SUCCESS) {
        return NULL;
    }
    
    if (format.include_newline_in_formatted_trace) {
        if (fast_append(&ptr, end, "\n") != KERN_SUCCESS) {
            return NULL;
        }
    }
    
    if (format.include_colors) {
        if (fast_append(&ptr, end, COLOR_RESET) != KERN_SUCCESS) {
            return NULL;
        }
    }
    
    return strdup(formatted_event_buf);
}

#define JSON_SAFE_ADD_FUNC(_root, _key, _ptr, _func) if (_ptr) json_object_object_add(_root, _key, _func(_ptr))
#define JSON_ADD_INT(_root, _key, _ptr) JSON_SAFE_ADD_FUNC(_root, _key, _ptr, json_object_new_int)
#define JSON_ADD_INT64(_root, _key, _ptr) JSON_SAFE_ADD_FUNC(_root, _key, _ptr, json_object_new_int64)
#define JSON_ADD_BOOL(_root, _key, _ptr) JSON_SAFE_ADD_FUNC(_root, _key, _ptr, json_object_new_boolean)
#define JSON_ADD_STRING(_root, _key, _ptr) JSON_SAFE_ADD_FUNC(_root, _key, _ptr, json_object_new_string)

const char *build_json_event_str(const tracer_t *tracer, const tracer_event_t *event) {
    if (event == NULL || tracer == NULL) {
        return NULL;
    }
    
    if (event->class_name == NULL || event->method_name == NULL) {
        return NULL;
    }
    
    json_object *root = json_object_new_object();
    if (root == NULL) {
        return NULL;
    }
    
    tracer_format_options_t format = tracer->config.format;
    if (format.include_formatted_trace) {
        const char *formatted = build_formatted_event_str(event, format);
        if (formatted) {
            JSON_ADD_STRING(root, "formatted_output", formatted);
            free((void *)formatted);
        }
    }
    
    if (format.include_event_json) {
        
        JSON_ADD_STRING(root, "class", event->class_name);
        JSON_ADD_STRING(root, "method", event->method_name);
        JSON_ADD_BOOL(root, "is_class_method", event->is_class_method);
        JSON_ADD_INT64(root, "thread_id", event->thread_id);
        JSON_ADD_INT(root, "depth", event->real_depth);
        JSON_ADD_STRING(root, "signature", event->method_signature);
        
        if (format.args != TRACER_ARG_FORMAT_NONE) {
            if (event->arguments && event->argument_count > 0) {
                json_object *args_array = json_object_new_array();
                if (args_array == NULL) {
                    json_object_put(root);
                    tracer_set_error((tracer_t *)tracer, "Failed to create JSON array for arguments");
                    return NULL;
                }
                
                for (size_t i = 0; i < event->argument_count; i++) {
                    const tracer_argument_t *curr_arg = &event->arguments[i];
                    if (curr_arg->type_encoding == NULL) {
                        tracer_set_error((tracer_t *)tracer, "Argument type encoding is NULL");
                        continue;
                    }

                    json_object *arg = json_object_new_object();
                    if (arg == NULL) {
                        json_object_put(args_array);
                        json_object_put(root);
                        tracer_set_error((tracer_t *)tracer, "Failed to create JSON object for argument");
                        return NULL;
                    }
                    
                    JSON_ADD_STRING(arg, "type", get_name_of_type_from_type_encoding(curr_arg->type_encoding));
                    JSON_ADD_STRING(arg, "class", curr_arg->objc_class_name);
                    JSON_ADD_STRING(arg, "block_signature", curr_arg->block_signature);
                    JSON_ADD_STRING(arg, "description", curr_arg->description);
                    JSON_ADD_STRING(arg, "objc_class", curr_arg->objc_class_name);
                    JSON_ADD_INT64(arg, "address", (uint64_t)curr_arg->address);
                    JSON_ADD_INT64(arg, "size", curr_arg->size);
                    json_object_array_add(args_array, arg);
                }
                
                json_object_object_add(root, "arguments", args_array);
            }
        }
    }
    
    const char *json_str = json_object_to_json_string(root);
    char *result = json_str ? strdup(json_str) : NULL;
    json_object_put(root);
    return result;
}
