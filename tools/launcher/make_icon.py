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
    """Solid mark: steel blade, blood grip, bone ring, blood drip. Grip is
    centered on the blade axis and the ring rides the same line, pulled in
    from the disc edge."""
    lx, ly = rot(wx, wy, -SAW_DEG)
    if -21.1 <= lx <= 12.3 and -2.8 <= ly <= 3.7:
        return STEEL
    if -20.2 <= lx <= 12.3 and -5.6 <= ly < -2.8:
        tw = 29.4 / 8.0
        k = int((lx + 21.1) // tw)
        bx = -21.1 + k * tw
        frac = (ly + 5.6) / 2.8
        if abs(lx - (bx + tw / 2.0)) <= (tw / 2.0) * frac:
            return STEEL
    if 10.5 <= lx <= 22.0 and abs(ly) <= 3.25:
        return BLOOD
    d = math.hypot(lx - 25.5, ly - 0.45)
    if 1.8 <= d <= 5.4:
        return BONE
    if (abs(lx - 1.8) <= 1.0 and 3.5 <= ly <= 7.9) or (
        (lx - 1.8) ** 2 + ((ly - 10.1) * 1.3) ** 2 <= 8.0
    ):
        return BLOOD
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


def sawcut_color(wx: float, wy: float) -> tuple[int, int, int] | None:
    """Saw mid-cut through a horizontal bone, blood pooling underneath."""
    # The bone, lying horizontally in the lower half.
    if -20.0 <= wx <= 20.0 and 4.8 <= wy <= 11.4:
        return BONE
    for ex in (-20.0, 20.0):
        for o in (-4.2, 4.2):
            if math.hypot(wx - ex, wy - 8.0) <= 4.2:
                return BONE
    # The blade, diagonal, mid-cut into the bone.
    lx, ly = rot(wx, wy, -SAW_DEG)
    if -12.0 <= lx <= 6.0 and -2.6 <= ly <= 3.4:
        return STEEL
    if -13.0 <= lx <= 8.5 and -5.4 <= ly < -2.8:
        tw = 22.0 / 6.0
        k = int((lx + 13.0) // tw)
        bx = -13.0 + k * tw
        frac = (ly + 5.4) / 2.6
        if abs(lx - (bx + tw / 2.0)) <= (tw / 2.0) * frac:
            return STEEL
    if 6.0 <= lx <= 20.0 and abs(ly) <= 3.0:
        return BLOOD
    d = math.hypot(lx - 28.5, ly)
    if 1.7 <= d <= 5.2:
        return BONE
    # Blood pooling under the cut.
    if ((wx - 2.0) / 6.5) ** 2 + ((wy - 14.5) / 2.8) ** 2 <= 1.0:
        return BLOOD
    return None


def bone_b_color(wx: float, wy: float) -> tuple[int, int, int] | None:
    """A B built from bones: vertical bone stem, two saw-handle rings as the
    bowls, one blood drip off the stem's foot."""
    x, y = wx, -wy  # work upright: local y up
    stem = -17.0 <= x <= -11.0 and -13.5 <= y <= 13.5
    for oy in (-13.5, 13.5):
        for o in (-4.4, 4.4):
            if math.hypot(x + 14.0, y - (oy2 := oy)) <= 4.4:
                return BONE
    if stem:
        return BONE
    d_top = math.hypot(x - 1.0, y - 7.5)
    if 4.2 <= math.hypot(x - 1.0, y - 7.5) <= 7.9 and x >= -13.0:
        return BONE
    d2 = math.hypot(x - 3.5, y + 9.0)
    if 4.4 <= math.hypot(x - 3.5, y + 9.5) <= 9.0 and x >= -12.0:
        return BONE
    if abs(x + 14.0) <= 1.0 and -20.0 <= y <= -17.5:
        return BLOOD
    return None


def skull_color(wx: float, wy: float) -> tuple[int, int, int] | None:
    """Stylized skull, saw entering from the upper left, blade buried in the
    crown, blood seeping from the cut. Saw axis +45: grip top-left, tip
    bottom-right, cutting into the crown."""
    # saw first, so it overlaps the skull: grip up-left, blade tip in-crown
    lx, ly = rot(wx, wy, -SAW_DEG)
    if -16.0 <= lx <= 6.0 and -2.6 <= ly <= 3.2:
        return STEEL
    if -15.0 <= lx <= 6.0 and -5.2 <= ly < -2.6:
        tw = 21.0 / 6.0
        k = int((lx + 13.0) // tw)
        bx = -13.0 + k * tw
        frac = (ly + 5.2) / 2.6
        if abs(lx - (bx + tw / 2.0)) <= (tw / 2.0) * frac:
            return STEEL
    if -27.0 <= lx <= -16.0 and abs(ly) <= 3.0:
        return BLOOD
    d = math.hypot(lx + 31.0, ly)
    if 1.6 <= d <= 5.0:
        return BONE
    # skull
    dx, dy = wx - 4.0, wy - 9.0
    if (dx / 14.5) ** 2 + ((dy + 2.0) / 12.5) ** 2 <= 1.0:
        if math.hypot(wx - (-1.0), wy - 9.0) <= 3.6 and not (-16.0 <= lx <= 6.0 and abs(ly) <= 3.2):
            return BG
        if math.hypot(wx - 10.5, wy - 8.0) <= 3.4:
            return BG
        return BONE
    if -6.0 <= wx <= 14.0 and 18.0 <= wy <= 23.0:
        if abs(wx - 1.0) <= 1.0 or abs(wx - 6.5) <= 1.0:
            return BG
        return BONE
    # blood at the cut, running down the brow
    if ((wx - 5.0) / 5.0) ** 2 + ((wy - 3.5) / 3.0) ** 2 <= 1.0:
        return BLOOD
    if abs(wx - 9.0) <= 1.0 and 16.0 <= wy <= 21.0:
        return BLOOD
    return None


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
