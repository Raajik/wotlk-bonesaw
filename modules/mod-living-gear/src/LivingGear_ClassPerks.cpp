/*
 * Living Gear class-spec combat perks. All 10 classes, all 3 specs each,
 * are implemented here except Paladin Holy/Retribution (LivingGear_Next.cpp)
 * and Rogue Assassination/Subtlety (LivingGear_Perks.cpp).
 *
 * Paladin Holy (910069) and Retribution (910071) already live in
 * LivingGear_Next.cpp; Rogue Assassination (910035) and Subtlety (910037)
 * already live in LivingGear_Perks.cpp. This file does not touch those.
 *
 * Fresh implementation informed by (not copied from) the pre-split
 * LivingGear.cpp.backup-20260818 "frankenfile" -- that file never compiled
 * cleanly and is not restored here. See A:\obsidian\jeremy\wiki\Bonesaw.md
 * "Crashes / never again" before changing anything in this file.
 *
 * Safety notes:
 *  - No SetStackAmount/ModStackAmount on any custom dummy aura anywhere in
 *    this file. Stacking state (Fury haste stacks) lives in a server-side
 *    map, same pattern as Kill Combo in LivingGear_Perks.cpp.
 *  - Bladestorm (Warrior Arms) has documented crash history in this
 *    codebase (effect-3/CheckCast-after-aura-removal). This file does NOT
 *    implement "never ends, recast to stop" -- only "learn it, no rage
 *    cost, no cooldown". See TryWarriorArmsOnCast for details.
 *  - No player->m_Events.AddEventAtOffset delayed-event scheduling. That
 *    pattern only exists in the abandoned backup, never in the live
 *    4-file codebase, so it is treated as unproven here. Multi-target
 *    effects (Avenger's Shield bounce, Thunder Clap cleave) run
 *    synchronously inside the triggering hook instead.
 */

#include "AllSpellScript.h"
#include "CellImpl.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "CreatureAI.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "DynamicObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Timer.h"
#include "Unit.h"
#include "Util.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// LivingGear_Perks.cpp -- owns Rogue Subtlety. Declared at global scope
// (not inside namespace LivingGearClassPerks below) so the unqualified
// call in SelectClassPerk() actually binds to the real global-scope
// definition instead of an unresolved namespace-local declaration.
void LivingGear_GrantSubtletyPerks(Player* player);

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

namespace LivingGearClassPerks
{
// -------------------------------------------------------------------------
// Class-perk pick spells (client name/icon/tooltip already shipped via
// tools/client-patch/build_patch.py's CUSTOM_SPELLS dict).
// -------------------------------------------------------------------------
uint32 const SPELL_MAGE_ARCANE = 910032;
uint32 const SPELL_MAGE_FIRE = 910033;
uint32 const SPELL_MAGE_FROST = 910034;
uint32 const SPELL_ROGUE_ASSASSINATION = 910035; // owned by LivingGear_Perks.cpp; only used here for selection bookkeeping
uint32 const SPELL_ROGUE_COMBAT = 910036;
uint32 const SPELL_ROGUE_SUBTLETY = 910037; // owned by LivingGear_Perks.cpp
uint32 const SPELL_PALADIN_HOLY = 910069; // owned by LivingGear_Next.cpp
uint32 const SPELL_PALADIN_PROTECTION = 910070;
uint32 const SPELL_PALADIN_RETRIBUTION = 910071; // owned by LivingGear_Next.cpp
uint32 const SPELL_WARRIOR_ARMS = 910083;
uint32 const SPELL_WARRIOR_FURY = 910084;
uint32 const SPELL_WARRIOR_PROTECTION = 910085;
uint32 const SPELL_HUNTER_MARKSMANSHIP = 910150;
uint32 const SPELL_SHAMAN_ELEMENTAL = 910151;
uint32 const SPELL_DK_UNHOLY = 910152;
uint32 const SPELL_HUNTER_BEAST_MASTERY = 910153;
uint32 const SPELL_HUNTER_SURVIVAL = 910154;
uint32 const SPELL_SHAMAN_ENHANCEMENT = 910155;
uint32 const SPELL_SHAMAN_RESTORATION = 910156;
uint32 const SPELL_WARLOCK_AFFLICTION = 910157;
uint32 const SPELL_WARLOCK_DEMONOLOGY = 910158;
uint32 const SPELL_WARLOCK_DESTRUCTION = 910159;
uint32 const SPELL_DRUID_BALANCE = 910160;
uint32 const SPELL_DRUID_FERAL = 910161;
uint32 const SPELL_DRUID_RESTORATION = 910162;
uint32 const SPELL_PRIEST_DISCIPLINE = 910163;
uint32 const SPELL_PRIEST_HOLY = 910164;
uint32 const SPELL_PRIEST_SHADOW = 910165;
uint32 const SPELL_DK_BLOOD = 910166;
uint32 const SPELL_DK_FROST = 910167;

uint32 const MAGE_CLASS_PERKS[] = { SPELL_MAGE_ARCANE, SPELL_MAGE_FIRE, SPELL_MAGE_FROST };
uint32 const ROGUE_CLASS_PERKS[] = { SPELL_ROGUE_ASSASSINATION, SPELL_ROGUE_COMBAT, SPELL_ROGUE_SUBTLETY };
uint32 const PALADIN_CLASS_PERKS[] = { SPELL_PALADIN_HOLY, SPELL_PALADIN_PROTECTION, SPELL_PALADIN_RETRIBUTION };
uint32 const WARRIOR_CLASS_PERKS[] = { SPELL_WARRIOR_ARMS, SPELL_WARRIOR_FURY, SPELL_WARRIOR_PROTECTION };
uint32 const HUNTER_CLASS_PERKS[] = { SPELL_HUNTER_MARKSMANSHIP, SPELL_HUNTER_BEAST_MASTERY, SPELL_HUNTER_SURVIVAL };
uint32 const SHAMAN_CLASS_PERKS[] = { SPELL_SHAMAN_ELEMENTAL, SPELL_SHAMAN_ENHANCEMENT, SPELL_SHAMAN_RESTORATION };
uint32 const DK_CLASS_PERKS[] = { SPELL_DK_UNHOLY, SPELL_DK_BLOOD, SPELL_DK_FROST };
uint32 const WARLOCK_CLASS_PERKS[] = { SPELL_WARLOCK_AFFLICTION, SPELL_WARLOCK_DEMONOLOGY, SPELL_WARLOCK_DESTRUCTION };
uint32 const DRUID_CLASS_PERKS[] = { SPELL_DRUID_BALANCE, SPELL_DRUID_FERAL, SPELL_DRUID_RESTORATION };
uint32 const PRIEST_CLASS_PERKS[] = { SPELL_PRIEST_DISCIPLINE, SPELL_PRIEST_HOLY, SPELL_PRIEST_SHADOW };

// -------------------------------------------------------------------------
// Real WotLK spells this module casts or reacts to.
// -------------------------------------------------------------------------
uint32 const SPELL_MIRROR_IMAGE = 55342;
uint32 const SPELL_LIVING_BOMB = 44457;
uint32 const SPELL_MAGE_COMBUSTION = 11129;
uint32 const SPELL_ICE_LANCE_R1 = 30455;
uint32 const SPELL_ICE_LANCE_R2 = 42913;
uint32 const SPELL_ICE_LANCE_R3 = 42914;
uint32 const SPELL_BLIZZARD_RANKS[] = { 10, 6141, 8427, 10185, 10186, 10187, 27085, 42939, 42940 };
uint32 const SPELL_BLADESTORM = 46924;
uint32 const SPELL_SHOCKWAVE = 46968;
uint32 const SPELL_THUNDER_CLAP = 6343;
uint32 const SPELL_REND = 772;
uint32 const SPELL_DEEP_WOUNDS_DOT = 12721;
uint32 const SPELL_DEEP_WOUNDS_TALENT[] = { 12834, 12849, 12867 };
uint32 const SPELL_AVENGERS_SHIELD = 31935;
uint32 const SPELL_DEVOTION_AURA = 465;
uint32 const SPELL_BLADE_FLURRY = 13877;
uint32 const SPELL_KILLING_SPREE = 51690;
uint32 const SPELL_CHIMERA_SHOT = 53209; // single rank
uint32 const SPELL_SERPENT_STING_R1 = 1978;
uint32 const SPELL_AIMED_SHOT_R1 = 19434;
uint32 const SPELL_THUNDERSTORM = 51490; // single rank
uint32 const SPELL_LAVA_BURST_R1 = 51505;
uint32 const SPELL_CHAIN_LIGHTNING_R1 = 421;
uint32 const SPELL_ARMY_OF_THE_DEAD = 42650;
uint32 const SPELL_SUMMON_GARGOYLE = 49206;
uint32 const NPC_GHOUL_TANK = 910202;
uint32 const NPC_GHOUL_HEALER = 910203;
uint32 const NPC_GHOUL_DPS = 910204;

// Hunter
uint32 const SPELL_BESTIAL_WRATH = 19574;
uint32 const SPELL_EXPLOSIVE_SHOT_R1 = 53301;
uint32 const SPELL_HUNTER_TRAP_FIRST_RANKS[] = { 13795 /*Immolation*/, 1499 /*Freezing*/, 13809 /*Frost*/, 13813 /*Explosive*/ };
uint32 const SPELL_SNAKE_TRAP = 34600; // no rank chain
// Shaman
uint32 const SPELL_FERAL_SPIRIT = 51533;
uint32 const SPELL_STORMSTRIKE = 17364;
uint32 const NPC_SPIRIT_WOLF = 29264;
uint32 const SPELL_RIPTIDE = 61295;
uint32 const SPELL_CHAIN_HEAL_R1 = 1064;
// Warlock
uint32 const SPELL_METAMORPHOSIS = 47241;
uint32 const SPELL_CHAOS_BOLT_R1 = 50796;
uint32 const SPELL_CONFLAGRATE_R1 = 17962;
// Druid
uint32 const SPELL_STARFALL = 48505;
uint32 const SPELL_ECLIPSE_SOLAR = 48517;
uint32 const SPELL_ECLIPSE_LUNAR = 48518;
uint32 const SPELL_BERSERK_DRUID = 50334;
uint32 const SPELL_WILD_GROWTH_R1 = 48438;
uint32 const SPELL_REJUVENATION_R1 = 774;
// Priest
uint32 const SPELL_PENANCE_R1 = 47540;
uint32 const SPELL_POWER_WORD_SHIELD_R1 = 17;
uint32 const SPELL_GUARDIAN_SPIRIT = 47788;
uint32 const SPELL_SHADOWFIEND = 34433;
uint32 const SPELL_MIND_FLAY_R1 = 15407;
// Death Knight
uint32 const SPELL_DANCING_RUNE_WEAPON = 49028;
uint32 const SPELL_HUNGERING_COLD = 49203;
uint32 const SPELL_FROST_STRIKE_R1 = 49143;
uint32 const SPELL_OBLITERATE_R1 = 49020;

float const CLASS_PERK_RANGE = 15.0f;
uint32 const MAGE_ARCANE_LINGER_MS = 60000;
uint32 const MAGE_FIRE_TICK_MS = 1000;
uint32 const MAGE_FROST_ICE_TICK_MS = 2000;
uint32 const FURY_HASTE_CAP = 20;
uint32 const FURY_HASTE_PCT_PER_STACK = 5;
uint32 const FURY_HASTE_LINGER_MS = 60000;
uint32 const ROGUE_ENERGY_TICK_MS = 2000;
int32 const ROGUE_ENERGY_TICK_BONUS = 10; // +50% of the ~20-per-2s baseline energy tick
uint32 const ROGUE_KS_CHANCE = 30;
uint32 const PALADIN_AS_BOUNCES = 30;
float const PALADIN_AS_HOP_RANGE = 10.0f;
float const PALADIN_DEVO_ALLY_RANGE = 40.0f;
float const PALADIN_DEVO_DR = 0.90f; // 10% incoming damage reduction
float const PALADIN_THORNS_PCT = 0.5f;
int32 const RADIUS_MOD_DOUBLE = 20000; // SetSpellValue(SPELLVALUE_RADIUS_MOD, x) -> RadiusMod = x/10000
uint32 const HUNTER_MM_AIMED_PROC_CHANCE = 20;
int32 const CHAIN_LIGHTNING_MAX_TARGETS = 99; // SetSpellValue(SPELLVALUE_MAX_TARGETS, x) removes the normal 3-target cap
uint32 const ARMY_GROUP_DESPAWN_MS = 60000;
uint32 const ARMY_HEALER_TICK_MS = 2000;
uint32 const ARMY_HEALER_HEAL_PCT = 15;
uint32 const ARMY_HEALER_HEAL_THRESHOLD_PCT = 90;
uint32 const BM_PACK_SIZE = 4;
uint32 const BM_PACK_DESPAWN_MS = 20000;
uint32 const HUNTER_TRAP_RADIUS_MOD = 20000; // SetSpellValue(SPELLVALUE_RADIUS_MOD, x) -> RadiusMod = x/10000
uint32 const WARLOCK_AFFLICTION_SPREAD_TICK_MS = 1000;
float const WARLOCK_AFFLICTION_SPREAD_RANGE = 15.0f;
uint32 const DRUID_ECLIPSE_TICK_MS = 3000;
int32 const DRUID_ECLIPSE_REFRESH_DURATION = 15000;
uint32 const DRUID_REJUV_SPREAD_TICK_MS = 3000;
float const DRUID_REJUV_SPREAD_RANGE = 15.0f;
int32 const WILD_GROWTH_MAX_TARGETS = 10;
float const ALLY_SPREAD_INJURED_PCT = 95.0f;
float const PRIEST_GS_SPREAD_RANGE = 20.0f;
float const SHAMAN_RIPTIDE_SPREAD_RANGE = 15.0f;
uint32 const SHAMAN_RIPTIDE_SPREAD_COUNT = 2;
uint32 const PRIEST_GS_SPREAD_COUNT = 2;

// -------------------------------------------------------------------------
// State
// -------------------------------------------------------------------------
struct MageState
{
    uint32 fireTickAcc = 0;
    uint32 frostTickAcc = 0;
};

struct WarriorFuryState
{
    uint32 stacks = 0;
    uint32 appliedPct = 0;
    uint32 lastHitMs = 0;
};

struct RogueState
{
    uint32 energyTickAcc = 0;
};

std::unordered_map<uint32, MageState> g_mage;
std::unordered_map<uint32, WarriorFuryState> g_fury;
std::unordered_map<uint32, RogueState> g_rogue;
std::unordered_map<uint32, uint32> g_classPerk;      // char guid -> selected class-perk spell id
std::unordered_set<uint32> g_classPerkLoaded;         // char guids already loaded from DB
std::unordered_set<uint32> g_reentryGuard;            // char guid -> currently applying a perk-triggered cast

// Unholy Death Knight "one-man raid group" (Army of the Dead redesign).
// char guid -> the 5 ghoul GUIDs currently alive for that owner, so the
// healer ghoul's AI can find its own group without a global creature scan.
std::unordered_map<uint32, std::vector<ObjectGuid>> g_armyGroup;

// Beast Mastery Hunter "call the pack" -- char guid -> the extra beast GUIDs
// cloned from the hunter's active pet. Same one-slot-per-owner dedupe shape
// as g_armyGroup.
std::unordered_map<uint32, std::vector<ObjectGuid>> g_bmPack;

// Shaman Elemental tick accumulator reused for a couple of other per-tick
// perks below (Warlock Affliction spread, Druid Eclipse refresh, Druid
// Rejuvenation spread) -- one small struct per class instead of yet more
// single-purpose maps.
struct TickState
{
    uint32 acc = 0;
};
std::unordered_map<uint32, TickState> g_afflictionTick;
std::unordered_map<uint32, TickState> g_eclipseTick;
std::unordered_map<uint32, TickState> g_rejuvTick;

std::unordered_set<uint32> g_perkLoaded;              // account ids already loaded
std::unordered_map<uint32, std::unordered_set<uint32>> g_perks; // account id -> unlocked spell ids

bool g_schemaReady = false;
bool g_hasClassPerkTable = false;

void DetectSchema()
{
    if (g_schemaReady)
        return;
    g_schemaReady = true;
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_char_class_perk'"))
        g_hasClassPerkTable = (*tables)[0].Get<uint64>() > 0;
}

// -------------------------------------------------------------------------
// Local copies of the three shared helpers (exact behavior matched to
// LivingGear_Perks.cpp lines ~150-210, per this module's self-contained
// per-file convention -- see LivingGear_Next.cpp/_Gather.cpp for the same
// pattern).
// -------------------------------------------------------------------------
void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
}

