/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_AUDIO_DECODER_H
#define AIRTIME_AUDIO_DECODER_H


#include <atomic>
#include <mutex>
#include <vector>

#include <String.h>

#include "AudioOutput.h"
#include "PacketQueue.h"


namespace airtime {

/*!	Decodes one audio stream, converts it to what the AudioOutput plays and,
	when the film runs faster or slower than normal, stretches it in time
	without changing the pitch (libavfilter's atempo).
*/
class AudioDecoder {
public:
								AudioDecoder(AVStream* stream,
									PacketQueue* queue, AudioOutput* output,
									bigtime_t startTime);
								~AudioDecoder();

			status_t			Init(BString* reason);
			BString				Name() const;

			// Runs until the queue is aborted.
			void				Run();

			void				SetSpeed(double speed);
			// Samples of the serial before the time are dropped.
			void				SetTrim(int serial, bigtime_t time);
			// The stream has ended for the serial and everything was handed
			// to the output.
			bool				Finished(int serial) const;
			bool				TempoAvailable() const
									{ return fTempoAvailable; }

private:
			void				_Reset();
			void				_HandleFrame(AVFrame* frame);
			bool				_SetupResampler(const AVFrame* frame);
			bool				_SetupTempo(double speed);
			void				_FreeTempo();
			void				_Output(const float* samples, int frames,
									bigtime_t pts);
			bool				_KeepWaiting();

			AVStream*			fStream;
			PacketQueue*		fQueue;
			AudioOutput*		fOutput;
			bigtime_t			fStartTime;
			AVCodecContext*		fContext;
			AVPacket*			fPacket;
			AVFrame*			fFrame;
			int					fSerial;

			SwrContext*			fResampler;
			int					fResamplerRate;
			int					fResamplerFormat;
			AVChannelLayout		fResamplerLayout;
			std::vector<float>	fConverted;
			bigtime_t			fNextPts;

			AVFilterGraph*		fTempoGraph;
			AVFilterContext*	fTempoSource;
			AVFilterContext*	fTempoSink;
			AVFrame*			fTempoFrame;
			double				fTempoSpeed;
			bigtime_t			fTempoBasePts;
			int64				fTempoIn;
			int64				fTempoOut;
			bool				fTempoAvailable;

			std::atomic<double>	fSpeed;
			mutable std::mutex	fStateLock;
			int					fTrimSerial;
			bigtime_t			fTrimTime;
			int					fFinishedSerial;
};

}	// namespace airtime

#endif	// AIRTIME_AUDIO_DECODER_H
