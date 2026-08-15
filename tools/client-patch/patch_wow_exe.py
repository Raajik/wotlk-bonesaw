"""
Patch stock 3.3.5a 12340 Wow.exe (7,704,216 bytes) so modified FrameXML can load.

Applies only:
  - Allow interface edits
  - Signature bypass

Byte patterns from pathetic-lynx/WoW_335a_Patcher (WoWFix335 lineage).
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

EXPECTED_SIZE = 7704216

# file offset, original, replacement
PATCHES: list[tuple[int, bytes, bytes]] = [
    # Allow interface edits
    (0x1F41BF, bytes([0x74]), bytes([0xEB])),
    (0x415A25, bytes([0x75]), bytes([0xEB])),
    (0x415A3F, bytes([0x01]), bytes([0x03])),
    (0x415A95, bytes([0x01]), bytes([0x03])),
    (0x415B46, bytes([0x7F]), bytes([0xEB])),
    (0x415B5F, bytes([0x83, 0xC0, 0x03, 0x5E, 0x8B, 0xE5, 0x5D]),
     bytes([0xB8, 0x03, 0x00, 0x00, 0x00, 0xEB, 0xED])),
    # Signature bypass
    (0x123676, bytes([0x8B, 0xCE, 0xE8, 0xA3, 0x18, 0x1F, 0x00]), bytes([0x90] * 7)),
    (0x1241C6, bytes([0x50]), bytes([0x90])),
    (0x1241C9, bytes([0xE8, 0x82, 0xC0, 0x1F, 0x00]), bytes([0x90] * 5)),
    (0x320273, bytes([0x0F, 0x85, 0xFE, 0x00, 0x00, 0x00]), bytes([0x90] * 6)),
    (0x320282, bytes([0x0F, 0x85, 0xEF, 0x00, 0x00, 0x00]), bytes([0x90] * 6)),
]


def apply(src: Path, dest: Path) -> None:
    data = bytearray(src.read_bytes())
    if len(data) != EXPECTED_SIZE:
        raise SystemExit(f"Unexpected size {len(data)} (want {EXPECTED_SIZE})")
    for off, orig, repl in PATCHES:
        got = bytes(data[off : off + len(orig)])
        if got == repl:
            continue
        if got != orig:
            raise SystemExit(f"Mismatch at {off:#x}: {got.hex()} (want {orig.hex()})")
        data[off : off + len(repl)] = repl
    dest.write_bytes(data)
    print(f"Wrote {dest} ({len(data)} bytes)")


def main() -> None:
    src = Path(sys.argv[1] if len(sys.argv) > 1 else r"B:\Games\WoW 3.3.5\Bonesaw\Wow.exe")
    dest = Path(sys.argv[2] if len(sys.argv) > 2 else src.with_name("Wow-Bonesaw.exe"))
    stock = src.with_name("Wow.exe.stock")
    if src.exists() and not stock.exists() and src.stat().st_size == EXPECTED_SIZE:
        shutil.copy2(src, stock)
        print(f"Backed up stock exe -> {stock}")
    apply(src if not stock.exists() else stock, dest)


if __name__ == "__main__":
    main()
