#!/usr/bin/env python3
"""Decode WotLK 3.3.5a client WDB caches into JSON and AzerothCore SQL.

    python wdb_dump.py "B:/Games/WoW 3.3.5/Synastria/Cache/WDB/enUS" -o out/synastria --sql

A .wdb file is the client's on-disk cache of the server's query responses --
SMSG_ITEM_QUERY_SINGLE_RESPONSE, SMSG_CREATURE_QUERY_RESPONSE and friends. The
payload of each record is the packet body verbatim, so decoding it recovers
almost the entire server-side template row for every object the client has ever
asked about. On a custom server that is the fastest route to their real data.

File layout (build 12340):

    magic[4]  'BDIW' item, 'BOMW' creature, 'BOGW' gameobject,
              'TSQW' quest, 'CPNW' npc text, 'BDNW' item name,
              'XTPW' page text
    uint32    client build
    char[4]   locale, reversed ('SUne' == enUS)
    uint32 x3 record size hint, table hash, wdb version
    records:  uint32 entry, uint32 size, uint8[size] payload
              size == 0 means "server said this entry does not exist"
    trailer:  eight zero bytes

Every decoder checks that it consumed the payload exactly. A non-zero leftover
means the server's struct differs from stock 3.3.5, which is itself a finding,
so those records are reported rather than silently emitted.
"""

import argparse
import json
import os
import struct
import sys
from collections import Counter

HEADER = 24


class Reader:
    """Little-endian cursor over one record payload."""

    def __init__(self, buf):
        self.b, self.i = buf, 0

    def u8(self):
        v = self.b[self.i]
        self.i += 1
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.i)[0]
        self.i += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.i)[0]
        self.i += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.b, self.i)[0]
        self.i += 4
        return round(v, 6)

    def cstr(self):
        end = self.b.index(b"\0", self.i)
        s = self.b[self.i:end].decode("utf-8", "replace")
        self.i = end + 1
        return s

    def u32s(self, n):
        return [self.u32() for _ in range(n)]

    def left(self):
        return len(self.b) - self.i


def read_wdb(path):
    """Yield (entry, payload) for every populated record."""
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < HEADER:
        return
    magic = data[0:4].decode("ascii", "replace")
    build = struct.unpack_from("<I", data, 4)[0]
    locale = data[8:12][::-1].decode("ascii", "replace")
    yield ("__header__", {"magic": magic, "build": build, "locale": locale})

    pos = HEADER
    while pos + 8 <= len(data):
        entry, size = struct.unpack_from("<II", data, pos)
        pos += 8
        if entry == 0 and size == 0:
            break
        if size == 0:
            continue  # negative cache entry
        if pos + size > len(data):
            break
        yield (entry, data[pos:pos + size])
        pos += size


# ------------------------------------------------------------------ decoders --

