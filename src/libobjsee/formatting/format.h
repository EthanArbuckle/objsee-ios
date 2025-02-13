//
//  format.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#ifndef TRACER_FORMAT_H
#define TRACER_FORMAT_H

#include "tracer_internal.h"

/**
 * @brief Build a JSON string representation of a trace event. This may include a formatted output string
 *
 * @param tracer The tracer instance
 * @param event The event to build a JSON string for
 * @return A JSON string representing the event
 */
const char *build_json_event_str(const tracer_t *tracer, const tracer_event_t *event);


/**
 * @brief Format a trace event according to the configured format type (indented, with colors, etc)
 *
 * @param event The event to format
 * @param format The format options to use for formatting
 * @return A formatted string representing the event
 */
char *build_formatted_event_str(const tracer_event_t *event, tracer_format_options_t format);


#endif // TRACER_FORMAT_H
