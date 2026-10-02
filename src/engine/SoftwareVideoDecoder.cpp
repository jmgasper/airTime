/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "VideoDecoder.h"

#include <stdio.h>
#include <thread>


namespace airtime {


VideoDecoder::VideoDecoder(AVStream* stream, PacketQueue* queue,
	bigtime_t startTime)
	:
	fStream(stream),
	fQueue(queue),
	fStartTime(startTime),
	fSerial(-1),
	fSkipBefore(0),
	fKeyframesOnly(false),
	fHurry(0),
	fPacketsWithoutFrame(0)
{
}


VideoDecoder::~VideoDecoder()
{
}


bigtime_t
VideoDecoder::_MediaTime(bigtime_t absolute) const
{
	if (absolute == kNoTime)
		return kNoTime;
	return absolute - fStartTime;
}


bigtime_t
VideoDecoder::_FrameDuration() const
{
	AVRational rate = fStream->avg_frame_rate;
	if (rate.num <= 0 || rate.den <= 0)
		rate = fStream->r_frame_rate;
	if (rate.num <= 0 || rate.den <= 0)
		return 40000;
	bigtime_t duration = (bigtime_t)(1000000.0 * rate.den / rate.num);
	if (duration <= 0 || duration > 1000000)
		return 40000;
	return duration;
}


// #pragma mark - SoftwareVideoDecoder


SoftwareVideoDecoder::SoftwareVideoDecoder(AVStream* stream,
	PacketQueue* queue, bigtime_t startTime)
	:
	VideoDecoder(stream, queue, startTime),
	fContext(NULL),
	fPacket(av_packet_alloc()),
	fFrame(av_frame_alloc()),
	fPacketPending(false),
	fDraining(false),
	fFinished(false),
	fKeyframesOnlyApplied(false),
	fHurryApplied(0)
{
}


SoftwareVideoDecoder::~SoftwareVideoDecoder()
{
	avcodec_free_context(&fContext);
	av_packet_free(&fPacket);
	av_frame_free(&fFrame);
}


status_t
SoftwareVideoDecoder::Init(BString* reason)
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
	if (avcodec_parameters_to_context(fContext, fStream->codecpar) < 0) {
		reason->SetTo("the stream parameters were refused");
		return B_ERROR;
	}
	fContext->pkt_timebase = fStream->time_base;

	// Frame threads give the most throughput; one per processor up to
	// sixteen, which is where libavcodec stops gaining.
	unsigned cpus = std::thread::hardware_concurrency();
	if (cpus == 0)
		cpus = 4;
	fContext->thread_count = cpus > 16 ? 16 : (int)cpus;
	fContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

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
SoftwareVideoDecoder::Name() const
{
	BString name;
	if (fContext != NULL && fContext->codec != NULL)
		name.SetToFormat("libavcodec (%s)", fContext->codec->name);
	else
		name = "libavcodec";
	return name;
}


status_t
SoftwareVideoDecoder::Decode(VideoFramePtr& output)
{
	if (fKeyframesOnly != fKeyframesOnlyApplied || fHurry != fHurryApplied) {
		fKeyframesOnlyApplied = fKeyframesOnly;
		fHurryApplied = fHurry;
		AVDiscard skipFrame = AVDISCARD_DEFAULT;
		AVDiscard skipFilter = AVDISCARD_DEFAULT;
		if (fKeyframesOnlyApplied || fHurryApplied >= 4)
			skipFrame = AVDISCARD_NONKEY;
		else if (fHurryApplied >= 2)
			skipFrame = AVDISCARD_NONREF;
		if (fHurryApplied >= 3)
			skipFilter = AVDISCARD_ALL;
		else if (fHurryApplied >= 1)
			skipFilter = AVDISCARD_NONREF;
		fContext->skip_frame = skipFrame;
		fContext->skip_loop_filter = skipFilter;
	}

	for (;;) {
		if (!fFinished) {
			int result = avcodec_receive_frame(fContext, fFrame);
			if (result >= 0) {
				fPacketsWithoutFrame = 0;
				int64 timestamp = fFrame->best_effort_timestamp;
				if (timestamp == AV_NOPTS_VALUE)
					timestamp = fFrame->pts;
				bigtime_t pts = _MediaTime(to_micros(timestamp,
					fStream->time_base));

				VideoFramePtr frame = std::make_shared<VideoFrame>();
				av_frame_move_ref(frame->frame, fFrame);
				frame->pts = pts;
				frame->serial = fSerial;
				frame->duration = frame->frame->duration > 0
					? to_micros(frame->frame->duration, fStream->time_base)
					: _FrameDuration();
				if (frame->duration <= 0 || frame->duration > 1000000)
					frame->duration = _FrameDuration();
				output = frame;
				return B_OK;
			}
			if (result == AVERROR_EOF) {
				fFinished = true;
				fDraining = false;
				avcodec_flush_buffers(fContext);
				return B_LAST_BUFFER_ERROR;
			}
			if (result != AVERROR(EAGAIN)) {
				// A broken picture; libavcodec carries on with the next.
			}
		}

		if (!fPacketPending) {
			int serial;
			int got = fQueue->Get(fPacket, &serial);
			if (got < 0)
				return B_CANCELED;
			if (got == 0)
				return B_INTERRUPTED;
			if (serial != fSerial) {
				avcodec_flush_buffers(fContext);
				fSerial = serial;
				fFinished = false;
				fDraining = false;
				fPacketsWithoutFrame = 0;
			} else if (fFinished) {
				// Packets after the end, before any seek: nothing to do.
				av_packet_unref(fPacket);
				continue;
			}
			fPacketPending = true;
		}

		if (fPacket->data == NULL) {
			// The end of the stream: let the decoder give up what it holds.
			if (!fDraining) {
				avcodec_send_packet(fContext, NULL);
				fDraining = true;
			}
			fPacketPending = false;
			av_packet_unref(fPacket);
			continue;
		}

		int result = avcodec_send_packet(fContext, fPacket);
		if (result == AVERROR(EAGAIN)) {
			// Take frames out first; the packet stays pending.
			continue;
		}
		fPacketsWithoutFrame++;
		fPacketPending = false;
		av_packet_unref(fPacket);
	}
}

}	// namespace airtime
