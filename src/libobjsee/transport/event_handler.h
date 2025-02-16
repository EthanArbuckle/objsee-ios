//
//  event_handler.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//

#include "tracer_internal.h"

void tracer_handle_event(tracer_t *tracer, tracer_event_t *event);

void cleanup_event_handler(void);
tracer_result_t init_event_handler(tracer_t *tracer);

