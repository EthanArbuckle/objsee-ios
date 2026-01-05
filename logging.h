//
//  logging.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/4/26.
//

#if __IPHONE_OS_VERSION_MIN_REQUIRED >= 110000

#include <os/log.h>
#define objsee_log(fmt, ...) os_log(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__)

#else

#include <syslog.h>
#define objsee_log(fmt, ...) syslog(LOG_NOTICE, fmt, ##__VA_ARGS__)

#endif