void LoadPerks(uint32 accountId)
{
    if (!g_perkLoaded.insert(accountId).second)
        return;
    auto& set = g_perks[accountId];
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `spell_id` FROM `lg_account_perk` WHERE `account_id` = {}", accountId))
    {
        do
            set.insert((*result)[0].Get<uint32>());
        while (result->NextRow());
    }
}

bool HasPerk(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession())
        return false;
    if (player->HasSpell(spellId))
        return true;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    return g_perks[acc].count(spellId) > 0;
}

// 2026-08-21: this file's only caller (GrantAndBroadcastClassPerks, below)
// unlocks the three class-spec pick badges -- pure account-perk flags,
// never meant to be castable/spellbook-visible (unlike LivingGear_Perks.cpp's
// UnlockPerk, which also grants some genuinely castable perks and keeps
// learnSpell). Calling player->learnSpell() here marked them "known" with
// no SkillLineAbility entry to categorize them, so the client dumped them
// into the General spellbook tab regardless of CASTABLE_SPELLS excluding
// them -- exactly why "*Rogue: Subtlety" etc. kept showing up there even
// after that client-side fix. HasPerk()'s g_perks[acc]/DB fallback still
// works fine without ever calling learnSpell.
void UnlockPerk(Player* player, uint32 spellId, char const* msg)
{
    if (!player || !player->GetSession() || !sSpellMgr->GetSpellInfo(spellId))
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    if (!g_perks[acc].insert(spellId).second)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
        acc, spellId);
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        ChatHandler(player->GetSession()).SendSysMessage(msg);
}

// -------------------------------------------------------------------------
// Small local helpers shared by several perks below.
// -------------------------------------------------------------------------
uint32 BestOwned(Player* player, uint32 firstId)
{
    if (!player)
        return 0;
    uint32 best = 0;
    for (SpellInfo const* info = sSpellMgr->GetSpellInfo(firstId); info; info = info->GetNextRankSpell())
        if (player->HasSpell(info->Id))
            best = info->Id;
    return best;
}

bool RankOf(SpellInfo const* info, uint32 firstId)
{
    if (!info)
        return false;
    uint32 const first = sSpellMgr->GetFirstSpellInChain(firstId);
    return first && sSpellMgr->GetFirstSpellInChain(info->Id) == first;
}

bool HasAuraRankOf(Unit* unit, uint32 firstId)
{
    if (!unit)
        return false;
    for (SpellInfo const* info = sSpellMgr->GetSpellInfo(firstId); info; info = info->GetNextRankSpell())
        if (unit->HasAura(info->Id))
            return true;
    return false;
}

template <typename Fn>
void ForEachHostileNear(Player* player, WorldObject* center, float range, Fn&& fn)
{
    if (!player || !center)
        return;
    std::list<Unit*> targets;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(center, player, range);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, targets, check);
    Cell::VisitObjects(center, searcher, range);
    for (Unit* target : targets)
        if (target && target->IsAlive())
            fn(target);
}

template <typename Fn>
void ForEachHostileInRange(Player* player, float range, Fn&& fn)
{
    ForEachHostileNear(player, player, range, fn);
}

// Friendly, alive, within range, and below ALLY_SPREAD_INJURED_PCT health --
// shared by every "spreads to nearby injured allies" perk below (Riptide,
// Guardian Spirit, Rejuvenation).
template <typename Fn>
void ForEachInjuredAllyNear(Player* player, WorldObject* center, float range, Fn&& fn)
{
    if (!player || !center)
        return;
    std::list<Unit*> targets;
    Acore::AnyFriendlyNotSelfUnitInObjectRangeCheck check(center, player, range);
    Acore::UnitListSearcher<Acore::AnyFriendlyNotSelfUnitInObjectRangeCheck> searcher(player, targets, check);
    Cell::VisitObjects(center, searcher, range);
    for (Unit* target : targets)
        if (target && target->IsAlive() && target->GetHealthPct() < ALLY_SPREAD_INJURED_PCT)
            fn(target);
}

