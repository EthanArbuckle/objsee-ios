//
//  color_utils.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//


#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

#include <CoreFoundation/CoreFoundation.h>

#define COLOR_THREAD_START 31
#define COLOR_THREAD_END 40
#define COLOR_DEPTH_START 244
#define COLOR_DEPTH_END 255
#define COLOR_CLASS_START 25
#define COLOR_CLASS_RANGE 108
#define COLOR_METHOD_START 39
#define COLOR_METHOD_RANGE 150

#define COLOR_RESET "\x1b[0m"

uint8_t get_consistent_color(const char *str, uint8_t start, uint16_t range);
int write_color(char *buffer, uint8_t color);

#endif // COLOR_UTILS_H
