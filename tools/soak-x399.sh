#!/usr/bin/env bash
# Play a list of films on the X399 one after another with the installed
# airTime: start, seek to the middle, scan forward, play on, and report for
# each what decoded it, how smoothly it played and whether it survived.
#   tools/soak-x399.sh films.txt      one path on the X399 per line
set -uo pipefail
X399=${X399:-/mnt/HaikuWork/x399}
SSH=(ssh -n -F "$X399/ssh/config" -o ConnectTimeout=10 ws-haiku)
APP=application/x-vnd.airOS-airTime
LIST=${1:?list of films}

field() { sed -n "s/.*[ \"]$1=\([^ \"]*\).*/\1/p" | head -1; }

while IFS= read -r film; do
	[ -z "$film" ] && continue
	out=$("${SSH[@]}" "A=$APP
		hey \$A quit >/dev/null 2>&1; sleep 1
		for t in \$(ps | grep -v grep | awk '/apps\\/airTime/ { for (i = 2; i <= NF; i++) if (\$i ~ /^[0-9]+\$/) { print \$i; break } }'); do kill -9 \$t; done
		(/boot/system/apps/airTime $(printf %q "$film") > /tmp/soak.log 2>&1 &)
		sleep 6
		hey \$A get Decoder of Window 0 | grep result
		hey \$A get Stats of Window 0 | grep result
		d=\$(hey \$A get Duration of Window 0 | sed -n 's/.*: \\([0-9]*\\) .*/\\1/p')
		hey \$A set Position of Window 0 to \$((d / 2)) >/dev/null; sleep 5
		hey \$A get Stats of Window 0 | grep result
		hey \$A set Rate of Window 0 to 8 >/dev/null; sleep 3
		hey \$A get Position of Window 0 | grep result
		hey \$A set Rate of Window 0 to 1 >/dev/null; sleep 5
		hey \$A get Stats of Window 0 | grep result
		ps | grep -v grep | grep -c 'apps/airTime'
		ls /boot/home/Desktop/airTime-*.report 2>/dev/null | wc -l" 2>&1)
	decoder=$(echo "$out" | sed -n '1s/.*: "\(.*\)"/\1/p')
	stats=$(echo "$out" | grep -c 'position=')
	last=$(echo "$out" | grep 'position=' | tail -1)
	alive=$(echo "$out" | tail -2 | head -1)
	printf '%s\n  %s\n  fps=%s dropped=%s decode=%s stats=%s alive=%s reports=%s\n' \
		"$(basename "$film")" "$decoder" "$(echo "$last" | field fps)" \
		"$(echo "$last" | field dropped)" "$(echo "$last" | field decode)" \
		"$stats" "$alive" "$(echo "$out" | tail -1)"
done < "$LIST"
"${SSH[@]}" "hey $APP quit >/dev/null 2>&1"
