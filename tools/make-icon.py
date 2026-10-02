#!/usr/bin/env python3
"""Build airTime's application icon: a glossy blue disc in a brushed silver
ring with clock marks, and a white play triangle; readable from 16 px up.

    python3 tools/make-icon.py resources/branding/airtime-icon.hvif [preview.png]
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hvif  # noqa: E402

C = hvif.hex_color
K = 0.5523


def circle(cx, cy, r):
    k = K * r
    return {'closed': True, 'points': [
        ((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r)),
        ((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
        ((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
        ((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k)),
    ]}


def ellipse(cx, cy, rx, ry):
    kx, ky = K * rx, K * ry
    return {'closed': True, 'points': [
        ((cx, cy - ry), (cx - kx, cy - ry), (cx + kx, cy - ry)),
        ((cx + rx, cy), (cx + rx, cy - ky), (cx + rx, cy + ky)),
        ((cx, cy + ry), (cx + kx, cy + ry), (cx - kx, cy + ry)),
        ((cx - rx, cy), (cx - rx, cy + ky), (cx - rx, cy - ky)),
    ]}


def tick(angle, inner, outer, cx=32, cy=32):
    a = math.radians(angle)
    return {'closed': False, 'points': [
        (cx + math.sin(a) * inner, cy - math.cos(a) * inner),
        (cx + math.sin(a) * outer, cy - math.cos(a) * outer)]}


# ------------------------------------------------------------------ styles
SHADOW = hvif.radial_gradient((32, 58), 24, [
    (0.0, (10, 20, 40, 110)), (0.6, (10, 20, 40, 40)), (1.0, (10, 20, 40, 0))],
    ratio=0.16)
RING = hvif.linear_gradient((32, 4), (32, 60), [
    (0.0, C('fbfbfd')), (0.45, C('c9ccd2')), (0.55, C('aeb2b9')),
    (1.0, C('e6e8ec'))])
RING_EDGE = {'color': C('4d535c')}
DISC = hvif.radial_gradient((28, 22), 40, [
    (0.0, C('8fd0ff')), (0.45, C('3a8ff0')), (1.0, C('0f3f9e'))])
DISC_EDGE = {'color': C('0b2c6e')}
GLOSS = hvif.linear_gradient((32, 13), (32, 33), [
    (0.0, (255, 255, 255, 190)), (1.0, (255, 255, 255, 0))])
TICKS = {'color': C('3a4048')}
PLAY = hvif.linear_gradient((26, 22), (26, 44), [
    (0.0, C('ffffff')), (1.0, C('dfe9f6'))])
PLAY_SHADOW = {'color': (0, 20, 60, 90)}

style_names = ['shadow', 'ring', 'ringEdge', 'disc', 'discEdge', 'gloss',
               'ticks', 'play', 'playShadow']
styles = [SHADOW, RING, RING_EDGE, DISC, DISC_EDGE, GLOSS, TICKS, PLAY,
          PLAY_SHADOW]
S = {name: index for index, name in enumerate(style_names)}

# ------------------------------------------------------------------ paths
play = {'closed': True, 'points': [(26, 21.5), (26, 42.5), (44, 32)]}
play_shadow = {'closed': True, 'points': [(27, 23.5), (27, 44.5), (45, 34)]}
paths = {
    'shadow': circle(32, 58, 1),
    'ring': circle(32, 32, 27),
    'disc': circle(32, 32, 21),
    'gloss': ellipse(32, 22, 15, 9.5),
    'play': play,
    'playShadow': play_shadow,
}
tick_names = []
for i, angle in enumerate(range(0, 360, 30)):
    name = 'tick%d' % i
    major = angle % 90 == 0
    paths[name] = tick(angle, 22.8, 25.8 if major else 24.6)
    tick_names.append((name, major))

path_names = list(paths.keys())
P = {name: index for index, name in enumerate(path_names)}


def shape(style, *names, **extra):
    result = {'style': S[style], 'paths': [P[n] for n in names]}
    result.update(extra)
    return result


SMALL = {'lod': (0.0, 0.5)}
LARGE = {'lod': (0.5, 4.0)}
contour = {'type': 'contour', 'width': 1.5, 'join': 2, 'miter': 4}
edge = {'type': 'contour', 'width': 1, 'join': 2, 'miter': 4}
tick_major = {'type': 'stroke', 'width': 2, 'join': 0, 'cap': 0, 'miter': 4}
tick_minor = {'type': 'stroke', 'width': 1, 'join': 0, 'cap': 0, 'miter': 4}

shapes = [
    shape('shadow', 'shadow', matrix=[24.0, 0.0, 0.0, 4.0, 32.0 - 32.0 * 24.0,
                                      58.0 - 58.0 * 4.0], **LARGE),
    shape('ringEdge', 'ring', transformers=[contour]),
    shape('ring', 'ring'),
    shape('ticks', *[n for n, major in tick_names if major],
          transformers=[tick_major], **LARGE),
    shape('ticks', *[n for n, major in tick_names if not major],
          transformers=[tick_minor], **LARGE),
    shape('discEdge', 'disc', transformers=[edge]),
    shape('disc', 'disc'),
    shape('gloss', 'gloss', **LARGE),
    shape('playShadow', 'playShadow', **LARGE),
    shape('play', 'play'),
]

icon = {'styles': styles, 'paths': [paths[n] for n in path_names],
        'shapes': shapes}

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'airtime-icon.hvif'
    data = hvif.encode(icon)
    hvif.decode(data)
    with open(out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes' % (out, len(data)))
    svg = out.rsplit('.', 1)[0] + '.svg'
    with open(svg, 'w') as f:
        f.write(hvif.to_svg(icon))
    if len(sys.argv) > 2:
        image = hvif.preview(icon, 256)
        image.save(sys.argv[2])
        print('%s: preview' % sys.argv[2])
