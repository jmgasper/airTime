#!/usr/bin/env bash
# Upload a file of any size to the ROCK 5's lab shell (/boot/home/rock5-lab),
# in 15 MiB pieces joined and checked on the board.
#   tools/r5-upload.sh <file> [name]
set -euo pipefail
SOURCE=$1
NAME=${2:-$(basename "$SOURCE")}
LAB=/mnt/HaikuWork/src/haiku/tools/rock5-itx
PY=/mnt/HaikuWork/nanokvm/.venv/bin/python
WORKDIR=/mnt/HaikuWork/tmp/airtime-r5/upload-$$
mkdir -p "$WORKDIR"
trap 'rm -rf "$WORKDIR"' EXIT
SUM=$(sha256sum "$SOURCE" | cut -d' ' -f1)
split -b 15M -d "$SOURCE" "$WORKDIR/part."
PARTS=()
for part in "$WORKDIR"/part.*; do
	piece="$NAME.$(basename "$part")"
	( cd /mnt/HaikuWork/src/haiku && timeout 600 "$PY" "$LAB/shell.py" upload \
		10.239.6.100 "$part" --name "$piece" --output "$WORKDIR/upload.txt" >/dev/null )
	PARTS+=("$piece")
done
{
	echo "cd /boot/home/rock5-lab"
	# A new file renamed into place: a program running from the old one keeps
	# it, where writing over it changes the code it is running (and crashes
	# it, as an airTime.dev still playing once did).
	echo "cat ${PARTS[*]} > .$NAME.new"
	echo "chmod --reference=$NAME .$NAME.new 2>/dev/null || true"
	echo "mv -f .$NAME.new $NAME"
	echo "rm ${PARTS[*]}"
	echo "test \"\$(sha256sum $NAME | cut -d' ' -f1)\" = $SUM && echo uploaded $NAME"
	# As in r5-hey.sh: the login shell would otherwise keep its PTY.
	echo '(sleep 5; kill -9 $$) > /dev/null 2>&1 &'
} > "$WORKDIR/join.cmd"
( cd /mnt/HaikuWork/src/haiku && timeout 300 "$PY" "$LAB/shell.py" run 10.239.6.100 \
	"$WORKDIR/join.cmd" --output "$WORKDIR/join.txt" >/dev/null 2>&1 ) || true
grep -E "uploaded|No such|ROCK5_END" "$WORKDIR/join.txt" | head -3
