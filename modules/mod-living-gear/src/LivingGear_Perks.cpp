/*
 * Living Gear account perks rebuilt as new code on the stub baseline.
 * Zone scale, auto-mount, Subtlety (Shaco kit), poisons, combo, travel,
 * craft speed, cooking regen, dungeon timer, curator, quest helpers, armory,
 * solo queue, instant/uniform mount.
 */

#include "AllMapScript.h"
#include "AllSpellScript.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config.h"
#include "CreatureAI.h"
#include "CreatureData.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Formulas.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Unit.h"
#include "Util.h"
#include "QuestDef.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
void LivingGear_GrantItemXp(Player* player, uint32 itemGuid, uint32 xp); // LivingGear.cpp
uint32 GetClassPerk(Player* player); // LivingGear_ClassPerks.cpp

namespace LivingGearPerks
{
uint32 const SPELL_FIND_QUESTS = 910088;
uint32 const SPELL_COMBO = 910089;
uint32 const SPELL_AUTO_QUEST = 910090;
uint32 const SPELL_ARMORY = 910091;
uint32 const SPELL_SOLO_QUEUE = 910092;
uint32 const SPELL_CRAFT[] = { 910093, 910094, 910095, 910096, 910097 };
uint32 const SPELL_TRAVEL[] = { 910073, 910074, 910075, 910076, 910077 };
uint32 const SPELL_COOK[] = { 910063, 910064, 910065, 910066, 910067, 910068 };
uint32 const SPELL_SWIM = 910098;
uint32 const SPELL_DUNGEON_SPEED = 910099;
uint32 const SPELL_DUNGEON_PACE = 910100;
uint32 const SPELL_CURATOR = 910101;
// Shadow Dance (2026-08-21, replaces the dropped Jack in the Box totem,
// reuses its freed spell ID): permanent Subtlety perk, two effects --
// lets stealth-only openers (Ambush/Garrote/Cheap Shot/etc.) be used
// without actually being stealthed (LivingGear_BypassStealthRequirement,
// a small core patch in Spell::CheckCast), and a +10% attack power buff
// to the whole party/raid (ShouldHaveShadowDanceBuff/TickShadowDanceBuff
// below, mirroring the existing Class Buffs pattern in LivingGear_Next.cpp).
uint32 const SPELL_SHADOW_DANCE = 910102;
uint32 const SPELL_SHADOW_CLONE = 910103;
uint32 const SPELL_MOUNTED_OPENER = 910104;
uint32 const SPELL_AUTO_MOUNT = 910105;
// Quadruples aggro/detection radius while toggled on -- native
// SPELL_AURA_MOD_DETECTED_RANGE (152), flat +60 yards, which roughly 4x's
// the ~20-yard same-level baseline in Creature::GetAttackDistance(). No
// core changes needed; this is exactly what that aura type is for.
uint32 const SPELL_PULL_RADIUS = 910168;
// Track Ore / Track Herbs -- toggle badges gating the native tracking
// spells (Find Minerals / Find Herbs, stock Blizzard spells not in our
// spell_dbc table since they're resolved from the compiled client DBC).
// Casting the native spell puts real nodes on the minimap; refreshed
// periodically the same way SPELL_PULL_RADIUS/SPELL_COMBO are, since we
// can't check the native spell's real duration from our sparse table.
uint32 const SPELL_TRACK_ORE = 910170;
uint32 const SPELL_TRACK_HERB = 910171;
uint32 const NATIVE_FIND_MINERALS = 2580;
uint32 const NATIVE_FIND_HERBS = 2383;
uint32 const SPELL_SUBTLETY = 910037;
uint32 const SPELL_ASSASSINATION = 910035;
uint32 const NPC_SHADOW_CLONE = 910201;
uint32 const SPELL_STEALTH = 1784;
uint32 const SPELL_SHADOWSTEP = 36554;
uint32 const SPELL_AMBUSH = 8676;
uint32 const SPELL_VANISH = 1856;
uint32 const SPELL_PICKPOCKET = 921;
// Hemorrhage AoE (2026-08-21, replaces Jack in the Box): on cast, applies
// Garrote + Pickpocket to every enemy within 10 yards, via BestOwned() to
// resolve each spell to the player's actual trained rank -- same pattern
// ChainAmbush already uses for Ambush. Cheap Shot was in the original ask
// but dropped (mass-stun on a whole pack agreed to be too strong).
uint32 const SPELL_HEMORRHAGE = 16511;
uint32 const SPELL_GARROTE = 703;
uint32 const SPELL_DEADLY_THROW = 26679;
uint32 const SPELL_SINISTER_STRIKE = 1752;
uint32 const SPELL_EVISCERATE = 2098;
uint32 const SPELL_KICK = 1766;
uint32 const SPELL_RUPTURE = 1943;
uint32 const SPELL_HOWL = 5484;
uint32 const SPELL_THUNDER_CLAP = 6343;
uint32 const SPELL_CRIPPLING = 3408;
uint32 const SPELL_WOUND = 13218;
uint32 const SPELL_DEADLY_POISON = 2818;
// Redesigned 2026-08-21: was 100 stacks/3% xp/3min independent-per-stack
// decay, never actually unlocked so nobody ever saw it. New spec: 10 stacks
// cap, 20% xp/stack, 5% move speed/stack (native MOD_INCREASE_SPEED aura,
// stacks with everything else normally), single 10-min timer that refreshes
// in full on every kill rather than each stack decaying independently.
uint32 const COMBO_MAX = 10;
uint32 const COMBO_MS = 600000;
float const COMBO_XP_PCT_PER_STACK = 0.20f;
int32 const COMBO_SPEED_PCT_PER_STACK = 5;
uint32 const COOK_MS = 1000;
float const AMBUSH_MULT = 6.0f;

struct PerkCfg
{
    bool zoneEnable = true;
    uint32 zoneBuffer = 3;
    uint32 zoneMinLevel = 10;
    float zoneFloor = 0.35f;
    float zoneDecay = 12.0f;
    float zoneIncoming = 0.12f;
    bool instantMount = true;
    bool uniformMount = true;
    bool autoMount = true;
    uint32 dungeonPar = 1800;
    uint32 curatorTick = 60000;
};

PerkCfg g_cfg;

struct ComboState
{
    uint32 stacks = 0;
    uint32 expiresAt = 0; // whole buff expires together; a new kill refreshes this to now + COMBO_MS
};

std::unordered_map<uint32, ComboState> g_combo;
std::unordered_map<uint32, ComboState> g_groupCombo;
std::unordered_map<uint32, bool> g_autoMountOn;
std::unordered_map<uint32, bool> g_soloQueue;
std::unordered_map<uint32, bool> g_chatOn;
std::unordered_map<uint32, uint32> g_lastMount;
std::unordered_map<uint32, ObjectGuid> g_cloneGuid;
std::unordered_map<uint32, uint32> g_dungeonStart;
std::unordered_map<uint32, bool> g_dungeonDone;
std::unordered_map<uint32, uint32> g_cookAcc;
std::unordered_map<uint32, uint32> g_curatorAcc;
std::unordered_map<uint32, uint32> g_comboTick;
// Shadow Dance's +10% attack power half -- keyed by player GUID (not
// account), since it's a real per-character stat modifier applied via
// Player::ApplyStatPctModifier, not a persisted account toggle.
std::unordered_map<uint32, bool> g_shadowDanceBuffOn;
std::unordered_map<uint32, uint32> g_shadowDanceTick;
std::unordered_map<uint32, uint32> g_pullRadiusTick;
std::unordered_map<uint32, uint32> g_trackOreTick;
std::unordered_map<uint32, uint32> g_trackHerbTick;
std::unordered_set<uint32> g_perkLoaded;
std::unordered_map<uint32, std::unordered_set<uint32>> g_perks;
bool g_hasAutoMountCol = false;
bool g_hasSoloCol = false;
bool g_hasPullRadiusCol = false;
bool g_hasTrackOreCol = false;
bool g_hasTrackHerbCol = false;
bool g_schemaReady = false;
bool g_hasZoneScale = false;
std::unordered_map<uint32, bool> g_pullRadiusOn;
std::unordered_map<uint32, bool> g_trackOreOn;
std::unordered_map<uint32, bool> g_trackHerbOn;

void DetectSchema()
{
    if (g_schemaReady)
        return;
    g_schemaReady = true;
    if (QueryResult cols = CharacterDatabase.Query(
        "SELECT `COLUMN_NAME` FROM `information_schema`.`COLUMNS` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta'"))
    {
        do
        {
            std::string const name = (*cols)[0].Get<std::string>();
            if (name == "auto_mount")
                g_hasAutoMountCol = true;
            else if (name == "solo_queue")
                g_hasSoloCol = true;
            else if (name == "pull_radius")
                g_hasPullRadiusCol = true;
            else if (name == "track_ore")
                g_hasTrackOreCol = true;
            else if (name == "track_herb")
                g_hasTrackHerbCol = true;
        } while (cols->NextRow());
    }
}

void SendLine(Player* player, std::string const& line)
{
    if (!player || !player->GetSession())
        return;
    player->Whisper(std::string("LG\t") + line, LANG_ADDON, player);
}

void Say(Player* player, char const* msg)
{
    if (!player || !player->GetSession())
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    auto it = g_chatOn.find(acc);
    if (it != g_chatOn.end() && !it->second)
        return;
    ChatHandler(player->GetSession()).SendSysMessage(msg);
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

// 2026-08-21: learnSpellToo defaults true (most perks unlocked here ARE
// meant to be real, castable spells) but must be false for pure account-
// perk flags with no CASTABLE_SPELLS/SkillLineAbility entry (Shadow Dance,
// Shadow Clone) -- player->learnSpell() marks them "known" with nothing to
// categorize them, so the client dumps them into the General spellbook tab
// regardless of being excluded from CASTABLE_SPELLS client-side. HasPerk()'s
// g_perks[acc]/DB fallback works fine without ever calling learnSpell.
void UnlockPerk(Player* player, uint32 spellId, char const* msg, bool learnSpellToo = true)
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
    if (learnSpellToo && !player->HasSpell(spellId))
        player->learnSpell(spellId);
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        Say(player, msg);
}

// A player's own DisplayId only carries the bare racial body model --
// skin/face/hair customization lives in PLAYER_BYTES-family fields that
// only exist on Player objects (a Creature's update-field array doesn't
// have room for them at all), so copying it onto a Creature-based clone
// renders as a blank, untextured white mesh -- confirmed live 2026-08-20.
// There is no way to get a pixel-exact match on a Creature; this picks a
// same-race/gender NPC model that actually has real textures instead, so
// it at least looks like a textured person of the right race/gender.
// Only Human is populated with any confidence right now (the only race
// actually tested); everything else falls back to the raw (broken) player
// DisplayId until someone tests a different race and it can be filled in.
uint32 LookAlikeDisplayId(Player* player)
{
    if (!player)
        return 0;
    if (player->getRace() == RACE_HUMAN)
        return player->getGender() == GENDER_FEMALE ? 3344 /* Priestess Anetta, Stormwind */
            : 3167 /* Stormwind City Guard */;
    return player->GetDisplayId();
}

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

uint32 AccountMaxLevel(Player* player)
{
    if (!player || !player->GetSession())
        return player ? player->GetLevel() : 1;
    uint32 maxLevel = player->GetLevel();
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT MAX(`level`) FROM `characters` WHERE `account` = {}",
        player->GetSession()->GetAccountId()))
        maxLevel = std::max(maxLevel, (*result)[0].Get<uint32>());
    return maxLevel;
}

