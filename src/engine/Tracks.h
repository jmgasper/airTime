/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_TRACKS_H
#define AIRTIME_TRACKS_H


#include <vector>

#include <String.h>

#include "FFmpeg.h"


namespace airtime {

enum track_kind {
	TRACK_VIDEO,
	TRACK_AUDIO,
	TRACK_SUBTITLE
};


struct TrackInfo {
	track_kind			kind;
	int					stream;			// stream in the file, or -1
	int					external;		// external subtitle file, or -1
	BString				language;		// as the file says it
	BString				title;
	BString				codec;			// "H.264", "AAC", "SubRip"
	int					channels;
	int					sampleRate;
	int					width;
	int					height;
	double				frameRate;
	int64				bitRate;
	bool				isDefault;
	bool				isForced;
	bool				isHearingImpaired;
	bool				isCommentary;
	bool				isBitmap;		// picture based subtitles
	bool				isCaptions;		// CEA-608/708 closed captions

						TrackInfo();

	// "English — AAC 5.1", "Français (Forced)", "Track 3"
	BString				Label(int number) const;
	BString				LanguageName() const;
};


struct Chapter {
	bigtime_t			start;
	bigtime_t			end;
	BString				title;
};


BString codec_display_name(AVCodecID codec);
BString channel_layout_name(int channels);

// "1:02:03", "2:03"; with fractions "1:02:03.25".
BString format_time(bigtime_t time, bool withFraction = false,
	bool forceHours = false);
// Parses "1:02:03.5", "62:03", "3723", "1h2m3s"; returns false if it cannot.
bool parse_time(const char* text, bigtime_t* time);

}	// namespace airtime

#endif	// AIRTIME_TRACKS_H
