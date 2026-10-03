/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 *
 * The HDR picture path: a synthetic PQ film, BT.2020, made into pixels by
 * the ten-bit scaler and the tone mapping, against the same arithmetic done
 * in double precision on the scaler's output (the scaler itself is checked
 * against swscale in the engine tests). Timed against swscale's path too:
 *   renderer_tests [repetitions]
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "FrameRenderer.h"
#include "YuvScaler.h"

using namespace airtime;


static AVFrame*
make_frame(AVPixelFormat format, int width, int height)
{
	AVFrame* frame = av_frame_alloc();
	frame->format = format;
	frame->width = width;
	frame->height = height;
	frame->color_trc = AVCOL_TRC_SMPTE2084;
	frame->color_primaries = AVCOL_PRI_BT2020;
	frame->colorspace = AVCOL_SPC_BT2020_NCL;
	frame->color_range = AVCOL_RANGE_MPEG;
	if (av_frame_get_buffer(frame, 0) != 0)
		return NULL;
	AVContentLightMetadata* light = av_content_light_metadata_create_side_data(
		frame);
	light->MaxCLL = 1000;
	bool p010 = format == AV_PIX_FMT_P010LE;
	int shift = p010 ? 6 : 0;
	for (int y = 0; y < height; y++) {
		uint16_t* row = (uint16_t*)(frame->data[0] + y * frame->linesize[0]);
		for (int x = 0; x < width; x++) {
			// Dark to beyond SDR white, with detail.
			int code = 64 + (int)(800.0 * x / width
				+ 60 * sin(y / 9.0) * sin(x / 13.0));
			row[x] = (uint16_t)(std::max(64, std::min(940, code)) << shift);
		}
	}
	for (int y = 0; y < height / 2; y++) {
		uint16_t* cb = (uint16_t*)(frame->data[1] + y * frame->linesize[1]);
		uint16_t* cr = (uint16_t*)(frame->data[p010 ? 1 : 2]
			+ y * frame->linesize[p010 ? 1 : 2]);
		for (int x = 0; x < width / 2; x++) {
			int u = 512 + (int)(100 * sin(x / 50.0 + y / 70.0));
			int v = 512 + (int)(100 * cos(y / 40.0));
			if (p010) {
				cb[2 * x] = (uint16_t)(u << 6);
				cb[2 * x + 1] = (uint16_t)(v << 6);
			} else {
				cb[x] = (uint16_t)u;
				cr[x] = (uint16_t)v;
			}
		}
	}
	return frame;
}


static double
pq_light(double code)
{
	code = std::max(0.0, std::min(1.0, code));
	const double m1 = 2610.0 / 16384;
	const double m2 = 2523.0 / 4096 * 128;
	const double c1 = 3424.0 / 4096;
	const double c2 = 2413.0 / 4096 * 32;
	const double c3 = 2392.0 / 4096 * 32;
	double power = pow(code, 1 / m2);
	return 10000 * pow(std::max(power - c1, 0.0) / (c2 - c3 * power), 1 / m1)
		/ 203;
}


static int
srgb(double linear)
{
	linear = std::max(0.0, std::min(1.0, linear));
	double encoded = linear <= 0.0031308 ? 12.92 * linear
		: 1.055 * pow(linear, 1 / 2.4) - 0.055;
	return (int)(255 * encoded + 0.5);
}


/*!	What FrameRenderer should make of one scaled Y'CbCr sample, limited
	range BT.2020, PQ, a 1000 nit film. */
