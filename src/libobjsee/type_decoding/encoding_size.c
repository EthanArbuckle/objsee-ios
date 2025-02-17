//
//  encoding_size.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 1/11/25.
//

#include <CoreFoundation/CoreFoundation.h>
#include "encoding_size.h"

static const char *skip_objc_qualifiers(const char *str) {
    while (*str == 'r' || *str == 'n' || *str == 'o' || *str == 'N' || *str == 'O' || *str == 'R' || *str == 'V') {
        str++;
    }
    return str;
}

static size_t parse_type_and_advance(const char **cursorPtr, size_t *alignmentOut);

static size_t parse_struct_fields(const char **cursorPtr, char openDelim) {
    char closeDelim = (openDelim == '{') ? '}' : ')';
    int braceDepth = 1;
    const char *start = *cursorPtr;
    const char *p = start;
    
    while (*p != '\0' && braceDepth > 0) {
        if (*p == openDelim) {
            braceDepth++;
        } else if (*p == closeDelim) {
            braceDepth--;
        }
        p++;
    }
    
    if (braceDepth != 0) {
        *cursorPtr = p;
        return 0;
    }
    
    const char *structEnd = p;
    const char *equalSign = NULL;
    for (const char *scan = start; scan < (structEnd - 1); scan++) {
        if (*scan == '=') {
            equalSign = scan;
            break;
        }
    }
    
    if (equalSign == NULL || equalSign >= structEnd) {
        *cursorPtr = structEnd;
        return 0;
    }
    
    const char *fieldPos = equalSign + 1;
    size_t offset = 0;
    size_t maxAlign = 1;
    
    while (fieldPos < (structEnd - 1)) {
        while (*fieldPos == ' ' || *fieldPos == ',' || *fieldPos == 'r' || *fieldPos == 'n' || *fieldPos == 'o' || *fieldPos == 'N' || *fieldPos == 'O' || *fieldPos == 'R' || *fieldPos == 'V') {
            fieldPos++;
        }
        
        if (*fieldPos == closeDelim || fieldPos >= (structEnd - 1)) {
            break;
        }
        
        const char *tempPtr = fieldPos;
        size_t fieldAlign = 1;
        size_t fieldSize = parse_type_and_advance(&tempPtr, &fieldAlign);
        
        if (fieldSize == 0) {
            *cursorPtr = structEnd;
            return 0;
        }
        
        if (fieldAlign > maxAlign) {
            maxAlign = fieldAlign;
        }
        
        offset = (offset + fieldAlign - 1) & ~(fieldAlign - 1);
        offset += fieldSize;
        fieldPos = tempPtr;
    }
    
    offset = (offset + maxAlign - 1) & ~(maxAlign - 1);
    *cursorPtr = structEnd;
    return offset;
}

static size_t parse_type_and_advance(const char **cursorPtr, size_t *alignmentOut) {
    const char *c = skip_objc_qualifiers(*cursorPtr);
    
    if (*c == '\0') {
        *cursorPtr = c;
        if (alignmentOut != NULL) {
            *alignmentOut = 1;
        }
        return 0;
    }
    
    size_t size = 0;
    size_t alignment = 1;
    
    switch (*c) {
        case 'c': case 'C':
            size = sizeof(char);
            alignment = _Alignof(char);
            c++;
            break;
            
        case 'i': case 'I':
            size = sizeof(int);
            alignment = _Alignof(int);
            c++;
            break;
            
        case 's': case 'S':
            size = sizeof(short);
            alignment = _Alignof(short);
            c++;
            break;
            
        case 'l': case 'L':
            size = sizeof(long);
            alignment = _Alignof(long);
            c++;
            break;
            
        case 'q': case 'Q':
            size = sizeof(long long);
            alignment = _Alignof(long long);
            c++;
            break;
            
        case 'f':
            size = sizeof(float);
            alignment = _Alignof(float);
            c++;
            break;
            
        case 'd':
            size = sizeof(double);
            alignment = _Alignof(double);
            c++;
            break;
            
        case 'B':
            size = sizeof(bool);
            alignment = _Alignof(bool);
            c++;
            break;
            
        case 'v':
            size = 0;
            alignment = 1;
            c++;
            break;
            
        case '*': case '@': case '#': case ':': {
            size = sizeof(void *);
            alignment = _Alignof(void *);
            c++;
            while (*c != '\0' && *c != '{' && *c != '(' && *c != '[' && *c != '^' && *c != '}' && *c != ')' && !isalpha((unsigned char)*c)) {
                c++;
            }
            break;
        }
            
        case '^':
            size = sizeof(void *);
            alignment = _Alignof(void *);
            c++;
            (void)parse_type_and_advance(&c, NULL);
            break;
            
        case '{': case '(': {
            char openDelim = *c;
            c++;
            *cursorPtr = c;
            size_t structSize = parse_struct_fields(cursorPtr, openDelim);
            if (structSize == 0) {
                return 0;
            }
            c = *cursorPtr;
            size = structSize;
            alignment = 1;
            if (size >= 8) {
                alignment = 8;
            } else if (size >= 4) {
                alignment = 4;
            } else if (size >= 2) {
                alignment = 2;
            }
            break;
        }
            
        default:
            *cursorPtr = c;
            if (alignmentOut != NULL) {
                *alignmentOut = 1;
            }
            return 0;
    }
    
    *cursorPtr = c;
    if (alignmentOut != NULL) {
        *alignmentOut = alignment;
    }
    return size;
}

size_t get_size_of_type_from_type_encoding(const char *type_encoding) {
    if (type_encoding == NULL || *type_encoding == '\0') {
        return 0;
    }
    
    const char *cursor = type_encoding;
    return parse_type_and_advance(&cursor, NULL);
}

kern_return_t get_offsets_of_args_using_type_encoding(const char *type_encoding, size_t *offsets, size_t arg_count) {
    if (type_encoding == NULL || offsets == NULL) {
        return KERN_FAILURE;
    }
    
    const char *cursor = type_encoding;
    size_t total_size = 0;
    
    while (*cursor != '\0' && !isdigit((unsigned char)*cursor)) {
        cursor++;
    }
    
    if (*cursor == '\0') {
        return KERN_FAILURE;
    }
    
    total_size = strtol(cursor, (char **)&cursor, 10);
    if (total_size == 0) {
        return KERN_FAILURE;
    }
    
    size_t current_offset = 0;
    size_t arg_index = 0;
    
    while (*cursor != '\0' && arg_index < arg_count) {
        while (*cursor != '\0' && !isdigit((unsigned char)*cursor)) {
            cursor++;
        }
        
        if (*cursor == '\0') {
            break;
        }
        
        current_offset = strtol(cursor, (char **)&cursor, 10);
        if (arg_index < arg_count) {
            offsets[arg_index++] = current_offset;
        }
        
        while (*cursor != '\0' && isdigit((unsigned char)*cursor)) {
            cursor++;
        }
    }
    
    if (arg_index != arg_count) {
        return KERN_FAILURE;
    }
    
    return KERN_SUCCESS;
}
