/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Subtitles.h"

#include <atomic>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Languages.h"
#include "Tracks.h"


namespace airtime {


static std::atomic<uint64> sNextEventID(1);


uint64
next_subtitle_event_id()
{
	return sNextEventID++;
}


bool
SubtitleLine::IsEmpty() const
{
	for (const SubtitleRun& run : runs) {
		for (int32 i = 0; i < run.text.Length(); i++) {
			if (!isspace((unsigned char)run.text[i]))
				return false;
		}
	}
	return true;
}


BString
SubtitleLine::PlainText() const
{
	BString text;
	for (const SubtitleRun& run : runs)
		text << run.text;
	return text;
}


SubtitleEvent::SubtitleEvent()
	:
	id(next_subtitle_event_id()),
	start(0),
	end(kNoTime),
	alignment(2),
	canvasWidth(0),
	canvasHeight(0),
	caption(false),
	positionY(-1)
{
}


bool
SubtitleEvent::IsEmpty() const
{
	if (!bitmaps.empty())
		return false;
	for (const SubtitleLine& line : lines) {
		if (!line.IsEmpty())
			return false;
	}
	return true;
}


bool
SubtitleEvent::SameContent(const SubtitleEvent& other) const
{
	if (start != other.start || end != other.end
		|| alignment != other.alignment
		|| lines.size() != other.lines.size()
		|| bitmaps.size() != other.bitmaps.size()) {
		return false;
	}
	for (size_t i = 0; i < lines.size(); i++) {
		if (lines[i].PlainText() != other.lines[i].PlainText())
			return false;
	}
	for (size_t i = 0; i < bitmaps.size(); i++) {
		const SubtitleBitmap& a = bitmaps[i];
		const SubtitleBitmap& b = other.bitmaps[i];
		if (a.x != b.x || a.y != b.y || a.width != b.width
			|| a.height != b.height || a.pixels != b.pixels) {
			return false;
		}
	}
	return true;
}


BString
subtitle_plain_text(const SubtitleEvent& event)
{
	BString text;
	for (size_t i = 0; i < event.lines.size(); i++) {
		if (i > 0)
			text << "\n";
		text << event.lines[i].PlainText();
	}
	return text;
}


// #pragma mark - SubtitleTrack


SubtitleTrack::SubtitleTrack()
	:
	fMaxOpenDuration(10000000),
	fGeneration(0)
{
}


void
SubtitleTrack::Add(const SubtitleEventPtr& event)
{
	std::lock_guard<std::mutex> lock(fLock);
	auto range = fEvents.equal_range(event->start);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second->SameContent(*event))
			return;
	}
	fEvents.insert(std::make_pair(event->start, event));
	fGeneration++;
}


std::vector<SubtitleEventPtr>
SubtitleTrack::Active(bigtime_t time)
{
	std::vector<SubtitleEventPtr> active;
	std::lock_guard<std::mutex> lock(fLock);
	if (fEvents.empty())
		return active;

	auto it = fEvents.upper_bound(time);
	// Walk back over everything that started in the last few minutes; long
	// events are rare, but a sign may stay up for a whole scene.
	int looked = 0;
	while (it != fEvents.begin()) {
		--it;
		const SubtitleEventPtr& event = it->second;
		if (time - event->start > 600000000LL || ++looked > 200)
			break;
		if (event->IsEmpty())
			continue;
		bigtime_t end = event->end;
		if (end == kNoTime) {
			auto next = fEvents.upper_bound(event->start);
			end = event->start + fMaxOpenDuration;
			if (next != fEvents.end() && next->first < end)
				end = next->first;
		}
		if (time < end)
			active.insert(active.begin(), event);
	}
	return active;
}


void
SubtitleTrack::Prune(bigtime_t before)
{
	std::lock_guard<std::mutex> lock(fLock);
	auto end = fEvents.lower_bound(before);
	if (end != fEvents.begin()) {
		fEvents.erase(fEvents.begin(), end);
		fGeneration++;
	}
}


