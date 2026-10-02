#!/usr/bin/env bash
# Start (or restart) the built airTime on the X399 with the given arguments,
# its output going to /boot/home/build/airTime/run.log.
#   tools/ws-run.sh [--keep] [airTime arguments...]
# airTime is single launch: a second start only hands its files to the first
# one, so the running one is quit first unless --keep is given.
set -euo pipefail
X399=${X399:-/mnt/HaikuWork/x399}
SSH=(ssh -F "$X399/ssh/config" -o ConnectTimeout=10 ws-haiku)
KEEP=0
if [ "${1:-}" = "--keep" ]; then KEEP=1; shift; fi
ARGS=$(printf ' %q' "$@")
"${SSH[@]}" "cd /boot/home/build/airTime
if [ $KEEP = 0 ]; then
	hey application/x-vnd.airOS-airTime quit >/dev/null 2>&1 || true
	for i in 1 2 3 4 5 6 7 8 9 10; do
		ps | grep -v -e bash -e grep | grep -q 'build-haiku/airTime' || break
		sleep 0.3
	done
	for team in \$(ps | grep -v -e bash -e grep -e awk | awk '/build-haiku\\/airTime/ { for (i = 2; i <= NF; i++) if (\$i ~ /^[0-9]+\$/) { print \$i; break } }'); do
		kill -9 \$team 2>/dev/null || true
	done
fi
(AIRTIME_TRACE=\${AIRTIME_TRACE:-1} nohup build-haiku/airTime $ARGS > run.log 2>&1 &)
sleep 1
echo started"