uint32 AccountMaxSkill(Player* player, uint32 skillId)
{
    uint32 best = player ? player->GetSkillValue(skillId) : 0;
    if (!player || !player->GetSession())
        return best;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT MAX(`value`) FROM `character_skills` cs "
        "INNER JOIN `characters` c ON c.`guid` = cs.`guid` "
        "WHERE c.`account` = {} AND cs.`skill` = {}",
        player->GetSession()->GetAccountId(), skillId))
        if (!result->Fetch()->IsNull())
            best = std::max(best, (*result)[0].Get<uint32>());
    return best;
}

uint32 ZoneLevel(Player* player)
{
    if (!player)
        return 1;
    uint32 areaId = player->GetAreaId();
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    if (!area)
        return 1;
    if (area->zone)
        if (AreaTableEntry const* parent = sAreaTableStore.LookupEntry(area->zone))
            area = parent;
    int32 level = area->area_level;
    uint32 zoneId = area->zone ? area->zone : area->ID;
    if (g_hasZoneScale)
        if (QueryResult ov = WorldDatabase.Query(
            "SELECT `max_level` FROM `lg_zone_scale` WHERE `zone_id` = {}", zoneId))
            level = int32((*ov)[0].Get<uint32>());
    if (level < 1)
        level = 1;
    return uint32(level);
}

bool OpenWorld(Player* player)
{
    if (!player || !player->GetMap())
        return false;
    Map* map = player->GetMap();
    return !map->IsDungeon() && !map->IsBattlegroundOrArena();
}

float ZoneRewardMult(Player* player)
{
    if (!g_cfg.zoneEnable || !OpenWorld(player))
        return 1.0f;
    uint32 const real = player->GetLevel();
    if (real < g_cfg.zoneMinLevel)
        return 1.0f;
    uint32 const z = ZoneLevel(player);
    uint32 const cap = z + g_cfg.zoneBuffer;
    if (real <= cap)
        return 1.0f;
    float gap = float(real - cap);
    return g_cfg.zoneFloor + (1.0f - g_cfg.zoneFloor) * std::exp(-gap / g_cfg.zoneDecay);
}

// Shared "is this creature eligible for zone scaling, and what level
// should it effectively be for this viewer" -- single source of truth for
// the displayed level (below), real combat damage exchanged (PerksUnit::
// OnDamage), and grey-kill XP (GrantScaledGreyKillXP), so all three always
// agree on the same number. Always roughly a rank above the viewer --
// "yellow, not trivial, not overwhelming" -- both up AND down from the
// creature's real level, so open-world zones stay relevant no matter how
// overleveled a player is, and mixed-level groups each get an
// appropriately-scaled fight on the same mob instead of the higher level
// just carrying. Returns 0 for "not eligible, use the real level."
uint32 EffectiveCreatureLevel(Creature const* creature, Player* viewer)
{
    if (!g_cfg.zoneEnable || !creature || !viewer || !creature->IsInWorld())
        return 0;
    if (!OpenWorld(viewer))
        return 0;
    if (creature->IsPet() || creature->IsTotem() || creature->IsGuardian() || creature->IsSummon())
        return 0;
    if (CreatureTemplate const* tmpl = creature->GetCreatureTemplate())
        if (tmpl->type == CREATURE_TYPE_CRITTER)
            return 0;
    uint32 const viewerLevel = viewer->GetLevel();
    if (viewerLevel < g_cfg.zoneMinLevel)
        return 0;
    uint32 effective = viewerLevel + 1;
    if (effective > 80)
        effective = 80;
    return effective;
}

// Per-viewer displayed creature level (called from PerksUnit's
// ShouldTrackValuesUpdatePosByIndex/OnPatchValuesUpdate UnitScript hooks --
// Unit::PatchValuesUpdate already takes a per-target Player, so different
// players looking at the SAME creature simultaneously can each see a
// different level scaled to their own). Same eligibility as
// EffectiveCreatureLevel, plus display-only requires the creature actually
// be a valid attack target (a friendly town NPC keeps showing its real
// level).
uint32 DisplayLevelOverride(Unit const* unit, Player* viewer)
{
    Creature const* creature = unit ? unit->ToCreature() : nullptr;
    if (!creature)
        return 0;
    uint32 const effective = EffectiveCreatureLevel(creature, viewer);
    if (!effective)
        return 0;
    if (!viewer->IsValidAttackTarget(creature))
        return 0;
    return effective;
}

// Combat halves of zone scaling: rather than faking a creature's shared
// (not per-viewer) health pool, scale the damage exchanged in both
// directions by the ratio between its real-level stats and its
// effective-level stats -- computed via the same engine formulas
// Creature::SelectLevel uses at spawn (CreatureBaseStats::GenerateHealth/
// GenerateBaseDamage, Creature.cpp/CreatureData.h). Hits-to-kill and
// damage-taken both converge toward what they'd genuinely be at the
// displayed level, without ever touching UNIT_FIELD_MAXHEALTH itself, so
// there's no per-viewer desync on the shared health bar.
float OutgoingScaleRatio(Creature* creature, uint32 effectiveLevel)
{
    CreatureTemplate const* info = creature->GetCreatureTemplate();
    CreatureBaseStats const* realStats = sObjectMgr->GetCreatureBaseStats(creature->GetLevel(), info->unit_class);
    CreatureBaseStats const* effStats = sObjectMgr->GetCreatureBaseStats(effectiveLevel, info->unit_class);
    if (!realStats || !effStats)
        return 1.0f;
    float const effHp = float(effStats->GenerateHealth(info));
    if (effHp <= 0.0f)
        return 1.0f;
    return std::clamp(float(realStats->GenerateHealth(info)) / effHp, 0.05f, 4.0f);
}

float IncomingScaleRatio(Creature* creature, uint32 effectiveLevel)
{
    CreatureTemplate const* info = creature->GetCreatureTemplate();
    CreatureBaseStats const* realStats = sObjectMgr->GetCreatureBaseStats(creature->GetLevel(), info->unit_class);
    CreatureBaseStats const* effStats = sObjectMgr->GetCreatureBaseStats(effectiveLevel, info->unit_class);
    if (!realStats || !effStats)
        return 1.0f;
    float const realDmg = realStats->GenerateBaseDamage(info);
    if (realDmg <= 0.0f)
        return 1.0f;
    return std::clamp(effStats->GenerateBaseDamage(info) / realDmg, 0.2f, 4.0f);
}

// XP half of zone scaling: AzerothCore's own kill-XP formula hard-zeroes a
// grey kill (mob_level <= GetGrayLevel(playerLevel)) well before any
// module hook runs, so the existing OnPlayerGiveXP multiplier below can
// only ever multiply that zero. Compute XP ourselves using the effective
// (displayed) level and grant it directly -- safe from double-granting
// since we only do this when the core's own grant for this exact kill was
// definitely zero. Killer-only: does not replicate KillRewarder's
// group-XP-sharing, so a grouped low-level member doesn't get a scaled
// share of a high-level leader's grey kill. Revisit if that matters.
void GrantScaledGreyKillXP(Player* killer, Creature* killed)
{
    if (!killer || !killed || !killer->IsAlive())
        return;
    if (killed->GetLevel() > Acore::XP::GetGrayLevel(uint8(killer->GetLevel())))
        return;
    uint32 const eff = EffectiveCreatureLevel(killed, killer);
    if (!eff)
        return;
    uint32 xp = Acore::XP::BaseGain(uint8(killer->GetLevel()), uint8(eff), CONTENT_1_60);
    if (!xp)
        return;
    xp = uint32(float(xp) * ZoneRewardMult(killer));
    if (xp)
        killer->GiveXP(xp, killed);
}

void NotifyZoneScale(Player* player)
{
    if (!player || !g_cfg.zoneEnable || !OpenWorld(player))
        return;
    uint32 const real = player->GetLevel();
    uint32 const z = ZoneLevel(player);
    uint32 eff = std::min(real, z + g_cfg.zoneBuffer);
    SendLine(player, Acore::StringFormat("ZSCALE|{}|{}|{}", eff, real, z));
}

uint32 ComboCount(Player* player)
{
    if (!player)
        return 0;
    uint32 now = getMSTime();
    ComboState* st = nullptr;
    if (player->GetGroup())
        st = &g_groupCombo[player->GetGroup()->GetGUID().GetCounter()];
    else
        st = &g_combo[player->GetGUID().GetCounter()];
    if (st->stacks && now >= st->expiresAt)
        st->stacks = 0;
    return st->stacks;
}

void RecastCombo(Player* player)
{
    if (!player || !HasPerk(player, SPELL_COMBO) || !sSpellMgr->GetSpellInfo(SPELL_COMBO))
        return;
    uint32 stacks = ComboCount(player);
    if (!stacks)
    {
        player->RemoveAurasDueToSpell(SPELL_COMBO);
        SendLine(player, "COMBO|0|0|0");
        return;
    }
    // bp0 is just a display/marker value (effect 1, dummy) -- the real xp
    // bonus is read from ComboCount() directly in OnPlayerGiveXP, not from
    // the aura. bp1 (effect 2, native MOD_INCREASE_SPEED) is what actually
    // grants the move-speed bonus, via the engine's normal aura handling.
    int32 xpMarker = int32(stacks);
    int32 speedPct = int32(stacks) * COMBO_SPEED_PCT_PER_STACK;
    player->CastCustomSpell(player, SPELL_COMBO, &xpMarker, &speedPct, nullptr, true);
    SendLine(player, Acore::StringFormat("COMBO|{}|{}|{}", stacks, COMBO_MAX, COMBO_MS / 1000));
}

void AddCombo(Player* player)
{
    if (!player || !HasPerk(player, SPELL_COMBO))
        return;
    uint32 now = getMSTime();
    ComboState* st = nullptr;
    if (Group* group = player->GetGroup())
        st = &g_groupCombo[group->GetGUID().GetCounter()];
    else
        st = &g_combo[player->GetGUID().GetCounter()];
    if (now >= st->expiresAt)
        st->stacks = 0; // previous buff had already fully expired -- start fresh
    if (st->stacks < COMBO_MAX)
        ++st->stacks;
    st->expiresAt = now + COMBO_MS; // every kill refreshes the whole timer, not just adds a new independently-decaying stack
    if (Group* group = player->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* m = itr->GetSource())
                RecastCombo(m);
    }
    else
        RecastCombo(player);
}