void
SubtitleTrack::Clear()
{
	std::lock_guard<std::mutex> lock(fLock);
	fEvents.clear();
	fGeneration++;
}


int
SubtitleTrack::Count()
{
	std::lock_guard<std::mutex> lock(fLock);
	return (int)fEvents.size();
}


uint32
SubtitleTrack::Generation()
{
	std::lock_guard<std::mutex> lock(fLock);
	return fGeneration;
}


// #pragma mark - ASS parsing


namespace {

struct AssState {
	bool		italic;
	bool		bold;
	bool		underline;
	bool		hasColor;
	rgb_color	color;
	bool		drawing;

	AssState()
		:
		italic(false),
		bold(false),
		underline(false),
		hasColor(false),
		drawing(false)
	{
		color.red = color.green = color.blue = color.alpha = 255;
	}
};


bool
parse_ass_color(const char* text, rgb_color* color)
{
	// &HBBGGRR& or &HAABBGGRR&, sometimes without the ampersands.
	while (*text == '&' || *text == 'H' || *text == 'h')
		text++;
	char* end;
	unsigned long value = strtoul(text, &end, 16);
	if (end == text)
		return false;
	color->red = value & 0xff;
	color->green = (value >> 8) & 0xff;
	color->blue = (value >> 16) & 0xff;
	color->alpha = 255;
	return true;
}


void
apply_override(const BString& tag, AssState& state, int* alignment,
	float* positionY)
{
	const char* t = tag.String();
	if (t[0] == 'i' && (t[1] == '0' || t[1] == '1') && t[2] == '\0') {
		state.italic = t[1] == '1';
	} else if (t[0] == 'b' && isdigit((unsigned char)t[1])) {
		int weight = atoi(t + 1);
		state.bold = weight == 1 || weight >= 600;
	} else if (t[0] == 'u' && (t[1] == '0' || t[1] == '1') && t[2] == '\0') {
		state.underline = t[1] == '1';
	} else if (strncmp(t, "an", 2) == 0 && isdigit((unsigned char)t[2])) {
		int value = atoi(t + 2);
		if (value >= 1 && value <= 9)
			*alignment = value;
	} else if (t[0] == 'a' && isdigit((unsigned char)t[1])) {
		// The old SSA numbering: 1-3 bottom, +4 top, +8 middle.
		int value = atoi(t + 1);
		int column = ((value - 1) & 3) + 1;
		if (column > 3)
			column = 2;
		if (value >= 9)
			*alignment = column + 3;
		else if (value >= 5)
			*alignment = column + 6;
		else
			*alignment = column;
	} else if (t[0] == 'c' && t[1] == '&') {
		state.hasColor = parse_ass_color(t + 1, &state.color);
	} else if (t[0] == '1' && t[1] == 'c') {
		state.hasColor = parse_ass_color(t + 2, &state.color);
	} else if (t[0] == 'r' && (t[1] == '\0' || isalpha((unsigned char)t[1]))) {
		bool drawing = state.drawing;
		state = AssState();
		state.drawing = drawing;
	} else if (t[0] == 'p' && isdigit((unsigned char)t[1])) {
		state.drawing = atoi(t + 1) > 0;
	} else if (strncmp(t, "pos(", 4) == 0) {
		const char* comma = strchr(t, ',');
		if (comma != NULL)
			*positionY = (float)atof(comma + 1);
	}
}

}	// namespace


