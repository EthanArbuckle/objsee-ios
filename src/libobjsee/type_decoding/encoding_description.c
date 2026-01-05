//
//  encoding_description.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 1/4/25.
//

#include "encoding_description.h"

typedef struct {
    const char *input;
    size_t position;
    char *output;
    size_t output_size;
    size_t output_pos;
    void *data;
    size_t data_offset;
    bool format_values;
} struct_parser_t;

static void parse_type(struct_parser_t *parser);

static void append_to_output(struct_parser_t *parser, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t remaining = parser->output_size - parser->output_pos;
    parser->output_pos += vsnprintf(parser->output + parser->output_pos, remaining, fmt, args);
    va_end(args);
}

static char peek_char(struct_parser_t *parser) {
    if (parser->input == NULL || parser->input[parser->position] == '\0') {
        return '\0';
    }
    return parser->input[parser->position];
}

static char consume_char(struct_parser_t *parser) {
    if (parser->input == NULL || parser->input[parser->position] == '\0') {
        return '\0';
    }
    return parser->input[parser->position++];
}

static size_t get_type_size(char c) {
#if __LP64__
    switch (c) {
        case 'c': case 'C': case 'B': return 1;
        case 's': case 'S': return 2;
        case 'i': case 'I': case 'f': return 4;
        case 'l': case 'L': return 4;
        case 'q': case 'Q': case 'd': return 8;
        case '*': case '@': case '#': case ':': case '^': return 8;
        default: return 0;
    }
#else
    switch (c) {
        case 'c': case 'C': case 'B': return 1;
        case 's': case 'S': return 2;
        case 'i': case 'I': case 'f': return 4;
        case 'l': case 'L': return 4;
        case 'q': case 'Q': case 'd': return 8;
        case '*': case '@': case '#': case ':': case '^': return 4;
        default: return 0;
    }
#endif
}

static size_t get_type_alignment(char c) {
#if __LP64__
    return get_type_size(c);
#else
    switch (c) {
        case 'q': case 'Q': case 'd': return 4;
        default: return get_type_size(c);
    }
#endif
}

static void align_offset(struct_parser_t *parser, size_t alignment) {
    if (alignment > 0) {
        parser->data_offset = (parser->data_offset + alignment - 1) & ~(alignment - 1);
    }
}

