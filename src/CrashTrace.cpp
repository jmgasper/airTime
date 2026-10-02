/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "CrashTrace.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>
#include <image.h>


namespace airtime {

namespace {

void
write_text(const char* text)
{
	write(STDERR_FILENO, text, strlen(text));
}


// Which loaded image an address is in, as image + offset from its text
// segment: what addr2line takes for a library linked at 0.
bool
describe(uintptr_t address, char* line, size_t size, bool onlyKnown)
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		uintptr_t text = (uintptr_t)info.text;
		if (address >= text && address < text + (uintptr_t)info.text_size) {
			const char* leaf = strrchr(info.name, '/');
			snprintf(line, size, "  %#lx  %s + %#lx\n", (unsigned long)address,
				leaf != NULL ? leaf + 1 : info.name,
				(unsigned long)(address - text));
			return true;
		}
	}
	if (onlyKnown)
		return false;
	snprintf(line, size, "  %#lx  ?\n", (unsigned long)address);
	return true;
}


void
crash_handler(int signalNumber, siginfo_t* signalInfo, void* context)
{
	char line[1200];
	thread_info thread;
	get_thread_info(find_thread(NULL), &thread);
	snprintf(line, sizeof(line), "airTime: signal %d (address %p) in thread "
		"%" B_PRId32 " \"%s\"\n", signalNumber,
		signalInfo != NULL ? signalInfo->si_addr : NULL, thread.thread,
		thread.name);
	write_text(line);

	ucontext_t* user = (ucontext_t*)context;
	uintptr_t pc = 0, frame = 0, stack = 0, link = 0;
#if defined(__aarch64__)
	pc = user->uc_mcontext.elr;
	link = user->uc_mcontext.lr;
	frame = user->uc_mcontext.x[29];
	stack = user->uc_mcontext.sp;
#elif defined(__x86_64__)
	pc = user->uc_mcontext.rip;
	frame = user->uc_mcontext.rbp;
	stack = user->uc_mcontext.rsp;
#endif
	write_text("airTime: at\n");
	describe(pc, line, sizeof(line), false);
	write_text(line);
	if (link != 0) {
		write_text("airTime: called from\n");
		describe(link, line, sizeof(line), false);
		write_text(line);
	}

	uintptr_t low = (uintptr_t)thread.stack_base;
	uintptr_t high = (uintptr_t)thread.stack_end;
	write_text("airTime: frames\n");
	for (int i = 0; i < 64; i++) {
		if (frame < low || frame + 2 * sizeof(uintptr_t) > high
			|| (frame & (sizeof(uintptr_t) - 1)) != 0) {
			break;
		}
		uintptr_t next = ((uintptr_t*)frame)[0];
		uintptr_t returnAddress = ((uintptr_t*)frame)[1];
		describe(returnAddress, line, sizeof(line), false);
		write_text(line);
		if (next <= frame)
			break;
		frame = next;
	}

	// Code without frame pointers leaves the walk short: what on the stack
	// looks like a return address, which may include old ones.
	write_text("airTime: on the stack\n");
	int found = 0;
	for (uintptr_t at = stack; at >= low && at + sizeof(uintptr_t) <= high
			&& at < stack + 16384 && found < 40; at += sizeof(uintptr_t)) {
		if (describe(*(uintptr_t*)at, line, sizeof(line), true)) {
			write_text(line);
			found++;
		}
	}
	// SA_RESETHAND has put the default action back: returning repeats the
	// fault, and the system deals with it as it would have.
}

}	// namespace


void
install_crash_trace()
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = crash_handler;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&action.sa_mask);
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);
	sigaction(SIGILL, &action, NULL);
	sigaction(SIGFPE, &action, NULL);
}

}	// namespace airtime