// -------------------------------------------------------------------------
// Class-perk selection bookkeeping (the "pick one spec" system that was
// missing entirely -- see LivingGear.cpp.backup-20260818 lines ~1171-1334
// for the pre-split intent this reimplements). Selection is per-character
// (a class-perk pick can differ per alt), stored in lg_char_class_perk.
//
// Note: this table only governs the four specs implemented in this file
// (Mage x3, Warrior x3, Paladin Protection, Rogue Combat). Paladin
// Holy/Retribution and Rogue Assassination/Subtlety are gated purely by
// HasPerk() in their own files and are intentionally left untouched, so a
// character could in principle have e.g. Rogue Assassination active
// (via HasPerk there) at the same time as Rogue Combat selected here.
// That's a known limitation of not editing the other files; see report.
// -------------------------------------------------------------------------
bool IsMageClassPerk(uint32 spellId)
{
    for (uint32 id : MAGE_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsRogueClassPerk(uint32 spellId)
{
    for (uint32 id : ROGUE_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsPaladinClassPerk(uint32 spellId)
{
    for (uint32 id : PALADIN_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsWarriorClassPerk(uint32 spellId)
{
    for (uint32 id : WARRIOR_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsHunterClassPerk(uint32 spellId)
{
    for (uint32 id : HUNTER_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsShamanClassPerk(uint32 spellId)
{
    for (uint32 id : SHAMAN_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsDkClassPerk(uint32 spellId)
{
    for (uint32 id : DK_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsWarlockClassPerk(uint32 spellId)
{
    for (uint32 id : WARLOCK_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsDruidClassPerk(uint32 spellId)
{
    for (uint32 id : DRUID_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

bool IsPriestClassPerk(uint32 spellId)
{
    for (uint32 id : PRIEST_CLASS_PERKS)
        if (id == spellId)
            return true;
    return false;
}

void LoadClassPerk(uint32 guid)
{
    if (!g_classPerkLoaded.insert(guid).second)
        return;
    DetectSchema();
    if (!g_hasClassPerkTable)
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `spell_id` FROM `lg_char_class_perk` WHERE `guid` = {}", guid))
        g_classPerk[guid] = (*result)[0].Get<uint32>();
}

uint32 GetClassPerk(Player* player)
{
    if (!player)
        return 0;
    uint32 const guid = player->GetGUID().GetCounter();
    LoadClassPerk(guid);
    auto it = g_classPerk.find(guid);
    return it != g_classPerk.end() ? it->second : 0;
}

bool CanSelectClassPerk(Player* player, uint32 spellId)
{
    if (!player || !HasPerk(player, spellId))
        return false;
    switch (player->getClass())
    {
        case CLASS_MAGE:
            return IsMageClassPerk(spellId);
        case CLASS_ROGUE:
            return IsRogueClassPerk(spellId);
        case CLASS_PALADIN:
            return IsPaladinClassPerk(spellId);
        case CLASS_WARRIOR:
            return IsWarriorClassPerk(spellId);
        case CLASS_HUNTER:
            return IsHunterClassPerk(spellId);
        case CLASS_SHAMAN:
            return IsShamanClassPerk(spellId);
        case CLASS_DEATH_KNIGHT:
            return IsDkClassPerk(spellId);
        case CLASS_WARLOCK:
            return IsWarlockClassPerk(spellId);
        case CLASS_DRUID:
            return IsDruidClassPerk(spellId);
        case CLASS_PRIEST:
            return IsPriestClassPerk(spellId);
        default:
            return false;
    }
}

// Grant the 3 spec spells for the player's class (so there is something to
// pick from) and tell the client which ones exist + which is selected.
// Nothing anywhere -- not even the pre-existing Paladin/Rogue Subtlety
// specs owned by other files -- ever did this, which is why the Class tab
// always showed "No class perk for your class yet." regardless of spec.
uint32 const* ClassPerkListFor(uint8 cls, uint32& count)
{
    switch (cls)
    {
        case CLASS_MAGE:    count = 3; return MAGE_CLASS_PERKS;
        case CLASS_ROGUE:   count = 3; return ROGUE_CLASS_PERKS;
        case CLASS_PALADIN: count = 3; return PALADIN_CLASS_PERKS;
        case CLASS_WARRIOR: count = 3; return WARRIOR_CLASS_PERKS;
        case CLASS_HUNTER:  count = 3; return HUNTER_CLASS_PERKS;
        case CLASS_SHAMAN:  count = 3; return SHAMAN_CLASS_PERKS;
        case CLASS_DEATH_KNIGHT: count = 3; return DK_CLASS_PERKS;
        case CLASS_WARLOCK: count = 3; return WARLOCK_CLASS_PERKS;
        case CLASS_DRUID:   count = 3; return DRUID_CLASS_PERKS;
        case CLASS_PRIEST:  count = 3; return PRIEST_CLASS_PERKS;
        default:            count = 0; return nullptr;
    }
}

void GrantAndBroadcastClassPerks(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 count = 0;
    uint32 const* list = ClassPerkListFor(player->getClass(), count);
    if (!list)
        return;
    uint32 const selected = GetClassPerk(player);
    std::string pairs;
    for (uint32 i = 0; i < count; ++i)
    {
        UnlockPerk(player, list[i], nullptr);
        if (!pairs.empty())
            pairs += ',';
        pairs += Acore::StringFormat("{}:{}", list[i], list[i] == selected ? 1 : 0);
    }
    SendLine(player, "CPKALL|" + pairs);
}

void ApplyRogueCombatBladeFlurry(Player* player); // defined below, near TickRogueCombat
void GrantMageFrostBlizzard(Player* player); // defined below, near IsBlizzardRank

void SelectClassPerk(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession())
        return;
    if (!CanSelectClassPerk(player, spellId))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const prev = GetClassPerk(player);
    if (prev == spellId)
        return;
    g_classPerk[guid] = spellId;
    DetectSchema();
    if (g_hasClassPerkTable)
        CharacterDatabase.DirectExecute(
            "REPLACE INTO `lg_char_class_perk` (`guid`, `spell_id`) VALUES ({}, {})",
            guid, spellId);
    if (prev == SPELL_ROGUE_COMBAT && spellId != SPELL_ROGUE_COMBAT)
        player->RemoveAurasDueToSpell(SPELL_BLADE_FLURRY);
    if (spellId == SPELL_ROGUE_COMBAT)
        ApplyRogueCombatBladeFlurry(player);
    if (spellId == SPELL_MAGE_FROST)
        GrantMageFrostBlizzard(player);
    if (spellId == SPELL_ROGUE_SUBTLETY)
        LivingGear_GrantSubtletyPerks(player);
    GrantAndBroadcastClassPerks(player); // sends CPKALL, which the client actually handles
    if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff66ccff[Account Perks]|r {} is now your active class perk.", info->SpellName[0]);
}

// -------------------------------------------------------------------------
// Mage: Arcane (910032)
// "While in combat, Mirror Images appear and chain-cast. They linger 60
// sec after combat."
//
// Chain-casting/attacking is the engine's own Mirror Image AI (already
// implemented server-side for the stock spell); this only keeps the
// images up during combat and lets the aura run out naturally 60s after
// combat ends.
// -------------------------------------------------------------------------
void TickMageArcane(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_ARCANE || !player->IsAlive() || !player->IsInCombat())
        return;
    if (!player->HasAura(SPELL_MIRROR_IMAGE))
    {
        player->CastSpell(player, SPELL_MIRROR_IMAGE, true);
        return;
    }
    if (Aura* aura = player->GetAura(SPELL_MIRROR_IMAGE))
        if (aura->GetDuration() >= 0 && aura->GetDuration() < 10000)
            aura->SetDuration(30000);
}

void OnLeaveCombatMage(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_ARCANE)
        return;
    if (Aura* aura = player->GetAura(SPELL_MIRROR_IMAGE))
        if (aura->GetDuration() < int32(MAGE_ARCANE_LINGER_MS))
            aura->SetDuration(int32(MAGE_ARCANE_LINGER_MS));
}

// -------------------------------------------------------------------------
// Mage: Fire (910033)
// "Fire spells apply Living Bomb. That effect spreads to enemies within
// 15 yards every 1 sec."
// -------------------------------------------------------------------------
bool IsHarmfulMageFireSpell(SpellInfo const* info)
{
    if (!info || info->SpellFamilyName != SPELLFAMILY_MAGE)
        return false;
    if (!(info->GetSchoolMask() & SPELL_SCHOOL_MASK_FIRE))
        return false;
    switch (info->Id)
    {
        case SPELL_LIVING_BOMB:
        case 44461: // Living Bomb explosion
        case 55359: // Living Bomb explosion (rank)
        case 55360:
        case 55361:
        case 55362:
        case SPELL_MAGE_COMBUSTION:
        case 28682: // Combustion crit-stack aura
            return false;
        default:
            break;
    }
    return true;
}

void ApplyMageCombustion(Player* player)
{
    if (!player->HasAura(SPELL_MAGE_COMBUSTION))
        player->CastSpell(player, SPELL_MAGE_COMBUSTION, true);
}

void TryMageFireOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_MAGE_FIRE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!IsHarmfulMageFireSpell(info))
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || target == player || !player->IsValidAttackTarget(target))
        return;
    if (!target->HasAura(SPELL_LIVING_BOMB, player->GetGUID()))
        player->CastSpell(target, SPELL_LIVING_BOMB, true);
    ApplyMageCombustion(player);
}

void TickMageFire(Player* player, MageState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FIRE || !player->IsAlive() || !player->IsInCombat())
        return;
    st.fireTickAcc += diff;
    if (st.fireTickAcc < MAGE_FIRE_TICK_MS)
        return;
    st.fireTickAcc = 0;
    bool anyBombed = false;
    ForEachHostileInRange(player, CLASS_PERK_RANGE, [player, &anyBombed](Unit* target)
    {
        if (target->HasAura(SPELL_LIVING_BOMB, player->GetGUID()))
            anyBombed = true;
    });
    if (!anyBombed)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ForEachHostileInRange(player, CLASS_PERK_RANGE, [player](Unit* target)
    {
        if (!target->HasAura(SPELL_LIVING_BOMB, player->GetGUID()))
            player->CastSpell(target, SPELL_LIVING_BOMB, true);
    });
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Mage: Frost (910034)
// "Blizzard is instant, no cooldown, and lingers like Death and Decay. In
// combat, Ice Lance hits enemies within 15 yards every 2 sec."
//
// Instant cast + no cooldown for every Blizzard rank is already applied
// globally by build_patch.py's Spell.dbc patch (BLIZZARD_RANKS), so this
// only implements the "lingers" and "Ice Lance cleave" parts of the perk.
// -------------------------------------------------------------------------
bool IsBlizzardRank(uint32 spellId)
{
    for (uint32 id : SPELL_BLIZZARD_RANKS)
        if (id == spellId)
            return true;
    return false;
}

// Blizzard is a real base-game spell mages only learn from a trainer at
// specific levels -- picking the Frost class perk edits its behavior
// (see PatchBlizzardServerSide) but never actually gave anyone the spell,
// so characters who hadn't separately trained it had no way to use or
// even test the perk. Grant the highest rank they qualify for. Standard
// WotLK level breakpoints, matching SPELL_BLIZZARD_RANKS' rank 1-9 order.
uint32 BlizzardRankForLevel(uint8 level)
{
    if (level >= 77) return SPELL_BLIZZARD_RANKS[8];
    if (level >= 68) return SPELL_BLIZZARD_RANKS[7];
    if (level >= 58) return SPELL_BLIZZARD_RANKS[6];
    if (level >= 50) return SPELL_BLIZZARD_RANKS[5];
    if (level >= 44) return SPELL_BLIZZARD_RANKS[4];
    if (level >= 38) return SPELL_BLIZZARD_RANKS[3];
    if (level >= 32) return SPELL_BLIZZARD_RANKS[2];
    if (level >= 26) return SPELL_BLIZZARD_RANKS[1];
    return SPELL_BLIZZARD_RANKS[0];
}

void GrantMageFrostBlizzard(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FROST)
        return;
    uint32 const rank = BlizzardRankForLevel(player->GetLevel());
    if (!player->HasSpell(rank))
        player->learnSpell(rank);
}

void TryMageFrostBlizzardLinger(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_MAGE_FROST)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !IsBlizzardRank(info->Id))
        return;
    if (DynamicObject* dyn = player->GetDynObject(info->Id))
        if (dyn->GetDuration() < 8000)
            dyn->SetDuration(8000);
}

void TickMageFrostBlizzard(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FROST || !player->IsAlive() || !player->IsInCombat())
        return;
    for (uint32 id : SPELL_BLIZZARD_RANKS)
        if (DynamicObject* dyn = player->GetDynObject(id))
            if (dyn->GetDuration() < 4000)
                dyn->SetDuration(8000);
}

uint32 IceLanceForLevel(uint8 level)
{
    if (level >= 78)
        return SPELL_ICE_LANCE_R3;
    if (level >= 72)
        return SPELL_ICE_LANCE_R2;
    return SPELL_ICE_LANCE_R1;
}

void TickMageFrostIceLance(Player* player, MageState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_MAGE_FROST || !player->IsAlive() || !player->IsInCombat())
        return;
    st.frostTickAcc += diff;
    if (st.frostTickAcc < MAGE_FROST_ICE_TICK_MS)
        return;
    st.frostTickAcc = 0;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    uint32 const ice = IceLanceForLevel(player->GetLevel());
    ForEachHostileInRange(player, CLASS_PERK_RANGE, [player, ice](Unit* target)
    {
        player->CastSpell(target, ice, true);
    });
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Rogue: Combat (910036)
// "Blade Flurry is always active. Energy regeneration increased by 50%.
// Combo builders have a 30% chance to cast free Killing Spree."
//
// Blade Flurry is a permanent buff applied once when Combat is selected
// (and re-applied on login if missing), not re-checked every server tick.
// The original tick-based "recast if !HasAura" approach cast Blade Flurry
// on every single OnPlayerUpdate call whenever the aura wasn't present --
// which included while dead (an aura can't apply to a dead unit), so a
// player who died spent every tick forever re-attempting the cast.
// -------------------------------------------------------------------------
void ApplyRogueCombatBladeFlurry(Player* player)
{
    if (!player || !player->IsAlive() || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    if (!player->HasAura(SPELL_BLADE_FLURRY))
        if (Aura* aura = player->AddAura(SPELL_BLADE_FLURRY, player))
        {
            aura->SetDuration(-1);
            aura->SetMaxDuration(-1);
        }
}

void TickRogueCombat(Player* player, RogueState& st, uint32 diff)
{
    if (!player || !player->IsAlive() || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    st.energyTickAcc += diff;
    if (st.energyTickAcc < ROGUE_ENERGY_TICK_MS)
        return;
    st.energyTickAcc = 0;
    if (player->GetPower(POWER_ENERGY) < player->GetMaxPower(POWER_ENERGY))
        player->ModifyPower(POWER_ENERGY, ROGUE_ENERGY_TICK_BONUS);
}

bool IsComboPointBuilder(SpellInfo const* info)
{
    if (!info || info->SpellFamilyName != SPELLFAMILY_ROGUE)
        return false;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (info->Effects[i].Effect == SPELL_EFFECT_ADD_COMBO_POINTS)
            return true;
    return false;
}

void TryRogueCombatKillingSpree(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_ROGUE_COMBAT)
        return;
    if (player->HasAura(SPELL_KILLING_SPREE))
        return;
    if (!IsComboPointBuilder(spell->GetSpellInfo()))
        return;
    if (!roll_chance_i(int32(ROGUE_KS_CHANCE)))
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    player->RemoveSpellCooldown(SPELL_KILLING_SPREE, true);
    Unit* dest = spell->m_targets.GetUnitTarget();
    if (!dest || !player->IsValidAttackTarget(dest))
        dest = player->GetVictim();
    if (dest && player->IsValidAttackTarget(dest))
        player->CastSpell(dest, SPELL_KILLING_SPREE, true);
    else
        player->CastSpell(player, SPELL_KILLING_SPREE, true);
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Warrior: Arms (910083)
// "Learn Bladestorm. No rage cost, no cooldown, and it does not end.
// Recast to stop. You can use other abilities while spinning."
//
// SAFETY SIMPLIFICATION: the "does not end / recast to stop" toggle is
// deliberately NOT implemented here. Bonesaw.md documents a real crash
// history for exactly that (effect-3/CheckCast-after-aura-removal on this
// same spell). What ships instead: learning Bladestorm, refunding its
// rage cost, and clearing its cooldown so it can be recast immediately --
// same player-facing value (spam it near-freely) without touching aura
// removal/CheckCast timing at all. "Use other abilities while spinning"
// is already handled by build_patch.py zeroing Bladestorm's
// ALLOW_ONLY_ABILITY effect (see Bonesaw.md), not something this file
// needs to add.
// -------------------------------------------------------------------------
void TryWarriorArmsOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARRIOR_ARMS)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_BLADESTORM)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_RAGE, cost);
    player->RemoveSpellCooldown(SPELL_BLADESTORM, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

// -------------------------------------------------------------------------
// Warrior: Fury (910084)
// "Titan's Grip. Each melee hit: +5% attack speed (20 stacks) and heal 1%
// of max health in combat. Attack speed lingers 60 sec after combat.
// Rend and Deep Wounds deal +300% damage."
// -------------------------------------------------------------------------
void SetFuryHastePct(Player* player, WarriorFuryState& st, uint32 pct)
{
    if (!player || pct == st.appliedPct)
        return;
    if (st.appliedPct)
    {
        player->ApplyAttackTimePercentMod(BASE_ATTACK, -float(st.appliedPct), false);
        player->ApplyAttackTimePercentMod(OFF_ATTACK, -float(st.appliedPct), false);
    }
    if (pct)
    {
        player->ApplyAttackTimePercentMod(BASE_ATTACK, -float(pct), true);
        player->ApplyAttackTimePercentMod(OFF_ATTACK, -float(pct), true);
    }
    st.appliedPct = pct;
}

void NoteFuryMeleeHit(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_FURY)
        return;
    if (!player->CanTitanGrip())
        player->SetCanTitanGrip(true);
    WarriorFuryState& st = g_fury[player->GetGUID().GetCounter()];
    if (st.stacks < FURY_HASTE_CAP)
        ++st.stacks;
    st.lastHitMs = getMSTime();
    SetFuryHastePct(player, st, st.stacks * FURY_HASTE_PCT_PER_STACK);
    if (player->IsInCombat() && player->GetHealth() < player->GetMaxHealth())
    {
        uint32 const heal = player->CountPctFromMaxHealth(1);
        if (heal)
            player->ModifyHealth(int32(heal));
    }
}

void TickWarriorFury(Player* player)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    auto it = g_fury.find(guid);
    if (it == g_fury.end() || !it->second.stacks)
        return;
    if (GetClassPerk(player) != SPELL_WARRIOR_FURY)
    {
        SetFuryHastePct(player, it->second, 0);
        it->second.stacks = 0;
        return;
    }
    if (player->IsInCombat())
        return;
    if (getMSTimeDiff(it->second.lastHitMs, getMSTime()) >= FURY_HASTE_LINGER_MS)
    {
        SetFuryHastePct(player, it->second, 0);
        it->second.stacks = 0;
    }
}

void ApplyFuryBleedDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_FURY)
        return;
    if (RankOf(info, SPELL_REND) || info->Id == SPELL_DEEP_WOUNDS_DOT)
        damage *= 4;
}

// -------------------------------------------------------------------------
// Warrior: Protection (910085)
// "Learn Shockwave with no cooldown and +300% damage. Thunder Clap radius
// doubled. Thunder Clap applies your Rend and Deep Wounds if trained."
// -------------------------------------------------------------------------
void ThunderClapApplyBleeds(Player* player)
{
    if (!player)
        return;
    uint32 const rend = BestOwned(player, SPELL_REND);
    bool deep = false;
    for (uint32 id : SPELL_DEEP_WOUNDS_TALENT)
        if (player->HasSpell(id))
        {
            deep = true;
            break;
        }
    if (!rend && !deep)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ForEachHostileInRange(player, CLASS_PERK_RANGE, [player, rend, deep](Unit* target)
    {
        if (rend)
            player->CastSpell(target, rend, true);
        if (deep)
        {
            int32 const bp = int32((player->GetFloatValue(UNIT_FIELD_MAXDAMAGE)
                + player->GetFloatValue(UNIT_FIELD_MINDAMAGE)) / 2.0f);
            player->CastCustomSpell(target, SPELL_DEEP_WOUNDS_DOT, &bp, nullptr, nullptr, true);
        }
    });
    g_reentryGuard.erase(guid);
}

void TryWarriorProtOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_SHOCKWAVE)
    {
        player->RemoveSpellCooldown(SPELL_SHOCKWAVE, true);
        if (uint32 const cat = info->GetCategory())
            player->RemoveCategoryCooldown(cat);
        return;
    }
    if (RankOf(info, SPELL_THUNDER_CLAP))
        ThunderClapApplyBleeds(player);
}

void ApplyProtThunderClapRadius(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    if (RankOf(info, SPELL_THUNDER_CLAP))
        spell->SetSpellValue(SPELLVALUE_RADIUS_MOD, RADIUS_MOD_DOUBLE);
}

void ApplyProtShockwaveDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || info->Id != SPELL_SHOCKWAVE)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_WARRIOR_PROTECTION)
        return;
    damage *= 4;
}

