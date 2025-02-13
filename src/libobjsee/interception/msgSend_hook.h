//
//  msgSend_hook.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include "tracer_internal.h"

extern void (*original_objc_msgSend)(void);

void *get_original_objc_msgSend(void);

tracer_result_t init_message_interception(tracer_t *tracer);
