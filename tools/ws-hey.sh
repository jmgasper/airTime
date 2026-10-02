#!/usr/bin/env bash
# Run a sequence of airTime scripting commands on the X399. Each argument is
# one command: "get Stats", "set Rate to 1.5", "sleep 2", "log".
#   tools/ws-hey.sh "get Stats" "set Position to 10000000" "sleep 1" ...
set -euo pipefail
X399=${X399:-/mnt/HaikuWork/x399}
SCRIPT='A=application/x-vnd.airOS-airTime'
for command in "$@"; do
	case "$command" in
		sleep*) SCRIPT+=$'\n'"$command" ;;
		log) SCRIPT+=$'\n'"cat /boot/home/build/airTime/run.log" ;;
		shell:*) SCRIPT+=$'\n'"${command#shell:}" ;;
		*)
			verb=${command%% *}
			rest=${command#* }
			property=${rest%% to *}
			value=""
			[[ "$rest" == *" to "* ]] && value=" to ${rest#* to }"
			SCRIPT+=$'\n'"printf '%s: ' \"$command\"; hey \$A $verb $property of Window 0$value 2>&1 | grep -E 'result|message|error' | grep -v 'error.*: 0 (0x' | sed -e 's/^ *//' | tr '\n' ' '; echo"
			;;
	esac
done
ssh -F "$X399/ssh/config" -o ConnectTimeout=10 ws-haiku "$SCRIPT"