def dec_item(r):
    o = {}
    o["class"] = r.u32()
    o["subclass"] = r.u32()
    o["SoundOverrideSubclass"] = r.i32()
    o["name"] = r.cstr()
    o["name2"], o["name3"], o["name4"] = r.cstr(), r.cstr(), r.cstr()
    o["displayid"] = r.u32()
    o["Quality"] = r.u32()
    o["Flags"] = r.u32()
    o["FlagsExtra"] = r.u32()
    o["BuyPrice"] = r.u32()
    o["SellPrice"] = r.u32()
    o["InventoryType"] = r.u32()
    o["AllowableClass"] = r.i32()
    o["AllowableRace"] = r.i32()
    o["ItemLevel"] = r.u32()
    o["RequiredLevel"] = r.u32()
    o["RequiredSkill"] = r.u32()
    o["RequiredSkillRank"] = r.u32()
    o["requiredspell"] = r.u32()
    o["requiredhonorrank"] = r.u32()
    o["RequiredCityRank"] = r.u32()
    o["RequiredReputationFaction"] = r.u32()
    o["RequiredReputationRank"] = r.u32()
    o["maxcount"] = r.i32()
    o["stackable"] = r.i32()
    o["ContainerSlots"] = r.u32()
    n = r.u32()
    o["StatsCount"] = n
    for i in range(1, 11):
        o["stat_type%d" % i], o["stat_value%d" % i] = 0, 0
    for i in range(1, n + 1):
        t, v = r.u32(), r.i32()
        if i <= 10:
            o["stat_type%d" % i], o["stat_value%d" % i] = t, v
    o["ScalingStatDistribution"] = r.u32()
    o["ScalingStatValue"] = r.u32()
    for i in (1, 2):
        o["dmg_min%d" % i] = r.f32()
        o["dmg_max%d" % i] = r.f32()
        o["dmg_type%d" % i] = r.u32()
    o["armor"] = r.u32()
    for k in ("holy_res", "fire_res", "nature_res", "frost_res", "shadow_res", "arcane_res"):
        o[k] = r.u32()
    o["delay"] = r.u32()
    o["ammo_type"] = r.u32()
    o["RangedModRange"] = r.f32()
    for i in (1, 2, 3, 4, 5):
        o["spellid_%d" % i] = r.u32()
        o["spelltrigger_%d" % i] = r.u32()
        o["spellcharges_%d" % i] = r.i32()
        o["spellcooldown_%d" % i] = r.i32()
        o["spellcategory_%d" % i] = r.u32()
        o["spellcategorycooldown_%d" % i] = r.i32()
    o["bonding"] = r.u32()
    o["description"] = r.cstr()
    o["PageText"] = r.u32()
    o["LanguageID"] = r.u32()
    o["PageMaterial"] = r.u32()
    o["startquest"] = r.u32()
    o["lockid"] = r.u32()
    o["Material"] = r.i32()
    o["sheath"] = r.u32()
    o["RandomProperty"] = r.i32()
    o["RandomSuffix"] = r.i32()
    o["block"] = r.u32()
    o["itemset"] = r.u32()
    o["MaxDurability"] = r.u32()
    o["area"] = r.u32()
    o["Map"] = r.i32()
    o["BagFamily"] = r.u32()
    o["TotemCategory"] = r.u32()
    for i in (1, 2, 3):
        o["socketColor_%d" % i] = r.u32()
        o["socketContent_%d" % i] = r.u32()
    o["socketBonus"] = r.u32()
    o["GemProperties"] = r.u32()
    o["RequiredDisenchantSkill"] = r.i32()
    o["ArmorDamageModifier"] = r.f32()
    o["duration"] = r.u32()
    o["ItemLimitCategory"] = r.u32()
    o["HolidayId"] = r.u32()
    return o


def dec_creature(r):
    o = {}
    o["name"] = r.cstr()
    o["name2"], o["name3"], o["name4"] = r.cstr(), r.cstr(), r.cstr()
    o["subname"] = r.cstr()
    o["IconName"] = r.cstr()
    o["type_flags"] = r.u32()
    o["type"] = r.u32()
    o["family"] = r.u32()
    o["rank"] = r.u32()
    o["KillCredit1"] = r.u32()
    o["KillCredit2"] = r.u32()
    o["modelid1"] = r.u32()
    o["modelid2"] = r.u32()
    o["modelid3"] = r.u32()
    o["modelid4"] = r.u32()
    o["HealthModifier"] = r.f32()
    o["ManaModifier"] = r.f32()
    o["RacialLeader"] = r.u8()
    o["questItems"] = r.u32s(6)
    o["movementId"] = r.u32()
    return o


def dec_gameobject(r):
    o = {}
    o["type"] = r.u32()
    o["displayId"] = r.u32()
    o["name"] = r.cstr()
    o["name2"], o["name3"], o["name4"] = r.cstr(), r.cstr(), r.cstr()
    o["IconName"] = r.cstr()
    o["castBarCaption"] = r.cstr()
    o["unk1"] = r.cstr()
    # Trailing block is data[N] + float size + questItems[6]; N is 24 on stock
    # 3.3.5 but derive it so a server with a wider struct still decodes.
    n = (r.left() - 28) // 4
    o["_data_count"] = n
    o["data"] = r.u32s(max(n, 0))
    o["size"] = r.f32()
    o["questItems"] = r.u32s(6)
    return o


