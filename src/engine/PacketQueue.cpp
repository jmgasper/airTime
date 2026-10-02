/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "PacketQueue.h"

#include <chrono>


namespace airtime {


PacketQueue::PacketQueue()
	:
	fSerial(0),
	fBytes(0),
	fDuration(0),
	fAborted(true),
	fWakeups(0)
{
}


PacketQueue::~PacketQueue()
{
	Flush();
}


void
PacketQueue::Abort()
{
	std::lock_guard<std::mutex> lock(fLock);
	fAborted = true;
	fCondition.notify_all();
}


void
PacketQueue::Restart()
{
	std::lock_guard<std::mutex> lock(fLock);
	fAborted = false;
	fSerial++;
}


int
PacketQueue::Flush()
{
	std::lock_guard<std::mutex> lock(fLock);
	for (Entry& entry : fEntries)
		av_packet_free(&entry.packet);
	fEntries.clear();
	fBytes = 0;
	fDuration = 0;
	fSerial++;
	fCondition.notify_all();
	return fSerial;
}


void
PacketQueue::Put(AVPacket* packet, bigtime_t duration)
{
	AVPacket* copy = av_packet_alloc();
	if (copy == NULL) {
		av_packet_unref(packet);
		return;
	}
	av_packet_move_ref(copy, packet);

	std::lock_guard<std::mutex> lock(fLock);
	if (fAborted) {
		av_packet_free(&copy);
		return;
	}
	if (duration < 0 || duration == kNoTime)
		duration = 0;
	fEntries.push_back(Entry{copy, fSerial, duration});
	fBytes += copy->size + (int64)sizeof(*copy);
	fDuration += duration;
	fCondition.notify_all();
}


void
PacketQueue::PutEndOfStream(int streamIndex)
{
	AVPacket* packet = av_packet_alloc();
	if (packet == NULL)
		return;
	packet->stream_index = streamIndex;
	Put(packet);
	av_packet_free(&packet);
}


int
PacketQueue::Get(AVPacket* packet, int* serial, bigtime_t timeout)
{
	std::unique_lock<std::mutex> lock(fLock);
	int wakeups = fWakeups;
	for (;;) {
		if (fAborted)
			return -1;
		if (!fEntries.empty()) {
			Entry entry = fEntries.front();
			fEntries.pop_front();
			fBytes -= entry.packet->size + (int64)sizeof(*entry.packet);
			fDuration -= entry.duration;
			av_packet_move_ref(packet, entry.packet);
			av_packet_free(&entry.packet);
			if (serial != NULL)
				*serial = entry.serial;
			return 1;
		}
		if (timeout <= 0 || fWakeups != wakeups)
			return 0;
		if (timeout == B_INFINITE_TIMEOUT)
			fCondition.wait(lock);
		else if (fCondition.wait_for(lock, std::chrono::microseconds(timeout))
				== std::cv_status::timeout) {
			if (fEntries.empty())
				return 0;
		}
	}
}


bool
PacketQueue::HasNewerSerial(int serial)
{
	std::lock_guard<std::mutex> lock(fLock);
	return fSerial != serial;
}


int
PacketQueue::Serial()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fSerial;
}


int
PacketQueue::Count()
{
	std::lock_guard<std::mutex> lock(fLock);
	return (int)fEntries.size();
}


int64
PacketQueue::Bytes()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fBytes;
}


bigtime_t
PacketQueue::Duration()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fDuration;
}


bool
PacketQueue::Aborted()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fAborted;
}


void
PacketQueue::Wake()
{
	std::lock_guard<std::mutex> lock(fLock);
	fWakeups++;
	fCondition.notify_all();
}


}	// namespace airtime