// -------------------------------------------------------------------------
// Hunter: Marksmanship (910150)
// "Chimera Shot has no cooldown and refreshes Serpent Sting to full
// duration. Ranged shots have a chance to grant a free, instant Aimed
// Shot."
//
// SIMPLIFICATION: "ranged shots have a chance" procs off any non-triggered
// Hunter ranged-damage spell cast (SpellFamilyName Hunter + DmgClass
// Ranged), not specifically off a crit -- there is no crit-outcome hook
// exposed to script code anywhere in this codebase (checked: UnitScript
// only exposes post-hoc damage-modify hooks, not the crit roll itself), so
// "on crit" would mean adding new engine plumbing for one perk. Same
// risk/reward tradeoff already made for Warrior Fury/Protection's flat
// damage multipliers elsewhere in this file.
// -------------------------------------------------------------------------
void TryHunterMMOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_HUNTER_MARKSMANSHIP)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_CHIMERA_SHOT)
    {
        player->RemoveSpellCooldown(SPELL_CHIMERA_SHOT, true);
        if (uint32 const cat = info->GetCategory())
            player->RemoveCategoryCooldown(cat);
        if (Unit* target = spell->m_targets.GetUnitTarget())
        {
            uint32 const sting = BestOwned(player, SPELL_SERPENT_STING_R1);
            if (sting && player->IsValidAttackTarget(target))
                player->CastSpell(target, sting, true);
        }
        return;
    }
    if (info->SpellFamilyName != SPELLFAMILY_HUNTER || info->DmgClass != SPELL_DAMAGE_CLASS_RANGED)
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !player->IsValidAttackTarget(target))
        return;
    if (!roll_chance_i(int32(HUNTER_MM_AIMED_PROC_CHANCE)))
        return;
    uint32 const aimed = BestOwned(player, SPELL_AIMED_SHOT_R1);
    if (aimed)
        player->CastSpell(target, aimed, true);
}

// -------------------------------------------------------------------------
// Shaman: Elemental (910151)
// "Thunderstorm has no cooldown. Lava Burst deals double damage. Chain
// Lightning has no target cap."
// -------------------------------------------------------------------------
void TryShamanElementalOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_THUNDERSTORM)
        return;
    player->RemoveSpellCooldown(SPELL_THUNDERSTORM, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void ApplyShamanElementalLavaBurstDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || !RankOf(info, SPELL_LAVA_BURST_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return;
    damage *= 2;
}

void ApplyShamanElementalChainLightningTargets(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_SHAMAN_ELEMENTAL)
        return;
    if (RankOf(info, SPELL_CHAIN_LIGHTNING_R1))
        spell->SetSpellValue(SPELLVALUE_MAX_TARGETS, CHAIN_LIGHTNING_MAX_TARGETS);
}

// -------------------------------------------------------------------------
// Death Knight: Unholy (910152)
// "Summon Gargoyle has no cooldown. Army of the Dead has no cooldown and
// summons a 5-ghoul group instead of a uniform swarm: 1 tank (holds
// threat), 1 healer (heals whoever in the group is lowest), 3 dps."
//
// SIMPLIFICATION: "Summon Gargoyle lasts until it dies instead of on a
// timer" (as originally discussed) was dropped -- doing that safely means
// reaching into TempSummon's internal despawn timer, which nothing else in
// this codebase does and isn't proven safe here. Removing the cooldown
// gets the same practical result (near-permanent uptime via re-summoning)
// without touching that code at all.
//
// The vanilla ghoul swarm from Army of the Dead is left completely alone
// (its own engine-side aura/summon logic in spell_dk.cpp is not touched --
// safer than trying to suppress it). This perk summons its own separate
// 5-ghoul group alongside it, so the net effect is "the usual swarm, plus
// an actual mini raid group."
// -------------------------------------------------------------------------
// Both roles reuse the Army of the Dead ghoul's own model (26079, see
// rev_living_gear_army_of_the_dead.sql) for this first pass -- distinct
// per-role models (bulkier tank, ghostly healer) are a follow-up, same as
// how the Shadow Clone's look-alike model started Human-only and grew from
// there. All 5 are distinguishable by nameplate/behavior even while they
// share one look.
static TempSummon* SpawnArmyGhoul(Player* player, uint32 entry, Position const& pos, uint32 healthPct)
{
    TempSummon* s = player->SummonCreature(entry, pos, TEMPSUMMON_TIMED_DESPAWN, ARMY_GROUP_DESPAWN_MS);
    if (!s)
        return nullptr;
    s->SetOwnerGUID(player->GetGUID());
    s->SetFaction(player->GetFaction());
    s->SetLevel(player->GetLevel());
    uint32 const hp = std::max<uint32>(1, player->GetMaxHealth() * healthPct / 100);
    s->SetMaxHealth(hp);
    s->SetHealth(hp);
    return s;
}