def dec_npctext(r):
    """Gossip text: eight probability-weighted variants per entry."""
    o = {"texts": []}
    for _ in range(8):
        e = {"prob": r.f32(), "text0": r.cstr(), "text1": r.cstr(), "lang": r.u32()}
        e["emotes"] = [[r.u32(), r.u32()] for _ in range(3)]
        o["texts"].append(e)
    o["texts"] = [t for t in o["texts"] if t["text0"] or t["text1"]]
    return o


def dec_itemname(r):
    return {"name": r.cstr(), "InventoryType": r.u32()}


def dec_pagetext(r):
    return {"text": r.cstr(), "next_page": r.u32()}


def dec_quest(r):
    o = {}
    o["_entry_echo"] = r.u32()
    o["QuestMethod"] = r.u32()
    o["QuestLevel"] = r.i32()
    o["MinLevel"] = r.u32()
    o["ZoneOrSort"] = r.i32()
    o["Type"] = r.u32()
    o["SuggestedPlayers"] = r.u32()
    o["RequiredFactionId1"] = r.u32()
    o["RequiredFactionValue1"] = r.u32()
    o["RequiredFactionId2"] = r.u32()
    o["RequiredFactionValue2"] = r.u32()
    o["NextQuestIdChain"] = r.u32()
    o["RewardXPId"] = r.u32()
    o["RewardOrRequiredMoney"] = r.i32()
    o["RewardMoneyMaxLevel"] = r.u32()
    o["RewardSpell"] = r.u32()
    o["RewardSpellCast"] = r.i32()
    o["RewardHonor"] = r.u32()
    o["RewardHonorMultiplier"] = r.f32()
    o["StartItem"] = r.u32()
    o["Flags"] = r.u32()
    o["RewardTitleId"] = r.u32()
    o["RequiredPlayerKills"] = r.u32()
    o["RewardTalents"] = r.u32()
    o["RewardArenaPoints"] = r.u32()
    o["_unk0"] = r.u32()
    o["RewardItemId"] = r.u32s(4)
    o["RewardItemCount"] = r.u32s(4)
    o["RewardChoiceItemId"] = r.u32s(6)
    o["RewardChoiceItemCount"] = r.u32s(6)
    o["RewardFactionId"] = r.u32s(5)
    o["RewardFactionValueId"] = [r.i32() for _ in range(5)]
    o["RewardFactionValueIdOverride"] = [r.i32() for _ in range(5)]
    o["PointMapId"] = r.u32()
    o["PointX"], o["PointY"] = r.f32(), r.f32()
    o["PointOpt"] = r.u32()
    o["Title"] = r.cstr()
    o["Objectives"] = r.cstr()
    o["Details"] = r.cstr()
    o["EndText"] = r.cstr()
    o["OfferRewardText"] = r.cstr()
    o["RequiredNpcOrGo"] = []
    o["RequiredNpcOrGoCount"] = []
    o["RequiredSourceId"] = []
    o["RequiredSourceCount"] = []
    for _ in range(4):
        o["RequiredNpcOrGo"].append(r.i32())
        o["RequiredNpcOrGoCount"].append(r.u32())
        o["RequiredSourceId"].append(r.u32())
        o["RequiredSourceCount"].append(r.u32())
    o["RequiredItemId"] = []
    o["RequiredItemCount"] = []
    for _ in range(6):
        o["RequiredItemId"].append(r.u32())
        o["RequiredItemCount"].append(r.u32())
    o["ObjectiveText"] = [r.cstr() for _ in range(4)]
    return o


DECODERS = {
    "BDIW": ("item", dec_item),
    "BOMW": ("creature", dec_creature),
    "BOGW": ("gameobject", dec_gameobject),
    "TSQW": ("quest", dec_quest),
    "CPNW": ("npctext", dec_npctext),
    "BDNW": ("itemname", dec_itemname),
    "XTPW": ("pagetext", dec_pagetext),
    "XTIW": ("itemtext", dec_pagetext),
}


def decode_file(path):
    rows, header, bad = {}, None, []
    for entry, payload in read_wdb(path):
        if entry == "__header__":
            header = payload
            continue
        kind_dec = DECODERS.get(header["magic"] if header else "")
        if not kind_dec:
            return header, {}, [("unsupported magic", header)]
        _, dec = kind_dec
        r = Reader(payload)
        try:
            obj = dec(r)
        except (struct.error, ValueError, IndexError) as exc:
            bad.append((entry, "%s: %s" % (type(exc).__name__, exc)))
            continue
        leftover = r.left()
        if leftover:
            obj["_leftover_bytes"] = leftover
            bad.append((entry, "%d bytes unconsumed" % leftover))
        obj["entry"] = entry
        rows[entry] = obj
    return header, rows, bad