uint32 RandomPoison(Player* player)
{
    uint32 ids[3] = {
        BestOwned(player, SPELL_CRIPPLING),
        BestOwned(player, SPELL_WOUND),
        BestOwned(player, SPELL_DEADLY_POISON)
    };
    std::vector<uint32> have;
    for (uint32 id : ids)
        if (id)
            have.push_back(id);
    if (have.empty())
        return 0;
    return have[urand(0, uint32(have.size() - 1))];
}

void ApplyRandomPoison(Player* caster, Unit* target)
{
    if (!caster || !target)
        return;
    uint32 id = RandomPoison(caster);
    if (id)
        caster->CastSpell(target, id, true);
}

uint32 PickMount(Player* player)
{
    if (!player)
        return 0;
    uint32 last = g_lastMount[player->GetGUID().GetCounter()];
    if (last && player->HasSpell(last))
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(last))
            if (info->HasAura(SPELL_AURA_MOUNTED))
                return last;
    std::vector<uint32> mounts;
    for (auto const& sp : player->GetSpellMap())
    {
        if (!sp.second || sp.second->State == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(sp.first);
        if (info && info->HasAura(SPELL_AURA_MOUNTED))
            mounts.push_back(sp.first);
    }
    if (mounts.empty())
        return 0;
    return mounts[urand(0, uint32(mounts.size() - 1))];
}

void TryAutoMount(Player* player)
{
    if (!g_cfg.autoMount || !player || !HasPerk(player, SPELL_AUTO_MOUNT))
        return;
    uint32 acc = player->GetSession()->GetAccountId();
    if (!g_autoMountOn[acc])
        return;
    if (player->IsInCombat() || player->IsMounted() || player->isDead())
        return;
    if (player->GetVehicle() || player->isMoving())
        return;
    uint32 mount = PickMount(player);
    if (mount)
        player->CastSpell(player, mount, true);
}

void InstantAndMoveMount(Spell* spell, SpellInfo const* info)
{
    if (!g_cfg.instantMount || !spell || !info)
        return;
    if (!info->HasAura(SPELL_AURA_MOUNTED))
        return;
    spell->SetCastTime(0);
}

void UniformMount(Unit* unit, Aura* aura)
{
    if (!g_cfg.uniformMount || !unit || !aura || !unit->IsPlayer())
        return;
    SpellInfo const* info = aura->GetSpellInfo();
    if (!info || !info->HasAura(SPELL_AURA_MOUNTED))
        return;
    Player* player = unit->ToPlayer();
    float ground = 0.0f;
    float flight = 0.0f;
    for (auto const& sp : player->GetSpellMap())
    {
        if (!sp.second || sp.second->State == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* si = sSpellMgr->GetSpellInfo(sp.first);
        if (!si)
            continue;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (si->Effects[i].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
                ground = std::max(ground, float(si->Effects[i].CalcValue()));
            if (si->Effects[i].ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED)
                flight = std::max(flight, float(si->Effects[i].CalcValue()));
        }
    }
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        AuraEffect* eff = aura->GetEffect(i);
        if (!eff)
            continue;
        if (eff->GetAuraType() == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED && ground > 0.0f)
            eff->ChangeAmount(int32(ground));
        if (eff->GetAuraType() == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED && flight > 0.0f)
            eff->ChangeAmount(int32(flight));
    }
}

// Stealth's built-in movement-speed penalty is zeroed out for Subtlety
// perk holders (requested: match, and exceed via real talents, normal
// movement speed while stealthed). Neutralizes whatever negative speed
// effect(s) the Stealth aura carries rather than hardcoding a percentage,
// so it stays correct regardless of rank/exact DBC value. Leaves every
// other speed source (talents, other auras) untouched, so real speed
// talents still stack on top normally.
void NeutralizeStealthSpeed(Unit* unit, Aura* aura)
{
    if (!unit || !aura || !unit->IsPlayer())
        return;
    if (aura->GetSpellInfo()->Id != SPELL_STEALTH)
        return;
    if (GetClassPerk(unit->ToPlayer()) != SPELL_SUBTLETY)
        return;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        AuraEffect* eff = aura->GetEffect(i);
        if (!eff)
            continue;
        AuraType const type = eff->GetAuraType();
        if ((type == SPELL_AURA_MOD_DECREASE_SPEED || type == SPELL_AURA_MOD_SPEED_ALWAYS
            || type == SPELL_AURA_MOD_INCREASE_SPEED) && eff->GetAmount() < 0)
            eff->ChangeAmount(0);
    }
}

// Performs a single chain-ambush hit: summons a short-lived clone at the
// target, has it Pickpocket (humanoids only, and BEFORE Ambush since Ambush
// can kill the target outright), then Ambush, then Vanish, then despawn.
// Each hit is scheduled as its own standalone m_Events callback (see
// ChainAmbushImpl below) rather than run back-to-back in a tight loop --
// doing all 5 summon+cast+despawn sequences synchronously in one call was
// itself enough to trip Unit::_AddAura's "!m_cleanupDone" assert on the
// player (2026-08-20), even after the whole chain was already deferred out
// of the triggering Shadowstep cast's call stack.
// Summons `entry` with its display ID already set to `displayId` before the
// creature is ever added to the map (i.e. before any client can see it).
// Unit::SummonCreature() creates+broadcasts the creature and returns it
// already-visible with its default template model; calling SetDisplayId()
// on the result afterward (the obvious approach) makes every nearby client
// render a model-swap transition -- the "level up"-looking flash the clones
// were showing on spawn. Mirrors Map::SummonCreature()'s own Create()/
// AddToMap() sequence with the display override inserted between them.
static TempSummon* SummonCloneWithDisplay(Player* player, uint32 entry, Position const& pos,
    uint32 despawnMs, uint32 displayId)
{
    if (!player || !player->IsInWorld())
        return nullptr;
    Map* map = player->GetMap();
    TempSummon* summon = new TempSummon(nullptr, player->GetGUID());
    if (!summon->Create(map->GenerateLowGuid<HighGuid::Unit>(), map, player->GetPhaseMask(),
        entry, 0, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation()))
    {
        delete summon;
        return nullptr;
    }
    summon->SetHomePosition(pos);
    summon->InitStats(despawnMs);
    summon->SetDisplayId(displayId);
    if (!map->AddToMap(summon->ToCreature(), true)) // owner is always a player here
    {
        delete summon;
        return nullptr;
    }
    summon->InitSummon();
    return summon;
}

static void ChainAmbushHit(ObjectGuid playerGuid, ObjectGuid targetGuid, uint32 ambush, int32 dmg)
{
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player || !player->IsInWorld())
        return;
    Unit* t = ObjectAccessor::GetUnit(*player, targetGuid);
    if (!t || !t->IsInWorld() || !t->IsAlive())
        return;
    Position pos;
    pos.Relocate(t->GetPositionX(), t->GetPositionY(), t->GetPositionZ(), t->GetOrientation());
    TempSummon* clone = SummonCloneWithDisplay(player, NPC_SHADOW_CLONE, pos, 1500, LookAlikeDisplayId(player));
    if (!clone)
        return;
    clone->SetOwnerGUID(player->GetGUID());
    clone->SetFaction(player->GetFaction());
    clone->SetLevel(player->GetLevel());
    // Mirror the player's weapons onto the clone -- Ambush is a
    // weapon-damage-based ability, so an unarmed clone deals ~0 real
    // damage from its own swing no matter what bp0 override is passed to
    // CastCustomSpell (that only replaces the flat-bonus effect).
    if (Item* mh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
        clone->SetVirtualItem(0, mh->GetEntry());
    if (Item* oh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        clone->SetVirtualItem(1, oh->GetEntry());
    if (Item* rh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
        clone->SetVirtualItem(2, rh->GetEntry());
    // Shadowform (15473) was tried here as a cosmetic dark-tint overlay per
    // request, but it's a full shapeshift/transform spell with its own
    // prominent "swirl" visual on application -- almost certainly what was
    // being seen and reported as a "level up"-looking flash on spawn.
    // Removed rather than guessed-at further without a way to see it live.

    if (t->GetCreatureType() == CREATURE_TYPE_HUMANOID)
        clone->CastSpell(t, SPELL_PICKPOCKET, true);
    clone->CastCustomSpell(t, ambush, &dmg, nullptr, nullptr, true,
        nullptr, nullptr, player->GetGUID());
    // Ambush is a weapon-damage-based ability -- CastCustomSpell's bp0
    // override only replaces its flat bonus effect, not the weapon-damage
    // component, and the clone has no weapon equipped, so the cast above
    // plays the impact visual but deals ~0 real damage. Force the actual
    // damage through directly so the chain hits for something real.
    if (t->IsAlive())
        Unit::DealDamage(clone, t, uint32(std::max(1, dmg)), nullptr, DIRECT_DAMAGE,
            SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
    // The clone (not the player) is the one actually swinging, so without
    // this the target's threat table only ever sees a summon that vanishes
    // a moment later -- it loses its target and tries to evade/reset even
    // though the player is standing right next to it fighting the chain.
    player->SetInCombatWith(t);
    if (t->IsAlive())
        t->GetThreatMgr().AddThreat(player, float(dmg), nullptr, true, true);
    clone->CastSpell(clone, SPELL_VANISH, true);
    clone->DespawnOrUnsummon(std::chrono::milliseconds(400));
}

// 2026-08-21: the 6s cooldown override used to live inside ChainAmbushImpl,
// which only runs when a chain-ambush target actually resolves -- if the
// player casts Shadowstep with no target, or the target guid stops
// resolving one tick later, that whole path was skipped and the real
// client-side Shadowstep cooldown (not present in our spell_dbc table at
// all -- it's a stock spell resolved from the compiled DBC, ~30s) was left
// standing. Split out into its own function that always runs, independent
// of whether the chain-ambush effect itself fires.
static void ApplyShadowstepCooldown(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    // Still has to be deferred rather than set from OnPlayerSpellCast
    // directly -- that fires mid-way through Shadowstep's own cast(), before
    // it's applied its own cooldown, so setting one there just gets
    // overwritten once cast() finishes. 300ms gives ample margin for the
    // engine's own real cooldown-set to have already happened.
    //
    // 2026-08-21: AddSpellCooldown's needSendToClient flag does NOT push a
    // live packet -- _AddSpellCooldown (Player.cpp) only ever *stores* it on
    // the cooldown entry, and the only place that flag is ever read is
    // SendInitialSpells, i.e. login/spec-change. So the server's own
    // castability was correctly set to 6s, but the client's action-bar
    // swirl never got corrected -- it kept counting down from the real
    // ~30s it originally predicted from the compiled DBC, and the client
    // blocks *sending* another cast attempt locally while it thinks a
    // spell is still on cooldown. Switched to ModifySpellCooldown, which
    // DOES send SMSG_MODIFY_COOLDOWN live -- read the real remaining
    // cooldown first and apply the exact delta needed to land on 6000ms.
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld())
            return;
        int32 const remaining = int32(p->GetSpellCooldownDelay(SPELL_SHADOWSTEP));
        int32 const delta = 6000 - remaining;
        if (delta != 0)
            p->ModifySpellCooldown(SPELL_SHADOWSTEP, delta);
    }, std::chrono::milliseconds(300));
}

