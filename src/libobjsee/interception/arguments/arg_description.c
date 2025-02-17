//
//  arg_description.c
//  objsee
//
//  Created by Ethan Arbuckle on 2/7/25.
//


#include <objc/runtime.h>
#include <mach/mach.h>
#include "objc_arg_description.h"
#include "encoding_description.h"
#include "tracer_internal.h"
#include "blocks.h"

static kern_return_t _description_for_id(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_selector(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_class(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_float(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_pointer(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_struct(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_bool(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_long_long(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_unsigned_short(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_unsigned_long_long(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_char_ptr(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_unsigned_char(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);
static kern_return_t _description_for_char(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);


kern_return_t description_for_argument(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    switch (arg->type_encoding[0]) {
        case '@': {
            return _description_for_id(arg, fmt, out_buf, buf_size);
        }
            
        case ':': {
            return _description_for_selector(arg, fmt, out_buf, buf_size);
        }
            
        case '#': {
            return _description_for_class(arg, fmt, out_buf, buf_size);
        }
            
        case 'f':
        case 'd': {
            return _description_for_float(arg, fmt, out_buf, buf_size);
        }
            
        case '^': {
            return _description_for_pointer(arg, fmt, out_buf, buf_size);
        }
            
        case '{': {
            return _description_for_struct(arg, fmt, out_buf, buf_size);
        }
            
        case 'B': {
            return _description_for_bool(arg, fmt, out_buf, buf_size);
        }
            
        case 'q': {
            return _description_for_long_long(arg, fmt, out_buf, buf_size);
        }
            
        case 'S': {
            return _description_for_unsigned_short(arg, fmt, out_buf, buf_size);
        }
            
        case 'Q': {
            return _description_for_unsigned_long_long(arg, fmt, out_buf, buf_size);
        }
            
        case '*': {
            return _description_for_char_ptr(arg, fmt, out_buf, buf_size);
        }
            
        case 'C': {
            return _description_for_unsigned_char(arg, fmt, out_buf, buf_size);
        }
            
        case 'c': {
            return _description_for_char(arg, fmt, out_buf, buf_size);
        }
            
        case 'r': {
            if (arg->type_encoding[1] != '\0') {
                return description_for_argument(&(tracer_argument_t){.type_encoding = arg->type_encoding + 1, .address = arg->address}, fmt, out_buf, buf_size);
            }
            return KERN_INVALID_ARGUMENT;
        }
            
        default: {
            return KERN_INVALID_ARGUMENT;
        }
    }
}

static kern_return_t _description_for_id(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (arg->address == NULL || arg->objc_class_name == NULL) {
        if (strlcpy(out_buf, "nil", buf_size) >= buf_size) {
            return KERN_NO_SPACE;
        }
        return KERN_SUCCESS;
    }
    
    switch (fmt) {
        case TRACER_ARG_FORMAT_NONE: {
            out_buf[0] = '\0';
            return KERN_SUCCESS;
        }
            
        case TRACER_ARG_FORMAT_BASIC: {
            // <0xaddress>
            if (snprintf(out_buf, buf_size, "<%s: %p>", arg->type_encoding ? arg->type_encoding : "id", arg->address) >= buf_size) {
                return KERN_NO_SPACE;
            }
            break;
        }
            
        case TRACER_ARG_FORMAT_CLASS: {
            // <ClassName: 0xaddress>
            if (snprintf(out_buf, buf_size, "<%s: %p>", arg->objc_class_name ? arg->objc_class_name : "id", arg->address) >= buf_size) {
                return KERN_NO_SPACE;
            }
            break;
        }
            
        case TRACER_ARG_FORMAT_DESCRIPTIVE: {
            
            // Handle blocks
            if (strcmp(arg->type_encoding, "@?") == 0) {
                
                char *decoded_block_signature = NULL;
                if (get_block_description(*(id *)arg->address, &decoded_block_signature) != KERN_SUCCESS) {
                    if (snprintf(out_buf, buf_size, "<Block: %p>", arg->address) >= buf_size) {
                        return KERN_NO_SPACE;
                    }
                }
                else {
                    if (snprintf(out_buf, buf_size, "%s", decoded_block_signature) >= buf_size) {
                        return KERN_NO_SPACE;
                    }
                    
                    free(decoded_block_signature);
                }
                break;
            }
            
            // Get the result of -description
            __unsafe_unretained id objc_object = *(id *)arg->address;
            const char *obj_description = lookup_description_for_address(objc_object, arg->objc_class);
            
            if (obj_description && fmt == TRACER_ARG_FORMAT_DESCRIPTIVE_COMPACT) {
                if (strchr(obj_description, '\n') != NULL) {
                    char *sanitized_str = strdup(obj_description);
                    if (sanitized_str == NULL) {
                        return KERN_NO_SPACE;
                    }
                    
                    for (size_t i = 0; i < strlen(sanitized_str); i++) {
                        if (sanitized_str[i] == '\n') {
                            sanitized_str[i] = ' ';
                        }
                    }
                    
                    free((void *)obj_description);
                    obj_description = sanitized_str;
                }
                
                char *last_char = NULL;
                char *current_char = (char *)obj_description;
                while (*current_char != '\0') {
                    if (*current_char == ' ' && last_char != NULL && *last_char == ' ') {
                        memmove(current_char, current_char + 1, strlen(current_char));
                    }
                    else {
                        last_char = current_char;
                        current_char++;
                    }
                }
            }

            if (obj_description) {
                if (strlcpy(out_buf, obj_description, buf_size) >= buf_size) {
                    return KERN_NO_SPACE;
                }
            }
            else {
                // -description did not work, fallback to more basic descriptions.
                // <Class name, or type encoding, else just id: 0xaddress>
                if (arg->objc_class_name) {
                    // <ClassName: 0xaddress>
                    if (snprintf(out_buf, buf_size, "<%s: %p>", arg->objc_class_name, arg->address) >= buf_size) {
                        return KERN_NO_SPACE;
                    }
                }
                else if (arg->type_encoding) {
                    // <%encoding: 0xaddress>
                    if (snprintf(out_buf, buf_size, "<%s: %p>", arg->type_encoding, arg->address) >= buf_size) {
                        return KERN_NO_SPACE;
                    }
                }
                else {
                    // <id: 0xaddress>
                    if (snprintf(out_buf, buf_size, "<id: %p>", arg->address) >= buf_size) {
                        return KERN_NO_SPACE;
                    }
                }
            }
            break;
        }
            
        default: {
            return KERN_INVALID_ARGUMENT;
        }
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_selector(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
        
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (arg->address == NULL) {
        return KERN_INVALID_ADDRESS;
    }
    
    SEL sel = NULL;
    mach_vm_size_t sel_size = sizeof(SEL);
    if (mach_vm_read_overwrite(mach_task_self_, (mach_vm_address_t)arg->address, sizeof(SEL), (mach_vm_address_t)&sel, &sel_size) != KERN_SUCCESS || sel_size != sizeof(SEL)) {
        return KERN_INVALID_ADDRESS;
    }
    
    if (sel == NULL) {
        if (strlcpy(out_buf, "@selector(nil)", buf_size) >= buf_size) {
            return KERN_NO_SPACE;
        }
        return KERN_SUCCESS;
    }
    
    char sel_name_buf[1024];
    memset(sel_name_buf, 0, sizeof(sel_name_buf));
    mach_vm_size_t sizeof_sel_name_buf = sizeof(sel_name_buf);
    if (mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)sel_getName(sel), 1024, (mach_vm_address_t)sel_name_buf, &sizeof_sel_name_buf) != KERN_SUCCESS) {
        return KERN_INVALID_ADDRESS;
    }
    if (snprintf(out_buf, buf_size, "@selector(%s)", sel_name_buf) >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_class(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    Class cls = *(Class *)arg->address;
    if (cls == NULL) {
        if (strlcpy(out_buf, "nil", buf_size) >= buf_size) {
            return KERN_NO_SPACE;
        }
        return KERN_SUCCESS;
    }
    
    if (fmt == TRACER_ARG_FORMAT_BASIC) {
        if (snprintf(out_buf, buf_size, "%p", cls) >= buf_size) {
            return KERN_NO_SPACE;
        }
    }
    else if (fmt == TRACER_ARG_FORMAT_CLASS || fmt == TRACER_ARG_FORMAT_DESCRIPTIVE) {
        if (arg->objc_class_name) {
            if (snprintf(out_buf, buf_size, "%s", arg->objc_class_name) >= buf_size) {
                return KERN_NO_SPACE;
            }
        }
        else {
            if (snprintf(out_buf, buf_size, "%s", class_getName(cls)) >= buf_size) {
                return KERN_NO_SPACE;
            }
        }
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_float(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (snprintf(out_buf, buf_size, "%.2f", *(float *)arg->address) >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_pointer(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (snprintf(out_buf, buf_size, "%p", *(void **)arg->address) >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_struct(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    else if (fmt == TRACER_ARG_FORMAT_BASIC) {
        if (snprintf(out_buf, buf_size, "{%p}", arg->address) >= buf_size) {
            return KERN_NO_SPACE;
        }
    }
    else if (fmt == TRACER_ARG_FORMAT_CLASS) {
        if (snprintf(out_buf, buf_size, "{%s}", arg->type_encoding) >= buf_size) {
            return KERN_NO_SPACE;
        }
    }
    else if (fmt == TRACER_ARG_FORMAT_DESCRIPTIVE) {
        if (arg->type_encoding == NULL) {
            // Fallback to a basic description if the type encoding is missing
            if (snprintf(out_buf, buf_size, "{%p: %s}", arg->address, arg->type_encoding) >= buf_size) {
                return KERN_NO_SPACE;
            }
        }
        else {
            const char *decoded_struct = get_struct_description_from_type_encoding(arg->type_encoding);
            if (decoded_struct == NULL) {
                if (snprintf(out_buf, buf_size, "{%p: %s}", arg->address, arg->type_encoding) >= buf_size) {
                    return KERN_NO_SPACE;
                }
            }
            else {
                size_t written = snprintf(out_buf, buf_size, "%s", decoded_struct);
                free((void *)decoded_struct);
                if (written >= buf_size) {
                    return KERN_NO_SPACE;
                }
            }
        }
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_bool(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (snprintf(out_buf, buf_size, "%s", *(BOOL *)arg->address ? "true" : "false") >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_long_long(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    const long long *value_ptr = (const long long *)arg->address;
    int written = snprintf(out_buf, buf_size, "%lld", *value_ptr);
    if (written < 0 || (size_t)written >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_unsigned_short(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (snprintf(out_buf, buf_size, "%hu", *(unsigned short *)arg->address) >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_unsigned_long_long(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (snprintf(out_buf, buf_size, "%llu", *(unsigned long long *)arg->address) >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_char_ptr(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    const char *str = *(const char **)arg->address;
    if (str == NULL) {
        if (strlcpy(out_buf, "(null)", buf_size) >= buf_size) {
            return KERN_NO_SPACE;
        }
        return KERN_SUCCESS;
    }
    
    if (fmt == TRACER_ARG_FORMAT_BASIC) {
        if (snprintf(out_buf, buf_size, "%p", str) >= buf_size) {
            return KERN_NO_SPACE;
        }
    }
    else {
        if (strlcpy(out_buf, str, buf_size) >= buf_size) {
            return KERN_NO_SPACE;
        }
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_unsigned_char(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (snprintf(out_buf, buf_size, "%u", *(unsigned char *)arg->address) >= buf_size) {
        return KERN_NO_SPACE;
    }
    
    return KERN_SUCCESS;
}

static kern_return_t _description_for_char(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size) {
    if (arg == NULL || out_buf == NULL || buf_size == 0 || arg->address == NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    
    if (fmt == TRACER_ARG_FORMAT_NONE) {
        out_buf[0] = '\0';
        return KERN_SUCCESS;
    }
    
    if (buf_size < 5) {
        return KERN_NO_SPACE;
    }
    
    char value = 0;
    memcpy(&value, arg->address, sizeof(char));
    
    // If the value is printable, print it as a character
    if (isprint(value)) {
        if (snprintf(out_buf, buf_size, "'%c'", value) >= buf_size) {
            return KERN_NO_SPACE;
        }
    }
    else {
        // Otherwise, print it as a hex value
        if (snprintf(out_buf, buf_size, "0x%02x", value) >= buf_size) {
            return KERN_NO_SPACE;
        }
    }
    
    return KERN_SUCCESS;
}