# ----------------------------------------------------------------------- sql --

ITEM_SKIP = {"name2", "name3", "name4", "StatsCount", "entry", "_leftover_bytes"}

CREATURE_COLS = [
    ("name", "name"), ("subname", "subname"), ("IconName", "IconName"),
    ("type_flags", "type_flags"), ("type", "type"), ("family", "family"),
    ("rank", "rank"), ("KillCredit1", "KillCredit1"), ("KillCredit2", "KillCredit2"),
    ("HealthModifier", "HealthModifier"), ("ManaModifier", "ManaModifier"),
    ("RacialLeader", "RacialLeader"), ("movementId", "movementId"),
]


def sql_value(v):
    if isinstance(v, str):
        return "'" + v.replace("\\", "\\\\").replace("'", "\\'") + "'"
    if isinstance(v, float):
        return repr(v)
    return str(v)


def write_item_sql(rows, fh):
    fh.write("-- item_template rows rebuilt from the client's itemcache.wdb.\n")
    fh.write("-- BuyCount and spellppmRate are not carried in the query response.\n")
    for entry, o in sorted(rows.items()):
        cols = ["entry"] + [k for k in o if k not in ITEM_SKIP and not k.startswith("_")]
        vals = [str(entry)] + [sql_value(o[k]) for k in cols[1:]]
        fh.write("REPLACE INTO `item_template` (`%s`) VALUES (%s);\n"
                 % ("`, `".join(cols), ", ".join(vals)))


def write_creature_sql(rows, fh):
    fh.write("-- creature_template rows rebuilt from the client's creaturecache.wdb.\n")
    fh.write("-- Only fields the query response carries; combat stats are server-side.\n")
    for entry, o in sorted(rows.items()):
        cols = ["entry"] + [c for c, _ in CREATURE_COLS]
        vals = [str(entry)] + [sql_value(o[src]) for _, src in CREATURE_COLS]
        for i in range(1, 5):
            cols.append("modelid%d" % i)
            vals.append(str(o["modelid%d" % i]))
        fh.write("REPLACE INTO `creature_template` (`%s`) VALUES (%s);\n"
                 % ("`, `".join(cols), ", ".join(vals)))


def write_gameobject_sql(rows, fh):
    fh.write("-- gameobject_template rows rebuilt from the client's gameobjectcache.wdb.\n")
    for entry, o in sorted(rows.items()):
        cols = ["entry", "type", "displayId", "name", "IconName", "castBarCaption", "unk1", "size"]
        vals = [str(entry), str(o["type"]), str(o["displayId"]), sql_value(o["name"]),
                sql_value(o["IconName"]), sql_value(o["castBarCaption"]),
                sql_value(o["unk1"]), repr(o["size"])]
        for i, d in enumerate(o["data"][:24]):
            cols.append("Data%d" % i)
            vals.append(str(d))
        fh.write("REPLACE INTO `gameobject_template` (`%s`) VALUES (%s);\n"
                 % ("`, `".join(cols), ", ".join(vals)))


SQL_WRITERS = {"item": write_item_sql, "creature": write_creature_sql,
               "gameobject": write_gameobject_sql}


# -------------------------------------------------------------------- report --