static void ChainAmbushImpl(Player* player, Unit* first)
{
    if (!player || !first || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    // 2026-08-21: dropped the free Stealth grant here (called out as
    // "cheaty" -- Shadowstep chaining into free Stealth every time was too
    // strong). Cooldown override now applied unconditionally by
    // ApplyShadowstepCooldown() regardless of whether this chain-ambush path
    // runs -- see that function for why.
    uint32 ambush = BestOwned(player, SPELL_AMBUSH);
    if (!ambush)
        ambush = SPELL_AMBUSH;
    SpellInfo const* ambushInfo = sSpellMgr->GetSpellInfo(ambush);
    int32 dmg = int32(player->GetLevel() * 12.0f * AMBUSH_MULT);
    if (ambushInfo)
        dmg = int32(float(std::max(1, ambushInfo->Effects[EFFECT_0].CalcValue(player))) * AMBUSH_MULT);

    std::vector<ObjectGuid> targets;
    targets.push_back(first->GetGUID());
    std::list<Unit*> around;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, 40.0f);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, around, check);
    Cell::VisitObjects(player, searcher, 40.0f);
    for (Unit* u : around)
        if (u && u != first && u->IsAlive() && player->IsValidAttackTarget(u))
            targets.push_back(u->GetGUID());
    if (targets.empty())
        return;

    ObjectGuid playerGuid = player->GetGUID();
    for (uint32 i = 0; i < 8; ++i) // was 5, bumped 2026-08-21
    {
        ObjectGuid tg = targets[i % targets.size()];
        player->m_Events.AddEventAtOffset([playerGuid, tg, ambush, dmg]()
        {
            ChainAmbushHit(playerGuid, tg, ambush, dmg);
        }, std::chrono::milliseconds(200 * (i + 1)));
    }
}

// Public entry point: defers to the next world tick instead of running
// ChainAmbushImpl() synchronously. OnPlayerSpellCast fires mid-way through
// the triggering Spell::cast() call (before that spell's own effects have
// finished applying), so casting more spells on the same player from in
// here is a reentrant call into the aura/spell system on a unit that is
// still "in progress" -- this is what caused the recurring
// Unit::_AddAura assert "!m_cleanupDone" crashes (2026-08-20), even after
// removing the earlier NearTeleportTo loop. Scheduling the real work via
// m_Events lets the triggering Shadowstep cast finish and unwind first.
void ChainAmbush(Player* player, Unit* first)
{
    if (!player || !first || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    ObjectGuid firstGuid = first->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, firstGuid]()
    {
        Player* p = ObjectAccessor::FindPlayer(playerGuid);
        if (!p || !p->IsInWorld())
            return;
        Unit* t = ObjectAccessor::GetUnit(*p, firstGuid);
        if (!t || !t->IsInWorld())
            return;
        ChainAmbushImpl(p, t);
    }, std::chrono::milliseconds(1));
}

static void HemorrhageAoEImpl(ObjectGuid playerGuid)
{
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player || !player->IsInWorld() || GetClassPerk(player) != SPELL_SUBTLETY)
    {
        // TEMP DEBUG 2026-08-21: Hemorrhage AoE reported not working; see
        // ShouldHaveShadowDanceBuff for context. Remove once confirmed.
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff8800[Hemo debug]|r bail in Impl: inWorld={} GetClassPerk={}",
                player->IsInWorld(), GetClassPerk(player));
        return;
    }
    uint32 garrote = BestOwned(player, SPELL_GARROTE);
    if (!garrote)
        garrote = SPELL_GARROTE;
    std::list<Unit*> around;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, 10.0f);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, around, check);
    Cell::VisitObjects(player, searcher, 10.0f);
    uint32 hitCount = 0;
    for (Unit* u : around)
    {
        if (!u || !u->IsAlive() || !player->IsValidAttackTarget(u))
            continue;
        player->CastSpell(u, garrote, true);
        player->CastSpell(u, SPELL_PICKPOCKET, true);
        ++hitCount;
    }
    if (player->GetSession())
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff8800[Hemo debug]|r Impl ran: garrote={} nearby={} hit={}",
            garrote, around.size(), hitCount);
}

// Deferred the same way ChainAmbush is -- OnPlayerSpellCast fires mid-way
// through Hemorrhage's own Spell::cast(), and casting more spells on the
// same player from in there is a reentrant call into the aura/spell
// system on a unit that's still "in progress" (the established
// Unit::_AddAura assert crash this file's other deferred casts all guard
// against the same way).
void ApplyHemorrhageAoE(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    ObjectGuid playerGuid = player->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid]()
    {
        HemorrhageAoEImpl(playerGuid);
    }, std::chrono::milliseconds(1));
}

// Shadow Dance's +10% attack power half. Mirrors LivingGear_Next.cpp's
// existing Class Buffs pattern exactly (ShouldHaveClassBuff/
// ApplyClassBuffs/TickClassBuffs) -- self or any group member within 100
// yards having the perk grants it to this player, applied via a native
// stat-percent modifier rather than a real spell/aura.
bool ShouldHaveShadowDanceBuff(Player* player)
{
    if (!player || !player->IsAlive())
        return false;
    // TEMP DEBUG 2026-08-21: Shadow Dance reported not activating despite
    // DB confirming both GetClassPerk (lg_char_class_perk) and HasPerk
    // (lg_account_perk) should be true for the test account -- code audit
    // found nothing wrong statically, so report the live values actually
    // seen here. Remove once confirmed working.
    static uint32 s_debugTick = 0;
    uint32 const now = getMSTime();
    bool const dbg = player->GetSession()
        && (!s_debugTick || getMSTimeDiff(s_debugTick, now) >= 3000);
    if (dbg)
    {
        s_debugTick = now;
        uint32 const cp = GetClassPerk(player);
        bool const hp = HasPerk(player, SPELL_SHADOW_DANCE);
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff8800[SD debug]|r self: GetClassPerk={} (SPELL_SUBTLETY={}) HasPerk(SHADOW_DANCE)={}",
            cp, SPELL_SUBTLETY, hp);
    }
    if (GetClassPerk(player) == SPELL_SUBTLETY && HasPerk(player, SPELL_SHADOW_DANCE))
        return true;
    Group* group = player->GetGroup();
    if (!group)
        return false;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || member == player || !member->IsInWorld())
            continue;
        if (!player->IsInMap(member) || !player->IsWithinDistInMap(member, 100.0f))
            continue;
        if (GetClassPerk(member) == SPELL_SUBTLETY && HasPerk(member, SPELL_SHADOW_DANCE))
            return true;
    }
    return false;
}

void ApplyShadowDanceBuff(Player* player, bool apply)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    // TEMP DEBUG 2026-08-21: gating condition confirmed true every tick,
    // but "no buff" still reported -- this is the one untested step past
    // that. It's also a raw stat modifier with no aura icon by design, so
    // even a successful apply has zero visible feedback -- print AP before/
    // after so there's an unambiguous signal either way.
    bool const wasOn = g_shadowDanceBuffOn[guid];
    static uint32 s_applyDebugTick = 0;
    uint32 const applyNow = getMSTime();
    bool const applyDbg = player->GetSession()
        && (!s_applyDebugTick || getMSTimeDiff(s_applyDebugTick, applyNow) >= 3000);
    if (applyDbg)
    {
        s_applyDebugTick = applyNow;
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff8800[SD debug]|r ApplyShadowDanceBuff: apply={} wasOn={} AP_before={}",
            apply, wasOn, player->GetTotalAttackPowerValue(BASE_ATTACK));
    }
    if (wasOn == apply)
        return;
    g_shadowDanceBuffOn[guid] = apply;
    player->ApplyStatPctModifier(UNIT_MOD_ATTACK_POWER, TOTAL_PCT, apply ? 10.0f : -10.0f);
    if (player->GetSession())
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff8800[SD debug]|r ApplyShadowDanceBuff: AP_after={}",
            player->GetTotalAttackPowerValue(BASE_ATTACK));
}

void TickShadowDanceBuff(Player* player)
{
    if (!player || !player->GetSession())
        return;
    ApplyShadowDanceBuff(player, ShouldHaveShadowDanceBuff(player));
}

// Shadow Dance's "openers usable without stealth" half. Called from a
// small core patch in Spell::CheckCast (Spell.cpp) -- self only, does NOT
// extend to the whole party like the attack power buff above, since this
// is about the caster's own ability to act, not a shared buff.
bool BypassStealthRequirement(Unit* caster)
{
    if (!caster)
        return false;
    Player* player = caster->ToPlayer();
    if (!player)
        return false;
    bool const result = GetClassPerk(player) == SPELL_SUBTLETY && HasPerk(player, SPELL_SHADOW_DANCE);
    // TEMP DEBUG 2026-08-21: see ShouldHaveShadowDanceBuff for context.
    if (player->GetSession())
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff8800[SD debug]|r stealth-bypass check: result={}", result);
    return result;
}

