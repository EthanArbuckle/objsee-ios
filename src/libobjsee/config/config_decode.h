//
//  config_decode.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#ifndef CONFIG_DECODE_H
#define CONFIG_DECODE_H

#include "tracer_internal.h"

#define CONFIG_ENV_VAR "OBJSEE_CONFIG"

/**
 * @brief Decode a tracer configuration from an encoded string
 *
 * @param config_str Base64 encoded json string representing the configuration
 * @param config The output configuration
 * @return tracer_result_t TRACER_SUCCESS on success, or an error code on failure
 */
tracer_result_t decode_tracer_config(const char *config_str, tracer_config_t *config);


/**
 * @brief Build a human readable description from a tracer configuration
 * @param config The configuration to describe
 * @return const char* A human readable description of the configuration. This string must be freed by the caller.
 */
const char *copy_human_readable_config(tracer_config_t config);

#endif // CONFIG_DECODE_H
