/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "AudioDecoder.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


namespace airtime {

static bool sTrace = getenv("AIRTIME_TRACE") != NULL;
#define TRACE(...) do { if (sTrace) fprintf(stderr, __VA_ARGS__); } while (0)


AudioDecoder::AudioDecoder(AVStream* stream, PacketQueue* queue,
	AudioOutput* output, bigtime_t startTime)
	:
	fStream(stream),
	fQueue(queue),
	fOutput(output),
	fStartTime(startTime),
	fContext(NULL),
	fPacket(av_packet_alloc()),
	fFrame(av_frame_alloc()),
	fSerial(-1),
	fResampler(NULL),
	fResamplerRate(0),
	fResamplerFormat(-1),
	fNextPts(kNoTime),
	fTempoGraph(NULL),
	fTempoSource(NULL),
	fTempoSink(NULL),
	fTempoFrame(av_frame_alloc()),
	fTempoSpeed(1.0),
	fTempoBasePts(0),
	fTempoIn(0),
	fTempoOut(0),
	fTempoAvailable(avfilter_get_by_name("atempo") != NULL),
	fSpeed(1.0),
	fTrimSerial(-1),
	fTrimTime(0),
	fFinishedSerial(-1)
{
	memset(&fResamplerLayout, 0, sizeof(fResamplerLayout));
}


AudioDecoder::~AudioDecoder()
{
	_FreeTempo();
	swr_free(&fResampler);
	av_channel_layout_uninit(&fResamplerLayout);
	avcodec_free_context(&fContext);
	av_packet_free(&fPacket);
	av_frame_free(&fFrame);
	av_frame_free(&fTempoFrame);
}


status_t
AudioDecoder::Init(BString* reason)
{
	const AVCodec* codec = avcodec_find_decoder(fStream->codecpar->codec_id);
	if (codec == NULL) {
		reason->SetToFormat("no decoder for %s",
			avcodec_get_name(fStream->codecpar->codec_id));
		return B_NOT_SUPPORTED;
	}
	fContext = avcodec_alloc_context3(codec);
	if (fContext == NULL)
		return B_NO_MEMORY;
	if (avcodec_parameters_to_context(fContext, fStream->codecpar) < 0)
		return B_ERROR;
	fContext->pkt_timebase = fStream->time_base;
	int error = avcodec_open2(fContext, codec, NULL);
	if (error < 0) {
		char text[128];
		reason->SetToFormat("%s would not open: %s", codec->name,
			av_error_string(error, text, sizeof(text)));
		return B_ERROR;
	}
	return B_OK;
}


BString
AudioDecoder::Name() const
{
	if (fContext == NULL || fContext->codec == NULL)
		return "libavcodec";
	BString name;
	name.SetToFormat("libavcodec (%s)", fContext->codec->name);
	return name;
}


void
AudioDecoder::SetSpeed(double speed)
{
	fSpeed = speed;
}


void
AudioDecoder::SetTrim(int serial, bigtime_t time)
{
	std::lock_guard<std::mutex> lock(fStateLock);
	fTrimSerial = serial;
	fTrimTime = time;
}


bool
AudioDecoder::Finished(int serial) const
{
	std::lock_guard<std::mutex> lock(fStateLock);
	return fFinishedSerial == serial;
}


bool
AudioDecoder::_KeepWaiting()
{
	return !fQueue->Aborted() && !fQueue->HasNewerSerial(fSerial);
}


void
AudioDecoder::_Reset()
{
	avcodec_flush_buffers(fContext);
	if (fResampler != NULL) {
		swr_free(&fResampler);
		fResamplerRate = 0;
		fResamplerFormat = -1;
	}
	_FreeTempo();
	fNextPts = kNoTime;
}


void
AudioDecoder::Run()
{
	bool draining = false;
	for (;;) {
		int serial;
		int got = fQueue->Get(fPacket, &serial);
		if (got < 0)
			break;
		if (got == 0)
			continue;

		if (serial != fSerial) {
			_Reset();
			fSerial = serial;
			draining = false;
		}

		if (fPacket->data == NULL) {
			if (!draining) {
				avcodec_send_packet(fContext, NULL);
				draining = true;
				while (avcodec_receive_frame(fContext, fFrame) >= 0) {
					_HandleFrame(fFrame);
					av_frame_unref(fFrame);
				}
				// Whatever the tempo filter still holds.
				if (fTempoGraph != NULL) {
					if (av_buffersrc_add_frame(fTempoSource, NULL) >= 0)
						_Output(NULL, 0, 0);
				}
				avcodec_flush_buffers(fContext);
				std::lock_guard<std::mutex> lock(fStateLock);
				fFinishedSerial = serial;
			}
			av_packet_unref(fPacket);
			continue;
		}

		int result = avcodec_send_packet(fContext, fPacket);
		av_packet_unref(fPacket);
		if (result < 0 && result != AVERROR(EAGAIN))
			continue;
		while (avcodec_receive_frame(fContext, fFrame) >= 0) {
			_HandleFrame(fFrame);
			av_frame_unref(fFrame);
			if (!_KeepWaiting())
				break;
		}
	}
}


bool
AudioDecoder::_SetupResampler(const AVFrame* frame)
{
	if (fResampler != NULL && fResamplerRate == frame->sample_rate
		&& fResamplerFormat == frame->format
		&& av_channel_layout_compare(&fResamplerLayout, &frame->ch_layout)
			== 0) {
		return true;
	}
	swr_free(&fResampler);
	av_channel_layout_uninit(&fResamplerLayout);

	AVChannelLayout input;
	if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC
		|| frame->ch_layout.nb_channels == 0) {
		av_channel_layout_default(&input,
			frame->ch_layout.nb_channels > 0 ? frame->ch_layout.nb_channels : 2);
	} else
		av_channel_layout_copy(&input, &frame->ch_layout);