// Rebuilt 2026-08-20/21: was a bare TempSummon + ScriptedAI ("not a real
// playerbot" -- mod-playerbots hard-requires a real Player+WorldSession, so
// that door is closed). This is a genuinely different approach: a real
// SUMMON_PET-type Pet (same engine object model as a Warlock demon --
// Spell::EffectSummonPet is the reference this mirrors), which gets the
// native pet frame with real Aggressive/Defensive/Passive stance buttons
// for free -- that's what actually gives the player aggro-level control,
// no custom UI needed. npc_lg_shadow_cloneAI (below) still drives target
// selection and which Rogue ability to cast, but now gates its own
// eagerness to engage on me->GetReactState() so the stance buttons
// actually mean something.
void SummonClone(Player* player)
{
    if (!player || GetClassPerk(player) != SPELL_SUBTLETY)
        return;
    auto it = g_cloneGuid.find(player->GetGUID().GetCounter());
    if (it != g_cloneGuid.end())
        if (Creature* c = player->GetMap()->GetCreature(it->second))
            c->DespawnOrUnsummon();
    if (Pet* existing = player->GetPet())
        existing->Remove(PET_SAVE_AS_DELETED);

    Pet* pet = player->SummonPet(NPC_SHADOW_CLONE, player->GetPositionX(), player->GetPositionY(),
        player->GetPositionZ(), player->GetOrientation(), SUMMON_PET);
    if (!pet)
        return;

    // Same model-swap-glitch note as before this rework: set the display
    // before anything else touches the pet, while it's still brand new.
    pet->SetDisplayId(LookAlikeDisplayId(player));
    pet->SetNativeDisplayId(LookAlikeDisplayId(player));
    pet->SetUInt32Value(UNIT_CREATED_BY_SPELL, SPELL_SHADOW_CLONE);
    pet->SetReactState(REACT_DEFENSIVE);
    pet->SetFaction(player->GetFaction());
    pet->SetLevel(player->GetLevel());
    pet->SetMaxHealth(player->GetMaxHealth());
    pet->SetHealth(player->GetMaxHealth());
    if (Item* mh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
        pet->SetVirtualItem(0, mh->GetEntry());
    if (Item* oh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        pet->SetVirtualItem(1, oh->GetEntry());
    if (Item* rh = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
        pet->SetVirtualItem(2, rh->GetEntry());
    pet->SetPower(POWER_ENERGY, player->GetMaxPower(POWER_ENERGY));
    // SetVirtualItem above is purely cosmetic (what weapon model it holds) --
    // a Creature/Pet's actual melee damage comes from its own weapon-damage
    // stat, not from any item, so without this it hits for whatever the
    // 910201 creature_template's near-nothing base damage is regardless of
    // what it's visibly holding. Copy the owner's real current weapon
    // damage range directly.
    pet->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, player->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE));
    pet->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, player->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE));
    pet->UpdateDamagePhysical(BASE_ATTACK);
    player->PetSpellInitialize(); // native pet frame: stance + follow/stay/attack commands
    g_cloneGuid[player->GetGUID().GetCounter()] = pet->GetGUID();
}

void TickCooking(Player* player, uint32 diff)
{
    if (!player)
        return;
    // TEMP DEBUG 2026-08-21: cooking regen reported not working.
    static uint32 s_debugTick = 0;
    uint32 const dbgNow = getMSTime();
    bool const dbg = player->GetSession()
        && (!s_debugTick || getMSTimeDiff(s_debugTick, dbgNow) >= 3000);
    if (dbg)
        s_debugTick = dbgNow;
    if (player->IsInCombat())
    {
        if (dbg) ChatHandler(player->GetSession()).PSendSysMessage("|cffff8800[Cook debug]|r bail: in combat");
        return;
    }
    uint32 acc = player->GetGUID().GetCounter();
    g_cookAcc[acc] += diff;
    if (g_cookAcc[acc] < COOK_MS)
        return;
    g_cookAcc[acc] = 0;
    uint32 skill = AccountMaxSkill(player, SKILL_COOKING);
    int pct = 0;
    if (skill >= 450 && HasPerk(player, SPELL_COOK[5]))
        pct = 6;
    else if (skill >= 375 && HasPerk(player, SPELL_COOK[4]))
        pct = 5;
    else if (skill >= 300 && HasPerk(player, SPELL_COOK[3]))
        pct = 4;
    else if (skill >= 225 && HasPerk(player, SPELL_COOK[2]))
        pct = 3;
    else if (skill >= 150 && HasPerk(player, SPELL_COOK[1]))
        pct = 2;
    else if (skill >= 75 && HasPerk(player, SPELL_COOK[0]))
        pct = 1;
    if (player->GetSession())
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff8800[Cook debug]|r tick: skill={} pct={} HasPerk[0..5]={},{},{},{},{},{}",
            skill, pct,
            HasPerk(player, SPELL_COOK[0]), HasPerk(player, SPELL_COOK[1]), HasPerk(player, SPELL_COOK[2]),
            HasPerk(player, SPELL_COOK[3]), HasPerk(player, SPELL_COOK[4]), HasPerk(player, SPELL_COOK[5]));
    if (!pct)
        return;
    uint32 hp = player->CountPctFromMaxHealth(pct);
    if (player->GetHealth() < player->GetMaxHealth())
        player->ModifyHealth(int32(hp));
    uint32 maxMana = player->GetMaxPower(POWER_MANA);
    if (maxMana)
    {
        int32 mana = int32(CalculatePct(maxMana, pct));
        if (player->GetPower(POWER_MANA) < int32(maxMana))
            player->ModifyPower(POWER_MANA, mana);
    }
}

void CatchUpProfession(Player* player)
{
    if (!player || !player->GetSession())
        return;
    // These three were fully implemented and mechanically sound but never
    // actually granted to anyone (found in a 2026-08-21 audit after the
    // Autoloot/Quests-Finish 910008/910090 instances of this same bug) --
    // no documented unlock condition for any of them, so grant unconditionally.
    UnlockPerk(player, SPELL_COMBO, nullptr);
    UnlockPerk(player, SPELL_ARMORY, nullptr);
    UnlockPerk(player, SPELL_SOLO_QUEUE, nullptr);
    UnlockPerk(player, SPELL_PULL_RADIUS, nullptr);
    UnlockPerk(player, SPELL_TRACK_ORE, nullptr);
    UnlockPerk(player, SPELL_TRACK_HERB, nullptr);
    if (player->GetRewardedQuestCount() >= 50)
        UnlockPerk(player, SPELL_FIND_QUESTS, "|cff66ccff[Account Perks]|r *Quests - Find unlocked!");
    uint32 craft = 0;
    uint32 const skills[] = {
        SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_LEATHERWORKING, SKILL_TAILORING,
        SKILL_ENGINEERING, SKILL_ENCHANTING, SKILL_JEWELCRAFTING, SKILL_INSCRIPTION,
        SKILL_COOKING
    };
    for (uint32 sk : skills)
        craft = std::max(craft, AccountMaxSkill(player, sk));
    uint32 need[] = { 75, 150, 225, 300, 375 };
    for (uint32 i = 0; i < 5; ++i)
        if (craft >= need[i])
            UnlockPerk(player, SPELL_CRAFT[i], nullptr);
    uint32 cook = AccountMaxSkill(player, SKILL_COOKING);
    uint32 cookNeed[] = { 75, 150, 225, 300, 375, 450 };
    for (uint32 i = 0; i < 6; ++i)
        if (cook >= cookNeed[i])
            UnlockPerk(player, SPELL_COOK[i], nullptr);
    uint32 maxLv = AccountMaxLevel(player);
    if (maxLv >= 10)
        UnlockPerk(player, SPELL_SWIM, nullptr);
    uint32 travelNeed[] = { 20, 40, 60, 70, 80 };
    for (uint32 i = 0; i < 5; ++i)
        if (maxLv >= travelNeed[i])
            UnlockPerk(player, SPELL_TRAVEL[i], nullptr);
    if (maxLv >= 40)
        UnlockPerk(player, SPELL_MOUNTED_OPENER, nullptr);
    if (PickMount(player))
        UnlockPerk(player, SPELL_AUTO_MOUNT, nullptr);
    if (QueryResult att = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `lg_absorb` WHERE `account_id` = {}",
        player->GetSession()->GetAccountId()))
        if ((*att)[0].Get<uint64>() >= 1000)
            UnlockPerk(player, SPELL_CURATOR, nullptr);
}

// Curator's 5 lowest-attunement bag/bank/armory pieces are treated as if
// equipped for attunement purposes (2026-08-20 redesign, see Bonesaw.md):
// each gets +1 xp per tick, rolled over and banked through the exact same
// LivingGear_GrantItemXp path an equipped item's kill XP goes through. The
// selection filter is `level < 25`, not `< 50` -- once a piece reaches
// attunement's cap level it stops adding anything further to the account
// (see LivingGear.cpp AbsorbPctForLevel), so there's no reason for
// Curator's limited tick budget to keep growing it past that instead of
// moving on to the next least-attuned piece. 25 must match LivingGear.cpp's
// `LivingGear.Attune.CapLevel` config (default 25) if that's ever changed.
void TickCurator(Player* player, uint32 diff)
{
    if (!player || !HasPerk(player, SPELL_CURATOR))
        return;
    uint32 id = player->GetGUID().GetCounter();
    g_curatorAcc[id] += diff;
    if (g_curatorAcc[id] < g_cfg.curatorTick)
        return;
    g_curatorAcc[id] = 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `item_guid` FROM `lg_item` WHERE `owner_guid` = {} AND `level` < 25 "
        "ORDER BY `level` ASC, `xp` ASC LIMIT 5", player->GetGUID().GetCounter()))
    {
        do
        {
            uint32 const itemGuid = (*result)[0].Get<uint32>();
            LivingGear_GrantItemXp(player, itemGuid, 1);
        } while (result->NextRow());
    }
}

void CheckDungeonClear(Player* player, Creature* killed)
{
    if (!player || !killed || !player->GetMap() || !player->GetMap()->IsDungeon())
        return;
    if (!killed->IsDungeonBoss())
        return;
    Map* map = player->GetMap();
    uint32 inst = map->GetInstanceId();
    if (!g_dungeonStart[inst])
        g_dungeonStart[inst] = getMSTime();
    bool allDead = true;
    if (InstanceMap* im = map->ToInstanceMap())
        if (InstanceScript* instScr = im->GetInstanceScript())
            allDead = instScr->AllBossesDone();
    if (!allDead || g_dungeonDone[inst])
        return;
    g_dungeonDone[inst] = true;
    uint32 elapsed = getMSTimeDiff(g_dungeonStart[inst], getMSTime()) / 1000;
    uint32 par = g_cfg.dungeonPar;
    if (QueryResult q = WorldDatabase.Query(
        "SELECT `par_sec` FROM `lg_dungeon_par` WHERE `map_id` = {} AND `difficulty` = {}",
        map->GetId(), map->GetDifficulty()))
        par = (*q)[0].Get<uint32>();
    int32 speedBp = 0;
    int32 paceBp = 0;
    uint32 dur = 0;
    if (elapsed * 2 <= par)
    {
        speedBp = 29;
        paceBp = 49;
        dur = 1800000;
    }
    else if (elapsed * 4 <= par * 3)
    {
        speedBp = 19;
        paceBp = 29;
        dur = 1200000;
    }
    else if (elapsed <= par)
    {
        speedBp = 9;
        paceBp = 14;
        dur = 900000;
    }
    if (!speedBp)
        return;
    Map::PlayerList const& plist = map->GetPlayers();
    for (auto const& ref : plist)
    {
        Player* m = ref.GetSource();
        if (!m)
            continue;
        m->CastCustomSpell(m, SPELL_DUNGEON_SPEED, &speedBp, &speedBp, nullptr, true);
        m->CastCustomSpell(m, SPELL_DUNGEON_PACE, &paceBp, nullptr, nullptr, true);
        if (Aura* a = m->GetAura(SPELL_DUNGEON_SPEED))
            a->SetDuration(int32(dur));
        if (Aura* a = m->GetAura(SPELL_DUNGEON_PACE))
            a->SetDuration(int32(dur));
        SendLine(m, Acore::StringFormat("DTIMER|clear|{}|{}|{}|{}|{}",
            elapsed, speedBp, speedBp + 1, paceBp + 1, dur / 1000));
    }
}

