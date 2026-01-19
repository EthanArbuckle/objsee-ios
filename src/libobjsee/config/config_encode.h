//
//  config_encode.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/1/24.
//

#ifndef CONFIG_ENCODE_H
#define CONFIG_ENCODE_H

#include "tracer.h"

/**
 * @brief Serialize a tracer configuration into a base64 encoded string
 *
 * @param config The configuration to encode
 * @return const char* A base64 encoded json string representing the configuration. This string must be freed by the caller.
 */
const char *encode_tracer_config(tracer_config_t *config);

#endif // CONFIG_ENCODE_H
