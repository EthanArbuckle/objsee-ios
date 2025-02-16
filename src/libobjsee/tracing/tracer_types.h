//
//  tracer_types.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/5/24.
//

#ifndef TRACER_TYPES_H
#define TRACER_TYPES_H

#include <objc/runtime.h>
#include <stdbool.h>

#define TRACER_MAX_FILTERS 32


typedef enum {
    // Capturing arguments introduces instability and may cause crashes,
    // with increasing likelihood as the requested level of detail increases
    
    // Don't capture any arguments - safest option
    TRACER_ARG_FORMAT_NONE,
    
    // Capture only the address of the argument
    TRACER_ARG_FORMAT_BASIC,
    
    // Capture both the address and class name of the argument (if applicable)
    TRACER_ARG_FORMAT_CLASS,
    
    // Capture the full result of calling -description on the argument
    TRACER_ARG_FORMAT_DESCRIPTIVE,
    
    // Same as DESCRIPTIVE but with newlines and whitespace trimmed
    TRACER_ARG_FORMAT_DESCRIPTIVE_COMPACT,
} tracer_argument_format_t;

typedef struct {
    // Should the published event contain a fully formatted string representing the trace (colors, indents, args, etc)
    bool include_formatted_trace;
    // Should the published event contain all details about the trace, represented as a json string
    bool include_event_json;
/*
     {
         "class": "NSDateFormatter",          // Class name (string)
         "method": "new",                     // Method name (string)
         "is_class_method": true,             // Class vs instance method (bool)
         "thread_id": 25673,                  // Thread identifier (integer)
         "depth": 2,                          // Call stack depth (integer)
         "signature": "v24@0:8@16",           // Method signature (optional, string)
         "formatted_output": "+[NSDateFormatter new]\n"  // Formatted trace (string). May include ANSI color codes and other formatting
     }
*/

    // When true, the format of the published event will be a json string. This is implicitly enabled when both include_formatted_trace and include_event_json are true.
    // If this is false (implying only one of `include_formatted_trace` or `include_event_json` is true)
    bool output_as_json;
    
    // When true, the formatted string will include colors
    bool include_colors;
    // When true, formatted events will include a thread id
    bool include_thread_id;
    
    // Argument formatting options
    tracer_argument_format_t args;
    
    // When true, the formatted string will include indents for call depth
    bool include_indents;
    // Include a separator character between indents (ie. |)
    bool include_indent_separators;
    // When true, the spacing between separator characters will start high and gradually decrease with depth.
    // This is useful when printing traces with deep call stacks as it will prevent the output from becoming too wide.
    bool variable_separator_spacing;
    // Static spacing between separator characters (used when variable_separator_spacing is false)
    uint32_t static_separator_spacing;
    // The primary indent character (ie. .)
    const char *indent_char;
    // The character to use as a separator between indents (ie. |)
    const char *indent_separator_char;
    // hack
    bool include_newline_in_formatted_trace;
    
/*
    [thread id] ....{indent_char} |{indent_separator_char}

    [0x931c] ....|....|.-[NWConcrete_nw_address_endpoint type]
    [0x931c] ....|....|.-[NWConcrete_nw_address_endpoint isEqualToEndpoint:35.190.88.7]
    [0x931c] ....|....|....|.-[NWConcrete_nw_address_endpoint type]
    [0x931c] ....|....|....|...|.-[NWConcrete_nw_host_endpoint createDescription:NO]
    [0x931c] ....|....|....|...|.-[NWConcrete_nw_host_endpoint type]
    [0x931c] ....|....|.-[NWConcrete_nw_host_endpoint getHash]
    [0x931c] ....|....|.-[NWConcrete_nw_protocol_instance getProtocolStructure]
    [0x931c] ....|.-[ASConfiguration respondsToSelector:@selector(textureConfiguration)]
    [0x931c] -[ASConfigurationManager init]
*/
} tracer_format_options_t;

typedef enum {
    TRACER_TRANSPORT_SOCKET,
    TRACER_TRANSPORT_FILE,
    TRACER_TRANSPORT_STDOUT,
    TRACER_TRANSPORT_CUSTOM,
} tracer_transport_type_t;

typedef struct {
    const char *type_encoding;      // Never NULL
    const char *objc_class_name;    // NULL if not an object
    const char *block_signature;    // NULL if not a block
    const char *description;        // May be NULL
    Class objc_class;               // NULL if not an object
    void *address;                  // Never NULL
    size_t size;
} tracer_argument_t;

typedef struct {
    const char *host;
    uint32_t port;
    const char *file_path;
    void *custom_context;
} tracer_transport_config_t;

typedef struct tracer_event_t {
    const char *formatted_output;
    const char *class_name;
    const char *method_name;
    bool is_class_method;
    const char *image_path;
    uint16_t thread_id;
    uint32_t trace_depth;
    uint32_t real_depth;
    const char *method_signature;
    tracer_argument_t *arguments;
    size_t argument_count;
} tracer_event_t;

typedef struct tracer_filter {
    const char *class_pattern;
    const char *method_pattern;
    const char *image_pattern;
    bool exclude;
    bool (*custom_filter)(struct tracer_event_t *event, void *context);
    void *custom_filter_context;
} tracer_filter_t;

typedef void (tracer_event_handler_t)(const tracer_event_t *event, void *context);

typedef struct {
    tracer_filter_t filters[TRACER_MAX_FILTERS];
    int filter_count;
    
    tracer_format_options_t format;
    
    tracer_transport_type_t transport;
    tracer_transport_config_t transport_config;
    
    tracer_event_handler_t *event_handler;
    void *event_handler_context;
} tracer_config_t;

typedef enum {
    TRACER_SUCCESS = 0,
    TRACER_ERROR_INVALID_ARGUMENT = -1,
    TRACER_ERROR_INITIALIZATION = -2,
    TRACER_ERROR_MEMORY = -3,
    TRACER_ERROR_RUNTIME = -4,
    TRACER_ERROR_TIMEOUT = -5,
    TRACER_ERROR_ALREADY_INITIALIZED = -6,
} tracer_result_t;

#endif // TRACER_TYPES_H