void SendArmory(Player* player)
{
    if (!player || !player->GetSession())
        return;
    SendLine(player, "ARMCLR");
    if (QueryResult q = CharacterDatabase.Query(
        "SELECT `item_entry`, `item_level`, `str`, `agi`, `sta`, `intel`, `spi`, `armor` "
        "FROM `lg_absorb` WHERE `account_id` = {} LIMIT 80",
        player->GetSession()->GetAccountId()))
    {
        do
        {
            Field* f = q->Fetch();
            uint32 entry = f[0].Get<uint32>();
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            char const* name = proto ? proto->Name1.c_str() : "Item";
            uint32 slot = proto ? proto->InventoryType : 0;
            SendLine(player, Acore::StringFormat("ARM|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                slot, entry, f[1].Get<uint32>(), int32(f[2].Get<float>()), int32(f[3].Get<float>()),
                int32(f[4].Get<float>()), int32(f[5].Get<float>()), int32(f[6].Get<float>()), name));
        } while (q->NextRow());
    }
    SendLine(player, "ARMEND");
}

void FindQuests(Player* player)
{
    if (!player)
        return;
    std::list<Creature*> list;
    Acore::AllFriendlyCreaturesInGrid check(player);
    Acore::CreatureListSearcher<Acore::AllFriendlyCreaturesInGrid> searcher(player, list, check);
    Cell::VisitObjects(player, searcher, 60.0f);
    uint32 n = 0;
    for (Creature* c : list)
    {
        if (!c || !c->IsQuestGiver())
            continue;
        QuestRelationBounds bounds = sObjectMgr->GetCreatureQuestRelationBounds(c->GetEntry());
        for (auto i = bounds.first; i != bounds.second && n < 8; ++i)
        {
            Quest const* q = sObjectMgr->GetQuestTemplate(i->second);
            if (!q || player->GetQuestStatus(q->GetQuestId()) != QUEST_STATUS_NONE)
                continue;
            if (!player->CanTakeQuest(q, false))
                continue;
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff66ccff[Quests]|r {} -> {}", c->GetName(), q->GetTitle());
            ++n;
        }
    }
    if (!n)
        ChatHandler(player->GetSession()).SendSysMessage("|cff66ccff[Quests]|r No nearby quests.");
}

void AutoQuestFinish(Player* player)
{
    if (!player)
        return;
    uint32 spawned = 0;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE && spawned < 10; ++slot)
    {
        uint32 qid = player->GetQuestSlotQuestId(slot);
        if (!qid || player->GetQuestStatus(qid) != QUEST_STATUS_COMPLETE)
            continue;
        uint32 npc = 0;
        if (QueryResult q = WorldDatabase.Query(
            "SELECT `id` FROM `creature_questender` WHERE `quest` = {} LIMIT 1", qid))
            npc = (*q)[0].Get<uint32>();
        if (!npc)
            continue;
        if (player->SummonCreature(npc, player->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 60000))
            ++spawned;
    }
    Say(player, Acore::StringFormat("[Quests] Summoned {} turn-in NPC(s).", spawned).c_str());
}

