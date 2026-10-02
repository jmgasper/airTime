/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "FrameRenderer.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <thread>

extern "C" {
#include <libavutil/mastering_display_metadata.h>
}

#include "WorkerPool.h"


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
	fHDR(false),
	fSource(av_frame_alloc()),
	fTarget(av_frame_alloc()),
	fTransferKind(-1),
	fPeak(0)
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


static bool
is_hdr(const AVFrame* frame)
{
	return frame->color_trc == AVCOL_TRC_SMPTE2084
		|| frame->color_trc == AVCOL_TRC_ARIB_STD_B67;
}


bool
FrameRenderer::_Prepare(const AVFrame* frame, int width, int height, bool hdr)
{
	int colorSpace = sws_colorspace(frame);
	AVPixelFormat format = (AVPixelFormat)frame->format;
	int range = frame->color_range == AVCOL_RANGE_JPEG
		|| format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_YUVJ422P
		|| format == AV_PIX_FMT_YUVJ444P ? 1 : 0;

	if (fContext != NULL && fSourceWidth == frame->width
		&& fSourceHeight == frame->height && fSourceFormat == frame->format
		&& fWidth == width && fHeight == height && fColorSpace == colorSpace
		&& fRange == range && fHDR == hdr) {
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
	// High dynamic range goes through 16 bit R'G'B', still in its own
	// transfer and primaries, to be mapped down afterwards.
	av_opt_set_int(fContext, "dst_format",
		hdr ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_BGRA, 0);
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
	fHDR = hdr;
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

	if (is_hdr(frame))
		return _RenderHDR(frame, bits, bytesPerRow, width, height);

	if (!_Prepare(frame, width, height, false))
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

// #pragma mark - high dynamic range


// SMPTE ST 2084 (PQ)
static double
pq_to_nits(double value)
{
	const double m1 = 2610.0 / 16384;
	const double m2 = 2523.0 / 4096 * 128;
	const double c1 = 3424.0 / 4096;
	const double c2 = 2413.0 / 4096 * 32;
	const double c3 = 2392.0 / 4096 * 32;
	double power = pow(value, 1 / m2);
	double numerator = std::max(power - c1, 0.0);
	return 10000 * pow(numerator / (c2 - c3 * power), 1 / m1);
}


// ARIB STD-B67 (HLG): the inverse of the camera's curve, scene light 0-1.
static double
hlg_to_scene(double value)
{
	const double a = 0.17883277;
	const double b = 1 - 4 * a;
	const double c = 0.5 - a * log(4 * a);
	if (value <= 0.5)
		return value * value / 3;
	return (exp((value - c) / a) + b) / 12;
}


static const double kReferenceWhite = 203.0;
	// nits that SDR white stands for (ITU-R BT.2408)


void
FrameRenderer::_PrepareToneMapping(const AVFrame* frame)
{
	// How bright the film gets: the content light level, else the mastering
	// display, else a common 1000 nits.
	double peakNits = 0;
	const AVFrameSideData* light = av_frame_get_side_data(frame,
		AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
	if (light != NULL) {
		const AVContentLightMetadata* metadata
			= (const AVContentLightMetadata*)light->data;
		peakNits = metadata->MaxCLL;
	}
	if (peakNits <= 0) {
		const AVFrameSideData* mastering = av_frame_get_side_data(frame,
			AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
		if (mastering != NULL) {
			const AVMasteringDisplayMetadata* metadata
				= (const AVMasteringDisplayMetadata*)mastering->data;
			if (metadata->has_luminance)
				peakNits = av_q2d(metadata->max_luminance);
		}
	}
	int kind = frame->color_trc;
	if (kind == AVCOL_TRC_ARIB_STD_B67)
		peakNits = 1000;
	if (peakNits <= 0) {
		// Keep what an earlier picture said, if anything did.
		peakNits = fPeak > 0 ? fPeak * kReferenceWhite : 1000;
	}
	peakNits = std::max(peakNits, 400.0);
	float peak = (float)(peakNits / kReferenceWhite);

	if (kind == fTransferKind && fabsf(peak - fPeak) < 0.01f)
		return;
	fTransferKind = kind;
	fPeak = peak;

	fTransfer.resize(65536);
	for (int i = 0; i < 65536; i++) {
		double code = i / 65535.0;
		double light;
		if (kind == AVCOL_TRC_SMPTE2084)
			light = pq_to_nits(code) / kReferenceWhite;
		else
			light = hlg_to_scene(code);
		fTransfer[i] = (float)light;
	}

	if (fEncode.empty()) {
		fEncode.resize(4096);
		for (int i = 0; i < 4096; i++) {
			double linear = i / 4095.0;
			double encoded = linear <= 0.0031308 ? linear * 12.92
				: 1.055 * pow(linear, 1 / 2.4) - 0.055;
			fEncode[i] = (uint8)std::min(255.0, encoded * 255 + 0.5);
		}
	}
}


bool
FrameRenderer::_RenderHDR(const AVFrame* frame, uint8* bits,
	int32 bytesPerRow, int width, int height)
{
	if (!_Prepare(frame, width, height, true))
		return false;
	_PrepareToneMapping(frame);
	if (fPool.get() == NULL)
		fPool.reset(new WorkerPool());

	fLinear.resize((size_t)width * height * 3);
	uint8* target[4] = {(uint8*)fLinear.data(), NULL, NULL, NULL};
	int targetStride[4] = {width * 6, 0, 0, 0};

	av_frame_unref(fTarget);
	fTarget->format = AV_PIX_FMT_RGB48LE;
	fTarget->width = width;
	fTarget->height = height;
	fTarget->data[0] = target[0];
	fTarget->linesize[0] = targetStride[0];
	fTarget->buf[0] = av_buffer_create(target[0], width * 6 * height, no_free,
		NULL, 0);
	av_frame_unref(fSource);
	int result = -1;
	if (fTarget->buf[0] != NULL && av_frame_ref(fSource, frame) >= 0)
		result = sws_scale_frame(fContext, fTarget, fSource);
	av_frame_unref(fSource);
	av_frame_unref(fTarget);
	if (result < 0 && sws_scale(fContext, frame->data, frame->linesize, 0,
			frame->height, target, targetStride) <= 0) {
		return false;
	}

	// BT.2020 to BT.709, in linear light.
	static const float kGamut[9] = {
		1.6605f, -0.5876f, -0.0728f,
		-0.1246f, 1.1329f, -0.0083f,
		-0.0182f, -0.1006f, 1.1187f};
	const float* transfer = fTransfer.data();
	const uint8* encode = fEncode.data();
	const float peak = fPeak;
	const bool hlg = fTransferKind == AVCOL_TRC_ARIB_STD_B67;
	// Linear up to the knee; above it the excess is compressed so that the
	// peak lands on white (Reinhard, extended, on the excess alone, which
	// keeps the slope continuous at the knee).
	const float knee = 0.6f;
	const float excessPeak = std::max((peak - knee) / (1 - knee), 1.01f);
	const float inverseExcessPeakSquared = 1.0f / (excessPeak * excessPeak);
	const uint16* linear = fLinear.data();

	fPool->Run(height, [=](int first, int last) {
		for (int y = first; y < last; y++) {
			const uint16* in = linear + (size_t)y * width * 3;
			uint8* out = bits + y * bytesPerRow;
			for (int x = 0; x < width; x++, in += 3, out += 4) {
				float r = transfer[in[0]];
				float g = transfer[in[1]];
				float b = transfer[in[2]];
				if (hlg) {
					// The display's system gamma (1.2 at 1000 nits).
					float luminance = 0.2627f * r + 0.6780f * g + 0.0593f * b;
					float gain = 1000.0f / 203.0f
						* powf(std::max(luminance, 1e-6f), 0.2f);
					r *= gain;
					g *= gain;
					b *= gain;
				}
				float r709 = kGamut[0] * r + kGamut[1] * g + kGamut[2] * b;
				float g709 = kGamut[3] * r + kGamut[4] * g + kGamut[5] * b;
				float b709 = kGamut[6] * r + kGamut[7] * g + kGamut[8] * b;
				r709 = std::max(r709, 0.0f);
				g709 = std::max(g709, 0.0f);
				b709 = std::max(b709, 0.0f);

				// On the largest channel, so that hues hold.
				float maximum = std::max(r709, std::max(g709, b709));
				if (maximum > knee) {
					float excess = (maximum - knee) / (1 - knee);
					float mapped = knee + (1 - knee) * excess
						* (1 + excess * inverseExcessPeakSquared)
						/ (1 + excess);
					float scale = mapped / maximum;
					r709 *= scale;
					g709 *= scale;
					b709 *= scale;
				}
				out[2] = encode[std::min(4095, (int)(r709 * 4095 + 0.5f))];
				out[1] = encode[std::min(4095, (int)(g709 * 4095 + 0.5f))];
				out[0] = encode[std::min(4095, (int)(b709 * 4095 + 0.5f))];
				out[3] = 255;
			}
		}
	});
	return true;
}

}	// namespace airtime
