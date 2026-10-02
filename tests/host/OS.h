#ifndef HOST_OS_H
#define HOST_OS_H
#include "SupportDefs.h"
#include <time.h>
inline bigtime_t system_time()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (bigtime_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
#endif
