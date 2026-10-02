/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_VIDEO_DECODER_H
#define AIRTIME_VIDEO_DECODER_H


#include <atomic>
#include <deque>

#include <String.h>

#include "PacketQueue.h"
#include "VideoFrame.h"


namespace airtime {

/*!	Turns the packets of one video stream into pictures. A decoder pulls its
	packets from the queue itself, because a hardware decoder add-on asks for
	them from inside its own Decode().

	Decode() returns
	- B_OK with a frame,
	- B_LAST_BUFFER_ERROR once everything up to the end of the stream has been
	  delivered (until a seek brings packets with a new serial),
	- B_INTERRUPTED when the queue was flushed or woken; call it again,
	- B_CANCELED when the queue was aborted,
	- anything else when the decoder cannot go on; switch to another.
*/
class VideoDecoder {
public:
								VideoDecoder(AVStream* stream,
									PacketQueue* queue, bigtime_t startTime);
	virtual						~VideoDecoder();

	virtual	BString				Name() const = 0;
	virtual	bool				IsHardware() const = 0;

	virtual	status_t			Decode(VideoFramePtr& frame) = 0;

			int					Serial() const { return fSerial; }

			// Pictures before this media time need not be converted to
			// pixels; they will not be shown (an accurate seek).
			void				SetSkipBefore(bigtime_t time)
									{ fSkipBefore = time; }
			// Only key frames are wanted (fast forward and rewind).
			void				SetKeyframesOnly(bool only)
									{ fKeyframesOnly = only; }
			// How much work to leave out to keep up, when pictures come out
			// later than they should be shown: 0 none, 1 no deblocking of
			// pictures nothing refers to, 2 no such pictures at all, 3 no
			// deblocking at all, 4 key frames only. Decoders that cannot
			// leave anything out ignore it.
			void				SetHurry(int level) { fHurry = level; }

			// How many packets went in without a picture coming out; a
			// hardware decoder that only swallows packets is replaced.
			int					PacketsWithoutFrame() const
									{ return fPacketsWithoutFrame; }

protected:
			bigtime_t			_MediaTime(bigtime_t absolute) const;
			bigtime_t			_FrameDuration() const;

			AVStream*			fStream;
			PacketQueue*		fQueue;
			bigtime_t			fStartTime;
			int					fSerial;
			std::atomic<bigtime_t> fSkipBefore;
			std::atomic<bool>	fKeyframesOnly;
			std::atomic<int>	fHurry;
			int					fPacketsWithoutFrame;
};


class SoftwareVideoDecoder : public VideoDecoder {
public:
								SoftwareVideoDecoder(AVStream* stream,
									PacketQueue* queue, bigtime_t startTime);
	virtual						~SoftwareVideoDecoder();

			status_t			Init(BString* reason);

	virtual	BString				Name() const;
	virtual	bool				IsHardware() const { return false; }
	virtual	status_t			Decode(VideoFramePtr& frame);

private:
			VideoFramePtr		_MakeFrame(AVFrame* picture, AVRational timeBase,
									bigtime_t duration);
			bool				_Deinterlace(AVFrame* picture);
			bool				_SetupDeinterlacer(const AVFrame* picture);
			void				_FreeDeinterlacer();
			void				_TakeDeinterlaced();

			AVCodecContext*		fContext;
			AVPacket*			fPacket;
			AVFrame*			fFrame;
			bool				fPacketPending;
			bool				fDraining;
			bool				fFinished;
			bool				fKeyframesOnlyApplied;
			int					fHurryApplied;

			// Interlaced pictures go through libavfilter's bwdif, a picture
			// a field, or a picture a frame when the decoder is hurrying.
			AVFilterGraph*		fDeinterlaceGraph;
			AVFilterContext*	fDeinterlaceSource;
			AVFilterContext*	fDeinterlaceSink;
			bool				fDeinterlaceFields;
			bool				fDeinterlaceFailed;
			bool				fDeinterlaceWanted;
			std::deque<VideoFramePtr> fReady;
};


struct HardwareDecoderAddOn;

/*!	A decoder add-on of Haiku's Media Kit that hands the work to the
	graphics hardware: NVDEC on GeForce cards, the RK3588's video decoders
	through Rockchip MPP. It is loaded directly rather than through the Media
	Kit's lookup, so that airTime can choose it, and fall back to libavcodec
	when it refuses a stream.
*/
VideoDecoder* create_hardware_decoder(AVStream* stream, PacketQueue* queue,
	bigtime_t startTime, BString* reason);

// A human readable list of the hardware decoders installed, for the
// inspector: "NVDEC (H.264)".
BString hardware_decoder_summary();

}	// namespace airtime

#endif	// AIRTIME_VIDEO_DECODER_H
