//
//  blocks.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/10/24.
//

#include "blocks.h"
#include "tracer_internal.h"

#define MAX_TYPE_LEN 1024
#define IS_VALID_ADDR(addr) ((addr) && ((addr) & 0x7) == 0 && (addr) >= 0x100000000 && (addr) <= 0x2000000000)
#define MAX_PARAMS 32

static size_t append_to_block(char *dest, size_t dest_size, size_t current_pos, const char *src) {
    if (dest == NULL || src == NULL || current_pos >= dest_size) {
        return current_pos;
    }
    
    size_t remaining = dest_size - current_pos;
    size_t written = snprintf(dest + current_pos, remaining, "%s", src);
    if (written >= remaining) {
        return dest_size;
    }
    
    return current_pos + written;
}

static size_t decode_block_type(char *output, size_t output_size, size_t current_pos, const char **cursor, int nesting) {
    if (output == NULL || cursor == NULL || *cursor == NULL || current_pos >= output_size || nesting > 32) {
        return current_pos;
    }
    
    while (**cursor != '\0' && strchr("rnNoORV", **cursor) != NULL) {
        (*cursor)++;
    }
    
    if (**cursor == '@') {
        (*cursor)++;
        if (**cursor == '?') {
            (*cursor)++;
            current_pos = append_to_block(output, output_size, current_pos, "^");
        }
        else if (**cursor == '"') {
            (*cursor)++;
            const char *end = strchr(*cursor, '"');
            if (end != NULL) {
                size_t len = (size_t)(end - *cursor);
                if (len < (output_size - current_pos - 1)) {
                    strncpy(output + current_pos, *cursor, len);
                    current_pos += len;
                    output[current_pos] = '\0';
                }
                *cursor = end + 1;
            }
            else {
                current_pos = append_to_block(output, output_size, current_pos, "id");
            }
        }
        else {
            current_pos = append_to_block(output, output_size, current_pos, "id");
        }
        return current_pos;
    }
    
    switch (**cursor) {
        case '@':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "id");
        case 'v':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "void");
        case 'i':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "int");
        case 'f':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "float");
        case 'l':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "long");
        case 'q':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "long long");
        case 'B':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "BOOL");
        case '*':
            (*cursor)++;
            return append_to_block(output, output_size, current_pos, "char *");
        case '^':
            (*cursor)++;
            current_pos = decode_block_type(output, output_size, current_pos, cursor, nesting + 1);
            if (output[current_pos - 1] != '*') {
                current_pos = append_to_block(output, output_size, current_pos, " ");
            }
            return append_to_block(output, output_size, current_pos, "*");
        default:
            if (isdigit(**cursor) != 0) {
                while (**cursor != '\0' && isdigit(**cursor) != 0) {
                    (*cursor)++;
                }
                return current_pos;
            }
            (*cursor)++;
            char encoding_unknown[8];
            snprintf(encoding_unknown, sizeof(encoding_unknown), "%c", **cursor);
            return append_to_block(output, output_size, current_pos, encoding_unknown);
    }
}

kern_return_t get_block_description(id block, char **out_description) {
    if (block == NULL || out_description == NULL || IS_VALID_ADDR((uintptr_t)block) == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    
    struct BlockLiteral *literal = (struct BlockLiteral *)block;
    if (IS_VALID_ADDR((uintptr_t)literal->descriptor) == 0) {
        return KERN_INVALID_ADDRESS;
    }
    
    const char *signature = _Block_signature(block);
    if (signature == NULL) {
        return KERN_FAILURE;
    }
    
    char *result = calloc(MAX_TYPE_LEN, 1);
    if (result == NULL) {
        return KERN_RESOURCE_SHORTAGE;
    }
    
    size_t pos = 0;
    const char *cursor = signature;
    
    while (*cursor != '\0' && strchr("rnNoORV", *cursor) != NULL) {
        cursor++;
    }
    
    pos = append_to_block(result, MAX_TYPE_LEN, pos, "(");
    pos = decode_block_type(result, MAX_TYPE_LEN, pos, &cursor, 0);
    pos = append_to_block(result, MAX_TYPE_LEN, pos, " (^)");
    
    while (*cursor != '\0' && isdigit(*cursor) != 0) {
        cursor++;
    }
    
    if (*cursor == '@' && *(cursor + 1) == '?') {
        cursor += 2;
        while (*cursor != '\0' && isdigit(*cursor) != 0) {
            cursor++;
        }
    }
    
    char param_encodings[MAX_PARAMS][MAX_TYPE_LEN];
    memset(param_encodings, 0, sizeof(param_encodings));
    int param_count = 0;
    
    while (*cursor != '\0' && param_count < MAX_PARAMS) {
        size_t local_pos = 0;
        local_pos = decode_block_type(param_encodings[param_count], MAX_TYPE_LEN, local_pos, &cursor, 0);
        
        while (*cursor != '\0' && isdigit(*cursor) != 0) {
            cursor++;
        }
        
        if (local_pos > 0) {
            param_count++;
        }
    }
    
    if (param_count == 0) {
        pos = append_to_block(result, MAX_TYPE_LEN, pos, "(void)");
        pos = append_to_block(result, MAX_TYPE_LEN, pos, ")");
        *out_description = result;
        return KERN_SUCCESS;
    }
    
    int i = 0;
    while (i < param_count) {
        const char *t = param_encodings[i];
        if (strcmp(t, "^") == 0) {
            pos = append_to_block(result, MAX_TYPE_LEN, pos, "(^)");
            i++;
        }
        else {
            char group[MAX_TYPE_LEN];
            size_t gpos = 0;
            group[0] = '\0';
            gpos = append_to_block(group, MAX_TYPE_LEN, gpos, "(");
            gpos = append_to_block(group, MAX_TYPE_LEN, gpos, t);
            i++;
            
            while (i < param_count && strcmp(param_encodings[i], "^") != 0) {
                gpos = append_to_block(group, MAX_TYPE_LEN, gpos, ", ");
                gpos = append_to_block(group, MAX_TYPE_LEN, gpos, param_encodings[i]);
                i++;
            }
            gpos = append_to_block(group, MAX_TYPE_LEN, gpos, ")");
            pos = append_to_block(result, MAX_TYPE_LEN, pos, group);
        }
    }
    
    pos = append_to_block(result, MAX_TYPE_LEN, pos, ")");
    
    *out_description = result;
    return KERN_SUCCESS;
}