void
parse_ass_dialogue(const char* dialogue, SubtitleEvent& event)
{
	event.lines.clear();
	if (dialogue == NULL)
		return;

	// Skip the fields before the text: eight in libavcodec's format, nine
	// when a whole "Dialogue:" line is given.
	const char* text = dialogue;
	int fields = 8;
	if (strncmp(text, "Dialogue:", 9) == 0) {
		text += 9;
		fields = 9;
	}
	for (int i = 0; i < fields; i++) {
		const char* comma = strchr(text, ',');
		if (comma == NULL) {
			// Not a dialogue line at all; show it as it is.
			text = dialogue;
			break;
		}
		text = comma + 1;
	}

	AssState state;
	SubtitleLine line;
	SubtitleRun run;
	int alignment = 2;
	float positionY = -1;

	auto flushRun = [&]() {
		if (run.text.Length() > 0)
			line.runs.push_back(run);
		run = SubtitleRun();
		run.italic = state.italic;
		run.bold = state.bold;
		run.underline = state.underline;
		run.hasColor = state.hasColor;
		run.color = state.color;
	};
	auto flushLine = [&]() {
		flushRun();
		event.lines.push_back(line);
		line = SubtitleLine();
	};
	flushRun();

	for (const char* at = text; *at != '\0'; at++) {
		if (*at == '{') {
			const char* close = strchr(at, '}');
			if (close == NULL)
				break;
			// Tags are separated by backslashes: {\i1\an8}
			BString block(at + 1, close - at - 1);
			int32 start = 0;
			while (start < block.Length()) {
				int32 slash = block.FindFirst('\\', start);
				if (slash < 0)
					break;
				int32 next = block.FindFirst('\\', slash + 1);
				BString tag;
				block.CopyInto(tag, slash + 1,
					(next < 0 ? block.Length() : next) - slash - 1);
				tag.Trim();
				apply_override(tag, state, &alignment, &positionY);
				start = next < 0 ? block.Length() : next;
			}
			flushRun();
			at = close;
			continue;
		}
		if (state.drawing)
			continue;
		if (*at == '\\' && (at[1] == 'N' || at[1] == 'n' || at[1] == 'h')) {
			if (at[1] == 'N')
				flushLine();
			else
				run.text << ' ';
			at++;
			continue;
		}
		if (*at == '\r')
			continue;
		if (*at == '\n') {
			flushLine();
			continue;
		}
		run.text.Append(*at, 1);
	}
	flushLine();

	// Drop empty lines at either end.
	while (!event.lines.empty() && event.lines.back().IsEmpty())
		event.lines.pop_back();
	while (!event.lines.empty() && event.lines.front().IsEmpty())
		event.lines.erase(event.lines.begin());
	event.alignment = alignment;
	event.positionY = positionY;
}


// #pragma mark - SubtitleDecoder


static void
add_subtitle(SubtitleTrack* track, const AVSubtitle& subtitle,
	bigtime_t packetPts, bigtime_t packetDuration, bigtime_t startTime,
	int canvasWidth, int canvasHeight, bool textCodec, bool caption = false)
{
	bigtime_t base = subtitle.pts != AV_NOPTS_VALUE
		? subtitle.pts : packetPts;
	if (base == AV_NOPTS_VALUE || base == kNoTime)
		return;
	base -= startTime;

	auto event = std::make_shared<SubtitleEvent>();
	event->start = base + (bigtime_t)subtitle.start_display_time * 1000;
	if (subtitle.end_display_time != 0
		&& subtitle.end_display_time != UINT32_MAX
		&& subtitle.end_display_time > subtitle.start_display_time) {
		event->end = base + (bigtime_t)subtitle.end_display_time * 1000;
	} else if (textCodec && packetDuration > 0)
		event->end = base + packetDuration;
	event->canvasWidth = canvasWidth;
	event->canvasHeight = canvasHeight;
	event->caption = caption;

	for (unsigned i = 0; i < subtitle.num_rects; i++) {
		const AVSubtitleRect* rect = subtitle.rects[i];
		if (rect->type == SUBTITLE_BITMAP) {
			if (rect->w <= 0 || rect->h <= 0 || rect->data[0] == NULL
				|| rect->data[1] == NULL) {
				continue;
			}
			SubtitleBitmap bitmap;
			bitmap.x = rect->x;
			bitmap.y = rect->y;
			bitmap.width = rect->w;
			bitmap.height = rect->h;
			bitmap.pixels.resize((size_t)rect->w * rect->h);
			const uint32* palette = (const uint32*)rect->data[1];
			for (int y = 0; y < rect->h; y++) {
				const uint8* row = rect->data[0] + y * rect->linesize[0];
				uint32* out = bitmap.pixels.data() + (size_t)y * rect->w;
				for (int x = 0; x < rect->w; x++) {
					uint8 index = row[x];
					out[x] = index < rect->nb_colors ? palette[index] : 0;
				}
			}
			event->bitmaps.push_back(std::move(bitmap));
		} else if (rect->type == SUBTITLE_ASS && rect->ass != NULL) {
			SubtitleEvent part;
			parse_ass_dialogue(rect->ass, part);
			event->alignment = part.alignment;
			event->positionY = part.positionY;
			for (const SubtitleLine& line : part.lines)
				event->lines.push_back(line);
		} else if (rect->type == SUBTITLE_TEXT && rect->text != NULL) {
			SubtitleEvent part;
			BString text("0,0,Default,,0,0,0,,");
			text << rect->text;
			parse_ass_dialogue(text.String(), part);
			for (const SubtitleLine& line : part.lines)
				event->lines.push_back(line);
		}
	}
	// An event without anything in it still matters: it ends the one before
	// (PGS and VobSub clear the screen this way).
	track->Add(event);
}


