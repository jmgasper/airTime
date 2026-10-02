#!/bin/sh
# Run on Haiku (x86_64). Makes FFmpeg headers available to the airTime build
# without installing ffmpeg6_devel, whose exact version requirement would
# upgrade the system's ffmpeg6: the package is extracted, and a directory of
# links points at the libraries already installed.
set -e
DEPS=${1:-/boot/home/build/airtime-deps}
REPO=https://haikuports-repository.cdn.haiku-os.org/master/x86_64/current
PACKAGE=ffmpeg6_devel-6.1.6-1-x86_64.hpkg
mkdir -p "$DEPS/lib"
cd "$DEPS"
if [ ! -f "$PACKAGE" ]; then
	curl -fsSLO "$REPO/packages/$PACKAGE"
fi
if [ ! -d ffmpeg6_devel ]; then
	mkdir ffmpeg6_devel
	package extract -C ffmpeg6_devel "$PACKAGE"
fi
for lib in avformat avcodec avfilter avutil swscale swresample; do
	target=$(ls /boot/system/lib/lib$lib.so.* | sort | head -1)
	ln -sf "$target" "$DEPS/lib/lib$lib.so"
done
ls -l "$DEPS/lib"
