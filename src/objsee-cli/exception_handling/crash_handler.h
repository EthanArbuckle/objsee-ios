//
//  crash_handler.h
//  cli
//
//  Created by Ethan Arbuckle on 12/30/24.
//

#ifndef crash_handler_h
#define crash_handler_h

#include <CoreFoundation/CoreFoundation.h>

/**
 * Sets up an exception handler on a traced process to catch crashes
 * @param traced_app_pid The PID of the traced application
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t setup_exception_handler_on_process(pid_t traced_app_pid);

#endif /* crash_handler_h */
