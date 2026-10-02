/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "AudioOutput.h"

#include <chrono>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SoundPlayer.h>


namespace airtime {


static const int kSampleRate = 48000;
static const int kChannels = 2;
static const size_t kBufferFrames = 1024;


AudioOutput::AudioOutput(Clock* clock)
	:
	fClock(clock),
	fPlayer(NULL),
	fSampleRate(kSampleRate),
	fChannels(kChannels),
	fLatency(0),
	fCapacity(kSampleRate),
	fLimit(kSampleRate * 3 / 10),
	fWritten(0),
	fRead(0),
	fSerial(0),
	fPlayedSerial(-1),
	fPaused(true),
	fAborted(false),
	fVolume(1.0f),
	fGain(1.0f),
	fMuted(false)
{
	fRing.resize(fCapacity * fChannels);
}


AudioOutput::~AudioOutput()
{
	Abort();
	if (fPlayer != NULL) {
		fPlayer->Stop();
		delete fPlayer;
	}
}


status_t
AudioOutput::Init(const char* name)
{
	media_raw_audio_format format = media_raw_audio_format::wildcard;
	format.frame_rate = fSampleRate;
	format.channel_count = fChannels;
	format.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.byte_order = B_MEDIA_HOST_ENDIAN;
	format.buffer_size = kBufferFrames * fChannels * sizeof(float);

	fPlayer = new(std::nothrow) BSoundPlayer(&format, name, _PlayBuffer,
		NULL, this);
	if (fPlayer == NULL)
		return B_NO_MEMORY;
	status_t status = fPlayer->InitCheck();
	if (status != B_OK) {
		delete fPlayer;
		fPlayer = NULL;
		return status;
	}
	status = fPlayer->Start();
	if (status != B_OK)
		return status;
	fPlayer->SetHasData(true);
	fLatency = fPlayer->Latency();
	return B_OK;
}


bool
AudioOutput::Write(const float* samples, int frames, bigtime_t pts,
	double speed, int serial, const std::function<bool()>& keepWaiting)
{
	std::unique_lock<std::mutex> lock(fLock);
	while (frames > 0) {
		if (fAborted || serial != fSerial)
			return false;
		size_t used = fWritten - fRead;
		size_t free = used < fLimit ? fLimit - used : 0;
		if (free == 0) {
			if (!keepWaiting())
				return false;
			fSpace.wait_for(lock, std::chrono::milliseconds(20));
			continue;
		}
		size_t count = (size_t)frames < free ? (size_t)frames : free;

		// A new stretch whenever the time does not simply continue.
		bool continues = false;
		if (!fSegments.empty()) {
			const Segment& last = fSegments.back();
			bigtime_t expected = last.pts + (bigtime_t)((fWritten - last.start)
				* last.speed * 1000000.0 / fSampleRate);
			continues = last.serial == serial && last.speed == speed
				&& llabs(expected - pts) < 2000;
		}
		if (!continues)
			fSegments.push_back(Segment{fWritten, pts, speed, serial});

		for (size_t i = 0; i < count; i++) {
			size_t at = (size_t)((fWritten + i) % fCapacity) * fChannels;
			for (int c = 0; c < fChannels; c++)
				fRing[at + c] = samples[i * fChannels + c];
		}
		fWritten += count;
		samples += count * fChannels;
		frames -= count;
		pts += (bigtime_t)(count * speed * 1000000.0 / fSampleRate);
	}
	return true;
}


void
AudioOutput::Flush(int serial)
{
	std::lock_guard<std::mutex> lock(fLock);
	fRead = fWritten;
	fSegments.clear();
	fSerial = serial;
	fSpace.notify_all();
}


void
AudioOutput::SetPaused(bool paused)
{
	std::lock_guard<std::mutex> lock(fLock);
	fPaused = paused;
}


void
AudioOutput::SetVolume(float volume)
{
	std::lock_guard<std::mutex> lock(fLock);
	fVolume = volume < 0 ? 0 : volume > 1.5f ? 1.5f : volume;
}


void
AudioOutput::SetMuted(bool muted)
{
	std::lock_guard<std::mutex> lock(fLock);
	fMuted = muted;
}


bigtime_t
AudioOutput::Buffered()
{
	std::lock_guard<std::mutex> lock(fLock);
	return (bigtime_t)((fWritten - fRead) * 1000000 / fSampleRate);
}


bool
AudioOutput::Drained(int serial)
{
	std::lock_guard<std::mutex> lock(fLock);
	return serial == fSerial && fWritten == fRead;
}


void
AudioOutput::Abort()
{
	std::lock_guard<std::mutex> lock(fLock);
	fAborted = true;
	fSpace.notify_all();
}


/*static*/ void
AudioOutput::_PlayBuffer(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	AudioOutput* output = (AudioOutput*)cookie;
	if (format.format != media_raw_audio_format::B_AUDIO_FLOAT
		|| (int)format.channel_count != output->fChannels) {
		memset(buffer, 0, size);
		return;
	}
	output->_Fill((float*)buffer, size / (sizeof(float) * output->fChannels));
}


void
AudioOutput::_Fill(float* buffer, size_t frames)
{
	std::lock_guard<std::mutex> lock(fLock);

	size_t available = fWritten - fRead;
	if (fPaused || available == 0 || fAborted) {
		memset(buffer, 0, frames * fChannels * sizeof(float));
		return;
	}

	// The media time of the first frame handed over now; it is heard once
	// everything downstream of the player has passed it on.
	while (fSegments.size() > 1 && fSegments[1].start <= fRead)
		fSegments.pop_front();
	if (!fSegments.empty()) {
		const Segment& segment = fSegments.front();
		bigtime_t pts = segment.pts + (bigtime_t)((fRead - segment.start)
			* segment.speed * 1000000.0 / fSampleRate);
		fClock->Set(pts, segment.serial, system_time() + fLatency,
			segment.speed);
		fPlayedSerial = segment.serial;
	}

	size_t count = frames < available ? frames : available;
	float target = fMuted ? 0.0f : fVolume * fVolume;
		// a squared volume sounds more even across the slider
	float gain = fGain;
	float step = (target - gain) / (float)(count > 0 ? count : 1);
	for (size_t i = 0; i < count; i++) {
		size_t at = (size_t)((fRead + i) % fCapacity) * fChannels;
		for (int c = 0; c < fChannels; c++)
			buffer[i * fChannels + c] = fRing[at + c] * gain;
		gain += step;
	}
	fGain = target;
	fRead += count;
	if (count < frames) {
		memset(buffer + count * fChannels, 0,
			(frames - count) * fChannels * sizeof(float));
	}
	fSpace.notify_all();
}

}	// namespace airtime
