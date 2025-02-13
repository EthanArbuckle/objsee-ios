//
//  arg_capture.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/30/25.
//

#include "tracer_types.h"

void capture_arguments(tracer_t *tracer_ctx, struct tracer_thread_context_frame_t *frame, void *stack_base, tracer_event_t *event);
