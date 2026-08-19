"""Patch LFGDungeons.dbc + LFGDungeonGroup.dbc so Vanilla-WotLK raids show in LFD.

Stock 3.3.5 lists raids as TypeID 2 (Raid Browser). This converts them to TypeID 1
(Dungeon Finder) with ASCII size suffixes, and adds ICC/Ruby Sanctum heroics.
"""
from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CACHE = ROOT / "cache"
STAGING = ROOT / "staging" / "DBFilesClient"
SQL_OUT = ROOT.parent.parent / "data" / "sql" / "updates" / "pending_db_world" / "rev_lfg_raids.sql"

LFG_BASE = CACHE / "LFGDungeons.dbc.base"
LFG_OUT = STAGING / "LFGDungeons.dbc"
GROUP_BASE = CACHE / "LFGDungeonGroup.dbc.base"
GROUP_OUT = STAGING / "LFGDungeonGroup.dbc"
MAPDIFF = CACHE / "MapDifficulty.dbc"

LFG_TYPE_DUNGEON = 1
LFG_TYPE_RAID = 2
LFG_FLAGS_LFD = 3
NAME_MASK = 16712190
SKIP_MAPS = {229}  # Upper Blackrock Spire is not a raid map in 3.3.5a

RENAMES = {
    "Hyjal Past": "Hyjal Summit",
    "The Sunwell": "Sunwell Plateau",
    "The Eye of Eternity": "Eye of Eternity",
    "The Obsidian Sanctum": "Obsidian Sanctum",
    "Ahn'Qiraj Ruins": "Ruins of Ahn'Qiraj",
    "Ahn'Qiraj Temple": "Temple of Ahn'Qiraj",
}

# clone_id, new_id, difficulty, group_id
NEW_HEROICS = (
    (279, 800, 2, 8),  # Icecrown Citadel Heroic (10)
    (280, 801, 3, 9),  # Icecrown Citadel Heroic (25)
    (293, 802, 2, 8),  # Ruby Sanctum Heroic (10)
    (294, 803, 3, 9),  # Ruby Sanctum Heroic (25)
)

LOCALE_NAMES = 16


def _read_dbc(path: Path) -> tuple[int, int, int, list[list[int]], bytearray]:
    data = path.read_bytes()
    magic, records, fields, recsize, strsize = struct.unpack_from("<4sIIII", data, 0)
    if magic != b"WDBC":
        raise SystemExit(f"Bad DBC magic in {path}")
    recs = []
    for i in range(records):
        recs.append(list(struct.unpack_from("<" + "i" * fields, data, 20 + i * recsize)))
    sb = bytearray(data[20 + records * recsize :])
    return fields, recsize, strsize, recs, sb


def _get_str(sb: bytes, off: int) -> str:
    if not off:
        return ""
    try:
        return sb[off : sb.index(b"\x00", off)].decode("utf-8", "replace")
    except ValueError:
        return ""


def _add_str(sb: bytearray, text: str) -> int:
    if any(ord(ch) > 127 for ch in text):
        raise SystemExit(f"Non-ASCII LFG name: {text!r}")
    off = len(sb)
    sb.extend(text.encode("ascii") + b"\x00")
    return off


