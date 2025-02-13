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
NSString *first_booted_simulator_uuid(void);

/**
 * Launch an app for tracing in the simulator with a given bundle ID
 * @param simulatorUUID The UUID of the simulator to launch the app in
 * @param bundleID The bundle ID of the app to launch
 * @oaram configString The encoded tracer config to use
 * @return KERN_SUCCESS on success, an error code on failure
 */
kern_return_t launch_simulator_app_with_encoded_tracer_config(NSString *simulatorUUID, NSString *bundleID, NSString *configString);

#endif /* sim_launching_h */
