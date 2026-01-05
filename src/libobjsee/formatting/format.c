//
//  format.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <CoreFoundation/CoreFoundation.h>
#include <yyjson.h>
#include <dlfcn.h>
#include "color_utils.h"
#include "encoding_description.h"
#include "tracer_internal.h"

#define EVENT_FORMAT_BUF_SIZE 4096     // Size of buffer used for building formatted strings for trace events
#define BINDAT_FORMAT_BUF_SIZE 1024    // Size of the shared stack buffer each thread uses to build formatted strings for binary data
#define METHARGS_FORMAT_BUF_SIZE 2048  // Size of buffer used for combining selector names with argument descriptions

#if EVENT_FORMAT_BUF_SIZE < BINDAT_FORMAT_BUF_SIZE || EVENT_FORMAT_BUF_SIZE < METHARGS_FORMAT_BUF_SIZE
#error "EVENT_FORMAT_BUF_SIZE must be larger than BINDAT_FORMAT_BUF_SIZE and METHARGS_FORMAT_BUF_SIZE"
#endif

static inline kern_return_t fast_append(char **ptr, const char *end, const char *format, ...) {
    if (!ptr || !*ptr || !end || format == NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    
    ptrdiff_t remaining = end - *ptr;
    if (remaining < 0 || remaining > EVENT_FORMAT_BUF_SIZE) {
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
    static __thread char hex_buffer[BINDAT_FORMAT_BUF_SIZE];
    size_t display_size = size > 16 ? 16 : size;
    
    int written = snprintf(hex_buffer, BINDAT_FORMAT_BUF_SIZE, "<binary:%zu bytes: ", size);
    if (written < 0 || written >= BINDAT_FORMAT_BUF_SIZE) {
        return strdup("<format error>");
    }
    
    char *hex_ptr = hex_buffer + written;
    int remaining = BINDAT_FORMAT_BUF_SIZE - written;
    
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

const char *_do_format(char **inptr, const char *end, const char *class_name, char *assembled_method_name_buf, const tracer_event_t *event, tracer_format_options_t format) {
    char *ptr = *inptr;
    if (class_name && fast_append(&ptr, end, "%s[%s ", event->is_class_method ? "+" : "-", class_name) != KERN_SUCCESS) {
        return NULL;
    }
    
    if (strlcpy(assembled_method_name_buf, event->method_name, METHARGS_FORMAT_BUF_SIZE) >= METHARGS_FORMAT_BUF_SIZE) {
        return NULL;
    }
    
    size_t arg_index = 0;
    char *method_part_ctx = NULL;
    char *method_part = strtok_r(assembled_method_name_buf, ":", &method_part_ctx);
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
        
        const char *original_method_name_ptr = event->method_name + (method_part - assembled_method_name_buf);
        if (original_method_name_ptr < event->method_name + strlen(event->method_name) &&
            strchr(original_method_name_ptr, ':')) {
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
        method_part = strtok_r(NULL, ":", &method_part_ctx);
    }
    
    if (format.include_colors && event->class_name) {
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
    *inptr = ptr;
    return *inptr;
}

const char *build_formatted_event_str(const tracer_event_t *event, tracer_format_options_t format) {
    if (event == NULL || event->class_name == NULL || event->method_name == NULL) {
        return NULL;
    }
    
    char formatted_event_buf[EVENT_FORMAT_BUF_SIZE] = {0};
    char assembled_method_name_buf[METHARGS_FORMAT_BUF_SIZE] = {0};
    char *ptr = formatted_event_buf;
    const char *const end = formatted_event_buf + EVENT_FORMAT_BUF_SIZE;
    
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
    
    // goal is to have var class_name contain demangled name if applicable otherwise original event class_name
    const char *class_name = event->class_name;
    char *demangled_name_to_free = NULL;
    if (class_name && strncmp(class_name, "_Tt", 3) == 0) {
        // Demangle Swift class names
        const char *demangled_name = demangle_swift(class_name);
        if (demangled_name && demangled_name != event->class_name) {
            class_name = demangled_name;
            demangled_name_to_free = (char *)demangled_name;
        }
    }
    
    if (format.include_colors && class_name) {
        uint8_t class_color = get_consistent_color(class_name, COLOR_CLASS_START, COLOR_CLASS_RANGE);
        if (fast_write_color(&ptr, end, class_color) != KERN_SUCCESS) {
            if (demangled_name_to_free) {
                free(demangled_name_to_free);
            }
            return NULL;
        }
    }
    
    _do_format(&ptr, end, class_name, assembled_method_name_buf, event, format);
    
    if (demangled_name_to_free) {
        free(demangled_name_to_free);
    }
    
    return strdup(formatted_event_buf);
}

#define YYJSON_SAFE_ADD_STR(_doc, _obj, _key, _ptr) if (_ptr != NULL) yyjson_mut_obj_add_strcpy(_doc, _obj, _key, _ptr)
#define YYJSON_SAFE_ADD_INT(_doc, _obj, _key, _ptr) if (_ptr) yyjson_mut_obj_add_int(_doc, _obj, _key, _ptr)
#define YYJSON_SAFE_ADD_UINT(_doc, _obj, _key, _ptr) if (_ptr) yyjson_mut_obj_add_uint(_doc, _obj, _key, _ptr)
#define YYJSON_SAFE_ADD_BOOL(_doc, _obj, _key, _ptr) if (_ptr) yyjson_mut_obj_add_bool(_doc, _obj, _key, _ptr)

const char *build_json_event_str(const tracer_t *tracer, const tracer_event_t *event) {
    if (event == NULL || tracer == NULL) {
        return NULL;
    }
    
    if (event->class_name == NULL || event->method_name == NULL) {
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

    tracer_format_options_t format = tracer->config.format;
    if (format.include_formatted_trace) {
        const char *formatted = build_formatted_event_str(event, format);
        if (formatted != NULL) {
            yyjson_mut_obj_add_strcpy(doc, root, "formatted_output", formatted);
            free((void *)formatted);
        }
    }
    
    if (format.include_event_json) {
        yyjson_mut_obj_add_strcpy(doc, root, "class", event->class_name);
        yyjson_mut_obj_add_strcpy(doc, root, "method", event->method_name);
        yyjson_mut_obj_add_bool(doc, root, "is_class_method", event->is_class_method);
        yyjson_mut_obj_add_int(doc, root, "thread_id", event->thread_id);
        yyjson_mut_obj_add_int(doc, root, "depth", event->real_depth);
        YYJSON_SAFE_ADD_STR(doc, root, "signature", event->method_signature);

        if (format.args != TRACER_ARG_FORMAT_NONE) {
            if (event->arguments != NULL && event->argument_count > 0) {
                yyjson_mut_val *args_array = yyjson_mut_arr(doc);
                if (args_array == NULL) {
                    yyjson_mut_doc_free(doc);
                    tracer_set_error((tracer_t *)tracer, "Failed to create JSON array for arguments");
                    return NULL;
                }
                
                for (size_t i = 0; i < event->argument_count; i++) {
                    const tracer_argument_t *curr_arg = &event->arguments[i];
                    if (curr_arg->type_encoding == NULL) {
                        tracer_set_error((tracer_t *)tracer, "Argument type encoding is NULL");
                        continue;
                    }

                    yyjson_mut_val *arg = yyjson_mut_obj(doc);
                    if (arg == NULL){
                        yyjson_mut_doc_free(doc);
                        tracer_set_error((tracer_t *)tracer, "Failed to create JSON object for argument");
                        return NULL;
                    }

                    yyjson_mut_obj_add_strcpy(doc, arg, "type", get_name_of_type_from_type_encoding(curr_arg->type_encoding));
                    YYJSON_SAFE_ADD_STR(doc, arg, "class", curr_arg->objc_class_name);
                    YYJSON_SAFE_ADD_STR(doc, arg, "block_signature", curr_arg->block_signature);
                    YYJSON_SAFE_ADD_STR(doc, arg, "description", curr_arg->description);
                    YYJSON_SAFE_ADD_STR(doc, arg, "objc_class", curr_arg->objc_class_name);
                    yyjson_mut_obj_add_uint(doc, arg, "address", (uint64_t)curr_arg->address);
                    yyjson_mut_obj_add_uint(doc, arg, "size", curr_arg->size);
                    yyjson_mut_arr_append(args_array, arg);
                }

                yyjson_mut_obj_add_val(doc, root, "arguments", args_array);
            }
        }
    }

    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}