static void
exact_pixel(uint16 luma, uint16 cb, uint16 cr, int* rgb)
{
	double y = (luma / 64.0 - 64) / 876;
	double u = (cb / 64.0 - 512) / 896;
	double v = (cr / 64.0 - 512) / 896;
	const double kr = 0.2627;
	const double kb = 0.0593;
	const double kg = 1 - kr - kb;
	double r = pq_light(y + 2 * (1 - kr) * v);
	double g = pq_light(y - 2 * kb * (1 - kb) / kg * u
		- 2 * kr * (1 - kr) / kg * v);
	double b = pq_light(y + 2 * (1 - kb) * u);
	double light[3] = {
		std::max(0.0, 1.6605 * r - 0.5876 * g - 0.0728 * b),
		std::max(0.0, -0.1246 * r + 1.1329 * g - 0.0083 * b),
		std::max(0.0, -0.0182 * r - 0.1006 * g + 1.1187 * b)};
	double maximum = std::max(light[0], std::max(light[1], light[2]));
	const double knee = 0.6;
	const double excessPeak = std::max((1000 / 203.0 - knee) / (1 - knee),
		1.01);
	if (maximum > knee) {
		double excess = (maximum - knee) / (1 - knee);
		double mapped = knee + (1 - knee) * excess
			* (1 + excess / (excessPeak * excessPeak)) / (1 + excess);
		for (double& channel : light)
			channel *= mapped / maximum;
	}
	for (int c = 0; c < 3; c++)
		rgb[c] = srgb(light[c]);
}


int
main(int argc, char** argv)
{
	int repetitions = argc > 1 ? atoi(argv[1]) : 3;
	const int width = 3840;
	const int height = 1600;
	const int targetWidth = 1749;
	const int targetHeight = 729;
	int failures = 0;
	for (AVPixelFormat format : {AV_PIX_FMT_P010LE, AV_PIX_FMT_YUV420P10LE}) {
		AVFrame* frame = make_frame(format, width, height);
		if (frame == NULL) {
			printf("no frame\n");
			return 1;
		}
		std::vector<uint8> fused((size_t)targetWidth * targetHeight * 4);
		std::vector<uint8> plain(fused.size());
		FrameRenderer renderer;
		FrameRenderer reference;
		bigtime_t fusedTime = 0;
		bigtime_t plainTime = 0;
		for (int i = 0; i < repetitions; i++) {
			unsetenv("AIRTIME_HDR_SWSCALE");
			setenv("AIRTIME_HDR_SCALER", "1", 1);
			bigtime_t start = system_time();
			bool ok = renderer.Render(frame, fused.data(), targetWidth * 4,
				targetWidth, targetHeight);
			bigtime_t middle = system_time();
			unsetenv("AIRTIME_HDR_SCALER");
			setenv("AIRTIME_HDR_SWSCALE", "1", 1);
			ok = reference.Render(frame, plain.data(), targetWidth * 4,
				targetWidth, targetHeight) && ok;
			bigtime_t end = system_time();
			if (!ok) {
				printf("render failed\n");
				return 1;
			}
			if (i > 0 || repetitions == 1) {
				fusedTime += middle - start;
				plainTime += end - middle;
			}
		}
		int counted = repetitions > 1 ? repetitions - 1 : 1;
		YuvScaler scaler;
		YuvScaler::Scratch scratch;
		scaler.Prepare(frame, targetWidth, targetHeight);
		std::vector<uint16> luma(targetWidth), cb(targetWidth), cr(targetWidth);
		int worst = 0;
		double total = 0;
		double samples = 0;
		for (int y = 0; y < targetHeight; y += 3) {
			scaler.ScaleRow(frame, y, luma.data(), cb.data(), cr.data(),
				scratch);
			for (int x = 0; x < targetWidth; x++) {
				int rgb[3];
				exact_pixel(luma[x], cb[x], cr[x], rgb);
				const uint8* pixel = &fused[((size_t)y * targetWidth + x) * 4];
				for (int c = 0; c < 3; c++) {
					int difference = abs(pixel[2 - c] - rgb[c]);
					worst = std::max(worst, difference);
					total += difference;
					samples++;
				}
			}
		}
		double mean = total / samples;
		// The transfer and the sRGB encoding are tables of 4096 steps; where
		// a colour is at the edge of what BT.709 shows, its small channels
		// come from the difference of large ones and move a little more.
		bool good = worst <= 6 && mean < 0.15;
		printf("%s: new path %.1f ms, swscale path %.1f ms; from exact, mean"
			" %.3f, largest %d: %s\n", av_get_pix_fmt_name(format),
			fusedTime / 1000.0 / counted, plainTime / 1000.0 / counted, mean,
			worst, good ? "ok" : "FAILED");
		failures += good ? 0 : 1;
		av_frame_free(&frame);
	}
	printf("RENDERER_TESTS %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
