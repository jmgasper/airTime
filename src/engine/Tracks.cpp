/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Tracks.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Languages.h"


namespace airtime {


TrackInfo::TrackInfo()
	:
	kind(TRACK_VIDEO),
	stream(-1),
	external(-1),
	channels(0),
	sampleRate(0),
	width(0),
	height(0),
	frameRate(0),
	bitRate(0),
	isDefault(false),
	isForced(false),
	isHearingImpaired(false),
	isCommentary(false),
	isBitmap(false),
	isCaptions(false)
{
}


BString
TrackInfo::LanguageName() const
{
	return language_name(language.String());
}


BString
TrackInfo::Label(int number) const
{
	BString label = LanguageName();
	// A title that only repeats the language adds nothing.
	if (title.Length() > 0 && title.ICompare(label) != 0) {
		if (label.Length() > 0)
			label << " — " << title;
		else
			label = title;
	}
	if (label.Length() == 0)
		label.SetToFormat("Track %d", number);

	BString details;
	if (kind == TRACK_AUDIO) {
		details = codec;
		if (channels > 0) {
			if (details.Length() > 0)
				details << " ";
			details << channel_layout_name(channels);
		}
	} else if (kind == TRACK_SUBTITLE) {
		details = codec;
	}
	if (details.Length() > 0)
		label << " (" << details << ")";

	if (isCaptions)
		label << " [CC]";
	else if (isHearingImpaired)
		label << " [SDH]";
	if (isForced)
		label << " [Forced]";
	if (isCommentary && title.IFindFirst("comment") < 0)
		label << " [Commentary]";
	return label;
}


BString
codec_display_name(AVCodecID codec)
{
	switch (codec) {
		case AV_CODEC_ID_H264:			return "H.264";
		case AV_CODEC_ID_HEVC:			return "HEVC";
		case AV_CODEC_ID_AV1:			return "AV1";
		case AV_CODEC_ID_VP8:			return "VP8";
		case AV_CODEC_ID_VP9:			return "VP9";
		case AV_CODEC_ID_MPEG1VIDEO:	return "MPEG-1";
		case AV_CODEC_ID_MPEG2VIDEO:	return "MPEG-2";
		case AV_CODEC_ID_MPEG4:			return "MPEG-4";
		case AV_CODEC_ID_PRORES:		return "ProRes";
		case AV_CODEC_ID_THEORA:		return "Theora";
		case AV_CODEC_ID_VC1:			return "VC-1";
		case AV_CODEC_ID_WMV3:			return "WMV 9";
		case AV_CODEC_ID_MJPEG:			return "Motion JPEG";
		case AV_CODEC_ID_DNXHD:			return "DNxHD";
		case AV_CODEC_ID_AAC:			return "AAC";
		case AV_CODEC_ID_AC3:			return "Dolby Digital";
		case AV_CODEC_ID_EAC3:			return "Dolby Digital Plus";
		case AV_CODEC_ID_TRUEHD:		return "Dolby TrueHD";
		case AV_CODEC_ID_DTS:			return "DTS";
		case AV_CODEC_ID_MP2:			return "MPEG Audio";
		case AV_CODEC_ID_MP3:			return "MP3";
		case AV_CODEC_ID_OPUS:			return "Opus";
		case AV_CODEC_ID_VORBIS:		return "Vorbis";
		case AV_CODEC_ID_FLAC:			return "FLAC";
		case AV_CODEC_ID_ALAC:			return "Apple Lossless";
		case AV_CODEC_ID_WMAV2:			return "WMA";
		case AV_CODEC_ID_SUBRIP:		return "SubRip";
		case AV_CODEC_ID_ASS:			return "ASS";
		case AV_CODEC_ID_SSA:			return "SSA";
		case AV_CODEC_ID_WEBVTT:		return "WebVTT";
		case AV_CODEC_ID_MOV_TEXT:		return "Timed Text";
		case AV_CODEC_ID_TEXT:			return "Text";
		case AV_CODEC_ID_HDMV_PGS_SUBTITLE:	return "PGS";
		case AV_CODEC_ID_DVD_SUBTITLE:	return "VobSub";
		case AV_CODEC_ID_DVB_SUBTITLE:	return "DVB";
		case AV_CODEC_ID_DVB_TELETEXT:	return "Teletext";
		case AV_CODEC_ID_EIA_608:		return "CEA-608";
		default:
			break;
	}
	if (codec >= AV_CODEC_ID_PCM_S16LE && codec < AV_CODEC_ID_ADPCM_IMA_QT)
		return "PCM";
	const AVCodecDescriptor* descriptor = avcodec_descriptor_get(codec);
	if (descriptor != NULL) {
		BString name(descriptor->name);
		name.ToUpper();
		return name;
	}
	return "Unknown";
}


BString
channel_layout_name(int channels)
{
	switch (channels) {
		case 1:	return "Mono";
		case 2:	return "Stereo";
		case 3: return "2.1";
		case 6:	return "5.1";
		case 7: return "6.1";
		case 8:	return "7.1";
	}
	BString name;
	name.SetToFormat("%d ch", channels);
	return name;
}


BString
format_time(bigtime_t time, bool withFraction, bool forceHours)
{
	bool negative = time < 0;
	if (negative)
		time = -time;
	int64 hundredths = time / 10000;
	int64 seconds = hundredths / 100;
	int hours = (int)(seconds / 3600);
	int minutes = (int)(seconds / 60 % 60);
	int secs = (int)(seconds % 60);

	BString text;
	if (hours > 0 || forceHours)
		text.SetToFormat("%s%d:%02d:%02d", negative ? "-" : "", hours, minutes,
			secs);
	else
		text.SetToFormat("%s%d:%02d", negative ? "-" : "", minutes, secs);
	if (withFraction) {
		BString fraction;
		fraction.SetToFormat(".%02d", (int)(hundredths % 100));
		text << fraction;
	}
	return text;
}


bool
parse_time(const char* text, bigtime_t* _time)
{
	if (text == NULL)
		return false;
	while (isspace((unsigned char)*text))
		text++;
	if (*text == '\0')
		return false;

	// "1h2m3.5s" style
	if (strpbrk(text, "hHmMsS") != NULL && strchr(text, ':') == NULL) {
		double total = 0;
		const char* at = text;
		bool any = false;
		while (*at != '\0') {
			while (isspace((unsigned char)*at))
				at++;
			if (*at == '\0')
				break;
			char* end;
			double value = strtod(at, &end);
			if (end == at || value < 0)
				return false;
			while (isspace((unsigned char)*end))
				end++;
			switch (tolower((unsigned char)*end)) {
				case 'h': total += value * 3600; end++; break;
				case 'm': total += value * 60; end++; break;
				case 's': total += value; end++; break;
				case '\0': total += value; break;
				default: return false;
			}
			any = true;
			at = end;
		}
		if (!any)
			return false;
		*_time = (bigtime_t)(total * 1000000.0 + 0.5);
		return true;
	}

	// "[[h:]m:]s[.fraction]"
	double parts[3] = {0, 0, 0};
	int count = 0;
	const char* at = text;
	while (true) {
		char* end;
		double value = strtod(at, &end);
		if (end == at || value < 0 || count == 3)
			return false;
		parts[count++] = value;
		while (isspace((unsigned char)*end))
			end++;
		if (*end == ':') {
			at = end + 1;
			continue;
		}
		if (*end != '\0')
			return false;
		break;
	}
	// Only the last field may have a fraction or be 60 or more... except the
	// first, which is allowed to be as large as it likes ("90:00").
	for (int i = 0; i < count - 1; i++) {
		if (parts[i] != (int64)parts[i])
			return false;
	}
	for (int i = 1; i < count; i++) {
		if (parts[i] >= 60)
			return false;
	}
	double seconds = 0;
	for (int i = 0; i < count; i++)
		seconds = seconds * 60 + parts[i];
	*_time = (bigtime_t)(seconds * 1000000.0 + 0.5);
	return true;
}

}	// namespace airtime