	AVChannelLayout output;
	av_channel_layout_default(&output, fOutput->Channels());
	int error = swr_alloc_set_opts2(&fResampler, &output, AV_SAMPLE_FMT_FLT,
		fOutput->SampleRate(), &input, (AVSampleFormat)frame->format,
		frame->sample_rate, 0, NULL);
	av_channel_layout_uninit(&output);
	if (error < 0 || swr_init(fResampler) < 0) {
		swr_free(&fResampler);
		av_channel_layout_uninit(&input);
		return false;
	}
	fResamplerLayout = input;
	fResamplerRate = frame->sample_rate;
	fResamplerFormat = frame->format;
	return true;
}


void
AudioDecoder::_HandleFrame(AVFrame* frame)
{
	if (frame->nb_samples <= 0 || frame->sample_rate <= 0)
		return;
	if (!_SetupResampler(frame))
		return;

	int64 timestamp = frame->best_effort_timestamp;
	if (timestamp == AV_NOPTS_VALUE)
		timestamp = frame->pts;
	bigtime_t pts = timestamp != AV_NOPTS_VALUE
		? to_micros(timestamp, fStream->time_base) - fStartTime : fNextPts;
	if (pts == kNoTime)
		pts = 0;
	bigtime_t frameDuration = (bigtime_t)frame->nb_samples * 1000000
		/ frame->sample_rate;
	fNextPts = pts + frameDuration;

	// An accurate seek: drop what lies before the target.
	int skip = 0;
	{
		std::lock_guard<std::mutex> lock(fStateLock);
		if (fTrimSerial == fSerial && fTrimTime > pts) {
			if (fTrimTime >= pts + frameDuration)
				return;
			skip = (int)((fTrimTime - pts) * frame->sample_rate / 1000000);
		}
	}

	// The time of the first converted sample, allowing for what the
	// resampler still holds from earlier frames.
	int64 delay = swr_get_delay(fResampler, frame->sample_rate);
	bigtime_t outputPts = pts - delay * 1000000 / frame->sample_rate;

	int maxOut = swr_get_out_samples(fResampler, frame->nb_samples) + 32;
	fConverted.resize((size_t)maxOut * fOutput->Channels());
	uint8_t* out[1] = { (uint8_t*)fConverted.data() };
	int converted = swr_convert(fResampler, out, maxOut,
		(const uint8_t**)frame->extended_data, frame->nb_samples);
	if (converted <= 0)
		return;

	const float* samples = fConverted.data();
	if (skip > 0) {
		int skipOut = (int)((int64)skip * fOutput->SampleRate()
			/ frame->sample_rate);
		if (skipOut >= converted)
			return;
		samples += skipOut * fOutput->Channels();
		converted -= skipOut;
		outputPts += (bigtime_t)skipOut * 1000000 / fOutput->SampleRate();
	}
	_Output(samples, converted, outputPts);
}


void
AudioDecoder::_FreeTempo()
{
	avfilter_graph_free(&fTempoGraph);
	fTempoSource = NULL;
	fTempoSink = NULL;
	fTempoSpeed = 1.0;
}


