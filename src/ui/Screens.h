/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_SCREENS_H
#define AIRTIME_SCREENS_H


#include <Rect.h>


namespace airtime {

// The frame of the monitor most of `frame` is on. air/OS's app_server knows
// the monitors behind its one screen; where libbe cannot say, this is the
// whole screen.
BRect monitor_frame(BRect frame);

// The monitor the mouse pointer is on.
BRect monitor_frame_at_pointer();

}	// namespace airtime

#endif	// AIRTIME_SCREENS_H
