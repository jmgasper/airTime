/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_FFMPEG_H
#define AIRTIME_FFMPEG_H

// The FFmpeg libraries are C; everything airTime uses comes in through here.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <OS.h>
#include <SupportDefs.h>


namespace airtime {

// av_err2str() is a C99 compound literal and does not compile as C++.
inline const char*
av_error_string(int error, char* buffer, size_t size)
{
	av_strerror(error, buffer, size);
	return buffer;
}


// Media time is kept in microseconds everywhere, like bigtime_t.
inline bigtime_t
to_micros(int64 timestamp, AVRational timeBase)
{
	if (timestamp == AV_NOPTS_VALUE)
		return B_INFINITE_TIMEOUT;
	return av_rescale_q(timestamp, timeBase, AVRational{1, 1000000});
}


inline int64
from_micros(bigtime_t time, AVRational timeBase)
{
	return av_rescale_q(time, AVRational{1, 1000000}, timeBase);
}


const bigtime_t kNoTime = B_INFINITE_TIMEOUT;

}	// namespace airtime

#endif	// AIRTIME_FFMPEG_H
