#!/usr/bin/env python3
"""List DBFilesClient contents of one MPQ (SFileFindFirstFile walk)."""
import ctypes
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
SFileCloseArchive = storm.SFileCloseArchive


class SFILE_FIND_DATA(ctypes.Structure):
    _fields_ = [
        ("cFileName", ctypes.c_char * 2048),
        ("szPlainName", ctypes.c_char_p),
        ("dwHashIndex", wintypes.DWORD),
        ("dwBlockIndex", wintypes.DWORD),
        ("dwFileSize", wintypes.DWORD),
        ("dwFileFlags", wintypes.DWORD),
        ("dwCompSize", wintypes.DWORD),
        ("dwFileTimeLo", wintypes.DWORD),
        ("dwFileTimeHi", wintypes.DWORD),
    ]


SFileFindFirstFile = storm.SFileFindFirstFile
SFileFindFirstFile.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, wintypes.LPCSTR]
SFileFindFirstFile.restype = ctypes.c_void_p
SFileFindNextFile = storm.SFileFindNextFile
SFileFindNextFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
SFileFindNextFile.restype = wintypes.BOOL
SFileFindClose = storm.SFileFindClose
SFileFindClose.argtypes = [ctypes.c_void_p]

mpq = Path(r"B:\Games\WoW 3.3.5\Bonesaw\Data\enUS\patch-enUS-2.MPQ")
h = ctypes.c_void_p()
ok = SFileOpenArchive(str(mpq), 0, 0, ctypes.byref(h))
print("open:", bool(ok), mpq.name)
fd = SFILE_FIND_DATA()
hfind = SFileFindFirstFile(h, b"DBFilesClient\\*", ctypes.byref(fd), None)
n = 0
while hfind:
    name = bytes(fd.cFileName).split(b"\x00", 1)[0].decode("ascii", "replace")
    if "Area" in name or "Aura" in name or "Spell" in name:
        print(f"  {name} ({fd.dwFileSize} bytes)")
    n += 1
    if not SFileFindNextFile(hfind, ctypes.byref(fd)):
        break
print(f"total {n} files in DBFilesClient")
SFileFindClose(hfind)
SFileCloseArchive(h)
