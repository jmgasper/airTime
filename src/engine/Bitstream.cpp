/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Bitstream.h"

#include <string.h>


namespace airtime {


BitReader::BitReader(const uint8* data, size_t size)
	:
	fData(data),
	fSize(size),
	fBit(0),
	fOverrun(false)
{
}


uint32
BitReader::Read(int bits)
{
	uint32 value = 0;
	for (int i = 0; i < bits; i++) {
		if (fBit >= fSize * 8) {
			fOverrun = true;
			return value;
		}
		value = (value << 1) | ((fData[fBit >> 3] >> (7 - (fBit & 7))) & 1);
		fBit++;
	}
	return value;
}


uint32
BitReader::ReadUE()
{
	int zeros = 0;
	while (Read(1) == 0) {
		if (fOverrun || ++zeros > 31) {
			fOverrun = true;
			return 0;
		}
	}
	if (zeros == 0)
		return 0;
	return ((1u << zeros) - 1) + Read(zeros);
}


int32
BitReader::ReadSE()
{
	uint32 value = ReadUE();
	if (value & 1)
		return (int32)((value + 1) / 2);
	return -(int32)(value / 2);
}


void
BitReader::Skip(int bits)
{
	fBit += bits;
	if (fBit > fSize * 8) {
		fBit = fSize * 8;
		fOverrun = true;
	}
}


size_t
BitReader::BitsLeft() const
{
	return fSize * 8 - fBit;
}


std::vector<uint8>
unescape_rbsp(const uint8* data, size_t size)
{
	std::vector<uint8> output;
	output.reserve(size);
	int zeros = 0;
	for (size_t i = 0; i < size; i++) {
		if (zeros >= 2 && data[i] == 3) {
			zeros = 0;
			continue;
		}
		output.push_back(data[i]);
		zeros = data[i] == 0 ? zeros + 1 : 0;
	}
	return output;
}


void
for_each_nal(const uint8* data, size_t size, int lengthSize,
	const std::function<void(const uint8* nal, size_t size)>& function)
{
	if (data == NULL || size == 0)
		return;

	if (lengthSize > 0) {
		size_t at = 0;
		while (at + lengthSize <= size) {
			size_t length = 0;
			for (int i = 0; i < lengthSize; i++)
				length = (length << 8) | data[at + i];
			at += lengthSize;
			if (length == 0 || length > size - at)
				return;
			function(data + at, length);
			at += length;
		}
		return;
	}

	// Annex B: find start codes.
	size_t start = (size_t)-1;
	size_t i = 0;
	while (i + 3 <= size) {
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
			if (start != (size_t)-1) {
				size_t end = i;
				// A four byte start code leaves a zero on the previous NAL.
				while (end > start && data[end - 1] == 0)
					end--;
				if (end > start)
					function(data + start, end - start);
			}
			i += 3;
			start = i;
			continue;
		}
		i++;
	}
	if (start != (size_t)-1 && start < size)
		function(data + start, size - start);
}


int
nal_length_size(bool hevc, const uint8* extradata, size_t size)
{
	if (extradata == NULL || size < 7)
		return 0;
	// Annex B extra data starts with a start code.
	if (extradata[0] == 0 && extradata[1] == 0
		&& (extradata[2] == 1 || (extradata[2] == 0 && extradata[3] == 1))) {
		return 0;
	}
	if (!hevc)
		return extradata[0] == 1 ? (extradata[4] & 3) + 1 : 0;
	if (size < 23)
		return 0;
	return (extradata[21] & 3) + 1;
}


namespace {

void
skip_scaling_list(BitReader& reader, int size)
{
	int last = 8;
	int next = 8;
	for (int i = 0; i < size; i++) {
		if (next != 0) {
			int delta = reader.ReadSE();
			next = (last + delta + 256) % 256;
		}
		last = next == 0 ? last : next;
	}
}

}	// namespace


bool
parse_h264_sps(const uint8* nal, size_t size, H264SequenceInfo* info)
{
	if (size < 4 || (nal[0] & 0x1f) != 7)
		return false;
	std::vector<uint8> rbsp = unescape_rbsp(nal + 1, size - 1);
	BitReader reader(rbsp.data(), rbsp.size());

	memset(info, 0, sizeof(*info));
	info->profile = reader.Read(8);
	reader.Skip(8);		// constraint flags
	info->level = reader.Read(8);
	reader.ReadUE();	// seq_parameter_set_id
	info->chromaFormat = 1;
	info->bitDepthLuma = 8;
	info->bitDepthChroma = 8;

	switch (info->profile) {
		case 100: case 110: case 122: case 244: case 44: case 83: case 86:
		case 118: case 128: case 138: case 139: case 134: case 135:
		{
			info->chromaFormat = reader.ReadUE();
			if (info->chromaFormat == 3)
				reader.Skip(1);		// separate_colour_plane_flag
			info->bitDepthLuma = reader.ReadUE() + 8;
			info->bitDepthChroma = reader.ReadUE() + 8;
			reader.Skip(1);			// qpprime_y_zero_transform_bypass_flag
			if (reader.ReadFlag()) {	// seq_scaling_matrix_present_flag
				int lists = info->chromaFormat != 3 ? 8 : 12;
				for (int i = 0; i < lists; i++) {
					if (reader.ReadFlag())
						skip_scaling_list(reader, i < 6 ? 16 : 64);
				}
			}
			break;
		}
	}

	reader.ReadUE();	// log2_max_frame_num_minus4
	uint32 pocType = reader.ReadUE();
	if (pocType == 0) {
		reader.ReadUE();	// log2_max_pic_order_cnt_lsb_minus4
	} else if (pocType == 1) {
		reader.Skip(1);		// delta_pic_order_always_zero_flag
		reader.ReadSE();	// offset_for_non_ref_pic
		reader.ReadSE();	// offset_for_top_to_bottom_field
		uint32 cycle = reader.ReadUE();
		if (cycle > 255)
			return false;
		for (uint32 i = 0; i < cycle; i++)
			reader.ReadSE();
	}
	info->maxReferenceFrames = reader.ReadUE();
	reader.Skip(1);		// gaps_in_frame_num_value_allowed_flag
	uint32 widthInMbs = reader.ReadUE() + 1;
	uint32 heightInMapUnits = reader.ReadUE() + 1;
	info->frameMbsOnly = reader.ReadFlag();
	info->width = widthInMbs * 16;
	info->height = heightInMapUnits * 16 * (info->frameMbsOnly ? 1 : 2);
	return !reader.Overrun();
}


