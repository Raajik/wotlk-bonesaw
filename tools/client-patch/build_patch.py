"""
Build Bonesaw client patch-Y.MPQ.

Adds *Account Perks (910001) to Spell.dbc + SkillLineAbility.dbc.
FrameXML UI ships in patch-enUS-4.MPQ and patch-enGB-4.MPQ. Needs the patched Wow.exe.

Do not overwrite Data/patch-Z.mpq -- that archive already ships Item.dbc.

Usage (Windows):
  python tools/client-patch/build_patch.py
"""
from __future__ import annotations

import ctypes
import shutil
import struct
import sys
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
LOCALES = ("enUS", "enGB")
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
    910001: ("*Account Perks", "Open account perks and Living Gear.", 67),
    910002: ("*Mailbox", "Open your mailbox.", 129),
    910003: ("*Auction House", "Open the auction house.", 133),
    910004: ("*Class Trainer", "Open your class trainer.", 149),
    910005: ("*Bank", "Open your bank.", 249),
    910006: ("*Stable", "Open the pet stable.", 132),
    910007: ("*Bind", "Bind your hearthstone here.", 11),
    910008: ("*Autoloot", "Toggle automatic looting. On by default.", 185),
    910009: ("*Flight Master", "Open the flight map from anywhere.", 1721),
    910010: ("*Honor: Defeat", "Honor gains +100%. Unlocked by losing a battleground.", 395),
    910011: ("*Honor: Victory", "Honor gains +200%. Unlocked by winning a battleground.", 457),
    910012: ("*Honor: Bloodied", "Honor gains +200%. Unlocked by 100 honorable kills.", 1459),
    910013: ("*Rep: First Exalted", "Reputation gains +100%. Unlocked by 1 exalted faction.", 149),
    910014: ("*Rep: Five Exalted", "Reputation gains +100%. Unlocked by 5 exalted factions.", 201),
    910015: ("*Rep: Ten Exalted", "Reputation gains +100%. Unlocked by 10 exalted factions.", 457),
    910016: ("*Rep: Bloodsail", "Reputation gains +100%. Unlocked by Bloodsail Buccaneers exalted.", 395),
    910017: ("*Rep: Darkmoon", "Reputation gains +100%. Unlocked by Darkmoon Faire exalted.", 185),
    910018: ("*Rep: Ravenholdt", "Reputation gains +100%. Unlocked by Ravenholdt exalted.", 11),
    910019: ("*Rep: Shendralar", "Reputation gains +100%. Unlocked by Shendralar exalted.", 67),
    910020: ("*Rep: Arathor", "Reputation gains +100%. Unlocked by League of Arathor exalted.", 457),
    910021: ("*Rep: Defilers", "Reputation gains +100%. Unlocked by The Defilers exalted.", 395),
    910022: ("*Rep: Silverwing", "Reputation gains +100%. Unlocked by Silverwing Sentinels exalted.", 149),
    910023: ("*Rep: Warsong", "Reputation gains +100%. Unlocked by Warsong Outriders exalted.", 201),
    910024: ("*Rep: Stormpike", "Reputation gains +100%. Unlocked by Stormpike Guard exalted.", 132),
    910025: ("*Rep: Frostwolf", "Reputation gains +100%. Unlocked by Frostwolf Clan exalted.", 133),
    910026: ("*Trade: 75", "Profession skill-ups +100%. Unlocked by reaching skill 75.", 326),
    910027: ("*Trade: 150", "Profession skill-ups +100%. Unlocked by reaching skill 150.", 327),
    910028: ("*Trade: 225", "Profession skill-ups +100%. Unlocked by reaching skill 225.", 328),
    910029: ("*Trade: 300", "Profession skill-ups +100%. Unlocked by reaching skill 300.", 329),
    910030: ("*Trade: 375", "Profession skill-ups +100%. Unlocked by reaching skill 375.", 330),
    910031: ("*Trade: 450", "Profession skill-ups +100%. Unlocked by reaching skill 450.", 331),
    910032: ("*Mage: Arcane", "While in combat, Mirror Images appear and chain-cast. They linger 60 sec after combat.", 225),
    910033: ("*Mage: Fire", "Fire spells apply Living Bomb. That effect spreads to enemies within 15 yards every 1 sec.", 11),
    910034: ("*Mage: Frost", "Blizzard is instant, no cooldown, and lingers like Death and Decay. In combat, Ice Lance hits enemies within 15 yards every 2 sec.", 188),
    910035: ("*Rogue: Assassination", "Poisons deal 300% increased damage. DoT poisons spread to enemies within 10 yards.", 500),
    910036: ("*Rogue: Combat", "Blade Flurry is always active. Energy regeneration increased by 50%. Combo builders have a 30% chance to cast free Killing Spree.", 514),
    910037: ("*Rogue: Subtlety", "Gain Shadowstep with no cooldown. Shadowstep grants +40% movement speed for 30 sec. Clones Ambush, throw a poisoned dagger at the nearest enemy, then Vanish, hopping to nearby enemies.", 250),
    910038: ("*Quest: Wayfarer", "Movement speed +40%. Stacks with other speed effects. Unlocked by completing 100 quests.", 516),
    910039: ("*Jump: Double", "Jumps go twice as high and far. 10 sec cooldown between boosted jumps. Boosted jumps grant +40% movement speed for 30 sec. Unlocked at level 10.", 1299),
    910040: ("*Jump: Triple", "Jumps go three times as high and far. 10 sec cooldown between boosted jumps. Boosted jumps grant +40% movement speed for 30 sec. Unlocked at level 30.", 1762),
    910041: ("*Gear: Auto-Attune", "Auto-attune looted gear. Poor starts unlocked. Higher qualities unlock as you attune more items (10, 100, 1000, ...).", 1762),
    910042: ("*Attune Backpack", "Attune every living-gear piece in your bags. Items are destroyed and 10% of grown stats go to the account.", 249),
    910053: ("*Leveling: 1", "XP gains +50%. Unlocked by 1 character at level 80.", 148),
    910054: ("*Leveling: 2", "XP gains +50%. Unlocked by 2 characters at level 80.", 148),
    910055: ("*Leveling: 3", "XP gains +50%. Unlocked by 3 characters at level 80.", 148),
    910056: ("*Leveling: 4", "XP gains +50%. Unlocked by 4 characters at level 80.", 148),
    910057: ("*Leveling: 5", "XP gains +50%. Unlocked by 5 characters at level 80.", 148),
    910058: ("*Leveling: 6", "XP gains +50%. Unlocked by 6 characters at level 80.", 148),
    910059: ("*Leveling: 7", "XP gains +50%. Unlocked by 7 characters at level 80.", 148),
    910060: ("*Leveling: 8", "XP gains +50%. Unlocked by 8 characters at level 80.", 148),
    910061: ("*Leveling: 9", "XP gains +50%. Unlocked by 9 characters at level 80.", 148),
    910062: ("*Leveling: 10", "XP gains +50%. Unlocked by 10 characters at level 80.", 148),
    910063: ("*Cooking: 75", "Out of combat, heal 1% of max health every second. Unlocked by Cooking 75.", 1467),
    910064: ("*Cooking: 150", "Out of combat, heal 2% of max health every second. Unlocked by Cooking 150.", 1467),
    910065: ("*Cooking: 225", "Out of combat, heal 3% of max health every second. Unlocked by Cooking 225.", 1467),
    910066: ("*Cooking: 300", "Out of combat, heal 4% of max health every second. Unlocked by Cooking 300.", 1467),
    910067: ("*Cooking: 375", "Out of combat, heal 5% of max health every second. Unlocked by Cooking 375.", 1467),
    910068: ("*Cooking: 450", "Out of combat, heal 6% of max health every second. Unlocked by Cooking 450.", 1467),
    910069: ("*Paladin: Holy", "Consecration follows you and toggles off if recast. Consecration damage +1000%. Holy Shock damage +300% and hits enemies within 10 yards of the target.", 51),
    910070: ("*Paladin: Protection", "Avenger's Shield bounces 30 times and can rehit. Range 60 yards. Devotion Aura: 10% damage reduction and +20% run/mount speed for allies. You deal Holy thorns equal to 50% of your armor.", 2172),
    910071: ("*Paladin: Retribution", "Divine Storm radius doubled and each press hits 4 times. Learn Crusader Strike. While Retribution Aura is up, Crusader Strike also casts Exorcism on nearby enemies.", 3027),
    910072: ("*Paladin: Devotion Speed", "Run and mount speed +20% while a Protection paladin's Devotion Aura is active.", 291),
    910073: ("*Travel: 1", "Hearthstone cast time and cooldown -20%. Unlocked by using your Hearthstone 1 time.", 348),
    910074: ("*Travel: 2", "Hearthstone cast time and cooldown -20%. Unlocked by using your Hearthstone 2 times.", 348),
    910075: ("*Travel: 3", "Hearthstone cast time and cooldown -20%. Unlocked by using your Hearthstone 3 times.", 348),
    910076: ("*Travel: 4", "Hearthstone cast time and cooldown -20%. Unlocked by using your Hearthstone 4 times.", 348),
    910077: ("*Travel: 5", "Hearthstone cast time and cooldown -20%. Unlocked by using your Hearthstone 5 times.", 348),
    910083: ("*Warrior: Arms", "Learn Bladestorm. No rage cost, no cooldown, and it does not end. Recast to stop. You can use other abilities while spinning.", 193),
    910084: ("*Warrior: Fury", "Titan's Grip. Each melee hit: +5% attack speed (20 stacks) and heal 1% of max health in combat. Attack speed lingers 60 sec after combat. Rend and Deep Wounds deal +300% damage.", 38),
    910085: ("*Warrior: Protection", "Learn Shockwave with no cooldown and +300% damage. Thunder Clap radius doubled. Thunder Clap applies your Rend and Deep Wounds if trained.", 1672),
    910086: ("*Warrior: Fury Haste", "Melee haste from the Fury class perk.", 38),
    910087: ("*Living Gear Speed", "Movement speed +40% for 30 seconds.", 516),
    910088: ("*Find Quests", "Adds up to 5 available quests in your current zone, lowest level first. Unlocked by completing 50 quests.", 141),
    910089: ("*Kill Combo", "Party kills stack this. Kill XP +5% and movement speed +3% per stack. Lasts 60 seconds after the last party kill. Stacks up to 50 times.", 95),
    910090: ("*Auto-Quest", "Summon the questgivers for completed quests in your log for 60 sec. Turn in and take follow-ups from them. Unlocked by completing 1 quest.", 141),
    910091: ("*Attuned Armory", "Make a wearable copy of an item you have attuned. The attunement stays on the account.", 249),
    910092: ("*Solo Queue", "Queue for dungeons and raids by yourself. No group required.", 169),
    910093: ("*Craft: 1", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 75 in a crafting profession.", 326),
    910094: ("*Craft: 2", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 150 in a crafting profession.", 326),
    910095: ("*Craft: 3", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 225 in a crafting profession.", 326),
    910096: ("*Craft: 4", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 300 in a crafting profession.", 326),
    910097: ("*Craft: 5", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 375 in a crafting profession.", 326),
}

# Usable abilities only. World-tab ticks stay in Spell.dbc for names but are not
# added to SkillLineAbility, so they do not appear as spellbook skills.
CASTABLE_SPELLS = {
    910001, 910002, 910003, 910004, 910005, 910006, 910007, 910008, 910009,
    910032, 910033, 910034, 910035, 910036, 910037,
    910069, 910070, 910071,
    910083, 910084, 910085,
    910042, 910088, 910090, 910091, 910092,
}

# Hidden HoT used by First Aid Instant. Not added to SkillLineAbility / spellbook.
HOT_SPELLS = {
    910052: ("First Aid", "Heals the target over time.", 104),
}

# Timed buffs shown on the aura bar. DurationIndex 21 = 30 seconds, 3 = 60 seconds.
# Fields: duration, stacks, then (effect, aura, die_sides, base_points, target_a) per effect.
# 6 = APPLY_AURA. Do not mark PASSIVE (0x40) or the client hides the icon.
# 129/130 = run/mount speed. Server recasts Kill Combo with BP = stacks*3.
VISIBLE_AURAS = {
    910087: (21, 1, ((6, 129, 1, 39, 1), (6, 130, 1, 39, 1))),
    910089: (3, 1, ((6, 129, 1, 2, 1), (6, 130, 1, 2, 1))),
}
BANDAGE_TEMPLATE_ID = 746
CHANNELED_ATTR = 0x4 | 0x40
# Rank 1-9 Blizzard. Instant lingering AoE like Death and Decay.
BLIZZARD_RANKS = {10, 6141, 8427, 10185, 10186, 10187, 27085, 42939, 42940}
BLADESTORM_ID = 46924
# SPELL_ATTR5_ALLOW_ACTION_DURING_CHANNEL
ATTR5_ACTION_DURING_CHANNEL = 0x1

# Smelting: instant profession opener, no category, no recovery.
TEMPLATE_SPELL_ID = 2656
GENERIC_SKILL_LINE = 183
SLA_TEMPLATE_SPELL = 2656
# Ability + usable while mounted. Do not copy racial category 1182.
ATTR_ABILITY_MOUNTED = 0x01000010
ATTR_NOT_IN_COMBAT = 0x10000000
INTERRUPT_ON_HIT = 0x08 | 0x10
# Pick Lock + Opening / Treasure / kneeling / tinkering / vehicle.
CHEST_OPEN_LOCKTYPES = {1, 5, 6, 10, 12, 13, 14, 17, 21}


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
        if rec[0] in CUSTOM_SPELLS or rec[0] in HOT_SPELLS:
            continue
        keep_indices.append(i)

    new_records_data = bytearray()
    combat_open = 0
    blizzard_n = 0
    bladestorm_n = 0
    for i in keep_indices:
        rec = read_rec(i)
        if rec[0] in BLIZZARD_RANKS:
            rec[5] &= ~CHANNELED_ATTR
            rec[28] = 1
            rec[29] = 0
            rec[30] = 0
            rec[33] = 0
            new_records_data.extend(struct.pack("<" + "I" * fields, *rec))
            blizzard_n += 1
        elif rec[0] == BLADESTORM_ID:
            rec[9] |= ATTR5_ACTION_DURING_CHANNEL
            rec[29] = 0
            rec[30] = 0
            rec[33] = 0
            rec[42] = 0
            rec[73] = 0
            rec[97] = 0
            rec[204] = 0
            rec[205] = 0
            rec[206] = 0
            new_records_data.extend(struct.pack("<" + "I" * fields, *rec))
            bladestorm_n += 1
        elif rec[71] == 33 and rec[110] in CHEST_OPEN_LOCKTYPES:
            rec[4] &= ~ATTR_NOT_IN_COMBAT
            rec[31] &= ~INTERRUPT_ON_HIT
            new_records_data.extend(struct.pack("<" + "I" * fields, *rec))
            combat_open += 1
        else:
            off = 20 + i * recsize
            new_records_data.extend(data[off : off + recsize])
    print(f"Chest-open spells usable in combat: {combat_open}")
    print(f"Blizzard ranks made instant: {blizzard_n}")
    print(f"Bladestorm action-during-channel, no rage: {bladestorm_n}")

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
        aura = VISIBLE_AURAS.get(spell_id)
        if aura:
            duration, stacks, effects = aura
            rec[31] = 0
            rec[32] = 0
            rec[33] = 0
            rec[40] = duration
            rec[49] = stacks
            rec[71] = 0
            rec[72] = 0
            rec[73] = 0
            rec[74] = 0
            rec[75] = 0
            rec[76] = 0
            rec[80] = 0
            rec[81] = 0
            rec[82] = 0
            rec[86] = 0
            rec[87] = 0
            rec[88] = 0
            rec[95] = 0
            rec[96] = 0
            rec[97] = 0
            for i, (effect, aura_name, die_sides, base_points, target_a) in enumerate(effects):
                rec[71 + i] = effect
                rec[74 + i] = die_sides
                rec[80 + i] = base_points
                rec[86 + i] = target_a
                rec[95 + i] = aura_name
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

    if BANDAGE_TEMPLATE_ID not in idx_by_id:
        raise SystemExit(f"Bandage template {BANDAGE_TEMPLATE_ID} missing from Spell.dbc")

    def make_hot(spell_id: int, name: str, desc: str, icon: int) -> bytes:
        rec = read_rec(idx_by_id[BANDAGE_TEMPLATE_ID])
        rec[0] = spell_id
        rec[3] = 0
        rec[5] &= ~CHANNELED_ATTR
        rec[31] = 0
        rec[32] = 0
        rec[33] = 0
        rec[133] = icon
        name_off = add_str(name)
        desc_off = add_str(desc)
        for loc in range(136, 152):
            rec[loc] = name_off
        rec[152] = 16712190
        for loc in range(170, 186):
            rec[loc] = desc_off
        rec[186] = 16712190
        return struct.pack("<" + "I" * fields, *rec)

    for spell_id, (name, desc, icon) in HOT_SPELLS.items():
        new_records_data.extend(make_hot(spell_id, name, desc, icon))
        print(f"Added HoT spell {spell_id}: {name!r} icon={icon}")

    new_count = len(keep_indices) + len(CUSTOM_SPELLS) + len(HOT_SPELLS)
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
            f"attr={rec[4]} dur={rec[40]} stacks={rec[49]} cat={rec[1]} cd={rec[29]}/{rec[30]}"
        )
        aura = VISIBLE_AURAS.get(spell_id)
        want_effect = 6 if aura else 3
        if name != expected[0] or rec[133] != expected[2] or rec[71] != want_effect:
            raise SystemExit(f"Verify mismatch for {spell_id}")
        if aura and (rec[40] != aura[0] or rec[49] != aura[1]):
            raise SystemExit(f"Spell {spell_id} aura duration/stacks mismatch")
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
    for spell_id in CASTABLE_SPELLS:
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

    new_count = len(keep) + len(CASTABLE_SPELLS)
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
    # Large hash table so lookups for files we do not ship miss instead of colliding.
    ok = storm.SFileCreateArchive(str(dest), flags, 4096, ctypes.byref(h))
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
            (DBC_DIR / "LFGDungeons.dbc", b"DBFilesClient\\LFGDungeons.dbc"),
            (DBC_DIR / "LFGDungeonGroup.dbc", b"DBFilesClient\\LFGDungeonGroup.dbc"),
        ],
    )
    locale_files = [
        (FRAME_TOC, b"Interface\\FrameXML\\FrameXML.toc"),
        (UI_LUA, b"Interface\\FrameXML\\LivingGear.lua"),
    ]
    for locale in LOCALES:
        _create_mpq(storm, ROOT / "dist" / f"patch-{locale}-4.MPQ", locale_files)


def main():
    patch_spell_dbc()
    verify_dbc()
    patch_skill_line_ability()
    patch_framexml()
    sys.path.insert(0, str(ROOT))
    from patch_lfg_raids import patch as patch_lfg
    patch_lfg()
    build_mpq()
    print("Done. Copy patch-Y.MPQ to Data/ and patch-<locale>-4.MPQ to Data/<locale>/.")


if __name__ == "__main__":
    main()
