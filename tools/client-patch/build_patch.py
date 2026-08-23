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
    910033: ("*Mage: Fire", "Fire spells apply Living Bomb and grant Combustion. Living Bomb spreads to enemies within 15 yards every 1 sec.", 11),
    910034: ("*Mage: Frost", "Blizzard is instant, no cooldown, and lingers like Death and Decay. In combat, Ice Lance hits enemies within 15 yards every 2 sec.", 188),
    910035: ("*Rogue: Assassination", "Poisons deal 300% increased damage. DoT poisons spread to enemies within 10 yards.", 500),
    910036: ("*Rogue: Combat", "Blade Flurry is always active. Energy regeneration increased by 50%. Combo builders have a 30% chance to cast free Killing Spree.", 514),
    910037: ("*Rogue: Subtlety", "Gain Shadowstep (6 sec cooldown) pickpockets every humanoid within 20 yards where you land. Hemorrhage spreads a boosted Ambush and Garrote bleed to everything within 10 yards. Learn Shadow Dance.", 250),
    # Jack in the Box dropped entirely 2026-08-21, replaced by Shadow Dance
    # (reuses its freed spell ID). Icon 95 (Kill Combo's) reused
    # deliberately -- confirmed rendering fine in this build already; MPQ
    # extraction tooling to look up a real shadow-dance icon was broken
    # when this was attempted for the old totem icon (StormLib opens the
    # archives fine, but SFileOpenFileEx returns ERROR_FILE_NOT_FOUND for
    # paths that should exist -- not investigated further). Revisit with a
    # real icon once that's fixed or verified another way.
    910102: ("*Shadow Dance", "Permanent. Stealth-only abilities (Ambush, Garrote, Cheap Shot, etc.) can be used without being stealthed. +10% attack power to your party/raid.", 95),
    910104: ("*Movement: Mounted Opener", "While mounted: jump forward for a boosted leap (+50% forward momentum). Jump again midair to slam down, pull enemies within 20 yards, and Thunder Clap. Unlocked at level 40.", 1299),
    910105: ("*Auto-Mount", "Automatically mount when you leave combat. Toggle on the World tab or by casting this perk. Unlocked by learning a mount.", 132),
    910106: ("*Class Buffs", "After you clear Naxxramas 25 on a class, that class applies 10% primary stats to you and nearby party.", 149),
    910107: ("*Riding", "Riding skill is account-wide. Alts can mount from level 1 once anyone trained riding.", 132),
    910108: ("*Auto-Accept", "Auto-accept quests when you talk to an NPC. Hold Shift to skip. Does not accept on login.", 141),
    910109: ("*Mine: 150", "Mining nodes yield 2x. Unlocked by Mining 150. Stacks: 2x / 4x / 8x at 150 / 300 / 450.", 361),
    910110: ("*Mine: 300", "Mining nodes yield 4x. Unlocked by Mining 300.", 361),
    910111: ("*Mine: 450", "Mining nodes yield 8x. Unlocked by Mining 450.", 361),
    910112: ("*Mine: Reach 75", "Auto-gather mining nodes from +3 yards. Unlocked by Mining 75. Stacks to +9 yards at 375.", 361),
    910113: ("*Mine: Reach 225", "Auto-gather mining nodes from +6 yards. Unlocked by Mining 225.", 361),
    910114: ("*Mine: Reach 375", "Auto-gather mining nodes from +9 yards. Unlocked by Mining 375.", 361),
    910115: ("*Herb: 150", "Herb nodes yield 2x. Unlocked by Herbalism 150. Stacks: 2x / 4x / 8x at 150 / 300 / 450.", 960),
    910116: ("*Herb: 300", "Herb nodes yield 4x. Unlocked by Herbalism 300.", 960),
    910117: ("*Herb: 450", "Herb nodes yield 8x. Unlocked by Herbalism 450.", 960),
    910118: ("*Herb: Reach 75", "Auto-gather herbs from +3 yards. Unlocked by Herbalism 75. Stacks to +9 yards at 375.", 960),
    910119: ("*Herb: Reach 225", "Auto-gather herbs from +6 yards. Unlocked by Herbalism 225.", 960),
    910120: ("*Herb: Reach 375", "Auto-gather herbs from +9 yards. Unlocked by Herbalism 375.", 960),
    910121: ("*Skin: 150", "Skinning yields 2x. Unlocked by Skinning 150. Stacks: 2x / 4x / 8x at 150 / 300 / 450.", 277),
    910122: ("*Skin: 300", "Skinning yields 4x. Unlocked by Skinning 300.", 277),
    910123: ("*Skin: 450", "Skinning yields 8x. Unlocked by Skinning 450.", 277),
    910124: ("*Skin: Reach 75", "Auto-skin from +3 yards. Unlocked by Skinning 75. Stacks to +9 yards at 375.", 277),
    910125: ("*Skin: Reach 225", "Auto-skin from +6 yards. Unlocked by Skinning 225.", 277),
    910126: ("*Skin: Reach 375", "Auto-skin from +9 yards. Unlocked by Skinning 375.", 277),
    910127: ("*Fish: 150", "Fishing yields 2x. Unlocked by Fishing 150. Stacks: 2x / 4x / 8x at 150 / 300 / 450.", 580),
    910128: ("*Fish: 300", "Fishing yields 4x. Unlocked by Fishing 300.", 580),
    910129: ("*Fish: 450", "Fishing yields 8x. Unlocked by Fishing 450.", 580),
    910130: ("*Fish: Reach 75", "Auto-loot fishing pools from +3 yards. Unlocked by Fishing 75. Stacks to +9 yards at 375.", 580),
    910131: ("*Fish: Reach 225", "Auto-loot fishing pools from +6 yards. Unlocked by Fishing 225.", 580),
    910132: ("*Fish: Reach 375", "Auto-loot fishing pools from +9 yards. Unlocked by Fishing 375.", 580),
    910133: ("*Eng: 150", "Engineering crafts and blasting/salvage loot yield 2x. Unlocked by Engineering 150.", 333),
    910134: ("*Eng: 300", "Engineering crafts and blasting/salvage loot yield 4x. Unlocked by Engineering 300.", 333),
    910135: ("*Eng: 450", "Engineering crafts and blasting/salvage loot yield 8x. Unlocked by Engineering 450.", 333),
    910136: ("*Eng: Reach 75", "Auto-gather engineering blasting nodes and salvage from +3 yards. Unlocked by Engineering 75.", 333),
    910137: ("*Eng: Reach 225", "Auto-gather engineering blasting nodes and salvage from +6 yards. Unlocked by Engineering 225.", 333),
    910138: ("*Eng: Reach 375", "Auto-gather engineering blasting nodes and salvage from +9 yards. Unlocked by Engineering 375.", 333),
    910139: ("*Gather Sparkle Herb", "Herb node sparkle visual (Beacon of Light).", 149),
    910140: ("*Gather Sparkle Mine", "Mining node sparkle visual (Beacon of Light heal).", 361),
    910141: ("*Gather Sparkle Fish", "Fishing pool sparkle visual.", 580),
    910142: ("*Rare Pulse", "Rare creature pulse visual (Beacon of Light).", 149),
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
    910063: ("*Cooking: 75", "Out of combat, heal 1% of max health and mana every second. Unlocked by Cooking 75.", 1467),
    910064: ("*Cooking: 150", "Out of combat, heal 2% of max health and mana every second. Unlocked by Cooking 150.", 1467),
    910065: ("*Cooking: 225", "Out of combat, heal 3% of max health and mana every second. Unlocked by Cooking 225.", 1467),
    910066: ("*Cooking: 300", "Out of combat, heal 4% of max health and mana every second. Unlocked by Cooking 300.", 1467),
    910067: ("*Cooking: 375", "Out of combat, heal 5% of max health and mana every second. Unlocked by Cooking 375.", 1467),
    910068: ("*Cooking: 450", "Out of combat, heal 6% of max health and mana every second. Unlocked by Cooking 450.", 1467),
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
    910088: ("*Quests - Find", "Adds up to 5 available quests in your current zone, lowest level first. Unlocked by completing 50 quests.", 141),
    # No leading asterisk (bug report #14): the "*" prefix marks perks the player
    # casts from the Account Perks panel. Kill Combo is a buff that happens TO
    # you, so it reads as a normal buff and is named like one.
    910089: ("Kill Combo", "Kills stack this, up to 10. Kill XP increased by 20% per stack. Movement speed increased by 5% per stack, on foot, mounted and flying. Refreshes to 10 minutes on every kill. Survives death and logging out.", 95),
    910090: ("*Quests - Finish", "Summon the questgivers for completed quests in your log for 60 sec. Turn in and take follow-ups from them. Unlocked by completing 1 quest.", 141),
    910091: ("*Attuned Armory", "Make a wearable copy of an item you have attuned. The attunement stays on the account.", 249),
    910092: ("*Solo Queue", "Queue for dungeons and raids by yourself. No group required.", 169),
    910093: ("*Craft: 1", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 75 in a crafting profession.", 326),
    910094: ("*Craft: 2", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 150 in a crafting profession.", 326),
    910095: ("*Craft: 3", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 225 in a crafting profession.", 326),
    910096: ("*Craft: 4", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 300 in a crafting profession.", 326),
    910097: ("*Craft: 5", "Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 375 in a crafting profession.", 326),
    910098: ("*Travel: Swim", "Swim speed +500%. Unlocked at level 10.", 348),
    910101: ("*Gear: Curator", "Passively levels your 5 lowest collection pieces. Unlocked at 1000 attuned items.", 249),
    # Icon 95 (Kill Combo's icon, confirmed rendering) reused as a safe
    # placeholder for all three -- same reasoning as Jack in the Box's icon
    # swap above: MPQ extraction tooling to look up real class-ability
    # icons is broken (see Bonesaw.md), so this avoids guessing a
    # SpellIconID blind. Revisit once that tooling works.
    910150: ("*Hunter: Marksmanship", "Chimera Shot has no cooldown and refreshes Serpent Sting to full duration. Ranged shots have a chance to grant a free, instant Aimed Shot.", 95),
    910151: ("*Shaman: Elemental", "Thunderstorm has no cooldown. Lava Burst deals double damage. Chain Lightning has no target cap.", 95),
    910152: ("*Death Knight: Unholy", "Summon Gargoyle has no cooldown. Army of the Dead has no cooldown and also summons a 5-ghoul group: 1 tank, 1 healer, 3 dps.", 95),
    910153: ("*Hunter: Beast Mastery", "Bestial Wrath has no cooldown/focus cost. Call up to 4 more beasts from your stable to fight alongside your pet, each at 50% stats.", 95),
    910154: ("*Hunter: Survival", "Explosive Shot deals double damage. Traps lose their cooldown and get a bigger blast radius. You are immune to your own trap damage.", 95),
    910155: ("*Shaman: Enhancement", "Feral Spirit is a free toggle: your 2 spirit wolves never expire while it's active and deal double damage. Stormstrike has no cooldown.", 95),
    910156: ("*Shaman: Restoration", "Riptide has no cooldown and also jumps to 2 more injured allies within 15 yards. Chain Heal has no bounce cap.", 95),
    910157: ("*Warlock: Affliction", "Your DoTs spread to enemies within 15 yards every 1 sec. DoT tick damage is increased by your haste.", 95),
    910158: ("*Warlock: Demonology", "Metamorphosis has no cooldown or shard cost. Your demon pet's damage is doubled.", 95),
    910159: ("*Warlock: Destruction", "Chaos Bolt has no cooldown. Conflagrate also casts a free, instant Chaos Bolt.", 95),
    910160: ("*Druid: Balance", "Starfall has no cooldown/mana cost. You are permanently in both Solar and Lunar Eclipse at once.", 95),
    910161: ("*Druid: Feral", "Berserk is a free toggle. While active, Cat/Bear abilities cost no energy/rage and lose their cooldowns.", 95),
    910162: ("*Druid: Restoration", "Wild Growth has no cooldown and heals up to 10 allies within 30 yards. Rejuvenation spreads to injured allies within 15 yards every 3 sec.", 95),
    910163: ("*Priest: Discipline", "Penance has no cooldown and also applies Power Word: Shield to the target.", 95),
    910164: ("*Priest: Holy", "Guardian Spirit has no cooldown and also applies to 2 more injured allies within 20 yards.", 95),
    910165: ("*Priest: Shadow", "Shadowfiend has no cooldown. Mind Flay deals quadruple damage.", 95),
    910166: ("*Death Knight: Blood", "Dancing Rune Weapon has no cooldown/runic cost. While active, melee hits heal you for 5% of the damage dealt.", 95),
    910167: ("*Death Knight: Frost", "Hungering Cold has no cooldown/runic cost. Frost Strike and Obliterate deal double damage.", 95),
    # 910168-910172 were added server-side after this table was last touched
    # and had no client entry at all, so they had no name, tooltip or icon in
    # game. Added 2026-08-22 alongside the two below.
    910168: ("*Pull Radius", "Toggle. Quadruples the distance at which enemies notice you.", 132),
    910170: ("*Track Ore", "Toggle. Shows nearby mineral veins on the minimap.", 134),
    910171: ("*Track Herbs", "Toggle. Shows nearby herbs on the minimap.", 133),
    910172: ("*CC Reduction", "Passive. Stuns, roots, fears, snares and other crowd control last 95% less on you.", 253),
    910173: ("*Shadow Dance", "Attack power increased by 10%. Granted by a Subtlety Rogue in your party.", 95),
    910174: ("*Well Fed", "Your cooking keeps you going. Restores 1% of your health and mana per second for each cooking tier you have unlocked, at 75, 150, 225, 300, 375 and 450 skill.", 134),
}

# Copy SpellVisualID[2] from vanilla spells onto hidden sparkle/pulse dummies.
# 53563 Beacon of Light, 53652 Beacon heal, 7731 Fishing rank 2.
VISUAL_COPY = {
    910139: 53563,
    910140: 53652,
    910141: 7731,
    910142: 53563,
}

# Usable abilities only. World-tab ticks stay in Spell.dbc for names but are not
# added to SkillLineAbility, so they do not appear as spellbook skills.
#
# Autoloot (910008), Solo Queue (910092) and Auto-Mount (910105) were removed
# on 2026-08-23. They are STATE, not actions: casting one only flipped an
# account boolean that the Account Perks window already toggles, through
# ALSET / SOLOSET / AMSET, all of which have server handlers. A spellbook
# button that duplicates a checkbox is a second source of truth for the same
# switch, and they were the three worst entries on the missing-button list
# (733 and 723 characters had no Auto-Mount or Solo Queue button) precisely
# because nobody needed to notice they were gone.
CASTABLE_SPELLS = {
    910001, 910002, 910003, 910004, 910005, 910006, 910007, 910009,
    910042, 910088, 910090, 910091,
}

# Hidden HoT used by First Aid Instant. Not added to SkillLineAbility / spellbook.
HOT_SPELLS = {
    910052: ("First Aid", "Heals the target over time.", 104),
}

# Timed buffs shown on the aura bar.
# Fields: duration, stacks, then (effect, aura, die_sides, base_points, target_a) per effect.
# 6 = APPLY_AURA. Do not mark PASSIVE (0x40) or the client hides the icon.
# 129/130 = run/mount speed.
#
# DurationIndex values, read out of var/mmap-output/dbc/SpellDuration.dbc on
# 2026-08-22 rather than assumed -- the previous comment here claimed 21 = 30
# seconds and 32 = 180 seconds, and both were wrong:
#   3  = 60000ms     6  = 600000ms (10 min)
#   21 = -1 (permanent, module-managed)
#   32 = 6000ms  <-- Kill Combo was pointed at this, so the client believed
#                    the buff lasted six seconds.
VISIBLE_AURAS = {
    910087: (21, 1, ((6, 129, 1, 39, 1), (6, 130, 1, 39, 1))),
    # 129 on foot, 130 ground mount, 209 flying (bug report #27 -- the buff
    # did nothing in the air, because 129/130 do not cover flight).
    910089: (6, 10, ((6, 129, 1, 0, 1), (6, 130, 1, 0, 1), (6, 209, 1, 0, 1))),
    910098: (0, 0, ((6, 58, 1, 499, 1),)),
    # +10% attack power, permanent while a Subtlety Rogue is in the party.
    910173: (21, 0, ((6, 166, 0, 10, 1),)),
    # MOD_REGEN / MOD_POWER_REGEN, base points set per cast by the server.
    910174: (21, 0, ((6, 84, 0, 0, 1), (6, 85, 0, 0, 1))),
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

# make_custom() forces every custom spell's *effect* target to self
# (ImplicitTargetA_1 = 1), but never touches field 16 (Targets, the spell's
# own cast-time targeting *requirement*) -- that's inherited as-is from
# TEMPLATE_SPELL_ID, whatever it happens to require. If the template
# requires an enemy/unit target, every custom spell that isn't a toggle
# cast via a UI click (which never needs to pass a real target) inherits
# that requirement too, and casting from the action bar with nothing
# selected fails client-side with "Invalid target". No entries currently
# need this (Shadow Dance, 910102, isn't castable at all -- passive perk
# flag only, not in CASTABLE_SPELLS).
TARGETS_OVERRIDE = {}
# All custom spells currently get RecoveryTime/CategoryRecoveryTime forced
# to 0 (see make_custom), i.e. no real cooldown beyond the client's default
# GCD. Per-spell overrides (milliseconds) for anything that actually needs
# a real recast timer.
RECOVERY_OVERRIDE_MS = {}
# Pick Lock + Opening / Treasure / kneeling / tinkering / vehicle.
CHEST_OPEN_LOCKTYPES = {1, 5, 6, 10, 12, 13, 14, 17, 21}
# SPELL_AURA_MOUNTED = 78. Instant cast and usable while moving.
SPELL_AURA_MOUNTED = 78
INTERRUPT_FLAG_MOVEMENT = 0x08


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
    mount_n = 0
    for i in keep_indices:
        rec = read_rec(i)
        is_mount = SPELL_AURA_MOUNTED in (rec[95], rec[96], rec[97])
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
        elif is_mount:
            rec[28] = 1
            rec[31] &= ~INTERRUPT_FLAG_MOVEMENT
            new_records_data.extend(struct.pack("<" + "I" * fields, *rec))
            mount_n += 1
        else:
            off = 20 + i * recsize
            new_records_data.extend(data[off : off + recsize])
    print(f"Chest-open spells usable in combat: {combat_open}")
    print(f"Blizzard ranks made instant: {blizzard_n}")
    print(f"Bladestorm action-during-channel, no rage: {bladestorm_n}")
    print(f"Mount spells instant while moving: {mount_n}")

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
        visual_src = VISUAL_COPY.get(spell_id)
        if visual_src and visual_src in idx_by_id:
            src = read_rec(idx_by_id[visual_src])
            rec[131] = src[131]
            rec[132] = src[132]
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
        if spell_id in TARGETS_OVERRIDE:
            rec[16] = TARGETS_OVERRIDE[spell_id]
        if spell_id in RECOVERY_OVERRIDE_MS:
            rec[29] = RECOVERY_OVERRIDE_MS[spell_id]
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
        want_recovery = RECOVERY_OVERRIDE_MS.get(spell_id, 0)
        if rec[1] or rec[29] != want_recovery or rec[30]:
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


LUA_UPVALUE_WARN_LIMIT = 55  # Lua 5.1's real hard ceiling is 60; warn with margin to spare.
LUA_LOCAL_WARN_LIMIT = 180  # Lua 5.1's real hard ceiling is 200 *main-chunk* locals; same margin idea.


def check_lua_limits():
    """
    Lua 5.1 (what the WotLK 3.3.5 client actually runs) hard-caps any single
    function at 60 upvalues (outer-scope variables it references, including
    ones only used inside its own nested closures), AND separately caps the
    main chunk -- the whole file, treated as one implicit function -- at 200
    local variables, where every top-level `local`/`local function` costs
    one slot for the rest of the file. Both are *compile-time* failures --
    the whole file fails to load, silently, with no in-game symptom beyond
    "nothing in the addon works" (see Bonesaw.md, "Lua 5.1's 60-upvalue-
    per-function limit killed the entire addon silently" and "Lua 5.1's
    200-local main-chunk limit", both 2026-08-20 -- the second one was hit
    *by the fix for the first one*: extracting more top-level functions to
    fix the upvalue ceiling pushed the main-chunk local count over its own,
    separate ceiling the same night).

    Static analysis, not a real Lua 5.1 compile (none was available in this
    environment -- `lupa` wraps a later Lua version with a higher ceiling,
    so it can't be used to validate this specific limit). Walks every
    function in the file (top-level and nested), tracking which names are
    local to it or an enclosing function vs. which resolve to a file-level
    `local` declared before it -- the latter are upvalues. Requires the
    `luaparser` PyPI package; skips with a warning (does not fail the
    build) if it isn't installed, since it's a dev-time safety net, not a
    hard runtime dependency of this pipeline.
    """
    try:
        from luaparser import ast as lua_ast
        from luaparser import astnodes as A
    except ImportError:
        print("WARNING: luaparser not installed (pip install luaparser) -- "
              "skipped the Lua 5.1 upvalue and main-chunk-local checks. This "
              "is how the addon silently failed to load for an entire "
              "session on 2026-08-20 (twice) -- install it so this build "
              "actually catches that class of bug instead of shipping it.",
              file=sys.stderr)
        return

    src = UI_LUA_SRC.read_text(encoding="utf-8")
    tree = lua_ast.parse(src)
    body = tree.body.body if hasattr(tree.body, "body") else tree.body

    def stmt_local_names(stmt):
        names = []
        if isinstance(stmt, A.LocalAssign):
            names.extend(t.id for t in stmt.targets if isinstance(t, A.Name))
        elif isinstance(stmt, A.LocalFunction) and isinstance(stmt.name, A.Name):
            names.append(stmt.name.id)
        return names

    def upvalue_count(func_node, outer_locals):
        used = set()

        def walk(node, local_stack):
            if node is None:
                return
            if isinstance(node, list):
                for n in node:
                    walk(n, local_stack)
                return
            if isinstance(node, (A.Function, A.AnonymousFunction, A.LocalFunction)):
                new_stack = local_stack + [set()]
                for a in getattr(node, "args", None) or []:
                    if isinstance(a, A.Name):
                        new_stack[-1].add(a.id)
                walk(getattr(node, "body", None), new_stack)
                return
            if isinstance(node, A.LocalAssign):
                for e in node.values or []:
                    walk(e, local_stack)
                for t in node.targets:
                    if isinstance(t, A.Name):
                        local_stack[-1].add(t.id)
                return
            if isinstance(node, A.Fornum):
                new_stack = local_stack + [set()]
                if isinstance(node.target, A.Name):
                    new_stack[-1].add(node.target.id)
                for e in (node.start, node.stop, node.step):
                    if e is not None:
                        walk(e, local_stack)
                walk(node.body, new_stack)
                return
            if isinstance(node, A.Forin):
                new_stack = local_stack + [set()]
                for t in node.targets:
                    if isinstance(t, A.Name):
                        new_stack[-1].add(t.id)
                for e in node.iter:
                    walk(e, local_stack)
                walk(node.body, new_stack)
                return
            if isinstance(node, A.Name):
                name = node.id
                if any(name in scope for scope in local_stack):
                    return
                if name in outer_locals:
                    used.add(name)
                return
            if not hasattr(node, "__dict__"):
                return
            for field in vars(node):
                if field.startswith("_"):
                    continue
                val = getattr(node, field)
                if isinstance(val, A.Node):
                    walk(val, local_stack)
                elif isinstance(val, list):
                    for item in val:
                        if isinstance(item, A.Node):
                            walk(item, local_stack)

        params = set()
        for a in getattr(func_node, "args", None) or []:
            if isinstance(a, A.Name):
                params.add(a.id)
        walk(getattr(func_node, "body", None), [params])
        return used

    # Walk the whole file collecting (name, line, upvalue_count) for every
    # function -- top-level LocalFunction/Function statements, and anything
    # nested inside them -- checked against the set of file-level locals
    # declared before that function's own definition point.
    toplevel_locals = set()
    problems = []

    def check_named(stmt, name):
        count = upvalue_count(stmt, toplevel_locals)
        if len(count) > LUA_UPVALUE_WARN_LIMIT:
            line = getattr(stmt, "line", "?")
            problems.append((name, line, len(count), sorted(count)))

    for stmt in body:
        if isinstance(stmt, A.LocalFunction) and isinstance(stmt.name, A.Name):
            check_named(stmt, stmt.name.id)
        toplevel_locals.update(stmt_local_names(stmt))

    if problems:
        lines = [
            f"Lua 5.1 upvalue check failed -- {len(problems)} function(s) at or near "
            f"the real 60 ceiling (warn threshold {LUA_UPVALUE_WARN_LIMIT}):"
        ]
        for name, line, count, names in problems:
            lines.append(f"  {name} (line {line}): {count} upvalues")
            lines.append(f"    {', '.join(names)}")
        lines.append(
            "Extract part of the function's body into its own local function "
            "(gets a fresh 60-upvalue budget) rather than adding more outer-"
            "scope references directly -- see BuildClassTabs/BuildLootPanel "
            "in LivingGear.lua for the pattern, and the matching Bonesaw.md "
            "entry for why this matters."
        )
        raise SystemExit("\n".join(lines))
    print("Lua upvalue check: OK")

    local_count = len(toplevel_locals)
    if local_count > LUA_LOCAL_WARN_LIMIT:
        raise SystemExit(
            f"Lua 5.1 main-chunk local check failed -- {local_count} top-level "
            f"local declarations (warn threshold {LUA_LOCAL_WARN_LIMIT}, real "
            f"ceiling 200). Don't add another top-level `local function Foo()` "
            f"-- attach it as `function LG2.Foo()` instead (see LG2 at the top "
            f"of LivingGear.lua and the matching Bonesaw.md entry). That keeps "
            f"its own independent 60-upvalue budget without costing a main-"
            f"chunk local slot. Reserve top-level `local function` for things "
            f"actually called from many places, where a name is clearer than "
            f"a table lookup."
        )
    print(f"Lua main-chunk local check: OK ({local_count}/200)")


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
    check_lua_limits()
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
