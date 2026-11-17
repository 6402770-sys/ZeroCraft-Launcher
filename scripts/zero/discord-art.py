# SPDX-License-Identifier: GPL-3.0-only
# ZERO Launcher - renders the Discord Rich Presence art (program_info/discord/) from program_info/zero-logo-minecraft.svg.
# Usage: python scripts/zero/discord-art.py program_info/zero-logo-minecraft.svg program_info/discord
import re, sys
from PIL import Image, ImageDraw
src, outdir = sys.argv[1], sys.argv[2]
svg = open(src, encoding='utf-8').read()
cells = {}
for m in re.finditer(r'<rect x="(\d+)" y="(\d+)" width="(\d+)" height="(\d+)" fill="(#[0-9A-Fa-f]{6})"/>', svg):
    x, y, w, h = (int(v)//16 for v in m.groups()[:4]); c = m.group(5)
    for i in range(x, x+w):
        for j in range(y, y+h):
            cells[(i, j)] = c
def hexrgb(c): return tuple(int(c[k:k+2], 16) for k in (1, 3, 5)) + (255,)
def mark(px):
    im = Image.new('RGBA', (16*px, 16*px), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    for (i, j), c in cells.items():
        d.rectangle([i*px, j*px, (i+1)*px-1, (j+1)*px-1], fill=hexrgb(c))
    return im
# 1) square art asset, key "zero": 1024x1024, transparent, mark 896 px centred
sq = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0)); sq.alpha_composite(mark(56), (64, 64))
sq.save(f'{outdir}/zero-rich-presence-1024.png')
# 2) 16:9 cover 1920x1080: deepslate background, faint pixel grid, mark left, pixel wordmark right
FONT = {  # original 5x7 pixel glyphs
 'Z': ["11111","00001","00010","00100","01000","10000","11111"],
 'E': ["11111","10000","10000","11110","10000","10000","11111"],
 'R': ["11110","10001","10001","11110","10100","10010","10001"],
 'O': ["01110","10001","10011","10101","11001","10001","01110"],
 'L': ["10000","10000","10000","10000","10000","10000","11111"],
 'A': ["01110","10001","10001","11111","10001","10001","10001"],
 'U': ["10001","10001","10001","10001","10001","10001","01110"],
 'N': ["10001","11001","10101","10011","10001","10001","10001"],
 'C': ["01110","10001","10000","10000","10000","10001","01110"],
 'H': ["10001","10001","10001","11111","10001","10001","10001"],
 ' ': ["00000"]*7,
}
def text(d, s, x, y, px, col, gap=1):
    for ch in s:
        g = FONT[ch]
        for r, row in enumerate(g):
            for c, v in enumerate(row):
                if v == '1': d.rectangle([x+c*px, y+r*px, x+(c+1)*px-1, y+(r+1)*px-1], fill=col)
        x += (5+gap)*px
    return x
W, H = 1920, 1080
bg = (13, 14, 16, 255); cv = Image.new('RGBA', (W, H), bg); d = ImageDraw.Draw(cv)
tile = 40
for gx in range(0, W, tile):
    for gy in range(0, H, tile):
        if (gx//tile*7 + gy//tile*13) % 11 == 0: d.rectangle([gx, gy, gx+tile-1, gy+tile-1], fill=(20, 22, 25, 255))
# grass strip at the bottom
for gx in range(0, W, tile):
    d.rectangle([gx, H-2*tile, gx+tile-1, H-tile-1], fill=(0x8B,0xD4,0x6A,255) if (gx//tile) % 5 == 1 else (0x67,0xB8,0x4A,255))
    d.rectangle([gx, H-tile, gx+tile-1, H-1], fill=(0x2A,0x2C,0x30,255) if (gx//tile) % 3 else (0x22,0x24,0x28,255))
m = mark(30); cv.alpha_composite(m, (200, 311))  # centred on the wordmark block (330..772)
white, green, grey = (245,245,245,255), (0x8B,0xD4,0x6A,255), (0xA8,0xAD,0xB5,255)
text(d, "ZERO", 820, 330, 40, white, gap=1)
text(d, "LAUNCHER", 828, 660, 16, green, gap=1)
cv.convert('RGB').save(f'{outdir}/zero-rich-presence-cover-1920x1080.png')
print('ok')
