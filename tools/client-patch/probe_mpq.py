"""Probe an MPQ without listing (FindFirst * can hang)."""
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
SFileHasFile = storm.SFileHasFile
SFileHasFile.argtypes = [ctypes.c_void_p, wintypes.LPCSTR]
SFileHasFile.restype = wintypes.BOOL
SFileOpenFileEx = storm.SFileOpenFileEx
SFileOpenFileEx.argtypes = [ctypes.c_void_p, wintypes.LPCSTR, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
SFileOpenFileEx.restype = wintypes.BOOL
SFileGetFileSize = storm.SFileGetFileSize
SFileGetFileSize.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
SFileGetFileSize.restype = wintypes.DWORD
SFileReadFile = storm.SFileReadFile
SFileReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
SFileReadFile.restype = wintypes.BOOL
SFileCloseFile = storm.SFileCloseFile
SFileCloseFile.argtypes = [ctypes.c_void_p]
SFileCloseArchive = storm.SFileCloseArchive
SFileCloseArchive.argtypes = [ctypes.c_void_p]


def read_file(h, name: bytes, limit: int = 4096) -> bytes | None:
    fh = ctypes.c_void_p()
    if not SFileOpenFileEx(h, name, 0, ctypes.byref(fh)):
        return None
    hi = wintypes.DWORD(0)
    size = SFileGetFileSize(fh, ctypes.byref(hi))
    n = min(size, limit)
    buf = ctypes.create_string_buffer(n)
    read = wintypes.DWORD(0)
    SFileReadFile(fh, buf, n, ctypes.byref(read), None)
    SFileCloseFile(fh)
    return size, buf.raw[: read.value]


def main():
    path = sys.argv[1]
    h = ctypes.c_void_p()
    if not SFileOpenArchive(path, 0, 0, ctypes.byref(h)):
        raise SystemExit(f"open failed err={ctypes.GetLastError()}")
    print(f"opened {path} size={Path(path).stat().st_size}")

    names = [
        b"(listfile)",
        b"(attributes)",
        b"DBFilesClient\\Spell.dbc",
        b"DBFilesClient\\SkillLineAbility.dbc",
        b"DBFilesClient\\Item.dbc",
        b"Interface\\AddOns\\LivingGear\\LivingGear.toc",
        b"Interface\\AddOns\\CallOfTheArchmage\\CallOfTheArchmage.toc",
        b"Interface\\AddOns\\EchoesOfTheWorldsoulBridge\\EchoesOfTheWorldsoulBridge.toc",
        b"nope.xyz",
    ]
    for name in names:
        has = bool(SFileHasFile(h, name))
        data = read_file(h, name, 512)
        if data is None:
            print(f"  has={has} missing {name!r}")
        else:
            size, head = data
            preview = head[:120].replace(b"\r", b" ").replace(b"\n", b" | ")
            print(f"  has={has} {name!r} size={size} head={preview!r}")

    SFileCloseArchive(h)


if __name__ == "__main__":
    main()
