import struct
from pathlib import Path
data = Path(r"B:/Games/WoW 3.3.5/Bonesaw/Wow-Bonesaw.exe").read_bytes()
pe = struct.unpack_from("<I", data, 0x3C)[0]
chars = struct.unpack_from("<H", data, pe + 0x16)[0]
print("patched exe size:", len(data))
print("patched exe LAA set:", bool(chars & 0x20), hex(chars))
