//
//  realized_class_tracking.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/30/25.
//

#ifndef realized_class_tracking_h
#define realized_class_tracking_h

#include <objc/runtime.h>

// Capturing arguments within a objc_msgSend() hook creates the potential for interactions with unrealized classes. Attempting to read metadata about such classes
// will kill the process. To work around this, details are not captured for objects when it's the first occurrence of that class. Subsequent occurrences will be captured

/**
 * @brief Returns whether or not the class has been seen before (passed through record_class_encounter())
 * @param cls The class to check
 * @return KERN_SUCCESS if the class has been seen, otherwise an error code
 */
kern_return_t has_seen_class(Class cls);

/**
 * @brief Records that a class has been seen
 * @param cls The class to record
 * @return KERN_SUCCESS on success, otherwise an error code
 */
kern_return_t record_class_encounter(Class cls);

#endif /* realized_class_tracking_h */