def summarize(kind, rows, fh):
    w = fh.write
    if kind == "item":
        w("\n## Items (%d)\n\n" % len(rows))
        custom = [o for o in rows.values() if o["entry"] > 56000]
        w("Entries above 56000 (outside the retail 3.3.5a range): **%d**\n\n" % len(custom))
        by_q = Counter(o["Quality"] for o in rows.values())
        w("Quality spread: %s\n\n" % ", ".join("%s=%d" % kv for kv in sorted(by_q.items())))
        spelled = [o for o in rows.values() if o.get("spellid_1")]
        w("Items carrying a spell: %d\n\n" % len(spelled))
        if custom:
            w("| entry | name | ilvl | q | slot | spells |\n|---|---|---|---|---|---|\n")
            for o in sorted(custom, key=lambda x: -x["ItemLevel"])[:400]:
                spells = ",".join(str(o["spellid_%d" % i]) for i in range(1, 6)
                                  if o["spellid_%d" % i])
                w("| %d | %s | %d | %d | %d | %s |\n" % (
                    o["entry"], o["name"], o["ItemLevel"], o["Quality"],
                    o["InventoryType"], spells or "-"))
    elif kind == "creature":
        w("\n## Creatures (%d)\n\n" % len(rows))
        w("| entry | name | subname | rank | type |\n|---|---|---|---|---|\n")
        for o in sorted(rows.values(), key=lambda x: x["entry"]):
            w("| %d | %s | %s | %d | %d |\n"
              % (o["entry"], o["name"], o["subname"] or "", o["rank"], o["type"]))
    elif kind == "gameobject":
        w("\n## GameObjects (%d)\n\n" % len(rows))
        w("| entry | name | type | caption |\n|---|---|---|---|\n")
        for o in sorted(rows.values(), key=lambda x: x["entry"]):
            w("| %d | %s | %d | %s |\n"
              % (o["entry"], o["name"], o["type"], o["castBarCaption"] or ""))
    elif kind == "npctext":
        w("\n## Gossip text (%d entries)\n\n" % len(rows))
        for o in sorted(rows.values(), key=lambda x: x["entry"]):
            w("### %d\n\n" % o["entry"])
            for t in o["texts"]:
                body = t["text0"] or t["text1"]
                w("> %s\n\n" % body.replace("\n", "\n> "))
    elif kind == "quest":
        w("\n## Quests (%d)\n\n" % len(rows))
        for o in sorted(rows.values(), key=lambda x: x["entry"]):
            w("### %d -- %s (level %d)\n\n" % (o["entry"], o["Title"], o["QuestLevel"]))
            if o["Objectives"]:
                w("%s\n\n" % o["Objectives"])
            for t in o["ObjectiveText"]:
                if t:
                    w("- %s\n" % t)
            w("\n")
    else:
        w("\n## %s (%d)\n\n" % (kind, len(rows)))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wdb_dir", help="a Cache/WDB/<locale> directory, or one .wdb file")
    ap.add_argument("-o", "--out", default="wdb", help="output path prefix")
    ap.add_argument("--sql", action="store_true",
                    help="also emit AzerothCore REPLACE INTO statements")
    args = ap.parse_args()

    if os.path.isdir(args.wdb_dir):
        paths = [os.path.join(args.wdb_dir, n) for n in sorted(os.listdir(args.wdb_dir))
                 if n.lower().endswith(".wdb")]
    else:
        paths = [args.wdb_dir]
    if not paths:
        sys.exit("no .wdb files found in %s" % args.wdb_dir)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    everything, report_path = {}, args.out + ".md"
    with open(report_path, "w", encoding="utf-8") as md:
        md.write("# WDB cache dump\n\n")
        md.write("Source: `%s`\n\n" % args.wdb_dir)
        for path in paths:
            header, rows, bad = decode_file(path)
            if not header:
                continue
            kind = DECODERS.get(header["magic"], ("unknown", None))[0]
            print("%-22s %-6s build %-6s %5d records%s" % (
                os.path.basename(path), kind, header["build"], len(rows),
                "  (%d suspect)" % len(bad) if bad else ""))
            if bad:
                md.write("\n> `%s`: %d records did not decode cleanly. "
                         "First few: %s\n\n"
                         % (os.path.basename(path), len(bad),
                            "; ".join("%s %s" % b for b in bad[:5])))
            if not rows:
                continue
            everything[kind] = rows
            summarize(kind, rows, md)
            if args.sql and kind in SQL_WRITERS:
                sql_path = "%s_%s.sql" % (args.out, kind)
                with open(sql_path, "w", encoding="utf-8") as fh:
                    SQL_WRITERS[kind](rows, fh)
                print("  -> %s" % sql_path)

    with open(args.out + ".json", "w", encoding="utf-8") as fh:
        json.dump(everything, fh, indent=1, ensure_ascii=False)
    print("wrote %s.json and %s" % (args.out, report_path))


if __name__ == "__main__":
    main()
