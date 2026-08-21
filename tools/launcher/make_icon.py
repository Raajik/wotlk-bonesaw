"""
Generate assets/bonesaw.ico: a green B on a dark disc.

Written by hand because the repo has no imaging library. Each icon entry is a
32bpp BGRA DIB, which Windows accepts for every size we ship. Re-run only if the
mark changes; the .ico is committed.
"""
from __future__ import annotations

import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent / "assets" / "bonesaw.ico"
SIZES = [16, 20, 24, 32, 48, 64, 128, 256]

BG = (0x0E, 0x14, 0x0E)  # disc fill, RGB
RING = (0x4A, 0xD9, 0x3F)  # green
GLYPH = (0x4A, 0xD9, 0x3F)

# A 7x9 'B'. Kept blocky on purpose: it stays legible at 16px.
B_GLYPH = [
    "1111110",
    "1100011",
    "1100011",
    "1100011",
    "1111110",
    "1100011",
    "1100011",
    "1100011",
    "1111110",
]


def blend(dst, src, alpha):
    return tuple(round(d + (s - d) * alpha) for d, s in zip(dst, src))


def render(size: int) -> bytes:
    """Return BGRA rows, bottom-up, as a DIB expects."""
    px = [[(0, 0, 0, 0) for _ in range(size)] for _ in range(size)]
    c = (size - 1) / 2.0
    r_outer = size * 0.48
    r_ring = size * 0.40
    ss = 3  # supersample per axis, for tolerable edges without a raster library

    for y in range(size):
        for x in range(size):
            hits_disc = 0
            hits_ring = 0
            for sy in range(ss):
                for sx in range(ss):
                    fx = x + (sx + 0.5) / ss - c
                    fy = y + (sy + 0.5) / ss - c
                    d = (fx * fx + fy * fy) ** 0.5
                    if d <= r_outer:
                        hits_disc += 1
                    if r_ring <= d <= r_outer:
                        hits_ring += 1
            total = ss * ss
            if not hits_disc:
                continue
            a = hits_disc / total
            color = BG
            if hits_ring:
                color = blend(BG, RING, hits_ring / hits_disc)
            px[y][x] = (color[0], color[1], color[2], round(a * 255))

    # Glyph, scaled by whole pixels so the strokes stay even.
    gw, gh = len(B_GLYPH[0]), len(B_GLYPH)
    scale = max(1, int(size * 0.52 / gh))
    ox = round((size - gw * scale) / 2)
    oy = round((size - gh * scale) / 2)
    for gy, row in enumerate(B_GLYPH):
        for gx, ch in enumerate(row):
            if ch != "1":
                continue
            for dy in range(scale):
                for dx in range(scale):
                    x, y = ox + gx * scale + dx, oy + gy * scale + dy
                    if 0 <= x < size and 0 <= y < size:
                        px[y][x] = (GLYPH[0], GLYPH[1], GLYPH[2], 255)

    out = bytearray()
    for y in range(size - 1, -1, -1):  # DIB rows are bottom-up
        for x in range(size):
            r, g, b, a = px[y][x]
            out += bytes((b, g, r, a))
    return bytes(out)


def dib(size: int, bgra: bytes) -> bytes:
    header = struct.pack(
        "<IiiHHIIiiII",
        40,          # biSize
        size,        # biWidth
        size * 2,    # biHeight: colour rows + mask rows
        1,           # biPlanes
        32,          # biBitCount
        0,           # BI_RGB
        len(bgra),
        0, 0, 0, 0,
    )
    mask_stride = ((size + 31) // 32) * 4  # 1bpp, padded to 4 bytes
    return header + bgra + bytes(mask_stride * size)


def main() -> None:
    images = [(s, dib(s, render(s))) for s in SIZES]
    offset = 6 + 16 * len(images)
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for size, data in images:
        out += struct.pack(
            "<BBBBHHII",
            0 if size >= 256 else size,
            0 if size >= 256 else size,
            0, 0, 1, 32,
            len(data),
            offset,
        )
        offset += len(data)
    for _, data in images:
        out += data
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_bytes(bytes(out))
    print(f"wrote {OUT} ({len(out):,} bytes, sizes {SIZES})")


if __name__ == "__main__":
    main()
