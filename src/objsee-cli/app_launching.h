//
//  app_launching.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/17/25.
//

#ifndef app_launching_h
#define app_launching_h

#include <CoreFoundation/CoreFoundation.h>
#include "cli_args.h"

extern const char *OBJSEE_LIBRARY_PATH;

/**
 * Launches an app with a given bundle ID
 * @param bundle_id The bundle ID of the app to launch
 * @param encoded_config The encoded tracer config to use
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t launch_traced_app(const char *bundle_id, const char *encoded_config);

/**
 * Terminates an app with a given bundle ID
 * @param bundle_id The bundle ID of the app to terminate
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t terminate_app_if_running(const char *bundle_id);

/**
 * Waits for launch completion of an app with a given bundle ID
 * @param bundle_id The bundle ID of the app to wait for
 * @param completion The block to call when the app is launched
 */
void on_process_launch(const char *bundle_id, void (^completion)(pid_t pid));

/**
 * Finds a free socket port to use for the tracer transport
 * @return The port number
 */
int find_free_socket_port(void);

/**
 * Spawns a process with a given config string
 * @param options The CLI options
 * @param encoded_config The encoded tracer config to use
 * @param out_pid The PID of the spawned process
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t spawn_traced_process(cli_options_t *options, const char *encoded_config, pid_t *out_pid);

/**
 * Gets a PID from a process hint (bundle ID or file path)
 * @param hint The process hint
 * @return The PID, or -1 on failure
 */
pid_t pid_from_hint(const char *hint);

#endif /* app_launching_h */
