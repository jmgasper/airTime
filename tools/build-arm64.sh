#!/usr/bin/env bash
# Cross-builds airTime for arm64 (the ROCK 5 ITX) on the Linux build host and
# makes the package artifacts/airtime-<version>-arm64.hpkg.
#
#   tools/build-arm64.sh            build and package
#   tools/build-arm64.sh build      only build build-arm64/airTime
#   tools/build-arm64.sh tests      build the host tests for the board, to
#                                   try the NEON code there (engine_tests,
#                                   renderer_tests in build-arm64)
#
# What it needs from the air/OS work tree (all overridable):
#   CROSS     the arm64 cross compiler prefix
#   SYSROOT   an arm64 Haiku sysroot with libmedia, libgame, liblocalestub.a
#             and the private headers (Summit's is the most complete)
#   FFMPEG    the arm64 FFmpeg 6.1.6 stage (headers and libraries) the
#             rock5_ffmpeg package was made from
#   TOOLS     host builds of rc, xres, resattr, mimeset and package
#   MIMEDB    the arm64 build's system MIME database
#
# The package requires rock5_ffmpeg, which installs those libraries (and the
# RK3588 decoder add-on) on the board. It is staged the way
# tools/package-haiku.sh stages it natively; resattr and mimeset put the
# resources into attributes as mimeset would on Haiku.
set -euo pipefail
umask 022

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
WORK=/mnt/HaikuWork
CROSS=${CROSS:-$WORK/build/arm64/cross-tools-arm64/bin/aarch64-unknown-haiku-}
SYSROOT=${SYSROOT:-$WORK/build/summit-arm64/sysroot}
FFMPEG=${FFMPEG:-$WORK/artifacts/ffmpeg-arm64/stage/boot/system/non-packaged}
TOOLS=${TOOLS:-$WORK/build/arm64/objects/linux/x86_64/release/tools}
MIMEDB=${MIMEDB:-$WORK/build/arm64/objects/common/data/mime_db/mime_db}
JOBS=${JOBS:-12}
export TMPDIR=${TMPDIR:-$WORK/tmp}

BUILD=$ROOT/build-arm64
HOSTBIN=$BUILD/.hostbin
FARM=$BUILD/.ffmpeg
mkdir -p "$HOSTBIN" "$FARM/lib"

# rc and xres by name are the host builds; mimeset runs on the stage.
printf '#!/bin/sh\nexec "%s" "$@"\n' "$TOOLS/rc/rc" > "$HOSTBIN/rc"
printf '#!/bin/sh\nexec "%s" "$@"\n' "$TOOLS/xres" > "$HOSTBIN/xres"
chmod 755 "$HOSTBIN/rc" "$HOSTBIN/xres"
# The cross compiler links libgcc.a, whose private unwinder libstdc++'s
# __cxa_throw never sees: every C++ exception would end in std::terminate.
UNWIND_SPECS=$HOSTBIN/shared-unwinder.specs
printf '*libgcc:\n-lgcc_s -lgcc\n\n' > "$UNWIND_SPECS"

# Exactly the FFmpeg libraries -l should find.
for lib in avformat avcodec avfilter avutil swscale swresample; do
	target=$(ls "$FFMPEG/lib/lib$lib.so."* | sort | head -1)
	ln -sf "$target" "$FARM/lib/lib$lib.so"
done

CXX="${CROSS}g++ --sysroot=$SYSROOT -specs=$UNWIND_SPECS"
if [[ ${1:-} == tests ]]; then
	TESTFLAGS=(-std=c++17 -O2 -Wall -Wno-multichar -I"$ROOT/src/engine"
		-I"$ROOT/src/ui" -I"$FFMPEG/include")
	LINK=(-L"$FARM/lib" -Wl,-rpath-link,"$FFMPEG/lib" -lavformat -lavcodec
		-lswscale -lavutil -lbe)
	$CXX "${TESTFLAGS[@]}" -o "$BUILD/engine_tests" "$ROOT/tests/EngineTests.cpp" \
		"$ROOT"/src/engine/{Tracks,Languages,Bitstream,Subtitles}.cpp \
		"$ROOT/src/ui/YuvScaler.cpp" "${LINK[@]}"
	$CXX "${TESTFLAGS[@]}" -o "$BUILD/renderer_tests" \
		"$ROOT/tests/RendererTests.cpp" "$ROOT/src/ui/FrameRenderer.cpp" \
		"$ROOT/src/ui/YuvScaler.cpp" "${LINK[@]}"
	echo "$BUILD/engine_tests $BUILD/renderer_tests"
	exit 0
fi
make -C "$ROOT" -j"$JOBS" BUILD=build-arm64 CXX="$CXX" \
	HAIKU_HEADERS="$SYSROOT/boot/system/develop/headers" \
	FFMPEG_CFLAGS="-I$FFMPEG/include" \
	FFMPEG_LDFLAGS="-L$FARM/lib -Wl,-rpath-link,$FFMPEG/lib" \
	RC="$HOSTBIN/rc" XRES="$HOSTBIN/xres" MIMESET=true

[[ ${1:-} == build ]] && exit 0

# Stage and package.
INFO=$ROOT/resources/airTime.PackageInfo
NAME=$(awk '$1 == "name" { print $2; exit }' "$INFO")
VERSION=$(awk '$1 == "version" { print $2; exit }' "$INFO")
FILE=$ROOT/artifacts/$NAME-$VERSION-arm64.hpkg
STAGE=$BUILD/stage
[[ -e $STAGE ]] && "$TOOLS/rm_attrs" -rf "$STAGE"
mkdir -p "$STAGE/apps" "$STAGE/documentation/packages/airtime" \
	"$STAGE/data/deskbar/menu/Applications" "$STAGE/boot/post-install" \
	"$ROOT/artifacts"

cp "$BUILD/airTime" "$STAGE/apps/airTime"
chmod 755 "$STAGE/apps/airTime"
"${CROSS}strip" --strip-debug "$STAGE/apps/airTime"
"$TOOLS/xres" -o "$STAGE/apps/airTime" "$BUILD/airTime.rsrc"
# Tracker and Deskbar read the signature and icon from attributes.
"$TOOLS/resattr/resattr" -O -o "$STAGE/apps/airTime" "$BUILD/airTime.rsrc"

install -m 755 "$ROOT/resources/scripts/airtime-register-default.sh" \
	"$STAGE/boot/post-install/"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$STAGE/documentation/packages/airtime/"
ln -s ../../../../apps/airTime "$STAGE/data/deskbar/menu/Applications/airTime"

# arm64: FFmpeg comes from rock5_ffmpeg, which provides no lib: entries.
awk '
	/^architecture[ \t]/ { print "architecture arm64"; next }
	/^requires[ \t]*\{/ {
		print
		print "\thaiku >= r1~beta6"
		print "\trock5_ffmpeg >= 6.1.6"
		skip = 1
		next
	}
	skip && /^[ \t]*\}/ { print; skip = 0; next }
	skip { next }
	{ print }
' "$INFO" > "$STAGE/.PackageInfo"

( cd "$STAGE" && "$TOOLS/mimeset" --all -f --mimedb data/mime_db \
	--mimedb "$MIMEDB" apps/airTime )

rm -f "$FILE"
"$TOOLS/package/package" create -q -C "$STAGE" "$FILE"
"$TOOLS/package/package" list -i "$FILE" | grep -E "architecture|version|requires"
echo "$FILE"
