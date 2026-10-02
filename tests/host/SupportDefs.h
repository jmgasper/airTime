// Stand-in for Haiku's SupportDefs.h, so engine code can be tested on Linux.
#ifndef HOST_SUPPORT_DEFS_H
#define HOST_SUPPORT_DEFS_H
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef int32 status_t;
typedef int64 bigtime_t;
#define B_OK 0
#define B_ERROR (-1)
#define B_NO_MEMORY (-2147483647 - 1)
#define B_BAD_VALUE (-2147483647 + 4)
#define B_NOT_SUPPORTED (-2147483647 + 100)
#define B_ENTRY_NOT_FOUND (-2147483647 + 101)
#define B_INFINITE_TIMEOUT INT64_MAX
#define B_PRId32 PRId32
#define B_PRId64 PRId64
#endif
