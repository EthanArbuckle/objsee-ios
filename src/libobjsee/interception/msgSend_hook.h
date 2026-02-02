//
//  msgSend_hook.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include "tracer_internal.h"

extern void (*g_original_objc_msgSend)(void);

tracer_result_t init_message_interception(tracer_t *tracer);
tracer_result_t disable_message_interception(tracer_t *tracer);

