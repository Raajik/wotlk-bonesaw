"""
Build Bonesaw client patch-Y.MPQ.

Adds *Windblown (910001) to Spell.dbc + SkillLineAbility.dbc.
FrameXML UI ships in patch-enUS-4.MPQ and needs the patched Wow-Bonesaw.exe.

Do not overwrite Data/patch-Z.mpq -- that archive already ships Item.dbc.

Usage (Windows):
  python tools/client-patch/build_patch.py
"""
from __future__ import annotations

import ctypes
import shutil
import struct
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STAGING = ROOT / "staging"
DBC_DIR = STAGING / "DBFilesClient"
SPELL_DBC = DBC_DIR / "Spell.dbc"
SPELL_DBC_BASE = ROOT / "cache" / "Spell.dbc.base"
SLA_DBC = DBC_DIR / "SkillLineAbility.dbc"
SLA_DBC_BASE = ROOT / "cache" / "SkillLineAbility.dbc.base"
OUT_MPQ = ROOT / "dist" / "patch-Y.MPQ"
OUT_LOCALE_MPQ = ROOT / "dist" / "patch-enUS-4.MPQ"
FRAME_TOC_BASE = ROOT / "cache" / "FrameXML.toc.base"
FRAME_TOC = STAGING / "Interface" / "FrameXML" / "FrameXML.toc"
UI_LUA_SRC = (
    ROOT.parent.parent / "modules" / "mod-living-gear" / "client_addon" / "LivingGear" / "LivingGear.lua"
)
UI_LUA = STAGING / "Interface" / "FrameXML" / "LivingGear.lua"
STORMLIB = (
    ROOT.parent.parent
    / "archive"
    / "failed-eotw-cota-20260814"
    / "client-patch"
    / "bin"
    / "stormlib"
    / "x64"
    / "StormLib.dll"
)

CUSTOM_SPELLS = {
    910001: ("*Windblown", "Open Living Gear.", 67),
}

# Smelting: instant profession opener, no category, no recovery.
TEMPLATE_SPELL_ID = 2656
GENERIC_SKILL_LINE = 183
SLA_TEMPLATE_SPELL = 2656
# Ability + usable while mounted. Do not copy racial category 1182.
ATTR_ABILITY_MOUNTED = 0x01000010


