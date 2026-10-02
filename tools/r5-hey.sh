#!/usr/bin/env bash
# Run airTime scripting commands on the ROCK 5 through the lab shell, like
# tools/ws-hey.sh does on the X399:
#   tools/r5-hey.sh "run file" "get Stats" "set Rate to 2" "sleep 2" "log" ...
# "run <args>" (re)starts the installed airTime with those arguments.
set -euo pipefail
LAB=/mnt/HaikuWork/src/haiku/tools/rock5-itx
PY=/mnt/HaikuWork/nanokvm/.venv/bin/python
WORKDIR=/mnt/HaikuWork/tmp/airtime-r5
mkdir -p "$WORKDIR"
SCRIPT=$WORKDIR/hey-$$.cmd
OUT=$WORKDIR/hey-$$.txt
trap 'rm -f "$SCRIPT" "$OUT"' EXIT
APP=${AIRTIME_APP:-/boot/system/apps/airTime}
{
	echo 'A=application/x-vnd.airOS-airTime'
	echo 'cd /boot/home/rock5-lab'
	for command in "$@"; do
		case "$command" in
			run\ *)
				echo "hey \$A quit >/dev/null 2>&1 || true"
				echo "sleep 1"
				# Whichever copy is being restarted, and the installed one:
				# they share a signature, and a single-launch app hands its
				# files to the copy already running.
				echo "for t in \$(ps | grep -v -e grep -e bash | awk '/apps\\/airTime|airTime\\.dev/ { for (i = 2; i <= NF; i++) if (\$i ~ /^[0-9]+\$/) { print \$i; break } }'); do kill -9 \$t || true; done"
				echo "(AIRTIME_TRACE=1 $APP ${command#run } > airtime.log 2>&1 &)"
				echo "sleep 3"
				;;
			sleep*) echo "$command" ;;
			log) echo "cat airtime.log | grep -v -e Consider -e 'Could not find codec' | tail -40" ;;
			shell:*) echo "${command#shell:}" ;;
			*)
				verb=${command%% *}
				rest=${command#* }
				property=${rest%% to *}
				value=""
				[[ "$rest" == *" to "* ]] && value=" to ${rest#* to }"
				echo "printf '%s: ' \"$command\"; hey \$A $verb $property of Window 0$value 2>&1 | grep -E 'result|message|error' | grep -v 'error.*: 0 (0x' | sed -e 's/^ *//' | tr '\\n' ' '; echo"
				;;
		esac
	done
	echo "true"
} > "$SCRIPT"
( cd /mnt/HaikuWork/src/haiku && timeout 600 "$PY" "$LAB/shell.py" run 10.239.6.100 \
	"$SCRIPT" --timeout 500 --output "$OUT" >/dev/null 2>&1 ) || true
sed -n '/ROCK5_BEGIN/,/ROCK5_END/p' "$OUT" | grep -v "ROCK5_BEGIN\|ROCK5_END"
