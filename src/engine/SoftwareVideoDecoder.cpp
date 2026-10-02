/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "VideoDecoder.h"

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
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
	fHurryApplied(0),
	fDeinterlaceGraph(NULL),
	fDeinterlaceSource(NULL),
	fDeinterlaceSink(NULL),
	fDeinterlaceFields(true),
	fDeinterlaceFailed(false),
	fDeinterlaceWanted(getenv("AIRTIME_NO_DEINTERLACE") == NULL)
{
}


SoftwareVideoDecoder::~SoftwareVideoDecoder()
{
	_FreeDeinterlacer();
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
		if (!fReady.empty()) {
			output = fReady.front();
			fReady.pop_front();
			return B_OK;
		}
		if (!fFinished) {
			int result = avcodec_receive_frame(fContext, fFrame);
			if (result >= 0) {
				fPacketsWithoutFrame = 0;
				if (!fKeyframesOnlyApplied && _Deinterlace(fFrame))
					continue;
				int64 timestamp = fFrame->best_effort_timestamp;
				if (timestamp == AV_NOPTS_VALUE)
					timestamp = fFrame->pts;
				fFrame->pts = timestamp;
				bigtime_t duration = fFrame->duration > 0
					? to_micros(fFrame->duration, fStream->time_base)
					: _FrameDuration();
				output = _MakeFrame(fFrame, fStream->time_base, duration);
				return B_OK;
			}
			if (result == AVERROR_EOF) {
				// What the deinterlacer still holds comes out first.
				if (fDeinterlaceGraph != NULL) {
					if (av_buffersrc_add_frame(fDeinterlaceSource, NULL) >= 0)
						_TakeDeinterlaced();
					_FreeDeinterlacer();
					if (!fReady.empty())
						continue;
				}
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
				_FreeDeinterlacer();
				fReady.clear();
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

VideoFramePtr
SoftwareVideoDecoder::_MakeFrame(AVFrame* picture, AVRational timeBase,
	bigtime_t duration)
{
	VideoFramePtr frame = std::make_shared<VideoFrame>();
	int64 timestamp = picture->pts;
	frame->pts = timestamp != AV_NOPTS_VALUE
		? _MediaTime(to_micros(timestamp, timeBase)) : kNoTime;
	av_frame_move_ref(frame->frame, picture);
	frame->serial = fSerial;
	frame->duration = duration;
	if (frame->duration <= 0 || frame->duration > 1000000)
		frame->duration = _FrameDuration();
	return frame;
}


/*!	Sends an interlaced picture through the deinterlacer and queues what
	comes out. Returns false for a picture to be shown as it is. */
bool
SoftwareVideoDecoder::_Deinterlace(AVFrame* picture)
{
#ifdef AV_FRAME_FLAG_INTERLACED
	bool interlaced = (picture->flags & AV_FRAME_FLAG_INTERLACED) != 0;
#else
	bool interlaced = picture->interlaced_frame != 0;
#endif
	if (!fDeinterlaceWanted || fDeinterlaceFailed)
		return false;
	if (!interlaced && fDeinterlaceGraph == NULL)
		return false;

	// Hurrying: a picture a frame instead of one a field.
	bool fields = fHurryApplied < 2;
	if (fDeinterlaceGraph != NULL && fields != fDeinterlaceFields)
		_FreeDeinterlacer();
	fDeinterlaceFields = fields;
	if (fDeinterlaceGraph == NULL && !_SetupDeinterlacer(picture)) {
		fDeinterlaceFailed = true;
		return false;
	}

	int64 timestamp = picture->best_effort_timestamp;
	if (timestamp == AV_NOPTS_VALUE)
		timestamp = picture->pts;
	picture->pts = timestamp;
	if (av_buffersrc_add_frame(fDeinterlaceSource, picture) < 0) {
		av_frame_unref(picture);
		return true;
	}
	_TakeDeinterlaced();
	return true;
}


void
SoftwareVideoDecoder::_TakeDeinterlaced()
{
	AVRational timeBase = av_buffersink_get_time_base(fDeinterlaceSink);
	bigtime_t duration = fDeinterlaceFields
		? _FrameDuration() / 2 : _FrameDuration();
	for (;;) {
		AVFrame* picture = av_frame_alloc();
		if (picture == NULL)
			return;
		if (av_buffersink_get_frame(fDeinterlaceSink, picture) < 0) {
			av_frame_free(&picture);
			return;
		}
		fReady.push_back(_MakeFrame(picture, timeBase, duration));
		av_frame_free(&picture);
	}
}


bool
SoftwareVideoDecoder::_SetupDeinterlacer(const AVFrame* picture)
{
	fDeinterlaceGraph = avfilter_graph_alloc();
	if (fDeinterlaceGraph == NULL)
		return false;
	unsigned cpus = std::thread::hardware_concurrency();
	fDeinterlaceGraph->nb_threads = cpus > 0 ? (int)std::min(cpus, 8u) : 4;

	AVRational timeBase = fStream->time_base;
	AVRational aspect = picture->sample_aspect_ratio;
	if (aspect.num <= 0 || aspect.den <= 0)
		aspect = (AVRational){1, 1};
	AVRational rate = fStream->avg_frame_rate;
	if (rate.num <= 0 || rate.den <= 0)
		rate = fStream->r_frame_rate;
	if (rate.num <= 0 || rate.den <= 0)
		rate = (AVRational){25, 1};
	char arguments[256];
	snprintf(arguments, sizeof(arguments),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:"
		"frame_rate=%d/%d", picture->width, picture->height, picture->format,
		timeBase.num, timeBase.den, aspect.num, aspect.den, rate.num, rate.den);
	char filter[96];
	snprintf(filter, sizeof(filter), "mode=%s:parity=auto:deint=interlaced",
		fDeinterlaceFields ? "send_field" : "send_frame");

	AVFilterContext* deinterlacer = NULL;
	if (avfilter_graph_create_filter(&fDeinterlaceSource,
			avfilter_get_by_name("buffer"), "in", arguments, NULL,
			fDeinterlaceGraph) < 0
		|| avfilter_graph_create_filter(&deinterlacer,
			avfilter_get_by_name("bwdif"), "deinterlace", filter, NULL,
			fDeinterlaceGraph) < 0
		|| avfilter_graph_create_filter(&fDeinterlaceSink,
			avfilter_get_by_name("buffersink"), "out", NULL, NULL,
			fDeinterlaceGraph) < 0
		|| avfilter_link(fDeinterlaceSource, 0, deinterlacer, 0) < 0
		|| avfilter_link(deinterlacer, 0, fDeinterlaceSink, 0) < 0
		|| avfilter_graph_config(fDeinterlaceGraph, NULL) < 0) {
		fprintf(stderr, "airTime: no deinterlacer for this film\n");
		_FreeDeinterlacer();
		return false;
	}
	return true;
}


void
SoftwareVideoDecoder::_FreeDeinterlacer()
{
	avfilter_graph_free(&fDeinterlaceGraph);
	fDeinterlaceSource = NULL;
	fDeinterlaceSink = NULL;
}

}	// namespace airtime
