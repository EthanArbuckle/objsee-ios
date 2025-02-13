//
//  crash_handler.h
//  cli
//
//  Created by Ethan Arbuckle on 12/30/24.
//

#ifndef crash_handler_h
#define crash_handler_h

#include <CoreFoundation/CoreFoundation.h>

void setup_exception_handler_on_process(pid_t traced_app_pid);

#endif /* crash_handler_h */
