/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Screens.h"

#include <dlfcn.h>

#include <InterfaceDefs.h>
#include <Screen.h>


namespace airtime {

typedef status_t (*get_display_frame_function)(BRect frame, bool forZoom,
	BRect& displayFrame);


static get_display_frame_function
display_frame_function()
{
	static get_display_frame_function function = NULL;
	static bool looked = false;
	if (!looked) {
		looked = true;
		function = (get_display_frame_function)dlsym(RTLD_DEFAULT,
			"_ZN8BPrivate17get_display_frameE5BRectbRS0_");
	}
	return function;
}


BRect
monitor_frame(BRect frame)
{
	BRect screen = BScreen().Frame();
	get_display_frame_function function = display_frame_function();
	BRect monitor;
	if (function != NULL && function(frame, false, monitor) == B_OK
		&& monitor.IsValid()) {
		return monitor;
	}
	return screen;
}


BRect
monitor_frame_at_pointer()
{
	BPoint where;
	uint32 buttons;
	if (get_mouse(&where, &buttons) != B_OK)
		return BScreen().Frame();
	return monitor_frame(BRect(where, where));
}

}	// namespace airtime
