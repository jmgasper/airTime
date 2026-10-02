/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_CLOCK_H
#define AIRTIME_CLOCK_H


#include <mutex>

#include <OS.h>

#include "FFmpeg.h"


namespace airtime {

/*!	A media clock: a media time that was true at some real time, running at
	some speed from there. The audio output sets it from what it hands to the
	sound card; without audio the player runs it from the system clock.

	The serial says which seek the time belongs to; a clock whose serial is
	not the current one has no meaning yet.
*/
class Clock {
public:
	Clock()
		:
		fTime(0),
		fUpdated(0),
		fSpeed(1.0),
		fPaused(true),
		fSerial(-1)
	{
	}

	bigtime_t Get(int* serial = NULL)
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (serial != NULL)
			*serial = fSerial;
		return _Get(system_time());
	}

	void Set(bigtime_t time, int serial, bigtime_t when = -1,
		double speed = -1)
	{
		std::lock_guard<std::mutex> lock(fLock);
		fTime = time;
		fUpdated = when >= 0 ? when : system_time();
		fSerial = serial;
		if (speed > 0)
			fSpeed = speed;
	}

	void SetSpeed(double speed)
	{
		std::lock_guard<std::mutex> lock(fLock);
		bigtime_t now = system_time();
		fTime = _Get(now);
		fUpdated = now;
		fSpeed = speed;
	}

	double Speed()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return fSpeed;
	}

	void SetPaused(bool paused)
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (paused == fPaused)
			return;
		bigtime_t now = system_time();
		fTime = _Get(now);
		fUpdated = now;
		fPaused = paused;
	}

	bool Paused()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return fPaused;
	}

	int Serial()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return fSerial;
	}

	void Invalidate()
	{
		std::lock_guard<std::mutex> lock(fLock);
		fSerial = -1;
	}

private:
	bigtime_t _Get(bigtime_t now) const
	{
		if (fPaused)
			return fTime;
		return fTime + (bigtime_t)((now - fUpdated) * fSpeed);
	}

	std::mutex		fLock;
	bigtime_t		fTime;
	bigtime_t		fUpdated;
	double			fSpeed;
	bool			fPaused;
	int				fSerial;
};

}	// namespace airtime

#endif	// AIRTIME_CLOCK_H