def load_storm():
    if not STORMLIB.exists():
        raise SystemExit(f"Missing StormLib at {STORMLIB}")
    storm = ctypes.WinDLL(str(STORMLIB))
    storm.SFileCreateArchive.argtypes = [
        wintypes.LPCWSTR,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    storm.SFileCreateArchive.restype = wintypes.BOOL
    storm.SFileAddFileEx.argtypes = [
        ctypes.c_void_p,
        wintypes.LPCWSTR,
        wintypes.LPCSTR,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_uint32,
    ]
    storm.SFileAddFileEx.restype = wintypes.BOOL
    storm.SFileCloseArchive.argtypes = [ctypes.c_void_p]
    storm.SFileCloseArchive.restype = wintypes.BOOL
    return storm


def ensure_base_dbc():
    SPELL_DBC_BASE.parent.mkdir(parents=True, exist_ok=True)
    if not SPELL_DBC_BASE.exists():
        if not SPELL_DBC.exists():
            raise SystemExit(f"Missing Spell.dbc at {SPELL_DBC}")
        shutil.copy2(SPELL_DBC, SPELL_DBC_BASE)
        print(f"Cached base Spell.dbc -> {SPELL_DBC_BASE}")
    if not SLA_DBC_BASE.exists():
        if not SLA_DBC.exists():
            raise SystemExit(f"Missing SkillLineAbility.dbc at {SLA_DBC}")
        shutil.copy2(SLA_DBC, SLA_DBC_BASE)
        print(f"Cached base SkillLineAbility.dbc -> {SLA_DBC_BASE}")


def patch_spell_dbc():
    ensure_base_dbc()
    data = bytearray(SPELL_DBC_BASE.read_bytes())
    magic, records, fields, recsize, strsize = struct.unpack_from("<4sIIII", data, 0)
    if magic != b"WDBC" or fields != 234 or recsize != 936:
        raise SystemExit(f"Unexpected Spell.dbc header: {magic} {fields} {recsize}")

    str_off = 20 + records * recsize
    string_block = bytearray(data[str_off:])

    def get_str(off: int) -> str:
        if off == 0:
            return ""
        end = string_block.index(b"\x00", off)
        return string_block[off:end].decode("utf-8", errors="replace")

    def add_str(text: str) -> int:
        off = len(string_block)
        string_block.extend(text.encode("utf-8") + b"\x00")
        return off

    def read_rec(i: int) -> list[int]:
        off = 20 + i * recsize
        return list(struct.unpack_from("<" + "I" * fields, data, off))

    idx_by_id = {read_rec(i)[0]: i for i in range(records)}
    if TEMPLATE_SPELL_ID not in idx_by_id:
        raise SystemExit(f"Template spell {TEMPLATE_SPELL_ID} missing from Spell.dbc")

    template = read_rec(idx_by_id[TEMPLATE_SPELL_ID])
    print(f"Template {TEMPLATE_SPELL_ID}: {get_str(template[136])!r}")

    keep_indices = []
    for i in range(records):
        rec = read_rec(i)
        if rec[0] in CUSTOM_SPELLS:
            continue
        keep_indices.append(i)

    new_records_data = bytearray()
    for i in keep_indices:
        off = 20 + i * recsize
        new_records_data.extend(data[off : off + recsize])

    def make_custom(spell_id: int, name: str, desc: str, icon: int) -> bytes:
        rec = list(template)
        rec[0] = spell_id
        rec[1] = 0
        rec[4] = ATTR_ABILITY_MOUNTED
        rec[5] = 0
        for attr_ex in range(6, 12):
            rec[attr_ex] = 0
        rec[28] = 1
        rec[29] = 0
        rec[30] = 0
        rec[35] = 101
        rec[40] = 0
        rec[46] = 1
        rec[68] = 0xFFFFFFFF
        rec[69] = 0xFFFFFFFF
        rec[71] = 3
        rec[72] = 0
        rec[73] = 0
        rec[75] = 0
        rec[86] = 1
        rec[87] = 0
        rec[88] = 0
        for f in (95, 96, 97, 98, 99, 100):
            rec[f] = 0
        rec[111] = 0
        for f in range(122, 131):
            rec[f] = 0
        rec[133] = icon
        rec[134] = 0
        rec[135] = 0
        rec[205] = 0
        rec[206] = 0
        rec[208] = 0
        rec[209] = 0
        rec[210] = 0
        rec[211] = 0
        name_off = add_str(name)
        desc_off = add_str(desc)
        for loc in range(136, 152):
            rec[loc] = name_off
        rec[152] = 16712190
        for loc in range(153, 169):
            rec[loc] = 0
        rec[169] = 0
        for loc in range(170, 186):
            rec[loc] = desc_off
        rec[186] = 16712190
        for loc in range(187, 203):
            rec[loc] = 0
        rec[203] = 0
        return struct.pack("<" + "I" * fields, *rec)

    for spell_id, (name, desc, icon) in CUSTOM_SPELLS.items():
        new_records_data.extend(make_custom(spell_id, name, desc, icon))
        print(f"Added spell {spell_id}: {name!r} icon={icon}")

    new_count = len(keep_indices) + len(CUSTOM_SPELLS)
    header = struct.pack("<4sIIII", b"WDBC", new_count, fields, recsize, len(string_block))
    DBC_DIR.mkdir(parents=True, exist_ok=True)
    SPELL_DBC.write_bytes(header + bytes(new_records_data) + bytes(string_block))
    print(f"Wrote {SPELL_DBC} ({new_count} records)")


def verify_dbc():
    data = SPELL_DBC.read_bytes()
    magic, records, fields, recsize, strsize = struct.unpack_from("<4sIIII", data, 0)
    str_off = 20 + records * recsize
    sb = data[str_off:]

    def get_str(off: int) -> str:
        if not off:
            return ""
        return sb[off : sb.index(b"\x00", off)].decode("utf-8", "replace")

    found = {}
    for i in range(records):
        rec = list(struct.unpack_from("<" + "I" * fields, data, 20 + i * recsize))
        if rec[0] in CUSTOM_SPELLS:
            found[rec[0]] = rec
    for spell_id, expected in CUSTOM_SPELLS.items():
        if spell_id not in found:
            raise SystemExit(f"Verify failed: missing {spell_id}")
        rec = found[spell_id]
        name = get_str(rec[136])
        print(
            f"Verify {spell_id}: name={name!r} icon={rec[133]} effect={rec[71]} "
            f"attr={rec[4]} cat={rec[1]} cd={rec[29]}/{rec[30]}"
        )
        if name != expected[0] or rec[133] != expected[2] or rec[71] != 3:
            raise SystemExit(f"Verify mismatch for {spell_id}")
        if rec[4] & 0x40:
            raise SystemExit(f"Spell {spell_id} still marked PASSIVE")
        if rec[1] or rec[29] or rec[30]:
            raise SystemExit(f"Spell {spell_id} still has a cooldown/category")


def patch_skill_line_ability():
    data = bytearray(SLA_DBC_BASE.read_bytes())
    magic, records, fields, recsize, strsize = struct.unpack_from("<4sIIII", data, 0)
    if magic != b"WDBC" or fields != 14 or recsize != 56:
        raise SystemExit(f"Unexpected SkillLineAbility.dbc header: {magic} {fields} {recsize}")

    def read_rec(i: int) -> list[int]:
        return list(struct.unpack_from("<" + "I" * fields, data, 20 + i * recsize))

    template = None
    max_id = 0
    keep = []
    for i in range(records):
        rec = read_rec(i)
        max_id = max(max_id, rec[0])
        if rec[2] in CUSTOM_SPELLS:
            continue
        keep.append(rec)
        if rec[2] == SLA_TEMPLATE_SPELL and template is None:
            template = list(rec)

    if template is None:
        raise SystemExit(f"SLA template spell {SLA_TEMPLATE_SPELL} not found")

    new_blob = bytearray()
    for rec in keep:
        new_blob.extend(struct.pack("<" + "I" * fields, *rec))

    next_id = max(max_id + 1, 910001)
    for spell_id in CUSTOM_SPELLS:
        rec = list(template)
        rec[0] = next_id
        next_id += 1
        rec[1] = GENERIC_SKILL_LINE
        rec[2] = spell_id
        rec[3] = 0
        rec[4] = 0
        rec[7] = 0
        rec[9] = 2
        new_blob.extend(struct.pack("<" + "I" * fields, *rec))
        print(f"Added SkillLineAbility {rec[0]} -> spell {spell_id}")

    new_count = len(keep) + len(CUSTOM_SPELLS)
    string_block = data[20 + records * recsize :]
    header = struct.pack("<4sIIII", b"WDBC", new_count, fields, recsize, len(string_block))
    DBC_DIR.mkdir(parents=True, exist_ok=True)
    SLA_DBC.write_bytes(header + bytes(new_blob) + bytes(string_block))
    print(f"Wrote {SLA_DBC} ({new_count} records)")


def patch_framexml():
    if not FRAME_TOC_BASE.exists():
        raise SystemExit(f"Missing {FRAME_TOC_BASE}")
    if not UI_LUA_SRC.exists():
        raise SystemExit(f"Missing UI {UI_LUA_SRC}")
    raw = FRAME_TOC_BASE.read_bytes()
    newline = b"\r\n" if b"\r\n" in raw else b"\n"
    text = raw.decode("utf-8")
    if "LivingGear.lua" not in text:
        needle = "## add new modules above here"
        if needle not in text:
            raise SystemExit("FrameXML.toc is missing the module insertion marker")
        text = text.replace(needle, "LivingGear.lua\n" + needle)
    FRAME_TOC.parent.mkdir(parents=True, exist_ok=True)
    body = text.replace("\r\n", "\n").replace("\n", "\r\n" if newline == b"\r\n" else "\n")
    FRAME_TOC.write_bytes(body.encode("utf-8"))
    lua = UI_LUA_SRC.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
    UI_LUA.write_bytes(lua)
    print(f"Wrote {FRAME_TOC} and {UI_LUA}")


def _create_mpq(storm, dest: Path, files: list[tuple[Path, bytes]]):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        dest.unlink()

    MPQ_CREATE_LISTFILE = 0x00100000
    MPQ_CREATE_ATTRIBUTES = 0x00200000
    MPQ_CREATE_ARCHIVE_V1 = 0x00000000
    h = ctypes.c_void_p()
    flags = MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES | MPQ_CREATE_ARCHIVE_V1
    ok = storm.SFileCreateArchive(str(dest), flags, 64, ctypes.byref(h))
    if not ok:
        raise SystemExit(f"SFileCreateArchive failed for {dest} err={ctypes.GetLastError()}")

    MPQ_FILE_COMPRESS = 0x00000200
    MPQ_FILE_REPLACEEXISTING = 0x80000000
    MPQ_COMPRESSION_ZLIB = 0x02
    for local, archived in files:
        ok = storm.SFileAddFileEx(
            h,
            str(local),
            archived,
            MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING,
            MPQ_COMPRESSION_ZLIB,
            MPQ_COMPRESSION_ZLIB,
        )
        if not ok:
            storm.SFileCloseArchive(h)
            raise SystemExit(f"Failed to add {local} as {archived!r} err={ctypes.GetLastError()}")
        print(f"Added {archived.decode()} <- {local.name}")

    storm.SFileCloseArchive(h)
    print(f"Built {dest} ({dest.stat().st_size} bytes)")


def build_mpq():
    storm = load_storm()
    _create_mpq(
        storm,
        OUT_MPQ,
        [
            (SPELL_DBC, b"DBFilesClient\\Spell.dbc"),
            (SLA_DBC, b"DBFilesClient\\SkillLineAbility.dbc"),
        ],
    )
    _create_mpq(
        storm,
        OUT_LOCALE_MPQ,
        [
            (FRAME_TOC, b"Interface\\FrameXML\\FrameXML.toc"),
            (UI_LUA, b"Interface\\FrameXML\\LivingGear.lua"),
        ],
    )


def main():
    patch_spell_dbc()
    verify_dbc()
    patch_skill_line_ability()
    patch_framexml()
    build_mpq()
    print("Done. Copy patch-Y.MPQ to Data/ and patch-enUS-4.MPQ to Data/enUS/.")


if __name__ == "__main__":
    main()
