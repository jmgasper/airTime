/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_AUDIO_OUTPUT_H
#define AIRTIME_AUDIO_OUTPUT_H


#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include <MediaDefs.h>

#include "Clock.h"


class BSoundPlayer;


namespace airtime {

/*!	The sound card's side. A BSoundPlayer pulls from a ring of interleaved
	float samples; each stretch of samples remembers the media time it
	started at and how fast media time runs through it (more than one second
	of film per second of sound when playing fast), so that the clock can be
	set from whatever is being handed to the card.

	The player is started once and left running: BSoundPlayer does not
	reliably start again after Stop(). While paused it is fed silence.
*/
class AudioOutput {
public:
								AudioOutput(Clock* clock);
								~AudioOutput();

			status_t			Init(const char* name);
			int					SampleRate() const { return fSampleRate; }
			int					Channels() const { return fChannels; }

			// Blocks while the ring is full. Returns false when the samples
			// are no longer wanted: aborted, flushed past their serial, or
			// `keepWaiting` says so.
			bool				Write(const float* samples, int frames,
									bigtime_t pts, double speed, int serial,
									const std::function<bool()>& keepWaiting);

			// Forgets every sample older than the serial.
			void				Flush(int serial);
			void				SetPaused(bool paused);
			void				SetVolume(float volume);
			void				SetMuted(bool muted);

			// Media time the ring holds, measured in sound card time.
			bigtime_t			Buffered();
			// True when everything written for the serial has been played.
			bool				Drained(int serial);
			bigtime_t			Latency() const { return fLatency; }

			void				Abort();

private:
	static	void				_PlayBuffer(void* cookie, void* buffer,
									size_t size,
									const media_raw_audio_format& format);
			void				_Fill(float* buffer, size_t frames);

			struct Segment {
				uint64			start;		// ring position of the first frame
				bigtime_t		pts;
				double			speed;
				int				serial;
			};

			Clock*				fClock;
			BSoundPlayer*		fPlayer;
			int					fSampleRate;
			int					fChannels;
			bigtime_t			fLatency;

			std::mutex			fLock;
			std::condition_variable	fSpace;
			std::vector<float>	fRing;
			size_t				fCapacity;		// frames
			size_t				fLimit;			// frames we let in at most
			uint64				fWritten;
			uint64				fRead;
			std::deque<Segment>	fSegments;
			int					fSerial;
			int					fPlayedSerial;
			bool				fPaused;
			bool				fAborted;
			float				fVolume;
			float				fGain;
			bool				fMuted;
};

}	// namespace airtime

#endif	// AIRTIME_AUDIO_OUTPUT_H
