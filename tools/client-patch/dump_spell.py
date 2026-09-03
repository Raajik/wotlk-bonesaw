"""Dump selected Spell.dbc rows from the cached base file."""
import struct
import sys
from pathlib import Path

SPELL_DBC = Path(__file__).resolve().parent / "cache" / "Spell.dbc.base"
LABELS = {
    1: "Category",
    4: "Attributes",
    5: "AttributesEx",
    6: "AttributesEx2",
    7: "AttributesEx3",
    8: "AttributesEx4",
    12: "Stances",
    28: "CastingTimeIndex",
    29: "RecoveryTime",
    30: "CategoryRecoveryTime",
    31: "InterruptFlags",
    35: "ProcChance",
    40: "DurationIndex",
    46: "RangeIndex",
    71: "Effect1",
    72: "Effect2",
    73: "Effect3",
    86: "TargetA1",
    95: "Aura1",
    131: "Visual1",
    133: "Icon",
    204: "ManaCostPct",
    205: "StartRecoveryCategory",
    206: "StartRecoveryTime",
    208: "SpellFamilyName",
    209: "SpellFamilyFlags1",
    212: "DmgClass",
    213: "PreventionType",
}


def main():
    ids = [int(x) for x in sys.argv[1:]] or [2656, 4036, 2018, 2259, 59752, 910001]
    data = SPELL_DBC.read_bytes()
    magic, records, fields, recsize, strsize = struct.unpack_from("<4sIIII", data, 0)
    str_off = 20 + records * recsize
    sb = data[str_off:]

    def get_str(off: int) -> str:
        if not off:
            return ""
        return sb[off : sb.index(b"\x00", off)].decode("utf-8", "replace")

    found = {sid: None for sid in ids}
    for i in range(records):
        rec = list(struct.unpack_from("<" + "I" * fields, data, 20 + i * recsize))
        if rec[0] in found:
            found[rec[0]] = rec

    for sid in ids:
        rec = found.get(sid)
        if rec is None:
            print(f"==== {sid} MISSING")
            continue
        print(f"==== {sid} {get_str(rec[136])!r}")
        for i, v in enumerate(rec):
            if i >= 136 and i <= 203:
                continue
            if v:
                print(f"  [{i:3d}] {LABELS.get(i, ''):22s} {v}")


if __name__ == "__main__":
    main()
