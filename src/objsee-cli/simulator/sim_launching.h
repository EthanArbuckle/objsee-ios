//
//  sim_launching.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/18/25.
//

#ifndef sim_launching_h
#define sim_launching_h

#include <CoreFoundation/CoreFoundation.h>

/**
 * Get the UUID of the first booted simulator encountered
 * @return The UUID of the simulator, or nil on failure or no simulators booted
 */
const char *first_booted_simulator_uuid(void);

/**
 * Launch an app for tracing in the simulator with a given bundle ID
 * @param sim_uuid The UUID of the simulator to launch the app in
 * @param bundle_id The bundle ID of the app to launch
 * @oaram encoded_config The encoded tracer config to use
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t simulator_launch_traced_app(const char *sim_uuid, const char *bundle_id, const char *encoded_config);

#endif /* sim_launching_h */
