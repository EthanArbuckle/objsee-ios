//
//  encoding_description.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 1/4/25.
//

#include "encoding_description.h"
#include "encoding_size.h"

typedef struct {
    const char *input;
    size_t position;
    char *output;
    size_t output_size;
    size_t output_pos;
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
    if (!parser->input || parser->input[parser->position] == '\0') {
        return '\0';
    }
    return parser->input[parser->position];
}

static char consume_char(struct_parser_t *parser) {
    if (!parser->input || parser->input[parser->position] == '\0') {
        return '\0';
    }
    return parser->input[parser->position++];
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

static void parse_struct_members(struct_parser_t *parser) {
    bool first = true;
    while (peek_char(parser) && peek_char(parser) != '}') {
        if (!first) {
            append_to_output(parser, ", ");
        }
        
        if (peek_char(parser) == 'r') {
            consume_char(parser);
            append_to_output(parser, "const ");
        }
        
        if (peek_char(parser) == '{') {
            parse_type(parser);
        }
        else {
            char c = consume_char(parser);
            append_to_output(parser, get_name_of_type_from_type_encoding(&c));
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
    if (!c) {
        return;
    }
    
    while ((c = peek_char(parser)) == 'r') {
        consume_char(parser);
        append_to_output(parser, "const ");
    }
    
    c = peek_char(parser);
    if (c == '^') {
        consume_char(parser);
        parse_type(parser);
        append_to_output(parser, " *");
        return;
    }
    
    if (c == '{') {
        consume_char(parser);
        
        bool is_anonymous = (peek_char(parser) == '?');
        if (is_anonymous) {
            consume_char(parser);
            append_to_output(parser, "struct { ");
            
            if (peek_char(parser) == '=') {
                consume_char(parser);
                parse_struct_members(parser);
                append_to_output(parser, "}");
            }
        }
        else {
            char *name = parse_identifier(parser);
            if (name) {
                append_to_output(parser, "%s", name);
                free(name);
                if (peek_char(parser) == '=') {
                    consume_char(parser);
                    append_to_output(parser, " { ");
                    parse_struct_members(parser);
                    append_to_output(parser, "}");
                }
            }
        }
        
        if (peek_char(parser) == '}') {
            consume_char(parser);
        }
        
        return;
    }
    
    c = consume_char(parser);
    append_to_output(parser, get_name_of_type_from_type_encoding(&c));
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