bool
find_h264_sps(const uint8* data, size_t size, int lengthSize,
	H264SequenceInfo* info)
{
	if (data == NULL || size < 8)
		return false;

	// avcC: version 1, then SPS count and length prefixed SPSs.
	if (data[0] == 1 && lengthSize == 0 && size > 8 && (data[5] & 0x1f) > 0) {
		size_t length = ((size_t)data[6] << 8) | data[7];
		if (8 + length <= size)
			return parse_h264_sps(data + 8, length, info);
		return false;
	}

	bool found = false;
	for_each_nal(data, size, lengthSize, [&](const uint8* nal, size_t length) {
		if (!found && length > 0 && (nal[0] & 0x1f) == 7)
			found = parse_h264_sps(nal, length, info);
	});
	return found;
}


namespace {

/*!	One SEI message of type 4 (registered ITU-T T.35 user data): an ATSC
	caption payload is country 0xb5, provider 0x0031, "GA94", type 3. */
bool
parse_t35_captions(const uint8* payload, size_t size,
	std::vector<uint8>& captions)
{
	if (size < 10 || payload[0] != 0xb5)
		return false;
	uint16 provider = (payload[1] << 8) | payload[2];
	if (provider != 0x0031)
		return false;
	if (memcmp(payload + 3, "GA94", 4) != 0 || payload[7] != 0x03)
		return false;

	const uint8* ccData = payload + 8;
	size_t left = size - 8;
	if (left < 2 || (ccData[0] & 0x40) == 0)	// process_cc_data_flag
		return false;
	int count = ccData[0] & 0x1f;
	if ((size_t)count * 3 > left - 2)
		count = (left - 2) / 3;
	if (count <= 0)
		return false;
	captions.insert(captions.end(), ccData + 2, ccData + 2 + count * 3);
	return true;
}


bool
parse_sei_captions(const uint8* rbsp, size_t size,
	std::vector<uint8>& captions)
{
	bool found = false;
	size_t at = 0;
	while (at + 2 <= size) {
		// A trailing rbsp_stop_one_bit byte ends the messages.
		if (rbsp[at] == 0x80 && at + 1 == size)
			break;
		uint32 type = 0;
		while (at < size && rbsp[at] == 0xff)
			type += rbsp[at++];
		if (at >= size)
			break;
		type += rbsp[at++];
		uint32 payloadSize = 0;
		while (at < size && rbsp[at] == 0xff)
			payloadSize += rbsp[at++];
		if (at >= size)
			break;
		payloadSize += rbsp[at++];
		if (payloadSize > size - at)
			break;
		if (type == 4 && parse_t35_captions(rbsp + at, payloadSize, captions))
			found = true;
		at += payloadSize;
	}
	return found;
}

}	// namespace


bool
extract_a53_captions(caption_source source, const uint8* data, size_t size,
	int lengthSize, std::vector<uint8>& captions)
{
	if (data == NULL || size < 4)
		return false;

	bool found = false;
	if (source == CAPTIONS_MPEG2) {
		// user_data_start_code 00 00 01 b2, then "GA94" 03 cc_data.
		for (size_t i = 0; i + 12 <= size; i++) {
			if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1
				|| data[i + 3] != 0xb2) {
				continue;
			}
			const uint8* user = data + i + 4;
			size_t left = size - i - 4;
			if (left < 7 || memcmp(user, "GA94", 4) != 0 || user[4] != 0x03)
				continue;
			const uint8* ccData = user + 5;
			left -= 5;
			if ((ccData[0] & 0x40) == 0)
				continue;
			int count = ccData[0] & 0x1f;
			if ((size_t)count * 3 > left - 2)
				count = (left - 2) / 3;
			if (count > 0) {
				captions.insert(captions.end(), ccData + 2,
					ccData + 2 + count * 3);
				found = true;
			}
		}
		return found;
	}

	for_each_nal(data, size, lengthSize, [&](const uint8* nal, size_t length) {
		if (source == CAPTIONS_H264) {
			if (length < 2 || (nal[0] & 0x1f) != 6)
				return;
			std::vector<uint8> rbsp = unescape_rbsp(nal + 1, length - 1);
			if (parse_sei_captions(rbsp.data(), rbsp.size(), captions))
				found = true;
		} else {
			// HEVC prefix SEI, with a two byte NAL header.
			if (length < 3 || ((nal[0] >> 1) & 0x3f) != 39)
				return;
			std::vector<uint8> rbsp = unescape_rbsp(nal + 2, length - 2);
			if (parse_sei_captions(rbsp.data(), rbsp.size(), captions))
				found = true;
		}
	});
	return found;
}

}	// namespace airtime
