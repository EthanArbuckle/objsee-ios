//
//  logging.h
//  objsee
//
//  Created by Ethan Arbuckle on 1/4/26.
//

#if __IPHONE_OS_VERSION_MIN_REQUIRED >= 110000

#include <os/log.h>

#define objsee_log(fmt, ...) do { char _log_buf[1024]; snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__); os_log(OS_LOG_DEFAULT, "%{public}s", _log_buf); } while (0)

#else

#include <syslog.h>
#define objsee_log(fmt, ...) syslog(LOG_NOTICE, fmt, ##__VA_ARGS__)

#endif
