/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_CRASH_TRACE_H
#define AIRTIME_CRASH_TRACE_H


namespace airtime {

/*!	On a crash, says on standard error where it happened - the faulting
	address, the frame pointer chain and what looks like return addresses on
	the stack, each as image + offset - and then lets the system handle it as
	it would have. For machines without the Debugger, such as arm64 air/OS.
*/
void install_crash_trace();

}	// namespace airtime


#endif	// AIRTIME_CRASH_TRACE_H
