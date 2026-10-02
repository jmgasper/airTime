/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_PACKET_QUEUE_H
#define AIRTIME_PACKET_QUEUE_H


#include <condition_variable>
#include <deque>
#include <mutex>

#include <OS.h>

#include "FFmpeg.h"


namespace airtime {

/*!	Packets on their way from the demuxer to one decoder.

	Every packet carries the serial number the queue had when it was put in.
	A seek flushes the queue and bumps the serial; a decoder that sees a packet
	with a serial different from the last one flushes its own state first.
	A packet without data marks the end of the stream.
*/
class PacketQueue {
public:
								PacketQueue();
								~PacketQueue();

			void				Abort();
			void				Restart();
			int					Flush();

			// Takes over the packet's reference. The duration is only
			// bookkeeping for the demuxer's flow control.
			void				Put(AVPacket* packet, bigtime_t duration = 0);
			void				PutEndOfStream(int streamIndex);

			// 1 = got a packet, 0 = none (non blocking), -1 = aborted.
			// A blocking get also returns 0 when it times out.
			int					Get(AVPacket* packet, int* serial,
									bigtime_t timeout = B_INFINITE_TIMEOUT);
			bool				HasNewerSerial(int serial);

			int					Serial();
			int					Count();
			int64				Bytes();
			bigtime_t			Duration();
			bool				Aborted();

			// Wakes up whoever waits in Get(), without anything to get.
			void				Wake();

private:
			struct Entry {
				AVPacket*		packet;
				int				serial;
				bigtime_t		duration;
			};

			std::mutex			fLock;
			std::condition_variable	fCondition;
			std::deque<Entry>	fEntries;
			int					fSerial;
			int64				fBytes;
			int64				fDuration;
			bool				fAborted;
			int					fWakeups;
};

}	// namespace airtime

#endif	// AIRTIME_PACKET_QUEUE_H
