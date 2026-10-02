#!/usr/bin/env bash
# Grab the X399's screen into a PNG here.
#   tools/ws-shot.sh out.png [crop WxH+X+Y] [scale%]
set -euo pipefail
X399=${X399:-/mnt/HaikuWork/x399}
OUT=${1:?output png}
SSH=(ssh -F "$X399/ssh/config" -o ConnectTimeout=10 ws-haiku)
"${SSH[@]}" '/boot/home/build/grab /boot/home/build/grab.ppm >/dev/null'
scp -F "$X399/ssh/config" -q ws-haiku:/boot/home/build/grab.ppm "$OUT.ppm"
python3 - "$OUT.ppm" "$OUT" "${2:-}" "${3:-}" <<'PY'
import sys
from PIL import Image
image = Image.open(sys.argv[1])
crop = sys.argv[3]
if crop:
    size, x, y = crop.split('+')[0], int(crop.split('+')[1]), int(crop.split('+')[2])
    w, h = (int(v) for v in size.split('x'))
    image = image.crop((x, y, x + w, y + h))
if sys.argv[4]:
    scale = float(sys.argv[4]) / 100
    image = image.resize((int(image.width * scale), int(image.height * scale)), Image.LANCZOS)
image.save(sys.argv[2])
print(sys.argv[2], image.size)
PY
rm -f "$OUT.ppm"