static void format_primitive_value(struct_parser_t *parser, char type_char) {
    if (parser->data == NULL || !parser->format_values) {
        append_to_output(parser, "%s", get_name_of_type_from_type_encoding(&type_char));
        return;
    }

    size_t alignment = get_type_alignment(type_char);
    align_offset(parser, alignment);

    void *ptr = (uint8_t *)parser->data + parser->data_offset;
    size_t size = get_type_size(type_char);

    switch (type_char) {
        case 'c': {
            int8_t val = *(int8_t *)ptr;
            if (val >= 32 && val < 127) {
                append_to_output(parser, "'%c'", val);
            }
            else {
                append_to_output(parser, "%d", val);
            }
            break;
        }
        case 'C': {
            uint8_t val = *(uint8_t *)ptr;
            append_to_output(parser, "%u", val);
            break;
        }
        case 'B': {
            uint8_t val = *(uint8_t *)ptr;
            append_to_output(parser, "%s", val ? "true" : "false");
            break;
        }
        case 's': {
            int16_t val = *(int16_t *)ptr;
            append_to_output(parser, "%d", val);
            break;
        }
        case 'S': {
            uint16_t val = *(uint16_t *)ptr;
            append_to_output(parser, "%u", val);
            break;
        }
        case 'i': {
            int32_t val = *(int32_t *)ptr;
            append_to_output(parser, "%d", val);
            break;
        }
        case 'I': {
            uint32_t val = *(uint32_t *)ptr;
            append_to_output(parser, "%u", val);
            break;
        }
        case 'l': {
            int32_t val = *(int32_t *)ptr;
            append_to_output(parser, "%d", val);
            break;
        }
        case 'L': {
            uint32_t val = *(uint32_t *)ptr;
            append_to_output(parser, "%u", val);
            break;
        }
        case 'q': {
            int64_t val = *(int64_t *)ptr;
            append_to_output(parser, "%lld", val);
            break;
        }
        case 'Q': {
            uint64_t val = *(uint64_t *)ptr;
            append_to_output(parser, "%llu", val);
            break;
        }
        case 'f': {
            float val = *(float *)ptr;
            union { float f; uint32_t u; } ca_max = { .u = 0x7f000000 };
            union { float f; uint32_t u; } ca_neg_max = { .u = 0xff000000 };
            float half_flt_max = __FLT_MAX__ / 2.0f;
            if (val != val) {
                append_to_output(parser, "NAN");
            } else if (val == __builtin_inff()) {
                append_to_output(parser, "INFINITY");
            } else if (val == -__builtin_inff()) {
                append_to_output(parser, "-INFINITY");
            } else if (val == ca_max.f || val == half_flt_max) {
                append_to_output(parser, "CGFLOAT_MAX");
            } else if (val == ca_neg_max.f || val == -half_flt_max) {
                append_to_output(parser, "-CGFLOAT_MAX");
            } else if (val == __FLT_MAX__) {
                append_to_output(parser, "FLT_MAX");
            } else if (val == -__FLT_MAX__) {
                append_to_output(parser, "-FLT_MAX");
            } else if (val == __FLT_MIN__) {
                append_to_output(parser, "FLT_MIN");
            } else if (val == -__FLT_MIN__) {
                append_to_output(parser, "-FLT_MIN");
            } else if (val == __FLT_EPSILON__) {
                append_to_output(parser, "FLT_EPSILON");
            } else if (val == (int64_t)val) {
                append_to_output(parser, "%.1f", val);
            } else {
                append_to_output(parser, "%g", val);
            }
            break;
        }
        case 'd': {
            double val = *(double *)ptr;
            union { double d; uint64_t u; } ca_max = { .u = 0x7fe0000000000000ULL };
            union { double d; uint64_t u; } ca_neg_max = { .u = 0xffe0000000000000ULL };
            double half_dbl_max = __DBL_MAX__ / 2.0;
            if (val != val) {
                append_to_output(parser, "NAN");
            } else if (val == __builtin_inf()) {
                append_to_output(parser, "INFINITY");
            } else if (val == -__builtin_inf()) {
                append_to_output(parser, "-INFINITY");
            } else if (val == ca_max.d || val == half_dbl_max) {
                append_to_output(parser, "CGFLOAT_MAX");
            } else if (val == ca_neg_max.d || val == -half_dbl_max) {
                append_to_output(parser, "-CGFLOAT_MAX");
            } else if (val == __DBL_MAX__) {
                append_to_output(parser, "CGFLOAT_MAX");
            } else if (val == -__DBL_MAX__) {
                append_to_output(parser, "-CGFLOAT_MAX");
            } else if (val == __DBL_MIN__) {
                append_to_output(parser, "CGFLOAT_MIN");
            } else if (val == -__DBL_MIN__) {
                append_to_output(parser, "-CGFLOAT_MIN");
            } else if (val == __DBL_EPSILON__) {
                append_to_output(parser, "DBL_EPSILON");
            } else if (val == (int64_t)val) {
                append_to_output(parser, "%.1f", val);
            } else {
                append_to_output(parser, "%g", val);
            }
            break;
        }
        case '*': {
#if __LP64__
            void *val = *(void **)ptr;
#else
            void *val = *(void **)ptr;
#endif
            if (val == NULL) {
                append_to_output(parser, "NULL");
            }
            else {
                char *str = (char *)val;
                bool is_printable = true;
                size_t len = 0;
                
                for (size_t i = 0; i < 64 && str[i] != '\0'; i++) {
                    if (str[i] < 32 || str[i] > 126) {
                        is_printable = false;
                        break;
                    }
                    len++;
                }
                
                if (is_printable && len > 0 && len < 64) {
                    append_to_output(parser, "\"%s\"", str);
                }
                else {
                    append_to_output(parser, "%p", val);
                }
            }
            break;
        }
        case '@': {
#if __LP64__
            void *val = *(void **)ptr;
#else
            void *val = *(void **)ptr;
#endif
            if (val == NULL) {
                append_to_output(parser, "nil");
            }
            else {
                append_to_output(parser, "%p", val);
            }
            break;
        }
        case '#': {
#if __LP64__
            void *val = *(void **)ptr;
#else
            void *val = *(void **)ptr;
#endif
            if (val == NULL) {
                append_to_output(parser, "Nil");
            }
            else {
                append_to_output(parser, "%p", val);
            }
            break;
        }
        case ':': {
#if __LP64__
            void *val = *(void **)ptr;
#else
            void *val = *(void **)ptr;
#endif
            if (val == NULL) {
                append_to_output(parser, "NULL");
            }
            else {
                append_to_output(parser, "%p", val);
            }
            break;
        }
        default: {
            append_to_output(parser, "?");
            break;
        }
    }

    parser->data_offset += size;
}

