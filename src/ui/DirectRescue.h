/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_DIRECT_RESCUE_H
#define AIRTIME_DIRECT_RESCUE_H


class BDirectWindow;


namespace airtime {

/*!	When a window does not answer a change of its frame buffer access within
	half a second, the app_server gives up on it ("killed for a problem in
	DirectConnected()") and BDirectWindow's daemon thread ends. The window
	still thinks it is connected, and its destructor waits for ever for the
	stop that will not come: the window never closes and the application
	never quits. These find that state and get out of it.
*/

// Whether the window's daemon thread, which tells it of changes, still runs.
bool direct_daemon_alive(BDirectWindow* window);

// Lets the destructor go on if the daemon is gone; true if it was.
bool release_dead_direct_connection(BDirectWindow* window);

// For testing: ends the daemon as the app_server's timeout would.
void kill_direct_daemon(BDirectWindow* window);

}	// namespace airtime


#endif	// AIRTIME_DIRECT_RESCUE_H
