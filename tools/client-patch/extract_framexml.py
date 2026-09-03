"""Pull FrameXML files out of the client into cache/, newest archive wins.

    python extract_framexml.py LFGFrame.lua LFDFrame.lua LFDFrame.xml
    python extract_framexml.py --all      # everything FrameXML.toc lists

Unlike extract_file.py this opens with MPQ_OPEN_READ_ONLY, so it works while the
client is running -- otherwise SFileOpenArchive fails with a sharing violation
(error 32) and the file just looks absent. Each archive is opened once and all
wanted files are pulled from it, because opening these archives is the slow part.

Read the real UI before theorising about what it does.
"""
import ctypes
import sys
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_file as E

SEP = chr(92)
HERE = Path(__file__).resolve().parent
OUT = HERE / "cache"

D = "B:" + SEP + "Games" + SEP + "WoW 3.3.5" + SEP + "Bonesaw" + SEP + "Data" + SEP + "enUS" + SEP
# Lowest priority first; a later archive overrides an earlier one.
ARCS = [D + n for n in (
    "base-enUS.MPQ", "locale-enUS.MPQ", "patch-enUS.MPQ",
    "patch-enUS-2.MPQ", "patch-enUS-3.MPQ", "patch-enUS-4.MPQ",
)]


def read(h, inner):
    fh = ctypes.c_void_p()
    if not E.SFileOpenFileEx(h, inner.encode(), 0, ctypes.byref(fh)):
        return None
    hi = wintypes.DWORD(0)
    size = E.SFileGetFileSize(fh, ctypes.byref(hi))
    buf = ctypes.create_string_buffer(size)
    got = wintypes.DWORD(0)
    E.SFileReadFile(fh, buf, size, ctypes.byref(got), None)
    E.SFileCloseFile(fh)
    return buf.raw[: got.value]


def main(names):
    if names == ["--all"]:
        toc = (OUT / "FrameXML.toc.base").read_text(errors="replace")
        names = [l.strip() for l in toc.splitlines()
                 if l.strip().lower().endswith((".lua", ".xml")) and not l.startswith("#")]

    best = {}
    for arc in ARCS:
        h = ctypes.c_void_p()
        if not E.SFileOpenArchive(arc, 0, 0x100, ctypes.byref(h)):
            print("cannot open", arc)
            continue
        hits = 0
        for name in names:
            d = read(h, "Interface" + SEP + "FrameXML" + SEP + name)
            if d:
                best[name] = (d, arc)
                hits += 1
        E.SFileCloseArchive(h)
        print(f"{Path(arc).name}: {hits} hit(s)")

    OUT.mkdir(exist_ok=True)
    for name in names:
        if name in best:
            data, arc = best[name]
            (OUT / name).write_bytes(data)
        else:
            print("MISSING", name)
    print(f"wrote {len(best)}/{len(names)} into {OUT}")


if __name__ == "__main__":
    main(sys.argv[1:] or ["--all"])