SubtitleDecoder::SubtitleDecoder(AVCodecParameters* parameters,
	AVRational timeBase, bigtime_t startTime, SubtitleTrack* track)
	:
	fParameters(parameters),
	fTimeBase(timeBase),
	fStartTime(startTime),
	fTrack(track),
	fContext(NULL),
	fVideoWidth(0),
	fVideoHeight(0)
{
}


SubtitleDecoder::~SubtitleDecoder()
{
	avcodec_free_context(&fContext);
}


status_t
SubtitleDecoder::Init(BString* reason)
{
	const AVCodec* codec = avcodec_find_decoder(fParameters->codec_id);
	if (codec == NULL) {
		reason->SetToFormat("no decoder for %s subtitles",
			avcodec_get_name(fParameters->codec_id));
		return B_NOT_SUPPORTED;
	}
	fContext = avcodec_alloc_context3(codec);
	if (fContext == NULL)
		return B_NO_MEMORY;
	avcodec_parameters_to_context(fContext, fParameters);
	fContext->pkt_timebase = fTimeBase;
	if (avcodec_open2(fContext, codec, NULL) < 0) {
		reason->SetToFormat("%s would not open", codec->name);
		avcodec_free_context(&fContext);
		return B_ERROR;
	}
	return B_OK;
}


void
SubtitleDecoder::SetVideoSize(int width, int height)
{
	fVideoWidth = width;
	fVideoHeight = height;
}


void
SubtitleDecoder::Decode(AVPacket* packet)
{
	if (fContext == NULL || packet->data == NULL)
		return;

	AVSubtitle subtitle;
	int got = 0;
	if (avcodec_decode_subtitle2(fContext, &subtitle, &got, packet) < 0
		|| !got) {
		return;
	}
	int64 timestamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
	bigtime_t packetPts = to_micros(timestamp, fTimeBase);
	bigtime_t duration = packet->duration > 0
		? to_micros(packet->duration, fTimeBase) : 0;
	int width = fContext->width > 0 ? fContext->width : fVideoWidth;
	int height = fContext->height > 0 ? fContext->height : fVideoHeight;
	bool text = avcodec_descriptor_get(fParameters->codec_id) != NULL
		&& (avcodec_descriptor_get(fParameters->codec_id)->props
			& AV_CODEC_PROP_TEXT_SUB) != 0;
	add_subtitle(fTrack, subtitle, packetPts, duration, fStartTime, width,
		height, text);
	avsubtitle_free(&subtitle);
}


void
SubtitleDecoder::Flush()
{
	if (fContext != NULL)
		avcodec_flush_buffers(fContext);
}


// #pragma mark - CaptionDecoder


static const size_t kCaptionReorderDepth = 16;


CaptionDecoder::CaptionDecoder(SubtitleTrack* track)
	:
	fTrack(track),
	fContext(NULL)
{
}


CaptionDecoder::~CaptionDecoder()
{
	avcodec_free_context(&fContext);
}