void SummonArmyGroup(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    // Despawn any previous group still standing (e.g. Army of the Dead
    // recast before the old group's timer ran out) rather than letting
    // groups pile up -- same one-slot-per-owner rule as the Shadow Clone.
    auto old = g_armyGroup.find(guid);
    if (old != g_armyGroup.end())
    {
        for (ObjectGuid const& g : old->second)
            if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                c->DespawnOrUnsummon();
        old->second.clear();
    }
    std::vector<ObjectGuid>& group = g_armyGroup[guid];
    Position center = player->GetPosition();
    if (TempSummon* tank = SpawnArmyGhoul(player, NPC_GHOUL_TANK, center, 300))
        group.push_back(tank->GetGUID());
    if (TempSummon* healer = SpawnArmyGhoul(player, NPC_GHOUL_HEALER, center, 100))
        group.push_back(healer->GetGUID());
    for (uint32 i = 0; i < 3; ++i)
    {
        Position p = center;
        p.RelocateOffset({ frand(-2.5f, 2.5f), frand(-2.5f, 2.5f), 0.0f, 0.0f });
        if (TempSummon* dps = SpawnArmyGhoul(player, NPC_GHOUL_DPS, p, 100))
            group.push_back(dps->GetGUID());
    }
}

void TryDkUnholyOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DK_UNHOLY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_SUMMON_GARGOYLE)
    {
        player->RemoveSpellCooldown(SPELL_SUMMON_GARGOYLE, true);
        if (uint32 const cat = info->GetCategory())
            player->RemoveCategoryCooldown(cat);
        return;
    }
    if (info->Id != SPELL_ARMY_OF_THE_DEAD)
        return;
    player->RemoveSpellCooldown(SPELL_ARMY_OF_THE_DEAD, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
    // Deferred: OnPlayerSpellCast fires mid-way through the triggering
    // spell's own Spell::cast() (see the reentrancy notes elsewhere in this
    // codebase / Bonesaw.md "Reentrant hook execution"), and summoning 5
    // creatures is exactly the kind of heavier reentrant call that pattern
    // warns about. Re-resolves the player by GUID a tick later instead of
    // summoning straight from inside this callback.
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
            if (p->IsInWorld() && GetClassPerk(p) == SPELL_DK_UNHOLY)
                SummonArmyGroup(p);
    }, std::chrono::milliseconds(1));
}

// -------------------------------------------------------------------------
// Army ghoul AI: shared struct, branches on me->GetEntry() for role. Not a
// real playerbot -- see the 2026-08-20 wiki note on why that's infeasible
// for a temporary summon (mod-playerbots hard-requires a real
// Player+WorldSession). Same "read the owner, follow/engage, despawn if
// abandoned" shape as npc_lg_shadow_cloneAI in LivingGear_Perks.cpp.
// -------------------------------------------------------------------------
struct npc_lg_army_ghoulAI : public ScriptedAI
{
    npc_lg_army_ghoulAI(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        me->SetReactState(REACT_DEFENSIVE);
        _healTickMs = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        Unit* ownerUnit = me->GetOwner();
        Player* owner = ownerUnit ? ownerUnit->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld() || !me->IsWithinDistInMap(owner, 60.0f))
        {
            me->DespawnOrUnsummon();
            return;
        }

        if (me->GetEntry() == NPC_GHOUL_HEALER)
        {
            UpdateHealer(owner, diff);
            return;
        }

        // Tank/dps: pet-aggressive self-defense first, same fix as the
        // Shadow Clone for "took ~10 seconds to react".
        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                if (me->GetEntry() == NPC_GHOUL_TANK)
                    HoldThreat(attacker);
                DoMeleeAttackIfReady();
                return;
            }
        }

        Unit* ownerTarget = owner->GetSelectedUnit();
        bool const ownerFighting = owner->IsInCombat() && ownerTarget && ownerTarget->IsAlive()
            && owner->IsValidAttackTarget(ownerTarget);

        if (!ownerFighting)
        {
            if (me->GetVictim())
                me->AttackStop();
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                me->GetMotionMaster()->MoveFollow(owner, 3.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }

        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);
        if (me->GetEntry() == NPC_GHOUL_TANK)
            HoldThreat(ownerTarget);
        DoMeleeAttackIfReady();
    }

private:
    uint32 _healTickMs = 0;

    // Not a real Taunt cast (bosses can be taunt-immune, and a resisted
    // Taunt would leave the tank ghoul doing nothing useful). Just makes
    // sure it always outranks the other 4 summons on this target's threat
    // table, which is all "hold aggro for the group" actually needs here.
    void HoldThreat(Unit* target)
    {
        if (target && target->IsAlive())
            target->GetThreatMgr().AddThreat(me, 500.0f, nullptr, true, true);
    }

    void UpdateHealer(Player* owner, uint32 diff)
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            me->GetMotionMaster()->MoveFollow(owner, 8.0f, frand(0.0f, 2.0f * float(M_PI)));

        if (_healTickMs > diff)
        {
            _healTickMs -= diff;
            return;
        }
        _healTickMs = ARMY_HEALER_TICK_MS;

        Unit* lowest = nullptr;
        float lowestPct = 100.0f;
        auto consider = [&lowest, &lowestPct](Unit* u)
        {
            if (!u || !u->IsAlive())
                return;
            float const pct = u->GetHealthPct();
            if (pct < lowestPct)
            {
                lowestPct = pct;
                lowest = u;
            }
        };
        consider(owner);
        auto it = g_armyGroup.find(owner->GetGUID().GetCounter());
        if (it != g_armyGroup.end())
            for (ObjectGuid const& g : it->second)
                if (Creature* c = ObjectAccessor::GetCreature(*owner, g))
                    consider(c);

        if (!lowest || lowestPct >= float(ARMY_HEALER_HEAL_THRESHOLD_PCT))
            return;
        uint32 const heal = std::max<uint32>(1, lowest->GetMaxHealth() * ARMY_HEALER_HEAL_PCT / 100);
        lowest->ModifyHealth(int32(heal));
    }
};

class npc_lg_army_ghoul_tank : public CreatureScript
{
public:
    npc_lg_army_ghoul_tank() : CreatureScript("npc_lg_army_ghoul_tank") { }
    CreatureAI* GetAI(Creature* creature) const override { return new npc_lg_army_ghoulAI(creature); }
};

class npc_lg_army_ghoul_healer : public CreatureScript
{
public:
    npc_lg_army_ghoul_healer() : CreatureScript("npc_lg_army_ghoul_healer") { }
    CreatureAI* GetAI(Creature* creature) const override { return new npc_lg_army_ghoulAI(creature); }
};

class npc_lg_army_ghoul_dps : public CreatureScript
{
public:
    npc_lg_army_ghoul_dps() : CreatureScript("npc_lg_army_ghoul_dps") { }
    CreatureAI* GetAI(Creature* creature) const override { return new npc_lg_army_ghoulAI(creature); }
};

// -------------------------------------------------------------------------
// Shared "temp pet" AI: follow owner, attack owner's target, pet-aggressive
// self-defense, despawn if abandoned. Used for the Beast Mastery pack below
// -- unlike the Army ghouls this is attached directly via AIM_Initialize()
// on an already-existing creature entry (the hunter's own pet's entry), so
// it needs no creature_template/ScriptName row of its own.
// -------------------------------------------------------------------------
struct npc_lg_temp_petAI : public ScriptedAI
{
    npc_lg_temp_petAI(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        me->SetReactState(REACT_DEFENSIVE);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        Unit* ownerUnit = me->GetOwner();
        Player* owner = ownerUnit ? ownerUnit->ToPlayer() : nullptr;
        if (!owner || !owner->IsInWorld() || !me->IsWithinDistInMap(owner, 60.0f))
        {
            me->DespawnOrUnsummon();
            return;
        }
        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                DoMeleeAttackIfReady();
                return;
            }
        }
        Unit* ownerTarget = owner->GetSelectedUnit();
        bool const ownerFighting = owner->IsInCombat() && ownerTarget && ownerTarget->IsAlive()
            && owner->IsValidAttackTarget(ownerTarget);
        if (!ownerFighting)
        {
            if (me->GetVictim())
                me->AttackStop();
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                me->GetMotionMaster()->MoveFollow(owner, 4.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }
        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);
        DoMeleeAttackIfReady();
    }
};

// -------------------------------------------------------------------------
// Hunter: Beast Mastery (910153)
// "The Beast Within (Bestial Wrath) has no cooldown/focus cost. Call up to
// 4 more beasts from your stable to fight alongside your pet, each at 50%
// of its stats."
//
// SIMPLIFICATION: "from your stable" is literal in the design discussion
// but reading the account's actual stabled pets (character_pet table,
// empty-stable edge case, cross-account rules) is a lot of extra surface
// for a first pass. This clones the hunter's currently active pet instead
// (same entry/model, just more of them) -- reads the same in practice
// ("more beasts show up") without touching pet-stable data at all.
// -------------------------------------------------------------------------
void SummonBmPack(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    Pet* pet = player->GetPet();
    if (!pet)
        return;
    uint32 const entry = pet->GetEntry();
    uint32 const guid = player->GetGUID().GetCounter();
    auto old = g_bmPack.find(guid);
    if (old != g_bmPack.end())
    {
        for (ObjectGuid const& g : old->second)
            if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                c->DespawnOrUnsummon();
        old->second.clear();
    }
    std::vector<ObjectGuid>& pack = g_bmPack[guid];
    uint32 const hp = std::max<uint32>(1, pet->GetMaxHealth() / 2);
    for (uint32 i = 0; i < BM_PACK_SIZE; ++i)
    {
        Position p = player->GetPosition();
        p.RelocateOffset({ frand(-3.0f, 3.0f), frand(-3.0f, 3.0f), 0.0f, 0.0f });
        TempSummon* s = player->SummonCreature(entry, p, TEMPSUMMON_TIMED_DESPAWN, BM_PACK_DESPAWN_MS);
        if (!s)
            continue;
        s->SetOwnerGUID(player->GetGUID());
        s->SetFaction(player->GetFaction());
        s->SetLevel(player->GetLevel());
        s->SetMaxHealth(hp);
        s->SetHealth(hp);
        s->AIM_Initialize(new npc_lg_temp_petAI(s));
        pack.push_back(s->GetGUID());
    }
}

void TryHunterBmOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_HUNTER_BEAST_MASTERY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_BESTIAL_WRATH)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_FOCUS, cost);
    player->RemoveSpellCooldown(SPELL_BESTIAL_WRATH, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
    // Deferred for the same reason as the Army of the Dead group summon --
    // see TryDkUnholyOnCast below.
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        if (Player* p = ObjectAccessor::FindPlayer(playerGuid))
            if (p->IsInWorld() && GetClassPerk(p) == SPELL_HUNTER_BEAST_MASTERY)
                SummonBmPack(p);
    }, std::chrono::milliseconds(1));
}