bool
AudioDecoder::_SetupTempo(double speed)
{
	_FreeTempo();
	if (!fTempoAvailable)
		return false;

	fTempoGraph = avfilter_graph_alloc();
	if (fTempoGraph == NULL)
		return false;
	fTempoGraph->nb_threads = 1;

	char arguments[256];
	snprintf(arguments, sizeof(arguments),
		"time_base=1/%d:sample_rate=%d:sample_fmt=flt:channel_layout=%s",
		fOutput->SampleRate(), fOutput->SampleRate(),
		fOutput->Channels() == 1 ? "mono" : "stereo");
	char tempo[64];
	snprintf(tempo, sizeof(tempo), "%.4f", speed);

	AVFilterContext* atempo = NULL;
	if (avfilter_graph_create_filter(&fTempoSource,
			avfilter_get_by_name("abuffer"), "in", arguments, NULL,
			fTempoGraph) < 0
		|| avfilter_graph_create_filter(&fTempoSink,
			avfilter_get_by_name("abuffersink"), "out", NULL, NULL,
			fTempoGraph) < 0
		|| avfilter_graph_create_filter(&atempo,
			avfilter_get_by_name("atempo"), "tempo", tempo, NULL,
			fTempoGraph) < 0
		|| avfilter_link(fTempoSource, 0, atempo, 0) < 0
		|| avfilter_link(atempo, 0, fTempoSink, 0) < 0
		|| avfilter_graph_config(fTempoGraph, NULL) < 0) {
		TRACE("airTime: the tempo filter could not be set up for %g\n", speed);
		_FreeTempo();
		return false;
	}
	TRACE("airTime: tempo filter at %g\n", speed);
	fTempoSpeed = speed;
	fTempoIn = 0;
	fTempoOut = 0;
	return true;
}


void
AudioDecoder::_Output(const float* samples, int frames, bigtime_t pts)
{
	double speed = fSpeed;
	int rate = fOutput->SampleRate();
	int channels = fOutput->Channels();
	auto keepWaiting = [this]() { return _KeepWaiting(); };

	if (samples != NULL && fabs(speed - 1.0) < 0.001) {
		if (fTempoGraph != NULL) {
			// Back to normal speed: what the filter holds is dropped, the
			// clock is set from the new samples anyway.
			_FreeTempo();
		}
		fOutput->Write(samples, frames, pts, 1.0, fSerial, keepWaiting);
		return;
	}

	if (samples != NULL && (fTempoGraph == NULL || fTempoSpeed != speed)) {
		if (!_SetupTempo(speed)) {
			// Without the filter, fast and slow playback stay silent; the
			// silence still carries the clock.
			std::vector<float> silence((size_t)frames * channels, 0.0f);
			int out = (int)(frames / speed);
			if (out <= 0)
				return;
			silence.resize((size_t)out * channels);
			fOutput->Write(silence.data(), out, pts, speed, fSerial,
				keepWaiting);
			return;
		}
		fTempoBasePts = pts;
	}
	if (fTempoGraph == NULL)
		return;

	if (samples != NULL) {
		AVFrame* input = av_frame_alloc();
		if (input == NULL)
			return;
		input->format = AV_SAMPLE_FMT_FLT;
		input->sample_rate = rate;
		av_channel_layout_default(&input->ch_layout, channels);
		input->nb_samples = frames;
		input->pts = fTempoIn;
		if (av_frame_get_buffer(input, 0) < 0) {
			av_frame_free(&input);
			return;
		}
		memcpy(input->data[0], samples, (size_t)frames * channels
			* sizeof(float));
		fTempoIn += frames;
		int added = av_buffersrc_add_frame(fTempoSource, input);
		if (added < 0) {
			char text[128];
			TRACE("airTime: tempo input refused: %s\n",
				av_error_string(added, text, sizeof(text)));
			av_frame_free(&input);
			return;
		}
		av_frame_free(&input);
	}

	while (av_buffersink_get_frame(fTempoSink, fTempoFrame) >= 0) {
		bigtime_t outPts = fTempoBasePts + (bigtime_t)(fTempoOut
			* fTempoSpeed * 1000000.0 / rate);
		fTempoOut += fTempoFrame->nb_samples;
		bool written = fOutput->Write((const float*)fTempoFrame->data[0],
			fTempoFrame->nb_samples, outPts, fTempoSpeed, fSerial,
			keepWaiting);
		av_frame_unref(fTempoFrame);
		if (!written)
			break;
	}
}

}	// namespace airtime