status_t
CaptionDecoder::Init()
{
	const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_EIA_608);
	if (codec == NULL)
		return B_NOT_SUPPORTED;
	fContext = avcodec_alloc_context3(codec);
	if (fContext == NULL)
		return B_NO_MEMORY;
	fContext->pkt_timebase = AVRational{1, 1000000};
	// By default the decoder hands over a caption only once it is taken off
	// the screen, which is too late to show it; in real time mode it hands
	// over every change as it happens, an empty one when the screen clears.
	AVDictionary* options = NULL;
	av_dict_set(&options, "real_time", "1", 0);
	av_dict_set(&options, "real_time_latency_msec", "0", 0);
	int result = avcodec_open2(fContext, codec, &options);
	av_dict_free(&options);
	if (result < 0) {
		avcodec_free_context(&fContext);
		return B_ERROR;
	}
	return B_OK;
}


void
CaptionDecoder::Add(bigtime_t pts, const uint8* triplets, size_t size)
{
	if (fContext == NULL || size < 3)
		return;
	fPending.insert(std::make_pair(pts,
		std::vector<uint8>(triplets, triplets + size)));
	while (fPending.size() > kCaptionReorderDepth) {
		auto first = fPending.begin();
		_Feed(first->first, first->second);
		fPending.erase(first);
	}
}


void
CaptionDecoder::AddPacket(AVPacket* packet, AVRational timeBase,
	bigtime_t startTime)
{
	if (fContext == NULL || packet->data == NULL || packet->size < 2)
		return;
	int64 timestamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
	bigtime_t pts = to_micros(timestamp, timeBase);
	if (pts == kNoTime)
		return;
	pts -= startTime;

	// QuickTime keeps byte pairs in 'cdat' (field 1) and 'cdt2' (field 2)
	// atoms; turn them into cc_data triplets.
	std::vector<uint8> triplets;
	const uint8* data = packet->data;
	size_t size = packet->size;
	if (size >= 8 && (memcmp(data + 4, "cdat", 4) == 0
			|| memcmp(data + 4, "cdt2", 4) == 0)) {
		size_t at = 0;
		while (at + 8 <= size) {
			uint32 atomSize = (data[at] << 24) | (data[at + 1] << 16)
				| (data[at + 2] << 8) | data[at + 3];
			if (atomSize < 8 || atomSize > size - at)
				break;
			uint8 type = memcmp(data + at + 4, "cdt2", 4) == 0 ? 1 : 0;
			for (size_t i = at + 8; i + 2 <= at + atomSize; i += 2) {
				triplets.push_back(0xfc | type);
				triplets.push_back(data[i]);
				triplets.push_back(data[i + 1]);
			}
			at += atomSize;
		}
	} else
		triplets.assign(data, data + size - size % 3);
	if (!triplets.empty())
		_Feed(pts, triplets);
}


void
CaptionDecoder::_Feed(bigtime_t pts, const std::vector<uint8>& triplets)
{
	AVPacket* packet = av_packet_alloc();
	if (packet == NULL)
		return;
	if (av_new_packet(packet, (int)triplets.size()) == 0) {
		memcpy(packet->data, triplets.data(), triplets.size());
		packet->pts = packet->dts = pts;
		AVSubtitle subtitle;
		int got = 0;
		if (avcodec_decode_subtitle2(fContext, &subtitle, &got, packet) >= 0
			&& got) {
			add_subtitle(fTrack, subtitle, pts, 0, 0, 0, 0, true, true);
			avsubtitle_free(&subtitle);
		}
	}
	av_packet_free(&packet);
}


void
CaptionDecoder::Flush()
{
	fPending.clear();
	if (fContext != NULL)
		avcodec_flush_buffers(fContext);
}


void
CaptionDecoder::EndOfStream()
{
	for (auto& entry : fPending)
		_Feed(entry.first, entry.second);
	fPending.clear();
}


// #pragma mark - subtitle files


