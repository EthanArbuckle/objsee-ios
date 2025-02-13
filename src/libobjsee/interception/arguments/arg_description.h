//
//  arg_description.h
//  objsee
//
//  Created by Ethan Arbuckle on 2/7/25.
//

#ifndef ARG_DESCRIPTION_H
#define ARG_DESCRIPTION_H

#include "tracer_types.h"

/**
 * @brief Get a human-readable description of an argument
 * @param arg The argument to describe
 * @param fmt The format to use
 * @param out_buf The buffer to write the description to
 * @param buf_size The size of the buffer
 * @return KERN_SUCCESS on success, otherwise an error code
 */
kern_return_t description_for_argument(const tracer_argument_t *arg, tracer_argument_format_t fmt, char *out_buf, size_t buf_size);

#endif /* ARG_DESCRIPTION_H */
