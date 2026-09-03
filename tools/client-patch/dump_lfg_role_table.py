"""Print how Wow.exe turns SMSG_LFG_PROPOSAL_UPDATE's role field into a string.

The evidence behind core-patch 0013. Finds every .text reference to the role-name
table at VA 0x00ad87b0 and dumps the surrounding bytes, which show a bit test
(2 = TANK, 4 = HEALER, 8 = DAMAGER, else "UNKNOWN") rather than a table index.
Run it before re-theorising "Unknown role: UNKNOWN" for a fifth time.
"""
import struct
import sys
from pathlib import Path

EXE = Path(sys.argv[1] if len(sys.argv) > 1 else r"B:\Games\WoW 3.3.5\Bonesaw\Wow.exe")
data = EXE.read_bytes()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsize = struct.unpack_from("<H", data, pe + 20)[0]
imagebase = struct.unpack_from("<I", data, pe + 24 + 28)[0]
sec0 = pe + 24 + optsize
sections = []
for i in range(nsec):
    off = sec0 + i * 40
    name = data[off:off + 8].rstrip(b"\x00").decode()
    vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, vaddr, vsize, rawptr, rawsize))

TEXT = next(s for s in sections if s[0] == ".text")


def f2v(off):
    for _, vaddr, vsize, rawptr, rawsize in sections:
        if rawptr <= off < rawptr + rawsize:
            return imagebase + vaddr + (off - rawptr)
    return None


TABLE = 0xAD87B0
for candidate in (TABLE, TABLE - 4, TABLE - 8, TABLE + 4, TABLE + 8):
    needle = struct.pack("<I", candidate)
    start = TEXT[3]
    end = TEXT[3] + TEXT[4]
    hits = []
    i = start
    while True:
        i = data.find(needle, i, end)
        if i < 0:
            break
        hits.append(i)
        i += 1
    if not hits:
        continue
    print(f"\n=== references to 0x{candidate:08x} ({len(hits)}) ===")
    for h in hits:
        lo, hi = h - 40, h + 24
        raw = data[lo:hi]
        hexs = " ".join(f"{b:02x}" for b in raw)
        print(f"\n  at file 0x{h:06x} VA 0x{f2v(h):08x}")
        print(f"  bytes -40..+24: {hexs}")