namespace {

bool
is_valid_utf8(const uint8* data, size_t size)
{
	size_t i = 0;
	while (i < size) {
		uint8 c = data[i];
		int extra;
		if (c < 0x80)
			extra = 0;
		else if ((c & 0xe0) == 0xc0 && c >= 0xc2)
			extra = 1;
		else if ((c & 0xf0) == 0xe0)
			extra = 2;
		else if ((c & 0xf8) == 0xf0 && c <= 0xf4)
			extra = 3;
		else
			return false;
		for (int k = 1; k <= extra; k++) {
			if (i + k >= size || (data[i + k] & 0xc0) != 0x80)
				return false;
		}
		i += extra + 1;
	}
	return true;
}


// Windows-1252's 0x80 - 0x9f; the rest is Latin-1.
const uint16 kWindows1252[32] = {
	0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
	0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
	0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
	0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
};


std::vector<uint8>
windows1252_to_utf8(const uint8* data, size_t size)
{
	std::vector<uint8> output;
	output.reserve(size + size / 4);
	for (size_t i = 0; i < size; i++) {
		uint32 c = data[i];
		if (c >= 0x80 && c < 0xa0)
			c = kWindows1252[c - 0x80];
		if (c < 0x80) {
			output.push_back(c);
		} else if (c < 0x800) {
			output.push_back(0xc0 | (c >> 6));
			output.push_back(0x80 | (c & 0x3f));
		} else {
			output.push_back(0xe0 | (c >> 12));
			output.push_back(0x80 | ((c >> 6) & 0x3f));
			output.push_back(0x80 | (c & 0x3f));
		}
	}
	return output;
}


struct MemoryInput {
	const uint8*	data;
	size_t			size;
	size_t			position;
};


int
memory_read(void* opaque, uint8_t* buffer, int size)
{
	MemoryInput* input = (MemoryInput*)opaque;
	size_t left = input->size - input->position;
	if (left == 0)
		return AVERROR_EOF;
	size_t count = (size_t)size < left ? (size_t)size : left;
	memcpy(buffer, input->data + input->position, count);
	input->position += count;
	return (int)count;
}


int64_t
memory_seek(void* opaque, int64_t offset, int whence)
{
	MemoryInput* input = (MemoryInput*)opaque;
	if (whence == AVSEEK_SIZE)
		return input->size;
	int64_t position;
	switch (whence & ~AVSEEK_FORCE) {
		case SEEK_SET: position = offset; break;
		case SEEK_CUR: position = input->position + offset; break;
		case SEEK_END: position = input->size + offset; break;
		default: return -1;
	}
	if (position < 0 || position > (int64_t)input->size)
		return -1;
	input->position = position;
	return position;
}


/*!	"Movie.en.srt", "Movie.eng.forced.srt", "Movie.English.srt". */
BString
language_from_file_name(const char* path)
{
	BString name(path);
	int32 slash = name.FindLast('/');
	if (slash >= 0)
		name.Remove(0, slash + 1);
	int32 dot = name.FindLast('.');
	if (dot <= 0)
		return "";
	name.Truncate(dot);

	for (int round = 0; round < 3; round++) {
		dot = name.FindLast('.');
		if (dot < 0)
			break;
		BString part;
		name.CopyInto(part, dot + 1, name.Length() - dot - 1);
		name.Truncate(dot);
		if (part.Length() == 2 || part.Length() == 3) {
			BString known = language_name(part.String());
			if (known.Length() > 0 && known != BString(part).ToUpper())
				return part.ToLower();
		}
		// A spelled out English name.
		for (const char* code : {"en", "fr", "de", "es", "it", "pt", "nl",
				"sv", "da", "no", "fi", "pl", "ru", "ja", "zh", "ko", "ar",
				"he", "tr", "el", "cs", "hu", "ro", "uk"}) {
			if (part.ICompare(language_name(code)) == 0)
				return code;
		}
	}
	return "";
}

}	// namespace


