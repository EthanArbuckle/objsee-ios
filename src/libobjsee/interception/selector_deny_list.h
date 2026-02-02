//
//  selector_deny_list.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 11/30/24.
//


#ifndef SELECTOR_DENY_LIST_H
#define SELECTOR_DENY_LIST_H

// Returns true if the selector name should not be traced
bool should_skip_selector_name(const char *selector_name);

#endif /* SELECTOR_DENY_LIST_H */

