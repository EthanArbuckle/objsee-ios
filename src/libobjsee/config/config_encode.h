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
 * @param out_str The output string
 * @return tracer_result_t
 */
tracer_result_t encode_tracer_config(tracer_config_t *config, char **out_str);

#endif // CONFIG_ENCODE_H
