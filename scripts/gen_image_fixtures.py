#!/usr/bin/env python3
"""Generates the image decoder test fixtures in tests/data/images.

Each case writes <name>.<ext> plus <name>.rgba: the expected decode as raw
RGBA8 bytes, top-to-bottom. PNGs are produced by a small encoder here (so
every color type, bit depth, tRNS and Adam7 combination is covered with
exactly known sample values); BMP and TGA come from Pillow.

With --verify, each PNG is also decoded by Pillow and compared against the
expected bytes, to check the encoder independently of Ion's decoder.

Usage: python3 scripts/gen_image_fixtures.py [--verify]
"""

import os
import struct
import sys
import zlib

W, H = 13, 11  # odd sizes exercise partial Adam7 passes
OUT = os.path.join(os.path.dirname(__file__), "..", "tests", "data", "images")

ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
         (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def to8(v, depth):
    return {1: v * 255, 2: v * 0x55, 4: v * 0x11, 8: v, 16: v >> 8}[depth]


def sample(x, y, c, depth):
    """Deterministic sample value for pixel (x, y), channel c."""
    maxv = (1 << depth) - 1
    return (x * (7 + c * 3) + y * (5 + c) * 3 + c * 11) * (1 if depth < 16 else 257) % (maxv + 1)


def pack_row(samples, depth):
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out, acc, bits = bytearray(), 0, 0
    for s in samples:
        acc = (acc << depth) | s
        bits += depth
        if bits == 8:
            out.append(acc)
            acc, bits = 0, 0
    if bits:
        out.append(acc << (8 - bits))
    return bytes(out)


def filter_row(row, prev, bpp, kind):
    out = bytearray([kind])
    for i, b in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0
        up = prev[i] if prev else 0
        c = prev[i - bpp] if prev and i >= bpp else 0
        if kind == 0:
            pred = 0
        elif kind == 1:
            pred = a
        elif kind == 2:
            pred = up
        elif kind == 3:
            pred = (a + up) // 2
        else:
            p = a + up - c
            pa, pb, pc = abs(p - a), abs(p - up), abs(p - c)
            pred = a if pa <= pb and pa <= pc else (up if pb <= pc else c)
        out.append((b - pred) & 0xFF)
    return bytes(out)


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def make_png(color, depth, interlace=False, trns=None, palette=None, pixels=None):
    """pixels[y][x] is a tuple of raw samples per channel."""
    ch = CHANNELS[color]
    bpp = max(1, ch * depth // 8)
    raw = bytearray()
    passes = ADAM7 if interlace else [(0, 0, 1, 1)]
    for x0, y0, dx, dy in passes:
        xs = list(range(x0, W, dx))
        ys = list(range(y0, H, dy))
        if not xs or not ys:
            continue
        prev = None
        for n, y in enumerate(ys):
            samples = [s for x in xs for s in pixels[y][x]]
            row = pack_row(samples, depth)
            raw += filter_row(row, prev, bpp, (n + y) % 5)  # cycle all filters
            prev = row
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, depth, color, 0, 0, 1 if interlace else 0))
    if palette:
        png += chunk(b"PLTE", bytes(c for rgb in palette for c in rgb))
    if trns is not None:
        png += chunk(b"tRNS", trns)
    png += chunk(b"IDAT", zlib.compress(bytes(raw)))
    png += chunk(b"IEND", b"")
    return png


def expected_rgba(color, depth, pixels, trns=None, palette=None):
    out = bytearray()
    for y in range(H):
        for x in range(W):
            s = pixels[y][x]
            if color == 0:
                v = to8(s[0], depth)
                a = 0 if trns is not None and s[0] == struct.unpack(">H", trns)[0] else 255
                out += bytes([v, v, v, a])
            elif color == 2:
                a = 255
                if trns is not None and tuple(s) == struct.unpack(">HHH", trns):
                    a = 0
                out += bytes([to8(s[0], depth), to8(s[1], depth), to8(s[2], depth), a])
            elif color == 3:
                r, g, b = palette[s[0]]
                a = trns[s[0]] if trns is not None and s[0] < len(trns) else 255
                out += bytes([r, g, b, a])
            elif color == 4:
                v = to8(s[0], depth)
                out += bytes([v, v, v, to8(s[1], depth)])
            else:
                out += bytes(to8(c, depth) for c in s)
    return bytes(out)


def grid(color, depth):
    ch = CHANNELS[color]
    return [[tuple(sample(x, y, c, depth) for c in range(ch)) for x in range(W)] for y in range(H)]


