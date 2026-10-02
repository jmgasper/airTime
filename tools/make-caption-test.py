#!/usr/bin/env python3
"""Make a test film with CEA-608 closed captions carried the way broadcast
and most web video carries them: ATSC A/53 cc_data in H.264 SEI messages.

    tools/make-caption-test.py <source video> <out dir>

Writes captions.mp4, captions.mkv and captions.ts (30 seconds, 1280x720,
30 fps), each with pop-on captions "CAPTION n AT t S" every four seconds,
shown for three seconds. Needs ffmpeg with libx264.
"""
import os
import subprocess
import sys

FPS = 30
SECONDS = 30


def odd_parity(value):
    value &= 0x7f
    return value | 0x80 if bin(value).count('1') % 2 == 0 else value


def caption_pairs(text):
    """Byte pairs that load a pop-on caption on row 15 and show it."""
    pairs = []

    def control(a, b):
        pairs.extend([(odd_parity(a), odd_parity(b))] * 2)

    control(0x14, 0x20)        # resume caption loading
    control(0x14, 0x2e)        # erase non-displayed memory
    control(0x14, 0x70)        # row 15, indent 0
    for i in range(0, len(text), 2):
        first = odd_parity(ord(text[i]))
        second = odd_parity(ord(text[i + 1])) if i + 1 < len(text) else 0x80
        pairs.append((first, second))
    control(0x14, 0x2f)        # end of caption: flip it onto the screen
    return pairs


def erase_pairs():
    pairs = []
    pairs.extend([(odd_parity(0x14), odd_parity(0x2c))] * 2)   # erase shown
    return pairs


def schedule():
    """The cc pair for every frame, in display order."""
    frames = [(0x80, 0x80)] * (FPS * SECONDS)
    number = 1
    for start in range(2, SECONDS - 3, 4):
        text = "CAPTION %d AT %d S" % (number, start)
        pairs = caption_pairs(text)
        # Load so that the flip happens on the second the caption starts.
        first = start * FPS - len(pairs) + 1
        for i, pair in enumerate(pairs):
            frames[first + i] = pair
        for i, pair in enumerate(erase_pairs()):
            frames[(start + 3) * FPS + i] = pair
        number += 1
    return frames


def sei_nal(pair):
    cc = [0xfc, pair[0], pair[1]]
    payload = [0xb5, 0x00, 0x31] + list(b'GA94') + [0x03, 0x40 | 1, 0xff] \
        + cc + [0xff]
    rbsp = [0x04, len(payload)] + payload + [0x80]
    nal = [0x06]
    zeros = 0
    for byte in rbsp:
        if zeros >= 2 and byte <= 3:
            nal.append(0x03)
            zeros = 0
        nal.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return bytes([0, 0, 0, 1] + nal)


def split_nals(data):
    """(start code offset, nal start, nal type) for every NAL unit."""
    units = []
    i = 0
    while True:
        j = data.find(b'\x00\x00\x01', i)
        if j < 0:
            break
        start = j + 3
        prefix = j - 1 if j > 0 and data[j - 1] == 0 else j
        units.append((prefix, start, data[start] & 0x1f))
        i = start
    return units


def main():
    source, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    raw = os.path.join(out, 'captions-raw.h264')
    subprocess.check_call([
        'ffmpeg', '-v', 'error', '-y', '-i', source, '-t', str(SECONDS),
        '-vf', 'scale=1280:720,fps=%d' % FPS, '-an', '-c:v', 'libx264',
        '-preset', 'veryfast', '-bf', '0', '-x264-params', 'aud=1',
        '-f', 'h264', raw])
    data = open(raw, 'rb').read()
    units = split_nals(data)
    frames = schedule()

    output = bytearray()
    frame = 0
    for index, (prefix, start, kind) in enumerate(units):
        end = units[index + 1][0] if index + 1 < len(units) else len(data)
        output += data[prefix:end]
        # The access unit delimiter opens every picture: the captions of
        # that picture follow it.
        if kind == 9:
            if frame < len(frames):
                output += sei_nal(frames[frame])
            frame += 1
    annexb = os.path.join(out, 'captions.h264')
    open(annexb, 'wb').write(bytes(output))
    os.remove(raw)

    for name, extra in (('captions.mp4', ['-movflags', '+faststart']),
                        ('captions.mkv', []), ('captions.ts', [])):
        subprocess.check_call([
            'ffmpeg', '-v', 'error', '-y', '-r', str(FPS), '-i', annexb,
            '-i', source, '-map', '0:v', '-map', '1:a', '-t', str(SECONDS),
            '-c:v', 'copy', '-c:a', 'aac'] + extra + [os.path.join(out, name)])
    print('%d pictures, captions every 4 s' % frame)


if __name__ == '__main__':
    main()
