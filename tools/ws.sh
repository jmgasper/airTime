#!/usr/bin/env bash
# Sync this tree to the X399 workstation and build it there.
#   tools/ws.sh [make arguments...]
# The workstation's clock runs ahead of this machine, so the tar is
# extracted with -m: files get the remote time and make sees them as new.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
X399=${X399:-/mnt/HaikuWork/x399}
HOST=${AIRTIME_WS_HOST:-ws-haiku}
SSH=(ssh -F "$X399/ssh/config" -o ConnectTimeout=10 "$HOST")
REMOTE=/boot/home/build/airTime
DEPS=/boot/home/build/airtime-deps

tar -C "$ROOT" --exclude=.git --exclude='build-*' --exclude=artifacts \
	--exclude=__pycache__ -cf - . \
	| "${SSH[@]}" "mkdir -p $REMOTE && tar -C $REMOTE -xmf -"
"${SSH[@]}" "cd $REMOTE && make -j16 \
	FFMPEG_CFLAGS=-I$DEPS/ffmpeg6_devel/develop/headers \
	FFMPEG_LDFLAGS=-L$DEPS/lib $*"
