/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Prints the closed captions airTime finds in a file, with their times:
 *	build-host/caption_dump <file>
 */


#include <stdio.h>

#include "Bitstream.h"
#include "FFmpeg.h"
#include "Subtitles.h"
#include "Tracks.h"


using namespace airtime;


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file>\n", argv[0]);
		return 2;
	}
	AVFormatContext* format = NULL;
	if (avformat_open_input(&format, argv[1], NULL, NULL) < 0)
		return 1;
	avformat_find_stream_info(format, NULL);
	int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL,
		0);
	if (video < 0)
		return 1;
	AVStream* stream = format->streams[video];
	bool hevc = stream->codecpar->codec_id == AV_CODEC_ID_HEVC;
	caption_source source = hevc ? CAPTIONS_HEVC
		: stream->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO
			? CAPTIONS_MPEG2 : CAPTIONS_H264;
	int lengthSize = nal_length_size(hevc, stream->codecpar->extradata,
		stream->codecpar->extradata_size);
	bigtime_t start = format->start_time != AV_NOPTS_VALUE
		? format->start_time : 0;

	SubtitleTrack track;
	CaptionDecoder decoder(&track);
	decoder.Init();
	AVPacket* packet = av_packet_alloc();
	int withCaptions = 0;
	bigtime_t last = 0;
	while (av_read_frame(format, packet) >= 0) {
		if (packet->stream_index == video) {
			std::vector<uint8> captions;
			if (extract_a53_captions(source, packet->data, packet->size,
					lengthSize, captions)) {
				int64 timestamp = packet->pts != AV_NOPTS_VALUE ? packet->pts
					: packet->dts;
				last = to_micros(timestamp, stream->time_base) - start;
				decoder.Add(last, captions.data(), captions.size());
				withCaptions++;
			}
		}
		av_packet_unref(packet);
	}
	decoder.EndOfStream();
	printf("%d pictures with captions, %d events\n", withCaptions,
		track.Count());
	for (bigtime_t time = 0; time <= last; time += 500000) {
		std::vector<SubtitleEventPtr> events = track.Active(time);
		BString text;
		for (const SubtitleEventPtr& event : events) {
			BString line = subtitle_plain_text(*event);
			text << "[" << format_time(event->start, true).String() << "] "
				<< line.String() << "  ";
		}
		printf("%8s  %s\n", format_time(time, true).String(), text.String());
	}
	av_packet_free(&packet);
	avformat_close_input(&format);
	return 0;
}
