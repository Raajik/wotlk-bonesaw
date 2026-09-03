"""Classify every reputation faction by which team it is legal for.

Mirrors ReputationMgr::GetBaseReputation's slot matching exactly (first
matching slot wins; raceMask==0+classMask!=0 is a catch-all; classMask==0 is
a wildcard). A team is LEGAL for a faction when the worst base reputation
over that team's races/classes is above the Hated floor (-42000) -- factions
put the opposite team at Hated through a base-rep slot. If BOTH teams sit at
-42000 the base is team-independent and cross-team sharing is consistent, so
those are treated as legal for both (Netherwing, Brood of Nozdormu).

Output: the alliance-only and horde-only ID lists for the repair SQL.
"""
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PATH = r"A:\wow-bonesaw\var\mmap-output\dbc\Faction.dbc"
HATED = -42000
ALLIANCE_RACES = 1101   # human, dwarf, night elf, gnome, draenei
HORDE_RACES = 690       # orc, undead, tauren, troll, blood elf
CLASS_BITS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 1024]

data = open(PATH, "rb").read()
magic, rows, fields, recsize, strsize = struct.unpack_from("<4sIIII", data, 0)
strbase = 20 + rows * recsize

def base_for(vals, race_bit, class_bit):
    for i in range(4):
        race_mask = vals[2 + i]
        class_mask = vals[6 + i]
        race_match = (race_mask & race_bit) != 0 or (race_mask == 0 and class_mask != 0)
        class_match = (class_mask & class_bit) != 0 or class_mask == 0
        if race_match and class_match:
            return vals[10 + i]
    return 0

def worst_team_base(vals, race_mask):
    worst = None
    for race_bit in [1 << n for n in range(11)]:
        if not (race_mask & race_bit):
            continue
        for class_bit in CLASS_BITS:
            b = base_for(vals, race_bit, class_bit)
            worst = b if worst is None else min(worst, b)
    return worst if worst is not None else 0

alliance_only, horde_only, both, neither = [], [], [], []
for i in range(rows):
    off = 20 + i * recsize
    vals = struct.unpack_from("<%di" % fields, data, off)
    fid, replist = vals[0], vals[1]
    if replist < 0:
        continue
    soff = vals[23]
    name = data[strbase + soff:data.index(b"\x00", strbase + soff)].decode("utf-8", "replace")
    wa = worst_team_base(vals, ALLIANCE_RACES)
    wh = worst_team_base(vals, HORDE_RACES)
    if wa > HATED and wh > HATED:
        both.append((fid, name, wa, wh))
    elif wa > HATED:
        alliance_only.append((fid, name, wa, wh))
    elif wh > HATED:
        horde_only.append((fid, name, wa, wh))
    else:
        neither.append((fid, name, wa, wh))

for label, lst in (("BOTH (sync fine)", both), ("ALLIANCE-ONLY", alliance_only),
                   ("HORDE-ONLY", horde_only), ("NEITHER (uniform Hated, sync fine)", neither)):
    print("== %s (%d) ==" % (label, len(lst)))
    for fid, name, wa, wh in lst:
        print("  %-6d %-32s allianceBase=%-7d hordeBase=%d" % (fid, name[:32], wa, wh))

print()
print("ALLIANCE_ONLY_SQL = %s" % ",".join(str(f[0]) for f in alliance_only))
print("HORDE_ONLY_SQL = %s" % ",".join(str(f[0]) for f in horde_only))
