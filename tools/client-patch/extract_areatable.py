#!/usr/bin/env python3
"""Extract AreaTable.dbc from the highest-priority client MPQ that has it."""
import ctypes
import sys
from ctypes import wintypes
from pathlib import Path

DLL = (
    Path(__file__).resolve().parent.parent.parent
    / "archive"
    / "failed-eotw-cota-20260814"
    / "client-patch"
    / "bin"
    / "stormlib"
    / "x64"
    / "StormLib.dll"
)
storm = ctypes.WinDLL(str(DLL))
SFileOpenArchive = storm.SFileOpenArchive
SFileOpenArchive.argtypes = [wintypes.LPCWSTR, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
SFileOpenArchive.restype = wintypes.BOOL
SFileOpenFileEx = storm.SFileOpenFileEx
SFileOpenFileEx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
SFileOpenFileEx.restype = wintypes.BOOL
SFileGetFileSize = storm.SFileGetFileSize
SFileGetFileSize.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
SFileGetFileSize.restype = wintypes.DWORD
SFileReadFile = storm.SFileReadFile
SFileReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
SFileCloseFile = storm.SFileCloseFile
SFileCloseArchive = storm.SFileCloseArchive

CLIENT = Path(r"B:\Games\WoW 3.3.5\Bonesaw\Data")
CANDIDATES = [
    "enUS/patch-enUS-3.MPQ", "enUS/patch-enUS-2.MPQ", "enUS/patch-enUS.MPQ",
    "patch-Y.MPQ",
    "patch-3.MPQ", "patch-2.MPQ", "patch.MPQ",
    "enUS/lichking-locale-enUS.MPQ", "enUS/expansion-locale-enUS.MPQ",
    "enUS/locale-enUS.MPQ", "enUS/base-enUS.MPQ",
    "lichking.MPQ", "expansion.MPQ", "common-2.MPQ", "common.MPQ",
]
inner = (sys.argv[1] if len(sys.argv) > 1 else r"DBFilesClient\AreaTable.dbc").encode("ascii")
for rel in CANDIDATES:
    p = CLIENT / rel
    h = ctypes.c_void_p()
    if not SFileOpenArchive(str(p), 0, 0, ctypes.byref(h)):
        continue
    fh = ctypes.c_void_p()
    if not SFileOpenFileEx(h, inner, 0, ctypes.byref(fh)):
        SFileCloseArchive(h)
        continue
    hi = wintypes.DWORD()
    size = SFileGetFileSize(fh, ctypes.byref(hi))
    print(f"  has file in {rel}, size={size}")
    if size == 0xFFFFFFFF or size == 0:
        SFileCloseFile(fh)
        SFileCloseArchive(h)
        continue
    buf = ctypes.create_string_buffer(size)
    read = wintypes.DWORD()
    SFileReadFile(fh, buf, size, ctypes.byref(read), None)
    SFileCloseFile(fh)
    SFileCloseArchive(h)
    out = Path(__file__).resolve().parent / "cache" / "AreaTable.dbc.base"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(buf.raw[: read.value])
    print(f"extracted {out} from {rel} ({read.value} bytes)")
    break