status_t
load_subtitle_file(const char* path, SubtitleTrack* track, BString* codecName,
	BString* language, BString* reason)
{
	*language = language_from_file_name(path);

	BString extension(path);
	int32 dot = extension.FindLast('.');
	extension.Remove(0, dot >= 0 ? dot + 1 : extension.Length());
	extension.ToLower();

	AVFormatContext* context = NULL;
	std::vector<uint8> contents;
	MemoryInput input = {NULL, 0, 0};
	bool openedByPath = extension == "idx";

	if (!openedByPath) {
		FILE* file = fopen(path, "rb");
		if (file == NULL) {
			reason->SetToFormat("cannot read the file: %s", strerror(errno));
			return B_ERROR;
		}
		uint8 buffer[65536];
		size_t got;
		while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
			contents.insert(contents.end(), buffer, buffer + got);
			if (contents.size() > 64 * 1024 * 1024)
				break;
		}
		fclose(file);
		if (contents.empty()) {
			reason->SetTo("the file is empty");
			return B_ERROR;
		}
		size_t skip = contents.size() >= 3 && contents[0] == 0xef
			&& contents[1] == 0xbb && contents[2] == 0xbf ? 3 : 0;
		bool utf16 = contents.size() >= 2 && ((contents[0] == 0xff
			&& contents[1] == 0xfe) || (contents[0] == 0xfe
			&& contents[1] == 0xff));
		if (!utf16 && !is_valid_utf8(contents.data() + skip,
				contents.size() - skip)) {
			contents = windows1252_to_utf8(contents.data(), contents.size());
		}

		context = avformat_alloc_context();
		unsigned char* ioBuffer = (unsigned char*)av_malloc(65536);
		input.data = contents.data();
		input.size = contents.size();
		AVIOContext* io = avio_alloc_context(ioBuffer, 65536, 0, &input,
			memory_read, NULL, memory_seek);
		if (context == NULL || io == NULL) {
			avformat_free_context(context);
			return B_NO_MEMORY;
		}
		context->pb = io;
		context->flags |= AVFMT_FLAG_CUSTOM_IO;
	}

	const AVInputFormat* format = NULL;
	if (extension == "srt")
		format = av_find_input_format("srt");
	else if (extension == "ass" || extension == "ssa")
		format = av_find_input_format("ass");
	else if (extension == "vtt")
		format = av_find_input_format("webvtt");
	else if (extension == "smi" || extension == "sami")
		format = av_find_input_format("sami");

	AVIOContext* customIO = openedByPath ? NULL : context->pb;
	int error = avformat_open_input(&context, path, format, NULL);
	if (error < 0) {
		char text[128];
		reason->SetToFormat("not a subtitle file airTime can read (%s)",
			av_error_string(error, text, sizeof(text)));
		if (customIO != NULL) {
			av_freep(&customIO->buffer);
			avio_context_free(&customIO);
		}
		return B_ERROR;
	}
	avformat_find_stream_info(context, NULL);

	int streamIndex = av_find_best_stream(context, AVMEDIA_TYPE_SUBTITLE, -1,
		-1, NULL, 0);
	status_t status = B_OK;
	if (streamIndex < 0) {
		reason->SetTo("the file has no subtitles in it");
		status = B_ERROR;
	} else {
		AVStream* stream = context->streams[streamIndex];
		*codecName = codec_display_name(stream->codecpar->codec_id);
		AVDictionaryEntry* tag = av_dict_get(stream->metadata, "language",
			NULL, 0);
		if (tag != NULL && language->Length() == 0)
			*language = tag->value;

		track->SetMaxOpenDuration(10000000);
		SubtitleDecoder decoder(stream->codecpar, stream->time_base, 0, track);
		status = decoder.Init(reason);
		if (status == B_OK) {
			AVPacket* packet = av_packet_alloc();
			while (av_read_frame(context, packet) >= 0) {
				if (packet->stream_index == streamIndex)
					decoder.Decode(packet);
				av_packet_unref(packet);
			}
			av_packet_free(&packet);
			if (track->Count() == 0) {
				reason->SetTo("no subtitles could be read from the file");
				status = B_ERROR;
			}
		}
	}

	avformat_close_input(&context);
	if (customIO != NULL) {
		av_freep(&customIO->buffer);
		avio_context_free(&customIO);
	}
	return status;
}

}	// namespace airtime