def png_cases():
    cases = []
    for color, depths in [(0, [1, 2, 4, 8, 16]), (2, [8, 16]), (4, [8, 16]), (6, [8, 16])]:
        for depth in depths:
            for interlace in (False, True):
                name = "png_c%d_d%d%s" % (color, depth, "_i" if interlace else "")
                cases.append((name, color, depth, interlace, None, None, grid(color, depth)))
    # Palettes at every depth, with partial per-entry alpha.
    for depth in (1, 2, 4, 8):
        n = 1 << depth
        palette = [((i * 37) % 256, (i * 91) % 256, (i * 151) % 256) for i in range(n)]
        trns = bytes((i * 60) % 256 for i in range(min(n, 3)))  # shorter than the palette
        pixels = [[((x + y * 3) % n,) for x in range(W)] for y in range(H)]
        for interlace in (False, True):
            name = "png_c3_d%d%s" % (depth, "_i" if interlace else "")
            cases.append((name, 3, depth, interlace, trns, palette, pixels))
    # tRNS color keys: the matching gray / RGB value becomes transparent.
    g = grid(0, 8)
    cases.append(("png_c0_d8_trns", 0, 8, False, struct.pack(">H", g[2][3][0]), None, g))
    g16 = grid(0, 16)
    cases.append(("png_c0_d16_trns", 0, 16, False, struct.pack(">H", g16[4][4][0]), None, g16))
    g4 = grid(0, 4)
    cases.append(("png_c0_d4_trns", 0, 4, True, struct.pack(">H", g4[1][1][0]), None, g4))
    rgb = grid(2, 8)
    cases.append(("png_c2_d8_trns", 2, 8, False, struct.pack(">HHH", *rgb[5][6]), None, rgb))
    rgb16 = grid(2, 16)
    cases.append(("png_c2_d16_trns", 2, 16, True, struct.pack(">HHH", *rgb16[0][0]), None, rgb16))
    return cases


def main():
    verify = "--verify" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    names = []
    for name, color, depth, interlace, trns, palette, pixels in png_cases():
        data = make_png(color, depth, interlace, trns, palette, pixels)
        expect = expected_rgba(color, depth, pixels, trns, palette)
        with open(os.path.join(OUT, name + ".png"), "wb") as f:
            f.write(data)
        with open(os.path.join(OUT, name + ".rgba"), "wb") as f:
            f.write(expect)
        names.append(name + ".png")
        if verify:
            from io import BytesIO
            from PIL import Image
            im = Image.open(BytesIO(data))
            # Pillow keeps 16-bit gray as I;16 and has no 16-bit RGBA mode;
            # verify the 8-bit-representable cases. Pillow also compares a
            # sub-8-bit gray tRNS key against the already-scaled sample (so
            # it never matches); the spec puts the key in the image's own
            # bit depth, which is what the expected bytes follow.
            if depth <= 8 and not (color == 0 and depth < 8 and trns is not None):
                got = im.convert("RGBA").tobytes()
                assert got == expect, "Pillow disagrees on " + name

    # BMP / TGA via Pillow, from a fixed RGBA pattern.
    from PIL import Image
    rgba = bytes(v for y in range(H) for x in range(W)
                 for v in ((x * 19) % 256, (y * 23) % 256, ((x + y) * 13) % 256,
                           255 if (x + y) % 3 else 128))
    im = Image.frombytes("RGBA", (W, H), rgba)
    rgb_expect = bytes(v for i in range(W * H) for v in (rgba[i * 4:i * 4 + 3] + b"\xff"))
    im.convert("RGB").save(os.path.join(OUT, "bmp24.bmp"))
    im.save(os.path.join(OUT, "bmp32.bmp"))
    im.convert("RGB").save(os.path.join(OUT, "tga24.tga"))
    im.save(os.path.join(OUT, "tga32_rle.tga"), rle=True)
    im.convert("L").save(os.path.join(OUT, "tga8_rle.tga"), rle=True)
    for name, expect in [("bmp24", rgb_expect), ("bmp32", rgba), ("tga24", rgb_expect),
                         ("tga32_rle", rgba)]:
        with open(os.path.join(OUT, name + ".rgba"), "wb") as f:
            f.write(expect)
        names.append(name + (".bmp" if name.startswith("bmp") else ".tga"))
    gray = Image.open(os.path.join(OUT, "tga8_rle.tga")).convert("RGBA").tobytes()
    with open(os.path.join(OUT, "tga8_rle.rgba"), "wb") as f:
        f.write(gray)
    names.append("tga8_rle.tga")
    with open(os.path.join(OUT, "MANIFEST"), "w") as f:
        f.write("%d %d\n" % (W, H))
        f.write("\n".join(names) + "\n")
    print("wrote %d fixtures to %s" % (len(names), os.path.normpath(OUT)))


if __name__ == "__main__":
    main()
