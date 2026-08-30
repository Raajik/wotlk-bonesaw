"""
Generate assets/bonesaw.ico: a bonesaw crossed with a femur -- an X -- on a
dark disc, matching the vector mark the launcher window draws (ui.rs
bonesaw_x). Hand-rolled because the repo has no imaging library; each icon
entry is a 32bpp BGRA DIB. Re-run only when the mark changes; the .ico is
committed.
"""
from __future__ import annotations

import math
import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent / "assets" / "bonesaw.ico"
SIZES = [16, 20, 24, 32, 48, 64, 128, 256]

BG = (0x1D, 0x1B, 0x1C)  # charcoal disc (stored RGB)
BONE = (0xE4, 0xDE, 0xD2)
STEEL = (0xF2, 0xF2, 0xF0)
BLOOD = (0x1C, 0x16, 0x9E)[::-1]  # COLORREF 0x001C169E is RGB(158,22,28)

SAW_DEG = -45.0  # saw axis: blade up-left, grip down-right
FEM_DEG = 45.0  # femur axis, crossing it
R_UNITS = 36.0  # the mark's radius in mark units


def rot(x: float, y: float, deg: float) -> tuple[float, float]:
    """Rotate a centered point by deg degrees (world -> local when negated)."""
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return (x * c - y * s, x * s + y * c)


def saw_inside(lx: float, ly: float) -> bool:
    """The saw silhouette (blade + teeth + grip + ring + drip), saw-local,
    scaled so the ring's outer edge (34.6) stays inside the r=36 disc."""
    if -21.1 <= lx <= 12.3 and -2.8 <= ly <= 3.7:
        return True
    if -20.2 <= lx <= 12.3 and -5.6 <= ly < -2.8:
        tw = 29.4 / 8.0
        k = int((lx + 21.1) // tw)
        bx = -21.1 + k * tw
        frac = (ly + 5.6) / 2.8
        half = (tw / 2.0) * frac
        if abs(lx - (bx + tw / 2.0)) <= half:
            return True
    if 12.3 <= lx <= 26.4 and abs(ly) <= 3.0:
        return True
    if math.hypot(lx - 29.0, ly) <= 5.6:
        return True
    if (abs(lx - 1.8) <= 1.0 and 3.5 <= ly <= 7.9) or (
        (lx - 1.8) ** 2 + ((ly - 10.1) * 1.3) ** 2 <= 8.0
    ):
        return True
    return False


def mark_color(wx: float, wy: float, w: float) -> tuple[int, int, int] | None:
    """White outline of the saw silhouette; w = stroke half-width in units."""
    def inside(ax: float, ay: float) -> bool:
        lx, ly = rot(ax, ay, -SAW_DEG)
        return saw_inside(lx, ly)

    if inside(wx, wy):
        return None
    for dx, dy in [(w, 0), (-w, 0), (0, w), (0, -w),
                   (w * 0.7, w * 0.7), (-w * 0.7, w * 0.7),
                   (w * 0.7, -w * 0.7), (-w * 0.7, w * 0.7)]:
        if inside(wx + dx, wy + dy):
            return (0xF2, 0xF2, 0xF0)
    return None



def render(size: int) -> bytes:
    """Return BGRA rows, bottom-up, as a DIB expects."""
    px = [[(0, 0, 0, 0) for _ in range(size)] for _ in range(size)]
    c = (size - 1) / 2.0
    r_disc = size * 0.47
    scale = r_disc / R_UNITS  # mark units -> pixels
    ss = 3
    # Stroke half-width in pixels, clamped so tiny sizes still read.
    w = min(6.0, max(1.4, size * 0.05)) / scale

    for y in range(size):
        for x in range(size):
            total = ss * ss
            disc = 0
            sums = [0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    wx = x + (sx + 0.5) / ss - c
                    wy = y + (sy + 0.5) / ss - c
                    if math.hypot(wx, wy) > r_disc:
                        continue
                    disc += 1
                    mark = mark_color(wx / scale, wy / scale, w)
                    col = mark if mark is not None else BG
                    sums[0] += col[0]
                    sums[1] += col[1]
                    sums[2] += col[2]
            if not disc:
                continue
            a = round(disc / total * 255)
            r = round(sums[0] / disc)
            g = round(sums[1] / disc)
            b = round(sums[2] / disc)
            px[y][x] = (r, g, b, a)

    out = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            r, g, b, a = px[y][x]
            out += bytes((b, g, r, a))
    return bytes(out)


def dib(size: int, bgra: bytes) -> bytes:
    header = struct.pack(
        "<IiiHHIIiiII",
        40, size, size * 2, 1, 32, 0,
        len(bgra), 0, 0, 0, 0,
    )
    mask_stride = ((size + 31) // 32) * 4
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
            len(data), offset,
        )
        offset += len(data)
    for _, data in images:
        out += data
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_bytes(bytes(out))
    print(f"wrote {OUT} ({len(out):,} bytes, sizes {SIZES})")


if __name__ == "__main__":
    main()
