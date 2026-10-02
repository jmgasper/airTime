/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_VIDEO_FRAME_H
#define AIRTIME_VIDEO_FRAME_H


#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#include "FFmpeg.h"


namespace airtime {

struct VideoFrame {
	AVFrame*		frame;			// owned; any pixel format swscale reads
	bigtime_t		pts;			// media time, from the start of the file
	bigtime_t		duration;
	int				serial;
	bool			hardware;

	VideoFrame()
		:
		frame(av_frame_alloc()),
		pts(0),
		duration(0),
		serial(0),
		hardware(false)
	{
	}

	~VideoFrame()
	{
		av_frame_free(&frame);
	}

	VideoFrame(const VideoFrame&) = delete;
	VideoFrame& operator=(const VideoFrame&) = delete;
};

typedef std::shared_ptr<VideoFrame> VideoFramePtr;


/*!	Decoded pictures waiting to be shown, oldest first. */
class FrameQueue {
public:
	FrameQueue()
		:
		fCapacity(4),
		fAborted(false)
	{
	}

	void SetCapacity(int capacity)
	{
		std::lock_guard<std::mutex> lock(fLock);
		fCapacity = capacity < 2 ? 2 : capacity;
		fCondition.notify_all();
	}

	// Blocks while the queue is full. Gives up, returning false, when the
	// queue is aborted or `keepWaiting` says the frame is no longer wanted.
	bool Push(const VideoFramePtr& frame,
		const std::function<bool()>& keepWaiting)
	{
		std::unique_lock<std::mutex> lock(fLock);
		while ((int)fFrames.size() >= fCapacity) {
			if (fAborted || !keepWaiting())
				return false;
			fCondition.wait_for(lock, std::chrono::milliseconds(20));
		}
		if (fAborted)
			return false;
		fFrames.push_back(frame);
		fCondition.notify_all();
		return true;
	}

	VideoFramePtr Peek(int index = 0)
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (index >= (int)fFrames.size())
			return VideoFramePtr();
		return fFrames[index];
	}

	void Pop()
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (!fFrames.empty())
			fFrames.pop_front();
		fCondition.notify_all();
	}

	int Count()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return (int)fFrames.size();
	}

	void Flush()
	{
		std::lock_guard<std::mutex> lock(fLock);
		fFrames.clear();
		fCondition.notify_all();
	}

	// Waits until there is a frame or the timeout passes.
	bool Wait(bigtime_t timeout)
	{
		std::unique_lock<std::mutex> lock(fLock);
		if (!fFrames.empty())
			return true;
		fCondition.wait_for(lock, std::chrono::microseconds(timeout));
		return !fFrames.empty();
	}

	void Wake()
	{
		std::lock_guard<std::mutex> lock(fLock);
		fCondition.notify_all();
	}

	void Abort()
	{
		std::lock_guard<std::mutex> lock(fLock);
		fAborted = true;
		fFrames.clear();
		fCondition.notify_all();
	}

	void Restart()
	{
		std::lock_guard<std::mutex> lock(fLock);
		fAborted = false;
	}

private:
	std::mutex			fLock;
	std::condition_variable	fCondition;
	std::deque<VideoFramePtr> fFrames;
	int					fCapacity;
	bool				fAborted;
};

}	// namespace airtime

#endif	// AIRTIME_VIDEO_FRAME_H