// -------------------------------------------------------------------------
// Hunter: Survival (910154)
// "Explosive Shot deals double damage. Traps lose their cooldown and get a
// bigger blast radius. You are immune to your own trap damage."
// -------------------------------------------------------------------------
void ApplyHunterSurvivalExplosiveShotDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || !RankOf(info, SPELL_EXPLOSIVE_SHOT_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL)
        return;
    damage *= 2;
}

bool IsHunterTrapSpell(SpellInfo const* info)
{
    if (!info)
        return false;
    if (info->Id == SPELL_SNAKE_TRAP)
        return true;
    for (uint32 id : SPELL_HUNTER_TRAP_FIRST_RANKS)
        if (RankOf(info, id))
            return true;
    return false;
}

void TryHunterSurvivalOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!IsHunterTrapSpell(info))
        return;
    player->RemoveSpellCooldown(info->Id, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void ApplyHunterSurvivalTrapRadius(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || GetClassPerk(player) != SPELL_HUNTER_SURVIVAL || !IsHunterTrapSpell(info))
        return;
    spell->SetSpellValue(SPELLVALUE_RADIUS_MOD, int32(HUNTER_TRAP_RADIUS_MOD));
}

// Own-trap self-damage immunity: traps attribute their explosion damage to
// the hunter who set them, so a self-hit shows up here as attacker==victim.
// Traps deal their damage as spell damage, never melee, so this only needs
// the int32 spell-damage-taken shape.
void ApplyHunterSurvivalTrapImmunity(Unit* attacker, Unit* victim, int32& damage, SpellInfo const* info)
{
    if (!attacker || attacker != victim || damage <= 0 || !IsHunterTrapSpell(info))
        return;
    Player* player = attacker->ToPlayer();
    if (player && GetClassPerk(player) == SPELL_HUNTER_SURVIVAL)
        damage = 0;
}

// -------------------------------------------------------------------------
// Shaman: Enhancement (910155)
// "Feral Spirit is a free toggle: your 2 spirit wolves never expire while
// it's active and deal double damage. Stormstrike has no cooldown."
// -------------------------------------------------------------------------
void TryShamanEnhOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_SHAMAN_ENHANCEMENT)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (info->Id == SPELL_FERAL_SPIRIT)
    {
        if (int32 const cost = spell->GetPowerCost())
            player->ModifyPower(POWER_MANA, cost);
        player->RemoveSpellCooldown(SPELL_FERAL_SPIRIT, true);
        if (uint32 const cat = info->GetCategory())
            player->RemoveCategoryCooldown(cat);
        return;
    }
    if (info->Id == SPELL_STORMSTRIKE)
    {
        player->RemoveSpellCooldown(SPELL_STORMSTRIKE, true);
        if (uint32 const cat = info->GetCategory())
            player->RemoveCategoryCooldown(cat);
    }
}

void TickShamanEnhWolves(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SHAMAN_ENHANCEMENT)
        return;
    if (!player->HasAura(SPELL_FERAL_SPIRIT))
        return;
    if (Aura* aura = player->GetAura(SPELL_FERAL_SPIRIT))
        if (aura->GetDuration() >= 0 && aura->GetDuration() < 10000)
            aura->SetDuration(45000);
}

bool IsOwnedSpiritWolf(Unit* unit, uint32& ownerGuidOut)
{
    if (!unit || unit->GetEntry() != NPC_SPIRIT_WOLF)
        return false;
    Unit* owner = unit->GetOwner();
    if (!owner)
        return false;
    ownerGuidOut = owner->GetGUID().GetCounter();
    return true;
}

void ApplyShamanEnhWolfMeleeDamage(Unit* attacker, uint32& damage)
{
    uint32 ownerGuid = 0;
    if (!IsOwnedSpiritWolf(attacker, ownerGuid))
        return;
    Player* owner = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(ownerGuid));
    if (owner && GetClassPerk(owner) == SPELL_SHAMAN_ENHANCEMENT)
        damage *= 2;
}

// -------------------------------------------------------------------------
// Shaman: Restoration (910156)
// "Riptide has no cooldown and its HoT also jumps to 2 more injured allies
// within 15 yards. Chain Heal has no bounce cap."
// -------------------------------------------------------------------------
void TryShamanRestOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_SHAMAN_RESTORATION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_RIPTIDE)
        return;
    player->RemoveSpellCooldown(SPELL_RIPTIDE, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target)
        return;
    uint32 spread = 0;
    ForEachInjuredAllyNear(player, target, SHAMAN_RIPTIDE_SPREAD_RANGE, [&](Unit* ally)
    {
        if (spread >= SHAMAN_RIPTIDE_SPREAD_COUNT || ally == target || ally->HasAura(SPELL_RIPTIDE, player->GetGUID()))
            return;
        player->CastSpell(ally, SPELL_RIPTIDE, true);
        ++spread;
    });
}

void ApplyShamanRestChainHealTargets(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_SHAMAN_RESTORATION)
        return;
    if (RankOf(info, SPELL_CHAIN_HEAL_R1))
        spell->SetSpellValue(SPELLVALUE_MAX_TARGETS, CHAIN_LIGHTNING_MAX_TARGETS);
}

// -------------------------------------------------------------------------
// Warlock: Affliction (910157)
// "Your DoTs spread to enemies within 15 yards every 1 sec. DoT tick
// damage is increased by your haste (duration is not shortened)."
// -------------------------------------------------------------------------
void TickWarlockAffliction(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_WARLOCK_AFFLICTION || !player->IsAlive() || !player->IsInCombat())
        return;
    st.acc += diff;
    if (st.acc < WARLOCK_AFFLICTION_SPREAD_TICK_MS)
        return;
    st.acc = 0;
    Unit* source = player->GetVictim();
    if (!source)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    std::vector<uint32> dots;
    Unit::AuraApplicationMap const& auras = source->GetAppliedAuras();
    for (Unit::AuraApplicationMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
    {
        AuraApplication* aa = itr->second;
        Aura* aura = aa ? aa->GetBase() : nullptr;
        if (!aura || aura->GetCasterGUID() != player->GetGUID())
            continue;
        SpellInfo const* dotInfo = aura->GetSpellInfo();
        if (dotInfo && dotInfo->SpellFamilyName == SPELLFAMILY_WARLOCK && dotInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE))
            dots.push_back(dotInfo->Id);
    }
    if (!dots.empty())
        ForEachHostileNear(player, source, WARLOCK_AFFLICTION_SPREAD_RANGE, [player, source, &dots](Unit* target)
        {
            if (target == source)
                return;
            for (uint32 id : dots)
                if (!target->HasAura(id, player->GetGUID()))
                    player->CastSpell(target, id, true);
        });
    g_reentryGuard.erase(guid);
}

void ApplyWarlockAfflictionHaste(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* info)
{
    if (!target || !attacker || !damage || !info || info->SpellFamilyName != SPELLFAMILY_WARLOCK)
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_WARLOCK_AFFLICTION)
        return;
    float const haste = player->GetRatingBonusValue(CR_HASTE_SPELL);
    if (haste > 0.0f)
        damage = uint32(float(damage) * (1.0f + haste / 100.0f));
}

// -------------------------------------------------------------------------
// Warlock: Demonology (910158)
// "Metamorphosis has no cooldown or shard cost. Your demon pet's damage is
// doubled."
//
// SIMPLIFICATION: "auto-resummons if it dies" was dropped -- there's no
// cheap way to know which summon spell produced the current pet (Imp vs
// Voidwalker vs Succubus vs Felhunter vs Felguard) without tracking every
// summon cast, and getting that wrong would resummon the wrong demon. Not
// worth it for one perk's flavor clause.
// -------------------------------------------------------------------------
void TryWarlockDemoOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARLOCK_DEMONOLOGY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_METAMORPHOSIS)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_MANA, cost);
    player->RemoveSpellCooldown(SPELL_METAMORPHOSIS, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void ApplyWarlockDemoPetDamage(Unit* attacker, uint32& damage)
{
    if (!attacker || !damage)
        return;
    Unit* owner = attacker->GetOwner();
    Player* player = owner ? owner->ToPlayer() : nullptr;
    if (!player || player->GetPet() != attacker || GetClassPerk(player) != SPELL_WARLOCK_DEMONOLOGY)
        return;
    damage *= 2;
}

void ApplyWarlockDemoPetSpellDamage(Unit* attacker, int32& damage)
{
    if (!attacker || damage <= 0)
        return;
    Unit* owner = attacker->GetOwner();
    Player* player = owner ? owner->ToPlayer() : nullptr;
    if (!player || player->GetPet() != attacker || GetClassPerk(player) != SPELL_WARLOCK_DEMONOLOGY)
        return;
    damage *= 2;
}

// -------------------------------------------------------------------------
// Warlock: Destruction (910159)
// "Chaos Bolt has no cooldown. Conflagrate also casts a free, instant
// Chaos Bolt at the same target."
// -------------------------------------------------------------------------
void TryWarlockDestroOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_WARLOCK_DESTRUCTION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (RankOf(info, SPELL_CHAOS_BOLT_R1))
    {
        player->RemoveSpellCooldown(info->Id, true);
        if (uint32 const cat = info->GetCategory())
            player->RemoveCategoryCooldown(cat);
        return;
    }
    if (!RankOf(info, SPELL_CONFLAGRATE_R1))
        return;
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target || !player->IsValidAttackTarget(target))
        return;
    uint32 const bolt = BestOwned(player, SPELL_CHAOS_BOLT_R1);
    if (bolt)
        player->CastSpell(target, bolt, true);
}

// -------------------------------------------------------------------------
// Druid: Balance (910160)
// "Starfall has no cooldown/mana cost. You are permanently in both Solar
// and Lunar Eclipse at once."
//
// SIMPLIFICATION: Starfall's "recast to stop it early" toggle is not
// implemented, same call already made for Warrior Arms/Bladestorm
// elsewhere in this file (see that comment for the crash history reason).
// No-cooldown/no-cost alone gets most of the value (spam it near-freely).
// -------------------------------------------------------------------------
void TryDruidBalanceOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DRUID_BALANCE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_STARFALL)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_MANA, cost);
    player->RemoveSpellCooldown(SPELL_STARFALL, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void TickDruidBalanceEclipse(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DRUID_BALANCE || !player->IsAlive())
        return;
    st.acc += diff;
    if (st.acc < DRUID_ECLIPSE_TICK_MS)
        return;
    st.acc = 0;
    for (uint32 id : { SPELL_ECLIPSE_SOLAR, SPELL_ECLIPSE_LUNAR })
    {
        if (Aura* aura = player->GetAura(id))
        {
            if (aura->GetDuration() >= 0 && aura->GetDuration() < DRUID_ECLIPSE_REFRESH_DURATION)
                aura->SetDuration(DRUID_ECLIPSE_REFRESH_DURATION);
        }
        else
            player->CastSpell(player, id, true);
    }
}

