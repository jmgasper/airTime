/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


// The one place that looks inside BDirectWindow, whose members are private:
// it has no way to let go of a connection the app_server has given up on.
#define private public
#include <DirectWindow.h>
#undef private

#include "DirectRescue.h"

#include <stdio.h>


namespace airtime {


bool
direct_daemon_alive(BDirectWindow* window)
{
	thread_info info;
	return window->fDirectDaemonId > 0
		&& get_thread_info(window->fDirectDaemonId, &info) == B_OK;
}


bool
release_dead_direct_connection(BDirectWindow* window)
{
	if (!window->fConnectionEnable || direct_daemon_alive(window))
		return false;
	// BDirectWindow's destructor waits for this to go false, which only
	// its daemon does - on a B_DIRECT_STOP that will never come.
	window->fConnectionEnable = false;
	fprintf(stderr, "airTime: the app_server ended this window's frame "
		"buffer access; closing it without waiting for that\n");
	return true;
}


void
kill_direct_daemon(BDirectWindow* window)
{
	if (window->fDirectDaemonId > 0)
		kill_thread(window->fDirectDaemonId);
}


}	// namespace airtime