bool HandleLgChat(Player* player, std::string msg)
{
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    uint32 acc = player->GetSession()->GetAccountId();
    uint32 v = 0;
    uint32 slot = 0;
    uint32 entry = 0;
    if (sscanf(msg.c_str(), "CHATSET|%u", &v) == 1)
    {
        g_chatOn[acc] = v != 0;
        return true;
    }
    if (sscanf(msg.c_str(), "AMSET|%u", &v) == 1)
    {
        g_autoMountOn[acc] = v != 0;
        DetectSchema();
        if (g_hasAutoMountCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `auto_mount`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `auto_mount` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        SendLine(player, Acore::StringFormat("AM|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "PULLSET|%u", &v) == 1)
    {
        g_pullRadiusOn[acc] = v != 0;
        DetectSchema();
        if (g_hasPullRadiusCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `pull_radius`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `pull_radius` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        if (!v)
            player->RemoveAurasDueToSpell(SPELL_PULL_RADIUS);
        SendLine(player, Acore::StringFormat("PULL|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "TRACKORESET|%u", &v) == 1)
    {
        g_trackOreOn[acc] = v != 0;
        DetectSchema();
        if (g_hasTrackOreCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `track_ore`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `track_ore` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        if (!v)
            player->RemoveAurasDueToSpell(NATIVE_FIND_MINERALS);
        SendLine(player, Acore::StringFormat("TRACKORE|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "TRACKHERBSET|%u", &v) == 1)
    {
        g_trackHerbOn[acc] = v != 0;
        DetectSchema();
        if (g_hasTrackHerbCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `track_herb`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `track_herb` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        if (!v)
            player->RemoveAurasDueToSpell(NATIVE_FIND_HERBS);
        SendLine(player, Acore::StringFormat("TRACKHERB|{}", v ? 1 : 0));
        return true;
    }
    if (sscanf(msg.c_str(), "SOLOSET|%u", &v) == 1)
    {
        g_soloQueue[acc] = v != 0;
        DetectSchema();
        if (g_hasSoloCol)
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `solo_queue`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `solo_queue` = {}",
                acc, v ? 1 : 0, v ? 1 : 0);
        SendLine(player, Acore::StringFormat("SQ|{}", v ? 1 : 0));
        return true;
    }
    if (msg == "ARMOPEN")
    {
        SendArmory(player);
        return true;
    }
    if (sscanf(msg.c_str(), "ARMEQUIP|%u|%u", &slot, &entry) == 2)
    {
        // The client only ever offers entries from the account's own
        // attuned list, but a whisper addon message is not a trusted
        // boundary -- without this check, a crafted ARMEQUIP message could
        // conjure any item entry for free. Require the account to actually
        // have it attuned first.
        if (QueryResult q = CharacterDatabase.Query(
            "SELECT 1 FROM `lg_absorb` WHERE `account_id` = {} AND `item_entry` = {}", acc, entry))
        {
            Item* created = nullptr;
            uint16 dest = 0;
            if (player->CanEquipNewItem(uint8(slot), dest, entry, true) == EQUIP_ERR_OK)
                created = player->EquipNewItem(dest, entry, true);
            else
            {
                ItemPosCountVec destVec;
                if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destVec, entry, 1) == EQUIP_ERR_OK)
                    created = player->StoreNewItem(destVec, entry, true);
            }
            // Recreated copies are soulbound -- they're a free re-materialization
            // of something already permanently converted to account stats, not
            // a tradeable item, so this closes off selling/trading duplicates.
            if (created)
                created->SetBinding(true);
        }
        return true;
    }
    return false;
}

class PerksPlayer : public PlayerScript
{
public:
    PerksPlayer() : PlayerScript("LivingGearPerksPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_UPDATE_ZONE,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_PLAYER_LEAVE_COMBAT,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
        PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL,
        PLAYERHOOK_ON_AFTER_SPEC_SLOT_CHANGED
    }) { }

    // Subtlety-gated grants (Shadowstep + Shadow Dance + Shadow Clone).
    // Called at login AND on a live dual-spec swap -- these used to only
    // run at login, so swapping into Subtlety mid-session without relogging
    // left Shadow Dance ungranted (HasPerk stayed false forever, even
    // though the AP-buff tick already re-checks spec live every second).
    static void GrantSubtletyPerks(Player* player)
    {
        if (GetClassPerk(player) != SPELL_SUBTLETY)
            return;
        player->learnSpell(SPELL_SHADOWSTEP);
        // UnlockPerk (not raw learnSpell) so the client's db.perks
        // actually gets a PK|id|1 for these -- otherwise the addon UI
        // can never show them as known even though the character has
        // them, since nothing else ever tells the client. learnSpellToo=false:
        // pure flags, no CASTABLE_SPELLS entry -- see UnlockPerk's comment.
        UnlockPerk(player, SPELL_SHADOW_DANCE, nullptr, false);
        UnlockPerk(player, SPELL_SHADOW_CLONE, nullptr, false);
    }

    void OnPlayerAfterSpecSlotChanged(Player* player, uint8 /*newSlot*/) override
    {
        if (player)
            GrantSubtletyPerks(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        DetectSchema();
        uint32 acc = player->GetSession()->GetAccountId();
        LoadPerks(acc);
        if (!g_perks[acc].empty())
        {
            // WotLK's addon-whisper channel silently truncates messages
            // around ~255 bytes -- a single account can have 50+ unlocked
            // perks, so one giant "PKALL|910002,910003,..." line can blow
            // past that and drop whatever IDs land after the cutoff (e.g.
            // Auto-Mount, 910105, always unlocking server-side but never
            // reaching db.perks client-side, so its World Perks toggle
            // looked locked and clicking it silently did nothing). The
            // client's PKALL handler is purely additive (no db.perks = {}
            // reset), so it's safe to split across multiple PKALL sends.
            std::string ids;
            for (uint32 spellId : g_perks[acc])
            {
                std::string next = std::to_string(spellId);
                if (!ids.empty() && ids.size() + 1 + next.size() > 200)
                {
                    SendLine(player, "PKALL|" + ids);
                    ids.clear();
                }
                if (!ids.empty())
                    ids += ',';
                ids += next;
            }
            if (!ids.empty())
                SendLine(player, "PKALL|" + ids);
        }
        CatchUpProfession(player);
        if (g_hasAutoMountCol)
        {
            g_autoMountOn[acc] = false;
            if (QueryResult q = CharacterDatabase.Query(
                "SELECT `auto_mount` FROM `lg_account_meta` WHERE `account_id` = {}", acc))
                g_autoMountOn[acc] = (*q)[0].Get<uint32>() != 0;
        }
        else if (g_autoMountOn.find(acc) == g_autoMountOn.end())
            g_autoMountOn[acc] = true;
        if (g_hasPullRadiusCol)
        {
            g_pullRadiusOn[acc] = false;
            if (QueryResult q = CharacterDatabase.Query(
                "SELECT `pull_radius` FROM `lg_account_meta` WHERE `account_id` = {}", acc))
                g_pullRadiusOn[acc] = (*q)[0].Get<uint32>() != 0;
        }
        if (g_hasTrackOreCol)
        {
            g_trackOreOn[acc] = false;
            if (QueryResult q = CharacterDatabase.Query(
                "SELECT `track_ore` FROM `lg_account_meta` WHERE `account_id` = {}", acc))
                g_trackOreOn[acc] = (*q)[0].Get<uint32>() != 0;
        }
        if (g_hasTrackHerbCol)
        {
            g_trackHerbOn[acc] = false;
            if (QueryResult q = CharacterDatabase.Query(
                "SELECT `track_herb` FROM `lg_account_meta` WHERE `account_id` = {}", acc))
                g_trackHerbOn[acc] = (*q)[0].Get<uint32>() != 0;
        }
        if (HasPerk(player, SPELL_SWIM))
            player->CastSpell(player, SPELL_SWIM, true);
        GrantSubtletyPerks(player);
        RecastCombo(player);
        NotifyZoneScale(player);
        SendLine(player, Acore::StringFormat("AM|{}", g_autoMountOn[acc] ? 1 : 0));
        SendLine(player, Acore::StringFormat("PULL|{}", g_pullRadiusOn[acc] ? 1 : 0));
        SendLine(player, Acore::StringFormat("TRACKORE|{}", g_trackOreOn[acc] ? 1 : 0));
        SendLine(player, Acore::StringFormat("TRACKHERB|{}", g_trackHerbOn[acc] ? 1 : 0));
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        uint32 g = player->GetGUID().GetCounter();
        g_combo.erase(g);
        g_comboTick.erase(g);
        g_pullRadiusTick.erase(g);
        g_trackOreTick.erase(g);
        g_trackHerbTick.erase(g);
        g_cloneGuid.erase(g);
        g_shadowDanceBuffOn.erase(g);
        g_shadowDanceTick.erase(g);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        TickCooking(player, diff);
        TickCurator(player, diff);
        {
            uint32 const id = player->GetGUID().GetCounter();
            g_shadowDanceTick[id] += diff;
            if (g_shadowDanceTick[id] >= 1000)
            {
                g_shadowDanceTick[id] = 0;
                TickShadowDanceBuff(player);
            }
        }
        if (HasPerk(player, SPELL_COMBO))
        {
            uint32 id = player->GetGUID().GetCounter();
            g_comboTick[id] += diff;
            if (g_comboTick[id] >= 1000)
            {
                g_comboTick[id] = 0;
                RecastCombo(player);
            }
        }
        if (player->GetSession() && g_pullRadiusOn[player->GetSession()->GetAccountId()]
            && HasPerk(player, SPELL_PULL_RADIUS))
        {
            uint32 id = player->GetGUID().GetCounter();
            g_pullRadiusTick[id] += diff;
            // SPELL_PULL_RADIUS's own duration is short (matches SPELL_COMBO's
            // DurationIndex) -- refresh well inside that window so the aura
            // never actually lapses between ticks.
            if (g_pullRadiusTick[id] >= 10000)
            {
                g_pullRadiusTick[id] = 0;
                player->CastSpell(player, SPELL_PULL_RADIUS, true);
            }
        }
        if (player->GetSession() && g_trackOreOn[player->GetSession()->GetAccountId()]
            && HasPerk(player, SPELL_TRACK_ORE))
        {
            uint32 id = player->GetGUID().GetCounter();
            g_trackOreTick[id] += diff;
            if (g_trackOreTick[id] >= 10000)
            {
                g_trackOreTick[id] = 0;
                player->CastSpell(player, NATIVE_FIND_MINERALS, true);
            }
        }
        if (player->GetSession() && g_trackHerbOn[player->GetSession()->GetAccountId()]
            && HasPerk(player, SPELL_TRACK_HERB))
        {
            uint32 id = player->GetGUID().GetCounter();
            g_trackHerbTick[id] += diff;
            if (g_trackHerbTick[id] >= 10000)
            {
                g_trackHerbTick[id] = 0;
                player->CastSpell(player, NATIVE_FIND_HERBS, true);
            }
        }
    }

    void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        NotifyZoneScale(player);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!player || !player->GetMap())
            return;
        NotifyZoneScale(player);
        if (player->GetMap()->IsDungeon())
        {
            uint32 inst = player->GetMap()->GetInstanceId();
            if (!g_dungeonStart[inst])
            {
                g_dungeonStart[inst] = getMSTime();
                SendLine(player, Acore::StringFormat("DTIMER|start|{}", g_cfg.dungeonPar));
            }
        }
        else
        {
            SendLine(player, "DTIMER|stop");
        }
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        AddCombo(killer);
        CheckDungeonClear(killer, killed);
        GrantScaledGreyKillXP(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        AddCombo(owner);
        CheckDungeonClear(owner, killed);
        GrantScaledGreyKillXP(owner, killed);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (!player || !amount)
            return;
        float mult = ZoneRewardMult(player);
        uint32 stacks = ComboCount(player);
        if (HasPerk(player, SPELL_COMBO) && stacks)
            mult *= 1.0f + COMBO_XP_PCT_PER_STACK * float(stacks);
        if (Aura* pace = player->GetAura(SPELL_DUNGEON_PACE))
            if (AuraEffect* e = pace->GetEffect(EFFECT_0))
                mult *= 1.0f + float(e->GetAmount() + 1) / 100.0f;
        amount = uint32(float(amount) * mult);
        if (!amount)
            amount = 1;
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skip*/) override
    {
        if (!player || !spell)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (info->HasAura(SPELL_AURA_MOUNTED))
            g_lastMount[player->GetGUID().GetCounter()] = info->Id;
        if (info->Id == SPELL_SHADOWSTEP && GetClassPerk(player) == SPELL_SUBTLETY)
        {
            ApplyShadowstepCooldown(player);
            Unit* t = spell->m_targets.GetUnitTarget();
            if (t)
                ChainAmbush(player, t);
        }
        if (GetClassPerk(player) == SPELL_SUBTLETY
            && sSpellMgr->GetFirstSpellInChain(info->Id) == SPELL_HEMORRHAGE)
            ApplyHemorrhageAoE(player);
        // Shadow Clone pet dropped 2026-08-21 -- the SUMMON_PET rework
        // (real pet frame, stance buttons) didn't play the way the user
        // wanted. SummonClone()/npc_lg_shadow_clone are kept but now
        // unreachable, same "inert isn't broken" precedent as the old
        // SacrificeItem path. Casting *Shadow Clone now does nothing;
        // Shadowstep's own chain-ambush effect (ChainAmbush, unrelated
        // code path, its own short-lived per-hit visual clone) is
        // untouched and is where the real improvements went instead.
        if (info->Id == 8690 || info->Id == 556) // Hearthstone / Astral Recall
        {
            // 2026-08-21: Travel used to make Hearthstone a fully instant,
            // no-cooldown teleport once unlocked -- a real "teleportation
            // stone," not a gradual reduction. Restoring that instead of
            // the partial per-rank scaling this got rewritten into earlier
            // this session. Cast time is zeroed unconditionally in
            // OnSpellPrepare above; this clears the cooldown entirely.
            uint32 travelRanks = 0;
            for (uint32 id : SPELL_TRAVEL)
                if (HasPerk(player, id))
                    ++travelRanks;
            if (travelRanks)
            {
                uint32 const spellId = info->Id;
                ObjectGuid playerGuid = player->GetGUID();
                // Still deferred -- OnPlayerSpellCast fires mid-way through
                // the triggering Spell::cast(), before the engine has
                // written its own real cooldown entry, so clearing it here
                // synchronously would just get overwritten once cast()
                // finishes and sets the real ~30 min cooldown.
                player->m_Events.AddEventAtOffset([playerGuid, spellId]()
                {
                    Player* p = ObjectAccessor::FindPlayer(playerGuid);
                    if (p && p->IsInWorld())
                        p->RemoveSpellCooldown(spellId, true);
                }, std::chrono::milliseconds(300));
            }
        }
        if (info->Id == SPELL_FIND_QUESTS)
            FindQuests(player);
        if (info->Id == SPELL_AUTO_QUEST)
            AutoQuestFinish(player);
        if (info->Id == SPELL_ARMORY)
            SendArmory(player);
        if (info->Id == SPELL_SOLO_QUEUE)
        {
            uint32 acc = player->GetSession()->GetAccountId();
            g_soloQueue[acc] = !g_soloQueue[acc];
            SendLine(player, Acore::StringFormat("SQ|{}", g_soloQueue[acc] ? 1 : 0));
        }
        if (info->Id == SPELL_AUTO_MOUNT)
        {
            uint32 acc = player->GetSession()->GetAccountId();
            g_autoMountOn[acc] = !g_autoMountOn[acc];
            SendLine(player, Acore::StringFormat("AM|{}", g_autoMountOn[acc] ? 1 : 0));
        }
        if (info->Id == SPELL_MOUNTED_OPENER && player->IsMounted())
        {
            std::list<Unit*> list;
            Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, 20.0f);
            Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(
                player, list, check);
            Cell::VisitObjects(player, searcher, 20.0f);
            for (Unit* u : list)
                if (u && u->IsAlive())
                    u->GetMotionMaster()->MoveJump(player->GetPositionX(), player->GetPositionY(),
                        player->GetPositionZ() + 0.5f, 20.0f, 8.0f);
            player->CastSpell(player, SPELL_THUNDER_CLAP, true);
        }
        if (GetClassPerk(player) == SPELL_SUBTLETY && info->SpellFamilyName == SPELLFAMILY_ROGUE
            && info->Id != SPELL_SHADOW_CLONE)
        {
            auto it = g_cloneGuid.find(player->GetGUID().GetCounter());
            if (it != g_cloneGuid.end())
                if (Creature* clone = player->GetMap()->GetCreature(it->second))
                    if (Unit* t = spell->m_targets.GetUnitTarget())
                        clone->CastSpell(t, info->Id, true);
        }
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
            if (info->HasAura(SPELL_AURA_MOUNTED))
                UnlockPerk(player, SPELL_AUTO_MOUNT, "[Account Perks] Auto-Mount unlocked.");
    }

    void OnPlayerLeaveCombat(Player* player) override
    {
        TryAutoMount(player);
    }

    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* /*skill*/,
        uint32 /*current_level*/, uint32& /*gain*/) override
    {
        CatchUpProfession(player);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg,
        Player* /*receiver*/) override
    {
        if (!player || language != LANG_ADDON || type != CHAT_MSG_WHISPER)
            return true;
        if (HandleLgChat(player, msg))
            return false;
        return true;
    }
};

class PerksSpell : public AllSpellScript
{
public:
    PerksSpell() : AllSpellScript("LivingGearPerksSpell", {
        ALLSPELLHOOK_ON_PREPARE
    }) { }

    void OnSpellPrepare(Spell* spell, Unit* caster, SpellInfo const* info) override
    {
        InstantAndMoveMount(spell, info);
        if (!caster || !caster->IsPlayer() || !info)
            return;
        Player* player = caster->ToPlayer();
        uint32 travel = 0;
        for (uint32 id : SPELL_TRAVEL)
            if (HasPerk(player, id))
                ++travel;
        // 2026-08-21: full instant cast once Travel is unlocked at all
        // ("teleportation stone"), not a gradual per-rank reduction.
        if (travel && (info->Id == 8690 || info->Id == 556))
            spell->SetCastTime(0);
        uint32 ranks = 0;
        for (uint32 id : SPELL_CRAFT)
            if (HasPerk(player, id))
                ++ranks;
        if (!ranks)
            return;
        bool craft = info->IsAbilityOfSkillType(SKILL_ALCHEMY)
            || info->IsAbilityOfSkillType(SKILL_BLACKSMITHING)
            || info->IsAbilityOfSkillType(SKILL_LEATHERWORKING)
            || info->IsAbilityOfSkillType(SKILL_TAILORING)
            || info->IsAbilityOfSkillType(SKILL_ENGINEERING)
            || info->IsAbilityOfSkillType(SKILL_ENCHANTING)
            || info->IsAbilityOfSkillType(SKILL_JEWELCRAFTING)
            || info->IsAbilityOfSkillType(SKILL_INSCRIPTION)
            || info->IsAbilityOfSkillType(SKILL_COOKING);
        if (craft)
            spell->SetCastTime(int32(float(spell->GetCastTime()) * std::pow(0.80f, float(ranks))));
    }
};

class PerksUnit : public UnitScript
{
public:
    PerksUnit() : UnitScript("LivingGearPerksUnit", true, {
        UNITHOOK_ON_DAMAGE,
        UNITHOOK_ON_AURA_APPLY,
        UNITHOOK_SHOULD_TRACK_VALUES_UPDATE_POS_BY_INDEX,
        UNITHOOK_ON_PATCH_VALUES_UPDATE
    }) { }

    bool ShouldTrackValuesUpdatePosByIndex(Unit const* unit, uint8 /*updateType*/, uint16 index) override
    {
        return unit && unit->GetTypeId() == TYPEID_UNIT && index == UNIT_FIELD_LEVEL;
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target) override
    {
        if (!unit || !target)
            return;
        auto it = posPointers.other.find(UNIT_FIELD_LEVEL);
        if (it == posPointers.other.end())
            return;
        if (uint32 const displayLevel = DisplayLevelOverride(unit, target))
            valuesUpdateBuf.put(it->second, displayLevel);
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || !damage)
            return;
        if (Player* p = attacker->ToPlayer())
        {
            if (Creature* c = victim->ToCreature())
                if (uint32 const eff = EffectiveCreatureLevel(c, p))
                    damage = uint32(float(damage) * OutgoingScaleRatio(c, eff));
            if (!damage)
                damage = 1;
            if (GetClassPerk(p) == SPELL_ASSASSINATION && roll_chance_i(20))
                ApplyRandomPoison(p, victim);
        }
        if (Player* v = victim->ToPlayer())
        {
            if (Creature* c = attacker->ToCreature())
                if (uint32 const eff = EffectiveCreatureLevel(c, v))
                    damage = uint32(float(damage) * IncomingScaleRatio(c, eff));
        }
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        UniformMount(unit, aura);
        NeutralizeStealthSpeed(unit, aura);
    }
};

class PerksMap : public AllMapScript
{
public:
    PerksMap() : AllMapScript("LivingGearPerksMap", { ALLMAPHOOK_ON_PLAYER_ENTER_ALL }) { }

    void OnPlayerEnterAll(Map* map, Player* player) override
    {
        if (!map || !player || !map->IsDungeon())
            return;
        uint32 inst = map->GetInstanceId();
        if (!g_dungeonStart[inst])
            g_dungeonStart[inst] = getMSTime();
    }
};

// Not a real playerbot (see the 2026-08-20 wiki note on why that's not
// feasible for a temporary summon -- mod-playerbots hard-requires a real
// Player+WorldSession). Instead this reads the owning player's actual
// known Rogue spells each decision and applies a simple priority rotation
// with them, so it fights using the owner's real kit rather than a
// hand-picked fixed ability. Follows the owner out of combat; auto-engages
// whatever the owner is fighting.
struct npc_lg_shadow_cloneAI : public ScriptedAI
{
    npc_lg_shadow_cloneAI(Creature* c) : ScriptedAI(c) { }

    void Reset() override
    {
        // REACT_DEFENSIVE so being attacked actually registers as combat
        // at the engine level; the immediate-response part (was taking
        // ~10s to react) is handled explicitly below since this AI doesn't
        // go through the standard UpdateVictim()/threat-list path at all.
        // No forced SetReactState here anymore -- this is a real SUMMON_PET
        // now (2026-08-21 rework), and the player's Aggressive/Defensive/
        // Passive choice from the native pet frame is what should govern
        // this, not a hardcoded reset every time Reset() fires.
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

        ReactStates const stance = me->GetReactState();

        // Passive: never auto-engage, just follow. Matches how a passive
        // pet behaves everywhere else in the game.
        if (stance == REACT_PASSIVE)
        {
            if (me->GetVictim())
                me->AttackStop();
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                me->GetMotionMaster()->MoveFollow(owner, 2.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }

        // Pet-aggressive: if something is actively hitting the clone and it
        // isn't already fighting back, engage immediately -- this is the
        // fix for "took ~10 seconds to react", since without it the clone
        // only ever picked a target from the owner's current selection.
        // Applies at Defensive too (fighting back when attacked is not the
        // same as picking fights), only true Passive skips this.
        if (!me->GetVictim())
        {
            if (Unit* attacker = me->getAttackerForHelper())
            {
                AttackStart(attacker);
                RunRotation(owner, attacker);
                DoMeleeAttackIfReady();
                return;
            }
        }

        // Aggressive: also proactively pick a fight with whatever's nearby,
        // same as the native stance means for any other controllable pet.
        if (stance == REACT_AGGRESSIVE && !me->GetVictim())
        {
            std::list<Unit*> nearby;
            Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(me, me, 20.0f);
            Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(me, nearby, check);
            Cell::VisitObjects(me, searcher, 20.0f);
            for (Unit* u : nearby)
            {
                if (u && u->IsAlive() && owner->IsValidAttackTarget(u))
                {
                    AttackStart(u);
                    RunRotation(owner, u);
                    DoMeleeAttackIfReady();
                    return;
                }
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
                me->GetMotionMaster()->MoveFollow(owner, 2.0f, frand(0.0f, 2.0f * float(M_PI)));
            return;
        }

        if (me->GetVictim() != ownerTarget)
            AttackStart(ownerTarget);

        if (!me->IsWithinMeleeRange(ownerTarget))
        {
            TryShadowstepTo(owner, ownerTarget);
            return;
        }

        RunRotation(owner, ownerTarget);
        DoMeleeAttackIfReady();
    }

private:
    void TryShadowstepTo(Player* owner, Unit* target)
    {
        uint32 const step = BestOwned(owner, SPELL_SHADOWSTEP);
        if (!step || me->HasSpellCooldown(step))
            return;
        me->CastSpell(target, step, false);
    }

    void RunRotation(Player* owner, Unit* target)
    {
        if (target->HasUnitState(UNIT_STATE_CASTING))
        {
            uint32 const kick = BestOwned(owner, SPELL_KICK);
            if (kick && !me->HasSpellCooldown(kick))
            {
                me->CastSpell(target, kick, false);
                return;
            }
        }
        if (target->GetHealthPct() < 35.0f)
        {
            uint32 const finisher = BestOwned(owner, SPELL_EVISCERATE);
            if (finisher && !me->HasSpellCooldown(finisher))
            {
                me->CastSpell(target, finisher, false);
                return;
            }
        }
        uint32 const rupture = BestOwned(owner, SPELL_RUPTURE);
        if (rupture && !me->HasSpellCooldown(rupture) && !target->HasAura(rupture, me->GetGUID()))
        {
            me->CastSpell(target, rupture, false);
            return;
        }
        uint32 const builder = BestOwned(owner, SPELL_SINISTER_STRIKE);
        if (builder && !me->HasSpellCooldown(builder))
            me->CastSpell(target, builder, false);
    }
};

class npc_lg_shadow_clone : public CreatureScript
{
public:
    npc_lg_shadow_clone() : CreatureScript("npc_lg_shadow_clone") { }
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lg_shadow_cloneAI(creature);
    }
};

void LoadPerkConfig()
{
    g_cfg.zoneEnable = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Enable", true);
    g_cfg.zoneBuffer = sConfigMgr->GetOption<uint32>("LivingGear.ZoneScale.CombatBuffer", 3);
    g_cfg.zoneMinLevel = sConfigMgr->GetOption<uint32>("LivingGear.ZoneScale.MinPlayerLevel", 10);
    g_cfg.zoneFloor = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardFloor", 0.35f);
    g_cfg.zoneDecay = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.RewardGapDecay", 12.0f);
    g_cfg.zoneIncoming = sConfigMgr->GetOption<float>("LivingGear.ZoneScale.IncomingPerLevel", 0.12f);
    g_cfg.instantMount = sConfigMgr->GetOption<bool>("LivingGear.InstantMount", true);
    g_cfg.uniformMount = sConfigMgr->GetOption<bool>("LivingGear.UniformMountSpeed", true);
    g_cfg.autoMount = sConfigMgr->GetOption<bool>("LivingGear.AutoMount", true);
    g_cfg.dungeonPar = sConfigMgr->GetOption<uint32>("LivingGear.DungeonTimer.DefaultParSec", 1800);
    g_cfg.curatorTick = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.TickMs", 60000);
    g_hasZoneScale = false;
    if (QueryResult t = WorldDatabase.Query(
        "SELECT 1 FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_zone_scale'"))
        g_hasZoneScale = true;
}

class PerksWorld : public WorldScript
{
public:
    PerksWorld() : WorldScript("LivingGearPerksWorld", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadPerkConfig();
    }

    void OnStartup() override
    {
        LoadPerkConfig();
        LOG_INFO("server.loading", "Living Gear perks module loaded (zone scale, auto-mount, Subtlety, poisons)");
    }
};

} // namespace LivingGearPerks

// Called from LivingGear_ClassPerks.cpp's SelectClassPerk() when a player
// actively picks Rogue: Subtlety as their class perk -- the login-only
// grant doesn't cover a live perk switch, so without this call Shadow
// Dance/Shadowstep/Shadow Clone stayed ungranted until the player's next
// login even after picking Subtlety.
void LivingGear_GrantSubtletyPerks(Player* player)
{
    LivingGearPerks::PerksPlayer::GrantSubtletyPerks(player);
}

// Called from a core patch in Spell::CheckCast (Spell.cpp).
bool LivingGear_BypassStealthRequirement(Unit* caster)
{
    return LivingGearPerks::BypassStealthRequirement(caster);
}

void AddSC_LivingGearPerks()
{
    new LivingGearPerks::PerksWorld();
    new LivingGearPerks::PerksPlayer();
    new LivingGearPerks::PerksSpell();
    new LivingGearPerks::PerksUnit();
    new LivingGearPerks::PerksMap();
    new LivingGearPerks::npc_lg_shadow_clone();
}