// -------------------------------------------------------------------------
// Druid: Feral (910161)
// "Berserk is a free toggle. While active, Cat/Bear abilities cost no
// energy/rage and lose their cooldowns."
// -------------------------------------------------------------------------
void TryDruidFeralOnCast(Player* player, Spell* spell)
{
    if (!player || !spell)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    if (GetClassPerk(player) == SPELL_DRUID_FERAL && info->Id == SPELL_BERSERK_DRUID)
    {
        player->RemoveSpellCooldown(SPELL_BERSERK_DRUID, true);
        if (uint32 const cat = info->GetCategory())
            player->RemoveCategoryCooldown(cat);
        return;
    }
    if (GetClassPerk(player) != SPELL_DRUID_FERAL || info->SpellFamilyName != SPELLFAMILY_DRUID)
        return;
    if (!player->HasAura(SPELL_BERSERK_DRUID))
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(Powers(info->PowerType), cost);
    player->RemoveSpellCooldown(info->Id, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

// -------------------------------------------------------------------------
// Druid: Restoration (910162)
// "Wild Growth has no cooldown and heals up to 10 allies within 30 yards.
// Rejuvenation spreads to injured allies within 15 yards every 3 sec."
// -------------------------------------------------------------------------
void TryDruidRestOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DRUID_RESTORATION)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_WILD_GROWTH_R1))
        return;
    player->RemoveSpellCooldown(info->Id, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void ApplyDruidRestWildGrowthTargets(Spell* spell, Player* player, SpellInfo const* info)
{
    if (!spell || !player || !info || GetClassPerk(player) != SPELL_DRUID_RESTORATION)
        return;
    if (RankOf(info, SPELL_WILD_GROWTH_R1))
        spell->SetSpellValue(SPELLVALUE_MAX_TARGETS, WILD_GROWTH_MAX_TARGETS);
}

void TickDruidRestRejuvSpread(Player* player, TickState& st, uint32 diff)
{
    if (!player || GetClassPerk(player) != SPELL_DRUID_RESTORATION || !player->IsAlive())
        return;
    st.acc += diff;
    if (st.acc < DRUID_REJUV_SPREAD_TICK_MS)
        return;
    st.acc = 0;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ForEachInjuredAllyNear(player, player, 60.0f, [player](Unit* source)
    {
        if (!HasAuraRankOf(source, SPELL_REJUVENATION_R1))
            return;
        uint32 const rejuv = BestOwned(player, SPELL_REJUVENATION_R1);
        if (!rejuv)
            return;
        ForEachInjuredAllyNear(player, source, DRUID_REJUV_SPREAD_RANGE, [player, source, rejuv](Unit* ally)
        {
            if (ally != source && !ally->HasAura(rejuv, player->GetGUID()))
                player->CastSpell(ally, rejuv, true);
        });
    });
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Priest: Discipline (910163)
// "Penance has no cooldown and also applies Power Word: Shield to the
// target."
// -------------------------------------------------------------------------
void TryPriestDiscOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_PRIEST_DISCIPLINE)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_PENANCE_R1))
        return;
    player->RemoveSpellCooldown(info->Id, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
    Unit* target = spell->m_targets.GetUnitTarget();
    uint32 const shield = BestOwned(player, SPELL_POWER_WORD_SHIELD_R1);
    if (target && shield)
        player->CastSpell(target, shield, true);
}

// -------------------------------------------------------------------------
// Priest: Holy (910164)
// "Guardian Spirit has no cooldown and also applies to 2 more injured
// allies within 20 yards."
// -------------------------------------------------------------------------
void TryPriestHolyOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_PRIEST_HOLY)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_GUARDIAN_SPIRIT)
        return;
    player->RemoveSpellCooldown(SPELL_GUARDIAN_SPIRIT, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
    Unit* target = spell->m_targets.GetUnitTarget();
    if (!target)
        return;
    uint32 spread = 0;
    ForEachInjuredAllyNear(player, target, PRIEST_GS_SPREAD_RANGE, [&](Unit* ally)
    {
        if (spread >= PRIEST_GS_SPREAD_COUNT || ally == target || ally->HasAura(SPELL_GUARDIAN_SPIRIT, player->GetGUID()))
            return;
        player->CastSpell(ally, SPELL_GUARDIAN_SPIRIT, true);
        ++spread;
    });
}

// -------------------------------------------------------------------------
// Priest: Shadow (910165)
// "Shadowfiend has no cooldown. Mind Flay deals quadruple damage."
//
// SIMPLIFICATION: "up to 3 out at once" was dropped -- no cooldown already
// gets near-permanent uptime from a single fiend via spam-recast, and a
// real "3 simultaneous fiends" would mean bypassing the engine's one-
// controlled-minion-slot rule the same way the Army of the Dead group does
// with brand new creatures, which felt like more risk than one perk's
// flavor clause was worth here.
// -------------------------------------------------------------------------
void TryPriestShadowOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_PRIEST_SHADOW)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_SHADOWFIEND)
        return;
    player->RemoveSpellCooldown(SPELL_SHADOWFIEND, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void ApplyPriestShadowMindFlayDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || !RankOf(info, SPELL_MIND_FLAY_R1))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_PRIEST_SHADOW)
        return;
    damage *= 4;
}

// -------------------------------------------------------------------------
// Death Knight: Blood (910166)
// "Dancing Rune Weapon has no cooldown/runic cost. While active, melee
// hits heal you for 5% of the damage dealt."
//
// SIMPLIFICATION: "+50% parry" was dropped -- applying a flat parry bonus
// means touching stat-modifier internals nothing else in this file goes
// near; the self-heal-on-hit clause reuses the exact mechanism already
// proven for Warrior Fury's "heal 1% of max health in combat" below.
// -------------------------------------------------------------------------
void TryDkBloodOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DK_BLOOD)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_DANCING_RUNE_WEAPON)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_RUNIC_POWER, cost);
    player->RemoveSpellCooldown(SPELL_DANCING_RUNE_WEAPON, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void NoteDkBloodMeleeHit(Player* player, uint32 damage)
{
    if (!player || !damage || GetClassPerk(player) != SPELL_DK_BLOOD || !player->HasAura(SPELL_DANCING_RUNE_WEAPON))
        return;
    if (!player->IsInCombat() || player->GetHealth() >= player->GetMaxHealth())
        return;
    uint32 const heal = damage / 20; // 5%
    if (heal)
        player->ModifyHealth(int32(heal));
}

// -------------------------------------------------------------------------
// Death Knight: Frost (910167)
// "Hungering Cold has no cooldown/runic cost. Frost Strike and Obliterate
// deal double damage."
// -------------------------------------------------------------------------
void TryDkFrostOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || GetClassPerk(player) != SPELL_DK_FROST)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || info->Id != SPELL_HUNGERING_COLD)
        return;
    if (int32 const cost = spell->GetPowerCost())
        player->ModifyPower(POWER_RUNIC_POWER, cost);
    player->RemoveSpellCooldown(SPELL_HUNGERING_COLD, true);
    if (uint32 const cat = info->GetCategory())
        player->RemoveCategoryCooldown(cat);
}

void ApplyDkFrostDamage(Unit* attacker, int32& damage, SpellInfo const* info)
{
    if (!attacker || damage <= 0 || !info || (!RankOf(info, SPELL_FROST_STRIKE_R1) && !RankOf(info, SPELL_OBLITERATE_R1)))
        return;
    Player* player = attacker->ToPlayer();
    if (!player || GetClassPerk(player) != SPELL_DK_FROST)
        return;
    damage *= 2;
}

// -------------------------------------------------------------------------
// Paladin: Protection (910070)
// "Avenger's Shield bounces 30 times and can rehit. Range 60 yards.
// Devotion Aura: 10% damage reduction and +20% run/mount speed for
// allies. You deal Holy thorns equal to 50% of your armor."
//
// SIMPLIFICATION: the initial-cast range increase to 60 yards is not
// implemented (would require mutating shared SpellInfo range data, which
// is riskier than it's worth for this pass); the bounce count, rehit,
// damage reduction, and thorns are. The +20% run/mount speed for allies
// is also left out -- this codebase has a documented crash/near-miss
// history specifically around ad hoc movement-speed code (extra jump
// disabled, 500% speed-cap near-miss; see Bonesaw.md), so a new
// speed-granting mechanic was judged not worth the risk for this pass.
// -------------------------------------------------------------------------
// Performs one Avenger's Shield bounce, then schedules the next one as its
// own separate m_Events callback (see AvengerBounceStep) instead of looping
// synchronously. Running up to PALADIN_AS_BOUNCES (30) CastSpell calls
// back-to-back in a single call, from inside OnPlayerSpellCast (which fires
// mid-way through the triggering spell's own Spell::cast()), is exactly the
// reentrant pattern that produced the recurring Unit::_AddAura assert
// "!m_cleanupDone" crashes on Shadowstep/ChainAmbush (2026-08-20) -- staggering
// each bounce onto its own tick avoids both the reentrancy and the "spam N
// heavy operations synchronously" failure mode.
static void AvengerBounceStep(ObjectGuid playerGuid, ObjectGuid currentGuid, uint32 spellId, uint32 bouncesLeft)
{
    // g_reentryGuard stays held for the whole staggered sequence (cleared
    // on every exit path below) so an overlapping non-triggered cast can't
    // kick off a second bounce chain while this one is still running.
    uint32 const guid = playerGuid.GetCounter();
    if (!bouncesLeft)
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player || !player->IsInWorld())
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Unit* current = ObjectAccessor::GetUnit(*player, currentGuid);
    if (!current || !current->IsInWorld())
    {
        g_reentryGuard.erase(guid);
        return;
    }
    Unit* next = nullptr;
    ForEachHostileNear(player, current, PALADIN_AS_HOP_RANGE, [&next](Unit* target)
    {
        if (!next)
            next = target;
    });
    if (!next && current->IsAlive() && player->IsValidAttackTarget(current))
        next = current;
    if (!next)
    {
        g_reentryGuard.erase(guid);
        return;
    }
    player->CastSpell(next, spellId, true);
    ObjectGuid nextGuid = next->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, nextGuid, spellId, bouncesLeft]()
    {
        AvengerBounceStep(playerGuid, nextGuid, spellId, bouncesLeft - 1);
    }, std::chrono::milliseconds(150));
}

void ScheduleAvengerBounces(Player* player, Spell* spell)
{
    if (!player || !spell)
        return;
    Unit* current = spell->m_targets.GetUnitTarget();
    if (!current)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    ObjectGuid currentGuid = current->GetGUID();
    uint32 spellId = info->Id;
    player->m_Events.AddEventAtOffset([playerGuid, currentGuid, spellId]()
    {
        AvengerBounceStep(playerGuid, currentGuid, spellId, PALADIN_AS_BOUNCES);
    }, std::chrono::milliseconds(1));
}

void TryPaladinProtOnCast(Player* player, Spell* spell)
{
    if (!player || !spell || spell->IsTriggered() || GetClassPerk(player) != SPELL_PALADIN_PROTECTION)
        return;
    if (g_reentryGuard.count(player->GetGUID().GetCounter()))
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!info || !RankOf(info, SPELL_AVENGERS_SHIELD))
        return;
    ScheduleAvengerBounces(player, spell);
}

bool NearProtDevotion(Unit* victim)
{
    if (!victim || !victim->IsAlive())
        return false;
    Player* vp = victim->ToPlayer();
    if (!vp)
        return false;
    if (GetClassPerk(vp) == SPELL_PALADIN_PROTECTION && HasAuraRankOf(vp, SPELL_DEVOTION_AURA))
        return true;
    Group* group = vp->GetGroup();
    if (!group)
        return false;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || member == vp || !member->IsInWorld())
            continue;
        if (member->getClass() != CLASS_PALADIN || GetClassPerk(member) != SPELL_PALADIN_PROTECTION)
            continue;
        if (!HasAuraRankOf(member, SPELL_DEVOTION_AURA))
            continue;
        if (vp->IsInMap(member) && vp->IsWithinDistInMap(member, PALADIN_DEVO_ALLY_RANGE))
            return true;
    }
    return false;
}

