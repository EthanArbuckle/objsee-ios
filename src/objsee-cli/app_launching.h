//
//  app_launching.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/17/25.
//

#ifndef app_launching_h
#define app_launching_h

#include <CoreFoundation/CoreFoundation.h>

extern const char *OBJSEE_LIBRARY_PATH;

/**
 * Launches an app with a given bundle ID
 * @param bundleID The bundle ID of the app to launch
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t launch_app_with_encoded_tracer_config(NSString *bundleID, NSString *configString);

/**
 * Terminates an app with a given bundle ID
 * @param bundleID The bundle ID of the app to terminate
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t terminate_app_if_running(NSString *bundleID);

/**
 * Waits for launch completion of an app with a given bundle ID
 * @param bundleID The bundle ID of the app to wait for
 * @param completion The block to call when the app is launched
 */
void on_process_launch(NSString *bundleID, void (^completion)(pid_t pid));

/**
 * Finds a free socket port to use for the tracer transport
 * @return The port number
 */
int find_free_socket_port(void);

#endif /* app_launching_h */
