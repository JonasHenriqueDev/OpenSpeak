#!/usr/bin/env python3
"""Gera o ícone do OpenSpeak: native/client/assets/openspeak.ico e public/favicon.svg.

Mesmo desenho de native/client/AppIcon.cpp (coordenadas em 512×512). Rode de novo
ao mudar o desenho:  python scripts/gen_icon.py   (requer Pillow)
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ACCENT = (0x16, 0xbc, 0xfb, 255)
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
OUTLINE = 38
SUPERSAMPLE = 8


def arc(cx, cy, r, start, end, steps=48):
    return [(cx + r * math.cos(math.radians(start + (end - start) * i / steps)),
             cy + r * math.sin(math.radians(start + (end - start) * i / steps))) for i in range(steps + 1)]


def cubic(p0, p1, p2, p3, steps=48):
    points = []
    for i in range(steps + 1):
        t = i / steps
        u = 1 - t
        points.append((u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
                       u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]))
    return points


def bubble():
    return (arc(382, 256, 80, -90, 0) + arc(382, 352, 80, 0, 90) + [(200, 432), (92, 486), (104, 426)]
            + cubic((104, 426), (72, 415), (50, 386), (50, 352)) + arc(130, 256, 80, 180, 270))


def eye():
    return [(122, 250), (232, 304)] + arc(194, 334, 38, 0, 90) + arc(160, 334, 38, 90, 180)


def antenna(p0, p1, p2, p3):
    # Mesma ampliação do grupo do SVG: translate(256 190) scale(1.25 1.15) translate(-256 -190).
    return [(256 + (x - 256) * 1.25, 190 + (y - 190) * 1.15) for x, y in cubic(p0, p1, p2, p3)]


def stroke(draw, points, width, color, scale, closed=False):
    pts = [(x * scale, y * scale) for x, y in points]
    if closed:
        pts.append(pts[0])
    draw.line(pts, fill=color, width=round(width * scale), joint='curve')
    r = width * scale / 2
    for x, y in pts:
        draw.ellipse((x - r, y - r, x + r, y + r), fill=color)


def render(size):
    full = size * SUPERSAMPLE
    scale = full / 512
    image = Image.new('RGBA', (full, full), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    stroke(draw, antenna((250, 170), (236, 150), (206, 140), (190, 128)), 34, ACCENT, scale)
    stroke(draw, antenna((262, 170), (286, 140), (330, 105), (372, 62)), 34, ACCENT, scale)
    stroke(draw, bubble(), OUTLINE, ACCENT, scale, closed=True)
    for mirror in (False, True):
        points = [(512 - x if mirror else x, y) for x, y in eye()]
        draw.polygon([(x * scale, y * scale) for x, y in points], fill=ACCENT)
        stroke(draw, points, 18, ACCENT, scale, closed=True)
    return image.resize((size, size), Image.LANCZOS)


def favicon():
    color = '#%02x%02x%02x' % ACCENT[:3]
    round_stroke = f'fill="none" stroke="{color}" stroke-linecap="round" stroke-linejoin="round"'
    eye_path = 'M122 250l110 54v30a38 38 0 0 1-38 38h-34a38 38 0 0 1-38-38z'
    eye_attrs = f'fill="{color}" stroke="{color}" stroke-width="18" stroke-linejoin="round"'
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img" aria-label="OpenSpeak">\n'
            f'<g transform="translate(256 190) scale(1.25 1.15) translate(-256 -190)">'
            f'<path d="M250 170C236 150 206 140 190 128" {round_stroke} stroke-width="34"/>'
            f'<path d="M262 170C286 140 330 105 372 62" {round_stroke} stroke-width="34"/></g>'
            f'<path d="M130 176h252a80 80 0 0 1 80 80v96a80 80 0 0 1-80 80H200l-108 54 12-60a80 80 0 0 1-54-74v-96a80 80 0 0 1 80-80z" {round_stroke} stroke-width="{OUTLINE}"/>'
            f'<path d="{eye_path}" {eye_attrs}/><g transform="matrix(-1 0 0 1 512 0)"><path d="{eye_path}" {eye_attrs}/></g>\n'
            '</svg>\n')


if __name__ == '__main__':
    images = [render(size) for size in SIZES]
    images[-1].save(ROOT / 'native/client/assets/openspeak.ico', sizes=[(s, s) for s in SIZES], append_images=images[:-1], bitmap_format='bmp')
    (ROOT / 'public/favicon.svg').write_text(favicon(), encoding='utf-8', newline='\r\n')
