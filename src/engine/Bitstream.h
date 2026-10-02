/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_BITSTREAM_H
#define AIRTIME_BITSTREAM_H


#include <functional>
#include <vector>

#include <SupportDefs.h>


namespace airtime {

class BitReader {
public:
							BitReader(const uint8* data, size_t size);

			uint32			Read(int bits);
			bool			ReadFlag() { return Read(1) != 0; }
			uint32			ReadUE();
			int32			ReadSE();
			void			Skip(int bits);
			bool			Overrun() const { return fOverrun; }
			size_t			BitsLeft() const;

private:
			const uint8*	fData;
			size_t			fSize;
			size_t			fBit;
			bool			fOverrun;
};


// Strips the emulation prevention bytes (00 00 03) from a NAL unit.
std::vector<uint8> unescape_rbsp(const uint8* data, size_t size);

// Calls the function for every NAL unit in a piece of stream. A length size of
// 0 means Annex B start codes; 1 to 4 means length prefixes of that size.
void for_each_nal(const uint8* data, size_t size, int lengthSize,
	const std::function<void(const uint8* nal, size_t size)>& function);

// The NAL length size an avcC (H.264) or hvcC (HEVC) configuration record
// declares, or 0 if the extra data is Annex B (or there is none).
int nal_length_size(bool hevc, const uint8* extradata, size_t size);


struct H264SequenceInfo {
	int				profile;
	int				level;
	int				chromaFormat;		// 0 = monochrome, 1 = 4:2:0, ...
	int				bitDepthLuma;
	int				bitDepthChroma;
	int				maxReferenceFrames;
	bool			frameMbsOnly;		// false = may contain fields
	int				width;
	int				height;
};

// Parses an H.264 sequence parameter set; the NAL includes its header byte.
bool parse_h264_sps(const uint8* nal, size_t size, H264SequenceInfo* info);

// Finds and parses the first sequence parameter set in an avcC record or in a
// piece of Annex B / length prefixed stream.
bool find_h264_sps(const uint8* data, size_t size, int lengthSize,
	H264SequenceInfo* info);


enum caption_source {
	CAPTIONS_H264,
	CAPTIONS_HEVC,
	CAPTIONS_MPEG2
};

// Appends the ATSC A/53 cc_data triplets of a video packet to `captions`.
// Returns true if the packet carried any.
bool extract_a53_captions(caption_source source, const uint8* data,
	size_t size, int lengthSize, std::vector<uint8>& captions);

}	// namespace airtime

#endif	// AIRTIME_BITSTREAM_H
