/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "SdrScaler.h"

#include <algorithm>
#include <math.h>
#include <string.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif


namespace airtime {


static const int kBlock = 16;
static const int kWindow = 64;
	// source samples sixteen target samples can draw on together
static const double kMaxScale = 2.0;


static inline int16
saturate16(int value)
{
	return (int16)std::max(-32768, std::min(32767, value));
}


// vqrdmulh
static inline int16
multiply_high(int16 a, int16 b)
{
	return saturate16((2 * (int)a * b + 32768) >> 16);
}


static inline uint8
to_pixel(int value)
{
	// vqrshrun by 6
	return (uint8)std::max(0, std::min(255, (value + 32) >> 6));
}


/*!	out = a * (128 - weight) + b * weight, in 128ths, rounded. */
static void
blend_rows(const uint8* a, const uint8* b, int weight, int count, uint8* out,
	bool vectors)
{
	if (weight == 0) {
		memcpy(out, a, count);
		return;
	}
	int x = 0;
#if defined(__aarch64__)
	if (vectors) {
		const uint8x8_t second = vdup_n_u8((uint8)weight);
		const uint8x8_t first = vdup_n_u8((uint8)(128 - weight));
		for (; x + 16 <= count; x += 16) {
			uint8x16_t top = vld1q_u8(a + x);
			uint8x16_t bottom = vld1q_u8(b + x);
			uint16x8_t low = vmull_u8(vget_low_u8(top), first);
			low = vmlal_u8(low, vget_low_u8(bottom), second);
			uint16x8_t high = vmull_u8(vget_high_u8(top), first);
			high = vmlal_u8(high, vget_high_u8(bottom), second);
			vst1q_u8(out + x, vcombine_u8(vrshrn_n_u16(low, 7),
				vrshrn_n_u16(high, 7)));
		}
	}
#endif
	for (; x < count; x++)
		out[x] = (uint8)((a[x] * (128 - weight) + b[x] * weight + 64) >> 7);
}


bool
SdrScaler::Handles(const AVFrame* frame, int width, int height)
{
	if (frame->format != AV_PIX_FMT_YUV420P
		&& frame->format != AV_PIX_FMT_YUVJ420P
		&& frame->format != AV_PIX_FMT_NV12) {
		return false;
	}
	// A picture shown at its own size is only converted, for which swscale
	// has vector code of its own that is twice as fast as scaling by one.
	if (frame->width == width && frame->height == height
		&& frame->format != AV_PIX_FMT_NV12) {
		return false;
	}
	return frame->width >= 2 && frame->height >= 2 && width >= 1
		&& height >= 1 && frame->width <= kMaxScale * width
		&& frame->height <= kMaxScale * height;
}


/*!	Target sample i is centred on source position (i + 0.5) * scale +
	offset, the edge samples standing in for those beyond the picture. */
void
SdrScaler::_MakeRows(Rows& rows, int sourceSize, int targetSize, double scale,
	double offset)
{
	rows.first.resize(targetSize);
	rows.second.resize(targetSize);
	rows.weight.resize(targetSize);
	for (int i = 0; i < targetSize; i++) {
		double position = (i + 0.5) * scale + offset;
		position = std::max(0.0, std::min(position, sourceSize - 1.0));
		int first = (int)floor(position);
		int weight = (int)lround((position - first) * 128);
		int second = std::min(first + 1, sourceSize - 1);
		if (weight == 128) {
			first = second;
			weight = 0;
		}
		rows.first[i] = first;
		rows.second[i] = second;
		rows.weight[i] = (uint8)weight;
	}
}


void
SdrScaler::_MakeColumns(Columns& columns, int sourceSize, int targetSize,
	double scale, double offset)
{
	Rows samples;
	_MakeRows(samples, sourceSize, targetSize, scale, offset);

	int blocks = (targetSize + kBlock - 1) / kBlock;
	columns.base.assign(blocks, 0);
	columns.first.assign((size_t)blocks * kBlock, 0);
	columns.second.assign((size_t)blocks * kBlock, 0);
	columns.weight.assign((size_t)blocks * kBlock, 0);
	for (int block = 0; block < blocks; block++) {
		int base = samples.first[block * kBlock];
		columns.base[block] = base;
		for (int lane = 0; lane < kBlock; lane++) {
			int i = block * kBlock + lane;
			if (i >= targetSize)
				break;
			size_t at = (size_t)block * kBlock + lane;
			columns.first[at]
				= (uint8)std::min(samples.first[i] - base, kWindow - 1);
			columns.second[at]
				= (uint8)std::min(samples.second[i] - base, kWindow - 1);
			columns.weight[at] = samples.weight[i];
		}
	}
}


bool
SdrScaler::Prepare(const AVFrame* frame, int width, int height)
{
	// Unlabelled: high definition is 709, the rest 601.
	int colorSpace = frame->colorspace;
	if (colorSpace != AVCOL_SPC_BT709 && colorSpace != AVCOL_SPC_BT470BG
		&& colorSpace != AVCOL_SPC_SMPTE170M
		&& colorSpace != AVCOL_SPC_BT2020_NCL
		&& colorSpace != AVCOL_SPC_BT2020_CL) {
		colorSpace = frame->height >= 720 ? AVCOL_SPC_BT709
			: AVCOL_SPC_SMPTE170M;
	}
	int range = frame->color_range == AVCOL_RANGE_JPEG
		|| frame->format == AV_PIX_FMT_YUVJ420P ? 1 : 0;

	if (fFormat == frame->format && fSourceWidth == frame->width
		&& fSourceHeight == frame->height && fWidth == width
		&& fHeight == height && fChromaLocation == frame->chroma_location
		&& fColorSpace == colorSpace && fColorRange == range) {
		return true;
	}

	// Where chroma sits between the luma samples: (MPEG-2's) left of a
	// pair and halfway down, unless the stream says otherwise.
	double across = -0.25;
	double down = -0.5;
	switch (frame->chroma_location) {
		case AVCHROMA_LOC_CENTER:
			across = -0.5;
			break;
		case AVCHROMA_LOC_TOPLEFT:
			down = -0.25;
			break;
		case AVCHROMA_LOC_TOP:
			across = -0.5;
			down = -0.25;
			break;
		case AVCHROMA_LOC_BOTTOMLEFT:
			down = -0.75;
			break;
		case AVCHROMA_LOC_BOTTOM:
			across = -0.5;
			down = -0.75;
			break;
		default:
			break;
	}
	double scaleX = (double)frame->width / width;
	double scaleY = (double)frame->height / height;
	_MakeColumns(fLumaColumns, frame->width, width, scaleX, -0.5);
	_MakeColumns(fChromaColumns, (frame->width + 1) / 2, width, scaleX / 2,
		across);
	_MakeRows(fLumaRows, frame->height, height, scaleY, -0.5);
	_MakeRows(fChromaRows, (frame->height + 1) / 2, height, scaleY / 2, down);

	double kr = 0.2126;
	double kb = 0.0722;
	if (colorSpace == AVCOL_SPC_BT470BG || colorSpace == AVCOL_SPC_SMPTE170M) {
		kr = 0.299;
		kb = 0.114;
	} else if (colorSpace != AVCOL_SPC_BT709) {
		kr = 0.2627;
		kb = 0.0593;
	}
	double kg = 1 - kr - kb;
	double lumaScale = range != 0 ? 1.0 : 255.0 / 219;
	double chromaScale = range != 0 ? 1.0 : 255.0 / 224;
	// Luma goes in times 128 and chroma times 256; the results are 64ths.
	fLumaOffset = range != 0 ? 0 : 16;
	fLumaFactor = (int16)lround(lumaScale * 16384);
	fRedCr = (int16)lround(2 * (1 - kr) * chromaScale * 8192);
	fGreenCb = (int16)lround(-2 * kb * (1 - kb) / kg * chromaScale * 8192);
	fGreenCr = (int16)lround(-2 * kr * (1 - kr) / kg * chromaScale * 8192);
	fBlueCb = (int16)lround(2 * (1 - kb) * chromaScale * 8192);

	fFormat = frame->format;
	fSourceWidth = frame->width;
	fSourceHeight = frame->height;
	fWidth = width;
	fHeight = height;
	fChromaLocation = frame->chroma_location;
	fColorSpace = colorSpace;
	fColorRange = range;
	return true;
}


void
SdrScaler::RenderRow(const AVFrame* frame, int y, uint8* out,
	Scratch& scratch, bool vectors) const
{
	// The rows this one lies between, as one row each; the horizontal pass
	// reads up to a window past its last sample.
	int pairs = (fSourceWidth + 1) / 2;
	scratch.luma.resize(fSourceWidth + kWindow);
	scratch.cb.resize(pairs + kWindow);
	scratch.cr.resize(pairs + kWindow);

	blend_rows(frame->data[0] + (size_t)fLumaRows.first[y] * frame->linesize[0],
		frame->data[0] + (size_t)fLumaRows.second[y] * frame->linesize[0],
		fLumaRows.weight[y], fSourceWidth, scratch.luma.data(), vectors);

	int first = fChromaRows.first[y];
	int second = fChromaRows.second[y];
	int weight = fChromaRows.weight[y];
	if (fFormat == AV_PIX_FMT_NV12) {
		scratch.pairs.resize(2 * pairs + kWindow);
		blend_rows(frame->data[1] + (size_t)first * frame->linesize[1],
			frame->data[1] + (size_t)second * frame->linesize[1], weight,
			2 * pairs, scratch.pairs.data(), vectors);
		const uint8* both = scratch.pairs.data();
		uint8* cb = scratch.cb.data();
		uint8* cr = scratch.cr.data();
		int x = 0;
#if defined(__aarch64__)
		for (; vectors && x + 16 <= pairs; x += 16) {
			uint8x16x2_t split = vld2q_u8(both + 2 * x);
			vst1q_u8(cb + x, split.val[0]);
			vst1q_u8(cr + x, split.val[1]);
		}
#endif
		for (; x < pairs; x++) {
			cb[x] = both[2 * x];
			cr[x] = both[2 * x + 1];
		}
	} else {
		blend_rows(frame->data[1] + (size_t)first * frame->linesize[1],
			frame->data[1] + (size_t)second * frame->linesize[1], weight,
			pairs, scratch.cb.data(), vectors);
		blend_rows(frame->data[2] + (size_t)first * frame->linesize[2],
			frame->data[2] + (size_t)second * frame->linesize[2], weight,
			pairs, scratch.cr.data(), vectors);
	}

	const uint8* luma = scratch.luma.data();
	const uint8* cb = scratch.cb.data();
	const uint8* cr = scratch.cr.data();
	int blocks = (fWidth + kBlock - 1) / kBlock;
	int block = 0;

#if defined(__aarch64__)
	if (vectors) {
		const uint8x16_t whole = vdupq_n_u8(128);
		const int16x8_t lumaOffset = vdupq_n_s16(fLumaOffset);
		const int16x8_t chromaOffset = vdupq_n_s16(128);
		const int16x8_t lumaFactor = vdupq_n_s16(fLumaFactor);
		const int16x8_t redCr = vdupq_n_s16(fRedCr);
		const int16x8_t greenCb = vdupq_n_s16(fGreenCb);
		const int16x8_t greenCr = vdupq_n_s16(fGreenCr);
		const int16x8_t blueCb = vdupq_n_s16(fBlueCb);

		for (; block < blocks; block++) {
			// sixteen samples of each of the three, picked out of a window
			// of the source row by table lookups
			uint8x16_t samples[3];
			for (int plane = 0; plane < 3; plane++) {
				const Columns& columns = plane == 0
					? fLumaColumns : fChromaColumns;
				const uint8* window = (plane == 0 ? luma : plane == 1
					? cb : cr) + columns.base[block];
				size_t at = (size_t)block * kBlock;
				uint8x16x4_t table;
				table.val[0] = vld1q_u8(window);
				table.val[1] = vld1q_u8(window + 16);
				table.val[2] = vld1q_u8(window + 32);
				table.val[3] = vld1q_u8(window + 48);
				uint8x16_t left = vqtbl4q_u8(table,
					vld1q_u8(&columns.first[at]));
				uint8x16_t right = vqtbl4q_u8(table,
					vld1q_u8(&columns.second[at]));
				uint8x16_t weight = vld1q_u8(&columns.weight[at]);
				uint8x16_t rest = vsubq_u8(whole, weight);
				uint16x8_t low = vmull_u8(vget_low_u8(left),
					vget_low_u8(rest));
				low = vmlal_u8(low, vget_low_u8(right), vget_low_u8(weight));
				uint16x8_t high = vmull_high_u8(left, rest);
				high = vmlal_high_u8(high, right, weight);
				samples[plane] = vcombine_u8(vrshrn_n_u16(low, 7),
					vrshrn_n_u16(high, 7));
			}

			uint8x16x4_t pixels;
			uint8x8_t channels[3][2];
			for (int half = 0; half < 2; half++) {
				uint8x8_t y8 = half == 0 ? vget_low_u8(samples[0])
					: vget_high_u8(samples[0]);
				uint8x8_t u8 = half == 0 ? vget_low_u8(samples[1])
					: vget_high_u8(samples[1]);
				uint8x8_t v8 = half == 0 ? vget_low_u8(samples[2])
					: vget_high_u8(samples[2]);
				int16x8_t l = vqrdmulhq_s16(vshlq_n_s16(vsubq_s16(
					vreinterpretq_s16_u16(vmovl_u8(y8)), lumaOffset), 7),
					lumaFactor);
				int16x8_t u = vshlq_n_s16(vsubq_s16(
					vreinterpretq_s16_u16(vmovl_u8(u8)), chromaOffset), 8);
				int16x8_t v = vshlq_n_s16(vsubq_s16(
					vreinterpretq_s16_u16(vmovl_u8(v8)), chromaOffset), 8);
				channels[0][half] = vqrshrun_n_s16(vqaddq_s16(l,
					vqrdmulhq_s16(u, blueCb)), 6);
				channels[1][half] = vqrshrun_n_s16(vqaddq_s16(l,
					vqaddq_s16(vqrdmulhq_s16(u, greenCb),
						vqrdmulhq_s16(v, greenCr))), 6);
				channels[2][half] = vqrshrun_n_s16(vqaddq_s16(l,
					vqrdmulhq_s16(v, redCr)), 6);
			}
			pixels.val[0] = vcombine_u8(channels[0][0], channels[0][1]);
			pixels.val[1] = vcombine_u8(channels[1][0], channels[1][1]);
			pixels.val[2] = vcombine_u8(channels[2][0], channels[2][1]);
			pixels.val[3] = vdupq_n_u8(255);

			if ((block + 1) * kBlock <= fWidth)
				vst4q_u8(out + (size_t)block * kBlock * 4, pixels);
			else {
				uint8 last[kBlock * 4];
				vst4q_u8(last, pixels);
				memcpy(out + (size_t)block * kBlock * 4, last,
					(size_t)(fWidth - block * kBlock) * 4);
			}
		}
		return;
	}
#endif

	for (; block < blocks; block++) {
		for (int lane = 0; lane < kBlock; lane++) {
			int x = block * kBlock + lane;
			if (x >= fWidth)
				break;
			size_t at = (size_t)block * kBlock + lane;
			int sample[3];
			for (int plane = 0; plane < 3; plane++) {
				const Columns& columns = plane == 0
					? fLumaColumns : fChromaColumns;
				const uint8* window = (plane == 0 ? luma : plane == 1
					? cb : cr) + columns.base[block];
				int weight = columns.weight[at];
				sample[plane] = (window[columns.first[at]] * (128 - weight)
					+ window[columns.second[at]] * weight + 64) >> 7;
			}
			int16 l = multiply_high((int16)((sample[0] - fLumaOffset) * 128),
				fLumaFactor);
			int16 u = (int16)((sample[1] - 128) * 256);
			int16 v = (int16)((sample[2] - 128) * 256);
			uint8* pixel = out + (size_t)x * 4;
			pixel[0] = to_pixel(saturate16(l + multiply_high(u, fBlueCb)));
			pixel[1] = to_pixel(saturate16(l + saturate16(
				multiply_high(u, fGreenCb) + multiply_high(v, fGreenCr))));
			pixel[2] = to_pixel(saturate16(l + multiply_high(v, fRedCr)));
			pixel[3] = 255;
		}
	}
}

}	// namespace airtime
