"""Extract one file from the first MPQ that has it (search order = last wins)."""
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


def extract(mpq: str, inner: bytes):
    h = ctypes.c_void_p()
    if not SFileOpenArchive(mpq, 0, 0, ctypes.byref(h)):
        return None
    fh = ctypes.c_void_p()
    if not SFileOpenFileEx(h, inner, 0, ctypes.byref(fh)):
        SFileCloseArchive(h)
        return None
    hi = wintypes.DWORD(0)
    size = SFileGetFileSize(fh, ctypes.byref(hi))
    if size == 0xFFFFFFFF or size > 20_000_000:
        SFileCloseFile(fh)
        SFileCloseArchive(h)
        return None
    buf = ctypes.create_string_buffer(size)
    read = wintypes.DWORD(0)
    SFileReadFile(fh, buf, size, ctypes.byref(read), None)
    SFileCloseFile(fh)
    SFileCloseArchive(h)
    return buf.raw[: read.value]


def main():
    inner = sys.argv[1].encode("ascii")
    out = Path(sys.argv[2])
    archives = sys.argv[3:]
    found = None
    src = None
    for mpq in archives:
        data = extract(mpq, inner)
        if data:
            found = data
            src = mpq
            print(f"hit {mpq} size={len(data)}")
    if found is None:
        raise SystemExit(f"not found: {inner!r}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(found)
    print(f"wrote {out} from {src} ({len(found)} bytes)")


if __name__ == "__main__":
    main()
