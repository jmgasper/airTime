/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "FrameRenderer.h"

#include <stdio.h>
#include <string.h>
#include <thread>


namespace airtime {


static void
no_free(void* opaque, uint8_t* data)
{
}


FrameRenderer::FrameRenderer()
	:
	fContext(NULL),
	fSourceWidth(0),
	fSourceHeight(0),
	fSourceFormat(-1),
	fColorSpace(-1),
	fRange(-1),
	fWidth(0),
	fHeight(0),
	fSource(av_frame_alloc()),
	fTarget(av_frame_alloc())
{
}


FrameRenderer::~FrameRenderer()
{
	sws_freeContext(fContext);
	av_frame_free(&fSource);
	av_frame_free(&fTarget);
}


static int
sws_colorspace(const AVFrame* frame)
{
	switch (frame->colorspace) {
		case AVCOL_SPC_BT709:
			return SWS_CS_ITU709;
		case AVCOL_SPC_BT470BG:
		case AVCOL_SPC_SMPTE170M:
			return SWS_CS_ITU601;
		case AVCOL_SPC_SMPTE240M:
			return SWS_CS_SMPTE240M;
		case AVCOL_SPC_FCC:
			return SWS_CS_FCC;
		case AVCOL_SPC_BT2020_NCL:
		case AVCOL_SPC_BT2020_CL:
			return SWS_CS_BT2020;
		default:
			// Unlabelled: high definition is 709, the rest 601.
			return frame->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
	}
}


bool
FrameRenderer::_Prepare(const AVFrame* frame, int width, int height)
{
	int colorSpace = sws_colorspace(frame);
	AVPixelFormat format = (AVPixelFormat)frame->format;
	int range = frame->color_range == AVCOL_RANGE_JPEG
		|| format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_YUVJ422P
		|| format == AV_PIX_FMT_YUVJ444P ? 1 : 0;

	if (fContext != NULL && fSourceWidth == frame->width
		&& fSourceHeight == frame->height && fSourceFormat == frame->format
		&& fWidth == width && fHeight == height && fColorSpace == colorSpace
		&& fRange == range) {
		return true;
	}
	sws_freeContext(fContext);
	fContext = sws_alloc_context();
	if (fContext == NULL)
		return false;

	// Bicubic when enlarging looks better; area averaging when shrinking a
	// lot keeps fine detail from flickering.
	int flags = SWS_BICUBIC;
	if (width < frame->width / 2)
		flags = SWS_AREA;
	else if (width <= frame->width)
		flags = SWS_BILINEAR;

	unsigned cpus = std::thread::hardware_concurrency();
	int threads = cpus > 8 ? 8 : cpus > 0 ? (int)cpus : 4;
	av_opt_set_int(fContext, "srcw", frame->width, 0);
	av_opt_set_int(fContext, "srch", frame->height, 0);
	av_opt_set_int(fContext, "src_format", frame->format, 0);
	av_opt_set_int(fContext, "dstw", width, 0);
	av_opt_set_int(fContext, "dsth", height, 0);
	av_opt_set_int(fContext, "dst_format", AV_PIX_FMT_BGRA, 0);
	av_opt_set_int(fContext, "sws_flags", flags, 0);
	av_opt_set_int(fContext, "threads", threads, 0);
	if (sws_init_context(fContext, NULL, NULL) < 0) {
		sws_freeContext(fContext);
		fContext = NULL;
		return false;
	}
	const int* coefficients = sws_getCoefficients(colorSpace);
	sws_setColorspaceDetails(fContext, coefficients, range,
		sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);

	fSourceWidth = frame->width;
	fSourceHeight = frame->height;
	fSourceFormat = frame->format;
	fWidth = width;
	fHeight = height;
	fColorSpace = colorSpace;
	fRange = range;
	return true;
}


bool
FrameRenderer::Render(const AVFrame* frame, uint8* bits, int32 bytesPerRow,
	int width, int height)
{
	if (frame == NULL || frame->width <= 0 || frame->height <= 0
		|| width <= 0 || height <= 0) {
		return false;
	}

	// Pixels from a hardware decoder at the size they are shown at: copy.
	if (frame->format == AV_PIX_FMT_BGRA && frame->width == width
		&& frame->height == height) {
		for (int y = 0; y < height; y++) {
			memcpy(bits + y * bytesPerRow,
				frame->data[0] + y * frame->linesize[0], width * 4);
		}
		return true;
	}

	if (!_Prepare(frame, width, height))
		return false;

	// swscale's threads only work through sws_scale_frame(), which wants
	// reference counted frames on both sides.
	av_frame_unref(fTarget);
	fTarget->format = AV_PIX_FMT_BGRA;
	fTarget->width = width;
	fTarget->height = height;
	fTarget->data[0] = bits;
	fTarget->linesize[0] = bytesPerRow;
	fTarget->buf[0] = av_buffer_create(bits, bytesPerRow * height, no_free,
		NULL, 0);
	if (fTarget->buf[0] == NULL)
		return false;

	av_frame_unref(fSource);
	if (av_frame_ref(fSource, frame) < 0) {
		av_frame_unref(fTarget);
		return false;
	}
	int result = sws_scale_frame(fContext, fTarget, fSource);
	av_frame_unref(fSource);
	av_frame_unref(fTarget);
	if (result < 0) {
		// Fall back to the plain, single threaded call.
		const uint8* const* source = frame->data;
		uint8* target[4] = {bits, NULL, NULL, NULL};
		int targetStride[4] = {bytesPerRow, 0, 0, 0};
		return sws_scale(fContext, source, frame->linesize, 0, frame->height,
			target, targetStride) > 0;
	}
	return true;
}

}	// namespace airtime