static char *parse_identifier(struct_parser_t *parser) {
    const char *start = parser->input + parser->position;
    size_t len = 0;
    
    while (isalnum(peek_char(parser)) || peek_char(parser) == '_') {
        consume_char(parser);
        len++;
    }
    
    if (len == 0) {
        return NULL;
    }
    
    char *identifier = malloc(len + 1);
    if (identifier == NULL) {
        return NULL;
    }
    
    strncpy(identifier, start, len);
    identifier[len] = '\0';
    return identifier;
}

static size_t calculate_struct_alignment(struct_parser_t *parser);

static size_t get_alignment_for_type_at_position(struct_parser_t *parser) {
    char c = peek_char(parser);
    while (c == 'r') {
        size_t saved = parser->position;
        consume_char(parser);
        size_t result = get_alignment_for_type_at_position(parser);
        parser->position = saved;
        return result;
    }

    if (c == '^') {
#if __LP64__
        return 8;
#else
        return 4;
#endif
    }

    if (c == '{') {
        return calculate_struct_alignment(parser);
    }

    return get_type_alignment(c);
}

static size_t calculate_struct_alignment(struct_parser_t *parser) {
    size_t saved_pos = parser->position;
    size_t max_align = 1;

    if (peek_char(parser) != '{') {
        return 1;
    }
    consume_char(parser);

    if (peek_char(parser) == '?') {
        consume_char(parser);
    }
    else {
        while (peek_char(parser) && peek_char(parser) != '=' && peek_char(parser) != '}') {
            consume_char(parser);
        }
    }

    if (peek_char(parser) == '=') {
        consume_char(parser);

        while (peek_char(parser) && peek_char(parser) != '}') {
            size_t align = get_alignment_for_type_at_position(parser);
            if (align > max_align) {
                max_align = align;
            }

            char c = peek_char(parser);
            if (c == 'r') {
                consume_char(parser);
                c = peek_char(parser);
            }
            
            if (c == '{') {
                int depth = 1;
                consume_char(parser);
                while (depth > 0 && peek_char(parser)) {
                    c = consume_char(parser);
                    if (c == '{') {
                        depth++;
                    }
                    else if (c == '}') {
                        depth--;
                    }
                }
            }
            else if (c == '^') {
                consume_char(parser);
                if (peek_char(parser) == '{') {
                    int depth = 1;
                    consume_char(parser);
                    while (depth > 0 && peek_char(parser)) {
                        c = consume_char(parser);
                        if (c == '{') {
                            depth++;
                        }
                        else if (c == '}') {
                            depth--;
                        }
                    }
                }
                else {
                    consume_char(parser);
                }
            }
            else {
                consume_char(parser);
            }
        }
    }

    parser->position = saved_pos;
    return max_align;
}

static void parse_struct_members(struct_parser_t *parser) {
    bool first = true;
    while (peek_char(parser) && peek_char(parser) != '}') {
        if (!first) {
            append_to_output(parser, ", ");
        }
        
        if (peek_char(parser) == 'r') {
            consume_char(parser);
            if (!parser->format_values) {
                append_to_output(parser, "const ");
            }
        }
        
        if (peek_char(parser) == '{') {
            parse_type(parser);
        }
        else if (peek_char(parser) == '^') {
            consume_char(parser);
            if (parser->format_values) {
                size_t alignment = get_type_alignment('^');
                align_offset(parser, alignment);
                void *ptr = (uint8_t *)parser->data + parser->data_offset;
#if __LP64__
                void *val = *(void **)ptr;
                parser->data_offset += 8;
#else
                void *val = *(void **)ptr;
                parser->data_offset += 4;
#endif
                if (val == NULL) {
                    append_to_output(parser, "NULL");
                }
                else {
                    append_to_output(parser, "%p", val);
                }
                char c = peek_char(parser);
                if (c == '{') {
                    int depth = 1;
                    consume_char(parser);
                    while (depth > 0 && peek_char(parser)) {
                        c = consume_char(parser);
                        if (c == '{') {
                            depth++;
                        }
                        else if (c == '}') {
                            depth--;
                        }
                    }
                }
                else if (c != '\0' && c != '}') {
                    consume_char(parser);
                }
            }
            else {
                parse_type(parser);
                append_to_output(parser, " *");
            }
        }
        else {
            char c = consume_char(parser);
            if (parser->format_values) {
                format_primitive_value(parser, c);
            }
            else {
                append_to_output(parser, "%s", get_name_of_type_from_type_encoding(&c));
            }
        }
        
        if (peek_char(parser) == '=') {
            consume_char(parser);
            parse_type(parser);
        }
        
        first = false;
    }
}

