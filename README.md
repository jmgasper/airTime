# airTime

The movie player of air/OS. It plays films and music in a window modelled on
the QuickTime Player of Mac OS 9 and the first versions of Mac OS X —
brushed aluminium, a khaki LCD, round metal transport buttons and the little
pentagon playhead — and it does the decoding on the graphics hardware where
it can.

![airTime icon](resources/branding/airtime-icon.svg)

## What it does

* **Everything FFmpeg reads**: MP4/MOV, Matroska/WebM, AVI, MPEG transport
  and program streams, Ogg, FLV, and the audio formats (MP3, AAC, FLAC,
  Opus, Vorbis, WAV, AIFF, AC-3, DTS…). Video: H.264, HEVC (8 and 10 bit),
  AV1, VP9, VP8, MPEG-2/4, ProRes, VC-1 and the rest of libavcodec.
* **Hardware decoding** through the Media Kit's decoder add-ons, which airTime
  loads itself so that it can choose them and fall back to libavcodec when
  one refuses a stream:
  * **X399 workstation** — NVDEC on the GeForce card (the `nvdec` add-on):
    8-bit 4:2:0 progressive H.264.
  * **ROCK 5 ITX** — the RK3588's own decoders through Rockchip MPP (the
    `00_rockchip_mpp` add-on): H.264 and HEVC on RKVDEC, AV1 on VPU981.

  The Movie Inspector (⌘I) says which decoder is in use and, when it is the
  processor, why the hardware was not used.
* **HDR10 and HLG** films are tone mapped for an ordinary display.
* **Window resizing** that keeps the film's proportions once the resize
  corner is let go (View ▸ Keep Proportions), plus Half, Actual and Double
  Size and Fit to Screen (⌘0–⌘3), as QuickTime had.
* **Subtitles**: embedded text tracks (SubRip, ASS/SSA with italics, colours
  and placement, WebVTT, MP4 timed text) and picture tracks (Blu-ray PGS,
  DVD VobSub, DVB); subtitle files next to the film are found by name
  (`Film.en.srt`, `Film.srt`); more can be dropped on the window or added
  from the File menu. Files that are not UTF-8 are read as Windows-1252.
  Forced subtitles in the language being heard are shown automatically.
* **Closed captions**: CEA-608 captions carried in H.264, HEVC and MPEG-2
  video (ATSC A/53, as broadcast and most web video carries them) and
  QuickTime `c608` tracks, shown on a black box as a television shows them
  (Subtitles ▸ Closed Captions, or C).
* **Audio tracks** listed by language, title, format and channels; the
  choice is remembered as a preferred language for the next film.
* **Fast forward and rewind**: hold the ◀◀ or ▶▶ button to scan, faster the
  longer it is held; click it to keep scanning (2×, 4×, 8×… up to 64×).
  J, K and L work as in editing programs. Scanning steps through key frames,
  so it is quick whatever the film.
* **Playback speeds** from half to triple speed with the pitch kept
  (Playback ▸ Playback Speed, or [ and ]).
* **Seeking** anywhere by dragging the playhead, to a typed time
  (Playback ▸ Go to Time…, ⌘G), by chapter, or a frame at a time.
* **Full screen** (⌘F, F, or a double click) on the monitor the window is on,
  with a floating controller that hides when the mouse rests.
* Opens what Tracker opens: once installed, double-clicking a film or a song
  plays it in airTime.

## Keys

| Key | |
| --- | --- |
| Space, or a click on the picture | Play / pause |
| ← → | One frame back / forward (Shift: 10 seconds) |
| Option ← → | Beginning / end |
| ↑ ↓ | Volume |
| J K L | Reverse, pause, forward — again for faster |
| [ ] \ | Slower, faster, normal speed |
| F, Esc | Full screen, leave it |
| S, A, C | Next subtitle track, next audio track, captions |
| M | Mute |
| Page Up / Down | Previous / next chapter |
| I | Movie Inspector |

## How it is built

```
src/engine/   playback: demuxing, decoding, clocks, subtitles, captions
src/ui/       the window, the metal controller, the video view
src/          the application and its settings
tests/        host-side tests of the engine (make check-host)
tools/        building, packaging, the icon, test material
```

* `Player` runs four threads: a reader (libavformat) that feeds packet
  queues, a video decoder, an audio decoder, and a presentation thread that
  times pictures against the audio clock (or the system clock).
* Video decoders pull their packets: libavcodec with frame threads, or a
  Media Kit decoder add-on driven through a chunk provider.
* Sound goes through libswresample to 48 kHz stereo float, through
  libavfilter's `atempo` when the speed is not normal, into a ring that a
  `BSoundPlayer` empties; the ring records the media time of each stretch, and
  that sets the clock.
* Pictures are scaled and converted by libswscale on several threads, to the
  size they are shown at; subtitles, captions and messages are drawn into
  them, and they go to the screen through the frame buffer (BDirectWindow)
  where the app_server allows it and DrawBitmap otherwise.

## Building

On air/OS or Haiku (x86_64), with `haiku_devel` and FFmpeg 6:

```
make                     # build-haiku/airTime
tools/package-haiku.sh   # artifacts/airtime-<version>-x86_64.hpkg
```

Where `ffmpeg6_devel` cannot be installed (its exact version requirement
would upgrade the system's FFmpeg), `tools/setup-ffmpeg-devel.sh` extracts
its headers instead; pass `FFMPEG_CFLAGS` and `FFMPEG_LDFLAGS` as
`tools/ws.sh` does.

For the ROCK 5 (arm64), cross-built on Linux: `tools/build-arm64.sh`.

## Testing

* `make check-host` runs the engine tests on Linux (time and language
  helpers, ASS markup, subtitle files in other encodings, SPS parsing,
  caption extraction and decoding).
* `tools/make-caption-test.py <film> <dir>` makes a clip with CEA-608
  captions in its H.264 SEI messages.
* The player answers to scripting, which the lab tools use: for example
  `hey airTime get Stats of Window 0`, `set Position of Window 0 to 60000000`,
  `set Rate of Window 0 to 8`, `get SubtitleText of Window 0`,
  `get Decoder of Window 0`.

## Licence

MIT. FFmpeg is LGPL 2.1 and is used as shared libraries.
