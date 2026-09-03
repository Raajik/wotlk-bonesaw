"""List files in an MPQ using StormLib."""
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


class SFILE_FIND_DATA(ctypes.Structure):
    _fields_ = [
        ("cFileName", ctypes.c_char * 1024),
        ("szPlainName", ctypes.c_char_p),
        ("dwHashIndex", wintypes.DWORD),
        ("dwBlockIndex", wintypes.DWORD),
        ("dwFileSize", wintypes.DWORD),
        ("dwFileFlags", wintypes.DWORD),
        ("dwCompSize", wintypes.DWORD),
        ("dwFileTimeLo", wintypes.DWORD),
        ("dwFileTimeHi", wintypes.DWORD),
        ("lcLocale", wintypes.DWORD),
    ]


SFileOpenArchive = storm.SFileOpenArchive
SFileOpenArchive.argtypes = [wintypes.LPCWSTR, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
SFileOpenArchive.restype = wintypes.BOOL
SFileFindFirstFile = storm.SFileFindFirstFile
SFileFindFirstFile.argtypes = [ctypes.c_void_p, wintypes.LPCSTR, ctypes.POINTER(SFILE_FIND_DATA), wintypes.LPCSTR]
SFileFindFirstFile.restype = ctypes.c_void_p
SFileFindNextFile = storm.SFileFindNextFile
SFileFindNextFile.argtypes = [ctypes.c_void_p, ctypes.POINTER(SFILE_FIND_DATA)]
SFileFindNextFile.restype = wintypes.BOOL
SFileFindClose = storm.SFileFindClose
SFileFindClose.argtypes = [ctypes.c_void_p]
SFileCloseArchive = storm.SFileCloseArchive
SFileCloseArchive.argtypes = [ctypes.c_void_p]


def list_mpq(path: str):
    h = ctypes.c_void_p()
    if not SFileOpenArchive(path, 0, 0, ctypes.byref(h)):
        raise SystemExit(f"Failed to open {path} err={ctypes.GetLastError()}")
    fd = SFILE_FIND_DATA()
    find = SFileFindFirstFile(h, b"*", ctypes.byref(fd), None)
    files = []
    if find:
        while True:
            files.append((fd.cFileName.decode("utf-8", "replace"), fd.dwFileSize))
            if not SFileFindNextFile(find, ctypes.byref(fd)):
                break
        SFileFindClose(find)
    SFileCloseArchive(h)
    return files


if __name__ == "__main__":
    path = sys.argv[1]
    for name, size in list_mpq(path):
        print(f"{size:10d}  {name}")
