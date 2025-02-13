//
//  color_utils.c
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//


#include "tracer_internal.h"
#include "color_utils.h"

uint8_t get_consistent_color(const char *str, uint8_t start, uint16_t range) {
    if (str == NULL) {
        return start;
    }
    uint32_t hash = fnv1a_hash(str);
    return start + (hash % range);
}

int write_color(char *buffer, uint8_t color) {
    return sprintf(buffer, "\x1b[38;5;%dm", color);
}
