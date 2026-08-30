"""
Generate assets/bonesaw.ico: the Bonesaw monogram -- "BS" in Uncial Antiqua,
bone on the charcoal disc, over a blood sawtooth underline. Sizes under 32 px
carry a bare B: at that size the S melts into the B and turns to mush. The
font is embedded in the launcher (see src/ui.rs) so window and icon agree;
the OFL license text sits next to the TTF in assets/.

Re-run only when the mark changes; the .ico is committed.
"""
from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "assets" / "bonesaw.ico"
TTF = Path(__file__).resolve().parent / "assets" / "UncialAntiqua-Regular.ttf"
SIZES = [16, 20, 24, 32, 48, 64, 128, 256]

BG = (0x1D, 0x1B, 0x1C)  # charcoal disc (stored RGB)
BONE = (0xE4, 0xDE, 0xD2)
BLOOD = (158, 22, 28)  # COLORREF 0x001C169E
SS = 4  # supersample factor


def fit(text: str, max_w: float, max_h: float):
    """Pick the largest font size that keeps the text inside the box."""
    f = ImageFont.truetype(str(TTF), 100)
    x0, y0, x1, y1 = f.getbbox(text)
    s = min(max_w / (x1 - x0), max_h / (y1 - y0))
    f = ImageFont.truetype(str(TTF), max(8, int(100 * s)))
    return f, f.getbbox(text)


def render(size: int, text: str, underline: bool) -> Image.Image:
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    inset = S * 0.03
    d.ellipse([inset, inset, S - inset, S - inset], fill=BG + (255,))
    multi = len(text) > 1
    f, (x0, y0, x1, y1) = fit(text, S * (0.62 if multi else 0.46),
                              S * (0.50 if multi else 0.56))
    tx = (S - (x1 - x0)) / 2 - x0
    ty = (S - (y1 - y0)) / 2 - y0 - S * 0.008
    d.text((tx, ty), text, font=f, fill=BONE + (255,))
    if underline:
        uw = max((x1 - x0) * 0.92, S * 0.24)
        ux0 = (S - uw) / 2
        uy = ty + y1 + S * 0.030
        uh = S * 0.042
        teeth = 5
        tw = uw / teeth
        pts = [(ux0, uy)]
        for i in range(teeth):
            pts += [(ux0 + (i + 0.5) * tw, uy + uh), (ux0 + (i + 1) * tw, uy)]
        pts += [(ux0, uy)]
        d.polygon(pts, fill=BLOOD + (255,))
    return img.resize((size, size), Image.LANCZOS)


def icon_cfg(size: int) -> dict:
    return dict(text=("BS" if size >= 32 else "B"), underline=True)


def bgra(img: Image.Image) -> bytes:
    """Top-down RGBA pixels -> bottom-up BGRA rows, as a DIB expects."""
    raw = img.transpose(Image.FLIP_TOP_BOTTOM).tobytes()
    out = bytearray(len(raw))
    out[0::4] = raw[2::4]
    out[1::4] = raw[1::4]
    out[2::4] = raw[0::4]
    out[3::4] = raw[3::4]
    return bytes(out)


def dib(size: int, data: bytes) -> bytes:
    header = struct.pack(
        "<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(data), 0, 0, 0, 0,
    )
    mask_stride = ((size + 31) // 32) * 4
    return header + data + bytes(mask_stride * size)


def main() -> None:
    images = [(s, dib(s, bgra(render(s, **icon_cfg(s))))) for s in SIZES]
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
    OUT.write_bytes(bytes(out))
    print(f"wrote {OUT} ({len(out):,} bytes, sizes {SIZES})")


if __name__ == "__main__":
    main()
