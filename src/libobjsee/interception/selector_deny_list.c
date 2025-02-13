//
//  runtime_utils.m
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include <objc/runtime.h>
#include "tracer_internal.h"


static const uint32_t selector_skip_list[] = {
    0x033DB2A4,  // isKindOfClass:
    0x0390DC47,  // zone
    0x1036AE7E,  // release
    0x378D6F42,  // allocWithZone:
    0x5B52EC16,  // _tryRetain
    0x767A90E3,  // retainCount
    0x88BCC57C,  // retain
    0xAB3E0BFF,  // class
    0xB271BD4D,  // _xref_dispose
    0xB8F35F41,  // .cxx_destruct
    0xBAB1BB16,  // alloc
    0xC8C9FA1F,  // autorelease
    0xD9929EB3,  // dealloc
    0xEFF52FB5,  // _isDeallocating
};
static const size_t selector_skip_list_len = 14;

__attribute__((aligned(16), hot, always_inline))
static inline bool hash_in_denylist(uint32_t hash) {
    if (selector_skip_list_len == 0) {
        return false;
    }
    size_t left = 0;
    size_t right = selector_skip_list_len - 1;
    
    while (left <= right) {
        size_t mid = left + ((right - left) >> 1);
        uint32_t mid_hash = selector_skip_list[mid];
        if (mid_hash == hash) {
            return true;
        }
        
        if (mid_hash < hash) {
            left = mid + 1;
        }
        else {
            if (mid == 0) {
                return false;
            }
            right = mid - 1;
        }
    }

    return false;
}

__attribute__((aligned(16), hot, always_inline))
bool selector_is_denylisted(SEL selector) {
    if (selector == NULL) {
        return false;
    }
    
    const char *selector_name = sel_getName(selector);
    if (selector_name == NULL || selector_name[0] == '\0') {
        return false;
    }
    
    char first_char = selector_name[0];
    if (first_char == 's') {
        return false;
    }
    else if (first_char == '.') {
        return true;
    }
    
    uint32_t hash = fnv1a_hash(selector_name);
    return hash_in_denylist(hash);
}