static void parse_type(struct_parser_t *parser) {
    char c = peek_char(parser);
    if (c == '\0') {
        return;
    }
    
    while ((c = peek_char(parser)) == 'r') {
        consume_char(parser);
        if (!parser->format_values) {
            append_to_output(parser, "const ");
        }
    }
    
    c = peek_char(parser);
    if (c == '^') {
        consume_char(parser);
        if (parser->format_values) {
            size_t alignment = get_type_alignment('^');
            align_offset(parser, alignment);
            void *ptr = (uint8_t *)parser->data + parser->data_offset;
#if __LP64__
            void *val = *(void **)ptr;
            parser->data_offset += 8;
#else
            void *val = *(void **)ptr;
            parser->data_offset += 4;
#endif
            if (val == NULL) {
                append_to_output(parser, "NULL");
            }
            else {
                append_to_output(parser, "%p", val);
            }
            c = peek_char(parser);
            if (c == '{') {
                int depth = 1;
                consume_char(parser);
                while (depth > 0 && peek_char(parser)) {
                    c = consume_char(parser);
                    if (c == '{') {
                        depth++;
                    }
                    else if (c == '}') {
                        depth--;
                    }
                }
            }
            else if (c != '\0') {
                consume_char(parser);
            }
        }
        else {
            parse_type(parser);
            append_to_output(parser, " *");
        }
        return;
    }
    
    if (c == '{') {
        consume_char(parser);
        
        size_t struct_alignment = calculate_struct_alignment(parser);
        align_offset(parser, struct_alignment);

        bool is_anonymous = (peek_char(parser) == '?');
        if (is_anonymous) {
            consume_char(parser);
            append_to_output(parser, "struct { ");
            
            if (peek_char(parser) == '=') {
                consume_char(parser);
                parse_struct_members(parser);
                append_to_output(parser, " }");
            }
        }
        else {
            char *name = parse_identifier(parser);
            if (name != NULL) {
                append_to_output(parser, "%s", name);
                free(name);
                if (peek_char(parser) == '=') {
                    consume_char(parser);
                    append_to_output(parser, " { ");
                    parse_struct_members(parser);
                    append_to_output(parser, " }");
                }
            }
        }
        
        if (peek_char(parser) == '}') {
            consume_char(parser);
        }
        
        align_offset(parser, struct_alignment);

        return;
    }
    
    c = consume_char(parser);
    if (parser->format_values) {
        format_primitive_value(parser, c);
    }
    else {
        append_to_output(parser, "%s", get_name_of_type_from_type_encoding(&c));
    }
}

char *get_struct_description_from_type_encoding(const char *encoding) {
    if (encoding == NULL || encoding[0] == '\0') {
        return strdup("invalid_encoding");
    }
    
    struct_parser_t parser = {
        .input = encoding,
        .position = 0,
        .output = malloc(1024),
        .output_size = 1024,
        .output_pos = 0,
        .data = NULL,
        .data_offset = 0,
        .format_values = false,
    };
    
    if (parser.output == NULL) {
        return NULL;
    }
    
    parse_type(&parser);
    parser.output[parser.output_pos] = '\0';
    
    char *output = strdup(parser.output);
    free(parser.output);
    return output;
}

char *get_struct_description_with_values(const char *encoding, void *data) {
    if (encoding == NULL || encoding[0] == '\0') {
        return strdup("invalid_encoding");
    }

    if (data == NULL) {
        return get_struct_description_from_type_encoding(encoding);
    }

    struct_parser_t parser = {
        .input = encoding,
        .position = 0,
        .output = malloc(4096),
        .output_size = 4096,
        .output_pos = 0,
        .data = data,
        .data_offset = 0,
        .format_values = true,
    };

    if (parser.output == NULL) {
        return NULL;
    }

    parse_type(&parser);
    parser.output[parser.output_pos] = '\0';

    char *output = strdup(parser.output);
    free(parser.output);
    return output;
}

const char *get_name_of_type_from_type_encoding(const char *type_encoding) {
    if (type_encoding == NULL) {
        return "NULL";
    }
    
    switch (type_encoding[0]) {
        case 'c': return "char";
        case 'i': return "int";
        case 's': return "short";
        case 'l': return "long";
        case 'q': return "long long";
        case 'C': return "unsigned char";
        case 'I': return "unsigned int";
        case 'S': return "unsigned short";
        case 'L': return "unsigned long";
        case 'Q': return "unsigned long long";
        case 'f': return "float";
        case 'd': return "double";
        case 'B': return "bool";
        case 'v': return "void";
        case '*': return "char *";
        case '@': return "id";
        case '#': return "Class";
        case ':': return "SEL";
        case '^': return "pointer";
        default: return "unknown_type";
    }
}