void ApplyDevotionDR(Unit* victim, uint32& damage)
{
    if (!victim || !damage)
        return;
    if (NearProtDevotion(victim))
    {
        uint32 const reduced = uint32(float(damage) * PALADIN_DEVO_DR);
        damage = reduced < 1 ? 1 : reduced;
    }
}

void TryProtThorns(Unit* attacker, Unit* victim, uint32 damage)
{
    if (!attacker || !victim || !damage || attacker == victim || !attacker->IsAlive())
        return;
    Player* paladin = victim->ToPlayer();
    if (!paladin || GetClassPerk(paladin) != SPELL_PALADIN_PROTECTION)
        return;
    if (!HasAuraRankOf(paladin, SPELL_DEVOTION_AURA))
        return;
    uint32 const guid = paladin->GetGUID().GetCounter();
    if (!g_reentryGuard.insert(guid).second)
        return;
    uint32 const thorns = uint32(float(paladin->GetArmor()) * PALADIN_THORNS_PCT);
    if (thorns)
        Unit::DealDamage(paladin, attacker, thorns, nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_HOLY, nullptr, false);
    g_reentryGuard.erase(guid);
}

// -------------------------------------------------------------------------
// Script hooks
// -------------------------------------------------------------------------
class ClassPerksPlayer : public PlayerScript
{
public:
    ClassPerksPlayer() : PlayerScript("LivingGearClassPerksPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_PLAYER_LEAVE_COMBAT,
        PLAYERHOOK_ON_PLAYER_RESURRECT
    }) { }

    void OnPlayerResurrect(Player* player, float /*restore_percent*/, bool& /*applySickness*/) override
    {
        ApplyRogueCombatBladeFlurry(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        DetectSchema();
        LoadClassPerk(player->GetGUID().GetCounter());
        GrantAndBroadcastClassPerks(player);
        ApplyRogueCombatBladeFlurry(player);
        GrantMageFrostBlizzard(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        uint32 const guid = player->GetGUID().GetCounter();
        auto it = g_fury.find(guid);
        if (it != g_fury.end() && it->second.appliedPct)
            SetFuryHastePct(player, it->second, 0);
        g_mage.erase(guid);
        g_fury.erase(guid);
        g_rogue.erase(guid);
        g_afflictionTick.erase(guid);
        g_eclipseTick.erase(guid);
        g_rejuvTick.erase(guid);
        auto army = g_armyGroup.find(guid);
        if (army != g_armyGroup.end())
        {
            for (ObjectGuid const& g : army->second)
                if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                    c->DespawnOrUnsummon();
            g_armyGroup.erase(army);
        }
        auto pack = g_bmPack.find(guid);
        if (pack != g_bmPack.end())
        {
            for (ObjectGuid const& g : pack->second)
                if (Creature* c = ObjectAccessor::GetCreature(*player, g))
                    c->DespawnOrUnsummon();
            g_bmPack.erase(pack);
        }
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!player || !player->IsInWorld())
            return;
        uint32 const selected = GetClassPerk(player);
        if (selected == SPELL_MAGE_ARCANE)
        {
            TickMageArcane(player);
        }
        else if (selected == SPELL_MAGE_FIRE)
        {
            TickMageFire(player, g_mage[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_MAGE_FROST)
        {
            TickMageFrostBlizzard(player);
            TickMageFrostIceLance(player, g_mage[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_ROGUE_COMBAT)
        {
            TickRogueCombat(player, g_rogue[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_SHAMAN_ENHANCEMENT)
        {
            TickShamanEnhWolves(player);
        }
        else if (selected == SPELL_WARLOCK_AFFLICTION)
        {
            TickWarlockAffliction(player, g_afflictionTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_DRUID_BALANCE)
        {
            TickDruidBalanceEclipse(player, g_eclipseTick[player->GetGUID().GetCounter()], diff);
        }
        else if (selected == SPELL_DRUID_RESTORATION)
        {
            TickDruidRestRejuvSpread(player, g_rejuvTick[player->GetGUID().GetCounter()], diff);
        }
        if (player->getClass() == CLASS_WARRIOR)
            TickWarriorFury(player);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skip*/) override
    {
        if (!player || !spell)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (!spell->IsTriggered() && CanSelectClassPerk(player, info->Id))
        {
            SelectClassPerk(player, info->Id);
            return;
        }
        if (spell->IsTriggered())
            return;
        TryMageFireOnCast(player, spell);
        TryMageFrostBlizzardLinger(player, spell);
        TryPaladinProtOnCast(player, spell);
        TryWarriorArmsOnCast(player, spell);
        TryWarriorProtOnCast(player, spell);
        TryRogueCombatKillingSpree(player, spell);
        TryHunterMMOnCast(player, spell);
        TryShamanElementalOnCast(player, spell);
        TryDkUnholyOnCast(player, spell);
        TryHunterBmOnCast(player, spell);
        TryHunterSurvivalOnCast(player, spell);
        TryShamanEnhOnCast(player, spell);
        TryShamanRestOnCast(player, spell);
        TryWarlockDemoOnCast(player, spell);
        TryWarlockDestroOnCast(player, spell);
        TryDruidBalanceOnCast(player, spell);
        TryDruidFeralOnCast(player, spell);
        TryDruidRestOnCast(player, spell);
        TryPriestDiscOnCast(player, spell);
        TryPriestHolyOnCast(player, spell);
        TryPriestShadowOnCast(player, spell);
        TryDkBloodOnCast(player, spell);
        TryDkFrostOnCast(player, spell);
    }

    void OnPlayerLeaveCombat(Player* player) override
    {
        OnLeaveCombatMage(player);
    }
};

class ClassPerksSpell : public AllSpellScript
{
public:
    ClassPerksSpell() : AllSpellScript("LivingGearClassPerksSpell", {
        ALLSPELLHOOK_ON_PREPARE
    }) { }

    void OnSpellPrepare(Spell* spell, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!spell || !caster || !spellInfo || !caster->IsPlayer())
            return;
        ApplyProtThunderClapRadius(spell, caster->ToPlayer(), spellInfo);
        ApplyShamanElementalChainLightningTargets(spell, caster->ToPlayer(), spellInfo);
        ApplyHunterSurvivalTrapRadius(spell, caster->ToPlayer(), spellInfo);
        ApplyShamanRestChainHealTargets(spell, caster->ToPlayer(), spellInfo);
        ApplyDruidRestWildGrowthTargets(spell, caster->ToPlayer(), spellInfo);
    }
};

class ClassPerksUnit : public UnitScript
{
public:
    ClassPerksUnit() : UnitScript("LivingGearClassPerksUnit", true, {
        UNITHOOK_ON_DAMAGE,
        UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
        UNITHOOK_MODIFY_MELEE_DAMAGE,
        UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK
    }) { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || !damage)
            return;
        ApplyDevotionDR(victim, damage);
        TryProtThorns(attacker, victim, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (!attacker || damage <= 0 || !spellInfo)
            return;
        ApplyProtShockwaveDamage(attacker, damage, spellInfo);
        ApplyFuryBleedDamage(attacker, damage, spellInfo);
        ApplyShamanElementalLavaBurstDamage(attacker, damage, spellInfo);
        ApplyHunterSurvivalExplosiveShotDamage(attacker, damage, spellInfo);
        ApplyPriestShadowMindFlayDamage(attacker, damage, spellInfo);
        ApplyDkFrostDamage(attacker, damage, spellInfo);
        ApplyWarlockDemoPetSpellDamage(attacker, damage);
        if (target)
            ApplyHunterSurvivalTrapImmunity(attacker, target, damage, spellInfo);
    }

    void ModifyMeleeDamage(Unit* /*target*/, Unit* attacker, uint32& damage) override
    {
        if (Player* player = attacker ? attacker->ToPlayer() : nullptr)
        {
            NoteFuryMeleeHit(player);
            NoteDkBloodMeleeHit(player, damage);
        }
        ApplyShamanEnhWolfMeleeDamage(attacker, damage);
        ApplyWarlockDemoPetDamage(attacker, damage);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
    {
        if (!attacker || !damage || !spellInfo)
            return;
        ApplyWarlockAfflictionHaste(target, attacker, damage, spellInfo);
    }
};

class ClassPerksWorld : public WorldScript
{
public:
    ClassPerksWorld() : WorldScript("LivingGearClassPerksWorld", {
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnStartup() override
    {
        DetectSchema();
        PatchBlizzardServerSide();
        LOG_INFO("server.loading",
            "Living Gear class perks module loaded (all 10 classes, 3 specs each except "
            "Paladin Holy/Retribution and Rogue Assassination/Subtlety, which live in other files)");
    }

private:
    // build_patch.py patches the CLIENT's Spell.dbc so Blizzard *looks*
    // instant/uncancelled, but the server loads its own independent copy of
    // spell data and was still enforcing the real channel (move = interrupt,
    // real cast/cooldown timers) underneath that visual. Mirror the same
    // "instant, no cooldown, not channeled" change server-side so it isn't
    // just cosmetic.
    void PatchBlizzardServerSide()
    {
        SpellCastTimesEntry const* instant = sSpellCastTimesStore.LookupEntry(1);
        uint32 n = 0;
        for (uint32 id : SPELL_BLIZZARD_RANKS)
        {
            SpellInfo* info = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(id));
            if (!info)
                continue;
            info->AttributesEx &= ~(SPELL_ATTR1_IS_CHANNELED | SPELL_ATTR1_IS_SELF_CHANNELED);
            info->CastTimeEntry = instant;
            info->RecoveryTime = 0;
            info->CategoryRecoveryTime = 0;
            info->StartRecoveryTime = 0;
            info->InterruptFlags = 0;
            info->ChannelInterruptFlags = 0;
            ++n;
        }
        LOG_INFO("server.loading", "Living Gear: patched {} Blizzard ranks server-side (instant, uncancellable, lingers like Death and Decay)", n);
    }
};

} // namespace LivingGearClassPerks

uint32 GetClassPerk(Player* player)
{
    return LivingGearClassPerks::GetClassPerk(player);
}

// Addon-command entry point, called by the dispatcher in LivingGear.cpp.
// The client's Class-tab buttons send "CLASS|<id>" (LivingGear.lua,
// ui.classBtns OnClick).
bool LivingGear_HandleClassPerksCommand(Player* player, std::string const& msg)
{
    uint32 spellId = 0;
    if (sscanf(msg.c_str(), "CLASS|%u", &spellId) != 1)
        return false;
    LivingGearClassPerks::SelectClassPerk(player, spellId);
    return true;
}

// CPKALL was login-only, so the Class tab was blank after any /reload
// until the player logged out and back in. Idempotent: UnlockPerk skips
// anything already known.
void LivingGear_SendClassPerksSync(Player* player)
{
    LivingGearClassPerks::GrantAndBroadcastClassPerks(player);
}

void AddSC_LivingGearClassPerks()
{
    new LivingGearClassPerks::ClassPerksWorld();
    new LivingGearClassPerks::ClassPerksPlayer();
    new LivingGearClassPerks::ClassPerksSpell();
    new LivingGearClassPerks::ClassPerksUnit();
    new LivingGearClassPerks::npc_lg_army_ghoul_tank();
    new LivingGearClassPerks::npc_lg_army_ghoul_healer();
    new LivingGearClassPerks::npc_lg_army_ghoul_dps();
}
