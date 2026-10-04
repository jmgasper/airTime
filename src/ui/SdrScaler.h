/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_SDR_SCALER_H
#define AIRTIME_SDR_SCALER_H


#include <vector>

#include "FFmpeg.h"


namespace airtime {

/*!	Eight bit 4:2:0 pictures (three planes, or NV12) to B_RGB32 pixels of
	the size they are shown at, a target row at a time: both chroma and luma
	are scaled between their two nearest rows and samples, and the row is
	made into pixels, all in vectors of sixteen.

	This is what swscale does with SWS_FAST_BILINEAR, several times as fast
	on the small ARM boards, where swscale's general scaler has little
	vector code: on the Raspberry Pi 4 a 1080p picture took 16 to 20 ms on
	four cores with swscale, which left a 30 Hz film no time to spare.
	Shrinking to less than half takes more than two samples to look right,
	and stays with swscale.

	Rows are independent, so several threads can make them at once, each
	with its own Scratch. The plain code computes exactly what the vector
	code does.
*/
class SdrScaler {
public:
	struct Scratch {
		std::vector<uint8>	luma;
		std::vector<uint8>	cb;
		std::vector<uint8>	cr;
		std::vector<uint8>	pairs;
	};

	static	bool				Handles(const AVFrame* frame, int width,
									int height);

			// Builds the tables when the picture or target size changed.
			bool				Prepare(const AVFrame* frame, int width,
									int height);
			void				RenderRow(const AVFrame* frame, int y,
									uint8* out, Scratch& scratch,
									bool vectors = true) const;

private:
	// How one dimension is sampled: each target sample between two source
	// samples, with the weight of the second in 128ths.
	struct Columns {
		std::vector<int32>	base;		// per sixteen target samples
		std::vector<uint8>	first;		// sixteen each, from the base
		std::vector<uint8>	second;
		std::vector<uint8>	weight;
	};
	struct Rows {
		std::vector<int32>	first;
		std::vector<int32>	second;
		std::vector<uint8>	weight;
	};

	static	void				_MakeColumns(Columns& columns, int sourceSize,
									int targetSize, double scale,
									double offset);
	static	void				_MakeRows(Rows& rows, int sourceSize,
									int targetSize, double scale,
									double offset);

			int					fFormat = -1;
			int					fSourceWidth = 0;
			int					fSourceHeight = 0;
			int					fWidth = 0;
			int					fHeight = 0;
			int					fChromaLocation = -1;
			int					fColorSpace = -1;
			int					fColorRange = -1;
			Columns				fLumaColumns;
			Columns				fChromaColumns;
			Rows				fLumaRows;
			Rows				fChromaRows;

			// Y'CbCr to R'G'B': factors for a rounding, doubling multiply
			// that keeps the high half (the results are 64ths)
			int16				fLumaOffset;
			int16				fLumaFactor;
			int16				fRedCr;
			int16				fGreenCb;
			int16				fGreenCr;
			int16				fBlueCb;
};

}	// namespace airtime

#endif	// AIRTIME_SDR_SCALER_H