def _write_dbc(path: Path, fields: int, recsize: int, recs: list[list[int]], sb: bytearray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    for rec in recs:
        blob.extend(struct.pack("<" + "i" * fields, *rec))
    header = struct.pack("<4sIIII", b"WDBC", len(recs), fields, recsize, len(sb))
    path.write_bytes(header + bytes(blob) + bytes(sb))


def load_map_max_players() -> dict[tuple[int, int], int]:
    fields, recsize, _, recs, _ = _read_dbc(MAPDIFF)
    out = {}
    for rec in recs:
        out[(rec[1], rec[2])] = rec[21]
    return out


def sized_name(name: str, size: int, heroic: bool) -> str:
    display = RENAMES.get(name, name)
    if heroic and "Heroic" not in display:
        display = f"{display} Heroic"
    suffix = f" ({size})"
    if display.endswith(suffix):
        return display
    return display + suffix


def patch_lfg_dungeons() -> list[list[int]]:
    if not LFG_BASE.exists():
        raise SystemExit(f"Missing {LFG_BASE}")
    fields, recsize, _, recs, sb = _read_dbc(LFG_BASE)
    if fields != 49 or recsize != 196:
        raise SystemExit(f"Unexpected LFGDungeons.dbc layout: fields={fields} recsize={recsize}")
    sizes = load_map_max_players()
    by_id = {rec[0]: rec for rec in recs}
    patched: list[list[int]] = []

    def apply_raid_row(rec: list[int], *, heroic: bool = False, size_diff: int | None = None) -> list[int]:
        rec = list(rec)
        map_id = rec[23]
        diff = rec[24] if size_diff is None else size_diff
        size = sizes.get((map_id, diff))
        if not size:
            raise SystemExit(f"No MapDifficulty maxPlayers for map {map_id} diff {diff}")
        old = _get_str(sb, rec[1])
        new_name = sized_name(old, size, heroic)
        name_off = _add_str(sb, new_name)
        for loc in range(1, 17):
            rec[loc] = name_off
        rec[17] = NAME_MASK
        rec[24] = diff
        rec[25] = LFG_FLAGS_LFD
        rec[26] = LFG_TYPE_DUNGEON
        rec[27] = -1
        return rec

    for rec in recs:
        if rec[26] == LFG_TYPE_RAID and rec[23] not in SKIP_MAPS:
            rec = apply_raid_row(rec)
            print(f"LFG raid {rec[0]}: {_get_str(sb, rec[1])!r} map={rec[23]} diff={rec[24]} group={rec[31]}")
        patched.append(rec)

    for clone_id, new_id, diff, group_id in NEW_HEROICS:
        src = by_id.get(clone_id)
        if src is None:
            raise SystemExit(f"Missing clone source LFG id {clone_id}")
        rec = apply_raid_row(src, heroic=True, size_diff=diff)
        rec[0] = new_id
        rec[31] = group_id
        patched.append(rec)
        print(f"LFG raid {rec[0]}: {_get_str(sb, rec[1])!r} map={rec[23]} diff={rec[24]} group={rec[31]}")

    _write_dbc(LFG_OUT, fields, recsize, patched, sb)
    print(f"Wrote {LFG_OUT} ({len(patched)} records)")
    return patched


def patch_lfg_groups() -> None:
    if not GROUP_BASE.exists():
        raise SystemExit(f"Missing {GROUP_BASE}")
    fields, recsize, _, recs, sb = _read_dbc(GROUP_BASE)
    changed = 0
    for rec in recs:
        # Raid browser groups 6-9: Classic / TBC / WotLK 10 / WotLK 25
        if rec[0] in (6, 7, 8, 9) and rec[20] == LFG_TYPE_RAID:
            rec[20] = LFG_TYPE_DUNGEON
            changed += 1
            print(f"LFG group {rec[0]} {_get_str(sb, rec[1])!r} type -> dungeon")
    _write_dbc(GROUP_OUT, fields, recsize, recs, sb)
    print(f"Wrote {GROUP_OUT} (converted {changed} raid groups to LFD)")


def _sql_str(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def write_sql(recs: list[list[int]]) -> None:
    raid_recs = [rec for rec in recs if rec[26] == LFG_TYPE_DUNGEON and rec[0] >= 42]
    # Keep only converted raids plus new heroics (type 1, map is raid-sized).
    raid_ids = []
    rows = []
    for rec in recs:
        if rec[26] != LFG_TYPE_DUNGEON:
            continue
        if rec[0] in {42, 46, 48, 50, 159, 160, 161, 175, 176, 177, 193, 194, 195, 196, 197, 199,
                      223, 224, 227, 237, 238, 239, 240, 243, 244, 246, 247, 248, 250, 257,
                      279, 280, 293, 294, 800, 801, 802, 803}:
            raid_ids.append(rec[0])
            rows.append(rec)
    raid_ids.sort()
    id_list = ", ".join(str(i) for i in raid_ids)
    lines = [
        "-- Looking For Dungeon entries for Vanilla through WotLK raids.",
        "-- Client names live in LFGDungeons.dbc (patch-Y.MPQ). This table overrides the server DBC.",
        "",
        f"DELETE FROM `lfgdungeons_dbc` WHERE `ID` IN ({id_list});",
        "INSERT INTO `lfgdungeons_dbc` (",
        "  `ID`,",
        "  `Name_Lang_enUS`, `Name_Lang_enGB`, `Name_Lang_koKR`, `Name_Lang_frFR`, `Name_Lang_deDE`,",
        "  `Name_Lang_enCN`, `Name_Lang_zhCN`, `Name_Lang_enTW`, `Name_Lang_zhTW`, `Name_Lang_esES`,",
        "  `Name_Lang_esMX`, `Name_Lang_ruRU`, `Name_Lang_ptPT`, `Name_Lang_ptBR`, `Name_Lang_itIT`,",
        "  `Name_Lang_Unk`, `Name_Lang_Mask`,",
        "  `MinLevel`, `MaxLevel`, `Target_Level`, `Target_Level_Min`, `Target_Level_Max`,",
        "  `MapID`, `Difficulty`, `Flags`, `TypeID`, `Faction`, `TextureFilename`,",
        "  `ExpansionLevel`, `Order_Index`, `Group_Id`,",
        "  `Description_Lang_enUS`, `Description_Lang_enGB`, `Description_Lang_koKR`, `Description_Lang_frFR`,",
        "  `Description_Lang_deDE`, `Description_Lang_enCN`, `Description_Lang_zhCN`, `Description_Lang_enTW`,",
        "  `Description_Lang_zhTW`, `Description_Lang_esES`, `Description_Lang_esMX`, `Description_Lang_ruRU`,",
        "  `Description_Lang_ptPT`, `Description_Lang_ptBR`, `Description_Lang_itIT`, `Description_Lang_Unk`,",
        "  `Description_Lang_Mask`",
        ") VALUES",
    ]
    # Re-read written DBC for string offsets after patch.
    fields, recsize, _, out_recs, sb = _read_dbc(LFG_OUT)
    by_id = {rec[0]: rec for rec in out_recs}
    value_lines = []
    for rid in raid_ids:
        rec = by_id[rid]
        name = _get_str(sb, rec[1])
        tex = _get_str(sb, rec[28])
        desc = _get_str(sb, rec[32])
        name_sql = _sql_str(name)
        tex_sql = _sql_str(tex) if tex else "''"
        desc_sql = _sql_str(desc) if desc else "NULL"
        empty_names = ", ".join([name_sql, name_sql] + ["NULL"] * 14)
        empty_descs = ", ".join([desc_sql] + ["NULL"] * 15)
        value_lines.append(
            f"({rid}, {empty_names}, {rec[17]}, "
            f"{rec[18]}, {rec[19]}, {rec[20]}, {rec[21]}, {rec[22]}, "
            f"{rec[23]}, {rec[24]}, {rec[25]}, {rec[26]}, {rec[27]}, {tex_sql}, "
            f"{rec[29]}, {rec[30]}, {rec[31]}, "
            f"{empty_descs}, {rec[48]})"
        )
    lines.append(",\n".join(value_lines) + ";")
    lines.append("")
    SQL_OUT.parent.mkdir(parents=True, exist_ok=True)
    SQL_OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {SQL_OUT} ({len(raid_ids)} raids)")


def patch() -> None:
    recs = patch_lfg_dungeons()
    patch_lfg_groups()
    write_sql(recs)


if __name__ == "__main__":
    patch()
