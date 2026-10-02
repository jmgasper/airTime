#!/usr/bin/env python3
"""Drive airTime's controls on the X399 with the mouse (over VNC) and check
what the player did through its scripting interface.

    python3 tools/ui-test-x399.py [--dev] [film]

Needs the workstation's VNC server and an installed airTime; --dev runs the
build tools/ws.sh made instead. The timings assume a film of a minute or
more; on shorter ones scanning reaches the end and the scan checks fail.
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, '/mnt/HaikuWork/x399/tools')
import vnc  # noqa: E402

SSH = ['ssh', '-F', '/mnt/HaikuWork/x399/ssh/config', 'ws-haiku']
APP = 'application/x-vnd.airOS-airTime'
DEV = '--dev' in sys.argv[1:]
ARGS = [a for a in sys.argv[1:] if a != '--dev']
FILM = ARGS[0] if ARGS else '/boot/home/media/multi.mkv'
failures = 0


def remote(command):
    return subprocess.run(SSH + [command], capture_output=True, text=True,
                          timeout=60).stdout


def hey(verb, prop, value=None):
    command = 'hey %s %s %s of Window 0' % (APP, verb, prop)
    if value is not None:
        command += " to '%s'" % value
    out = remote(command)
    match = re.search(r'"result" \([A-Z0-9_]+\) : (.*)', out)
    return match.group(1).strip() if match else out


def check(name, condition, detail=''):
    global failures
    print('%s %s %s' % ('PASS' if condition else 'FAIL', name, detail))
    if not condition:
        failures += 1


def rect():
    text = hey('get', 'Frame')
    numbers = [float(v) for v in re.findall(r'-?\d+(?:\.\d+)?', text)]
    return numbers[:4]


if DEV:
    subprocess.run([os.path.join(os.path.dirname(__file__), 'ws-run.sh'),
                    FILM], check=True)
else:
    remote('hey %s quit >/dev/null 2>&1; sleep 1; open %s' % (APP, FILM))
time.sleep(4)
hey('set', 'Frame', 'BRect(200,100,1159,700)')
time.sleep(1.5)                 # it snaps to the picture's aspect
left, top, right, bottom = rect()
width = right - left + 1
bar = bottom - 62 + 1          # top of the controller
row = bar + 43                 # its row of buttons
track = bar + 18               # the timeline
start = left + width / 2 - 74
play = (left + width / 2, row)
rewind = (start + 40, row)
forward = (start + 108, row)
to_start = (start + 11, row)
lcd = (right - 12 - 30 - 60, row)
full_screen = (right - 12 - 10, row)
volume_left = left + 12 + 16 + 5

client = vnc.VNC(password=os.environ.get('AIRTIME_VNC_PASSWORD', vnc.PASSWORD))
_pointer, _click, _drag = client.pointer, client.click, client.drag
client.pointer = lambda x, y, *a, **k: _pointer(int(x), int(y), *a, **k)
client.click = lambda x, y, *a, **k: _click(int(x), int(y), *a, **k)
client.drag = lambda x0, y0, x1, y1, *a, **k: _drag(int(x0), int(y0), int(x1),
                                                    int(y1), *a, **k)

# Play / pause
was = hey('get', 'Playing')
client.click(*play)
time.sleep(0.8)
now = hey('get', 'Playing')
check('play button toggles', was != now, '%s -> %s' % (was, now))
if now == 'FALSE':
    client.click(*play)
    time.sleep(0.5)

# A click on fast forward keeps scanning at 2x; play ends it.
client.click(*forward)
time.sleep(1.5)
rate = hey('get', 'Rate')
check('fast forward click scans', rate.startswith('2'), rate)
client.click(*forward)
time.sleep(1.0)
rate = hey('get', 'Rate')
check('second click doubles', rate.startswith('4'), rate)
client.click(*play)
time.sleep(1.0)
check('play returns to normal speed', hey('get', 'Rate').startswith('1'),
      hey('get', 'Rate'))

# Holding rewind scans backwards, and letting go plays on.
before = int(hey('get', 'Position').split()[0])
client.pointer(*rewind)
time.sleep(0.1)
client.pointer(*rewind, mask=1)
time.sleep(2.0)
rate = hey('get', 'Rate')
client.pointer(*rewind, mask=0)
time.sleep(1.0)
after = int(hey('get', 'Position').split()[0])
check('holding rewind scans backwards', rate.startswith('-'), rate)
check('rewinding moved back', after < before, '%d -> %d' % (before, after))
check('letting go resumes normal play', hey('get', 'Rate').startswith('1'))

# Dragging the playhead seeks.
duration = int(hey('get', 'Duration').split()[0])
x0 = left + 12 + (width - 24) * 0.25
x1 = left + 12 + (width - 24) * 0.75
client.drag(x0, track, x1, track)
time.sleep(1.5)
position = int(hey('get', 'Position').split()[0])
fraction = position / duration
check('dragging the timeline seeks', 0.7 < fraction < 0.85,
      '%.2f of the film' % fraction)

# Volume slider.
client.click(volume_left + 64 * 0.3, row)
time.sleep(0.5)
volume = float(hey('get', 'Volume'))
check('volume slider', 0.2 < volume < 0.4, '%.2f' % volume)
client.click(volume_left + 64 * 0.8, row)

# Go to the beginning.
client.click(*to_start)
time.sleep(0.3)
position = int(hey('get', 'Position').split()[0])
check('go to start', position < 2000000, str(position))

# Full screen button and Escape.
client.click(*full_screen)
time.sleep(2.0)
check('full screen button', hey('get', 'FullScreen') == 'TRUE')
client.key(vnc.keysym('Escape'))
time.sleep(2.0)
check('escape leaves full screen', hey('get', 'FullScreen') == 'FALSE')
check('the window comes back where it was', rect() == [left, top, right, bottom],
      str(rect()))

# Keyboard: space pauses, L plays forward faster.
client.click(left + width / 2, top + 150)      # focus the picture
time.sleep(0.8)
hey('set', 'Playing', 'true')
client.key(vnc.keysym(' '))
time.sleep(0.8)
check('space pauses', hey('get', 'Playing') == 'FALSE')
client.key(vnc.keysym('l'))
time.sleep(0.5)
client.key(vnc.keysym('l'))
time.sleep(1.0)
check('L twice plays at 2x', hey('get', 'Rate').startswith('2'),
      hey('get', 'Rate'))
client.key(vnc.keysym('k'))
time.sleep(0.8)
check('K pauses', hey('get', 'Playing') == 'FALSE')

check('the window stays put', rect() == [left, top, right, bottom],
      str(rect()))
client.close()
print('%d failures' % failures)
sys.exit(1 if failures else 0)
