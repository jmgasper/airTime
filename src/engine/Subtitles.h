/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_SUBTITLES_H
#define AIRTIME_SUBTITLES_H


#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <GraphicsDefs.h>
#include <String.h>

#include "FFmpeg.h"


namespace airtime {

struct SubtitleRun {
	BString			text;
	bool			italic;
	bool			bold;
	bool			underline;
	bool			hasColor;
	rgb_color		color;

	SubtitleRun()
		:
		italic(false),
		bold(false),
		underline(false),
		hasColor(false)
	{
		color.red = color.green = color.blue = color.alpha = 255;
	}
};


struct SubtitleLine {
	std::vector<SubtitleRun> runs;

	bool IsEmpty() const;
	BString PlainText() const;
};


struct SubtitleBitmap {
	int				x;
	int				y;
	int				width;
	int				height;
	std::vector<uint32> pixels;		// B_RGBA32, straight alpha
};


struct SubtitleEvent {
	uint64			id;
	bigtime_t		start;
	bigtime_t		end;			// kNoTime: until the next event
	int				alignment;		// numeric keypad layout, 2 = bottom
	std::vector<SubtitleLine> lines;
	std::vector<SubtitleBitmap> bitmaps;
	int				canvasWidth;
	int				canvasHeight;
	bool			caption;		// closed captions: drawn on a box
	float			positionY;		// \pos, in the script's units; -1 none

	SubtitleEvent();

	bool IsBitmap() const { return !bitmaps.empty(); }
	bool IsEmpty() const;
	bool SameContent(const SubtitleEvent& other) const;
};

typedef std::shared_ptr<const SubtitleEvent> SubtitleEventPtr;


/*!	The timed events of one subtitle track. Embedded tracks fill up while the
	file is read and forget what lies far behind the playback position;
	external files are read whole. */
class SubtitleTrack {
public:
								SubtitleTrack();

			void				Add(const SubtitleEventPtr& event);
			std::vector<SubtitleEventPtr> Active(bigtime_t time);
			void				Prune(bigtime_t before);
			void				Clear();
			int					Count();
			// Changes whenever the set of events does.
			uint32				Generation();

			void				SetMaxOpenDuration(bigtime_t duration)
									{ fMaxOpenDuration = duration; }

private:
			std::mutex			fLock;
			std::multimap<bigtime_t, SubtitleEventPtr> fEvents;
			bigtime_t			fMaxOpenDuration;
			uint32				fGeneration;
};


// Parses the text of an ASS dialogue line as libavcodec's subtitle decoders
// produce it ("ReadOrder,Layer,Style,...,Text") into styled lines.
void parse_ass_dialogue(const char* dialogue, SubtitleEvent& event);

// Text without styling, for the inspector and tests.
BString subtitle_plain_text(const SubtitleEvent& event);

uint64 next_subtitle_event_id();


/*!	Decodes one subtitle stream of a file into a SubtitleTrack. */
class SubtitleDecoder {
public:
								SubtitleDecoder(AVCodecParameters* parameters,
									AVRational timeBase, bigtime_t startTime,
									SubtitleTrack* track);
								~SubtitleDecoder();

			status_t			Init(BString* reason);
			void				Decode(AVPacket* packet);
			void				Flush();
			// For bitmap subtitles without their own canvas size.
			void				SetVideoSize(int width, int height);

private:
			AVCodecParameters*	fParameters;
			AVRational			fTimeBase;
			bigtime_t			fStartTime;
			SubtitleTrack*		fTrack;
			AVCodecContext*		fContext;
			int					fVideoWidth;
			int					fVideoHeight;
};


/*!	CEA-608 closed captions. The cc_data triplets come from the video
	packets in decoding order; they are put back into presentation order
	before libavcodec's caption decoder sees them. */
class CaptionDecoder {
public:
								CaptionDecoder(SubtitleTrack* track);
								~CaptionDecoder();

			status_t			Init();
			// pts is media time.
			void				Add(bigtime_t pts, const uint8* triplets,
									size_t size);
			// A 608 stream of its own (QuickTime c608 tracks).
			void				AddPacket(AVPacket* packet, AVRational timeBase,
									bigtime_t startTime);
			void				Flush();
			void				EndOfStream();

private:
			void				_Feed(bigtime_t pts,
									const std::vector<uint8>& triplets);

			SubtitleTrack*		fTrack;
			AVCodecContext*		fContext;
			std::multimap<bigtime_t, std::vector<uint8> > fPending;
};


/*!	Reads a subtitle file (SubRip, ASS/SSA, WebVTT, VobSub .idx and whatever
	else libavformat reads) into a track. Text that is not UTF-8 is taken to
	be Windows-1252. */
status_t load_subtitle_file(const char* path, SubtitleTrack* track,
	BString* codecName, BString* language, BString* reason);

}	// namespace airtime

#endif	// AIRTIME_SUBTITLES_H
