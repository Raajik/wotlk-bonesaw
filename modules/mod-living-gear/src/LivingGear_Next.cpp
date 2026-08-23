#include "AllCreatureScript.h"
#include "AllGameObjectScript.h"
#include "AllSpellScript.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config.h"
#include "DBCEnums.h"
#include "DatabaseEnv.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Unit.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

uint32 GetClassPerk(Player* player); // LivingGear_ClassPerks.cpp

namespace LivingGearNext
{
uint32 const SPELL_CLASS_BUFFS = 910106;
uint32 const SPELL_RIDING_SHARE = 910107;
uint32 const SPELL_AUTO_ACCEPT = 910108;
uint32 const SPELL_AUTOLOOT = 910008;
uint32 const SPELL_PALADIN_HOLY = 910069;
uint32 const SPELL_PALADIN_RETRIBUTION = 910071;
uint32 const SPELL_HAND_OF_FREEDOM = 1044;
uint32 const SPELL_CONSECRATION = 26573;
// Bug report #25, 2026-08-22. The Paladin perks promised these and the module
// did not contain the words "Holy Shock", "Crusader Strike", "Divine Storm" or
// "Exorcism" anywhere -- found by tools/perk_promise_audit.py. Across all 30
// specs Paladin was the only one with promises that had no implementation.
uint32 const SPELL_HOLY_SHOCK = 20473;
uint32 const SPELL_CRUSADER_STRIKE = 35395;
uint32 const SPELL_DIVINE_STORM = 53385;
uint32 const SPELL_EXORCISM = 879;
uint32 const SPELL_RETRIBUTION_AURA = 7294;
float const CONSECRATION_DAMAGE_MULT = 11.0f;  // "+1000%"
float const HOLY_SHOCK_DAMAGE_MULT = 4.0f;     // "+300%"
float const HOLY_SHOCK_SPLASH_RANGE = 10.0f;
uint32 const DIVINE_STORM_EXTRA_HITS = 3;      // "each press hits 4 times"
float const EXORCISM_SPLASH_RANGE = 10.0f;
uint32 const SPELL_AVENGERS_SHIELD = 31935;
uint32 const NPC_KELTHUZAD = 15990;
uint32 const MAP_NAXXRAMAS = 533;
uint32 const QUEST_RESPAWN_CAP = 10;
float const CLASS_BUFF_PCT = 10.0f;
float const MENTOR_XP = 2.0f;
float const AS_DAMAGE_MULT = 4.0f;
uint32 const AS_CD_MS = 5000;
uint32 const SPEED_CAP_DEFAULT = 500;

struct NextState
{
    uint32 speedCapPct = SPEED_CAP_DEFAULT;
    uint32 ridingSkill = 0;
    uint32 ridingMax = 0;
    uint32 ridingStep = 0;
    uint32 hofTarget = 0;
    uint32 lastTickMs = 0;
    bool classBuffOn = false;
    uint8 botOrigLevel = 0;
    bool weaponPeakOn = false;
    float peakStr = 0.f;
    float peakAgi = 0.f;
    float peakSta = 0.f;
    float peakInt = 0.f;
    float peakSpi = 0.f;
};

std::unordered_map<uint32, NextState> g_state;
std::unordered_map<uint32, std::unordered_set<uint8>> g_classBuffUnlock;
std::unordered_map<uint32, uint32> g_accountRiding;

// ---------------------------------------------------------------------
// Account-wide gold, honor and arena points (bug report #24, restored
// 2026-08-22).
//
// This existed before the 14k-line LivingGear.cpp was split and was lost in
// that split, like the amenity functions were. The `lg_account_meta` columns
// (shared_gold, shared_honor, shared_arena, shared_inited) survived with real
// data in them, so only the code needed rebuilding.
//
// One pool per account. Any character spending or earning updates the pool,
// and every other character of theirs that is online is updated to match.
//
// g_syncingShared is the recursion guard: ApplySharedCurrenciesTo calls
// SetMoney, which fires OnPlayerMoneyChanged, which would call
// PushSharedCurrencies straight back into this.
std::unordered_map<uint32, uint32> g_sharedGold;
std::unordered_map<uint32, uint32> g_sharedHonor;
std::unordered_map<uint32, uint32> g_sharedArena;
std::unordered_map<uint32, uint8> g_sharedInited;
std::unordered_set<uint32> g_sharedLoaded;
std::unordered_set<uint32> g_syncingShared;

// Playerbots must never take part. They have their own gold, there are ~90 bot
// accounts in lg_account_meta, and letting one write to a pool would be both
// wrong and very hard to notice.
bool SharedEligible(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot();
}

void LoadSharedCurrencies(uint32 accountId)
{
    if (!g_sharedLoaded.insert(accountId).second)
        return;
    g_sharedGold[accountId] = 0;
    g_sharedHonor[accountId] = 0;
    g_sharedArena[accountId] = 0;
    g_sharedInited[accountId] = 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `shared_gold`, `shared_honor`, `shared_arena`, `shared_inited` "
        "FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
    {
        g_sharedGold[accountId] = (*result)[0].Get<uint32>();
        g_sharedHonor[accountId] = (*result)[1].Get<uint32>();
        g_sharedArena[accountId] = (*result)[2].Get<uint32>();
        g_sharedInited[accountId] = (*result)[3].Get<uint8>();
    }
}

void SaveSharedCurrencies(uint32 accountId)
{
    CharacterDatabase.Execute(
        "INSERT INTO `lg_account_meta` (`account_id`, `shared_gold`, `shared_honor`, "
        "`shared_arena`, `shared_inited`) VALUES ({}, {}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE `shared_gold` = {}, `shared_honor` = {}, "
        "`shared_arena` = {}, `shared_inited` = {}",
        accountId, g_sharedGold[accountId], g_sharedHonor[accountId],
        g_sharedArena[accountId], g_sharedInited[accountId],
        g_sharedGold[accountId], g_sharedHonor[accountId],
        g_sharedArena[accountId], g_sharedInited[accountId]);
}

void ApplySharedCurrenciesTo(Player* player)
{
    if (!SharedEligible(player))
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    g_syncingShared.insert(accountId);
    player->SetMoney(g_sharedGold[accountId]);
    player->SetHonorPoints(g_sharedHonor[accountId]);
    player->SetArenaPoints(g_sharedArena[accountId]);
    g_syncingShared.erase(accountId);
}

void PushSharedCurrencies(Player* source)
{
    if (!SharedEligible(source))
        return;
    uint32 const accountId = source->GetSession()->GetAccountId();
    if (g_syncingShared.count(accountId))
        return;
    LoadSharedCurrencies(accountId);
    if (!g_sharedInited[accountId])
        return; // not seeded yet; EnsureSharedCurrencies owns the first write
    g_sharedGold[accountId] = source->GetMoney();
    g_sharedHonor[accountId] = source->GetHonorPoints();
    g_sharedArena[accountId] = source->GetArenaPoints();
    SaveSharedCurrencies(accountId);

    ObjectGuid const sourceGuid = source->GetGUID();
    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* alt = pair.second;
        if (!alt || alt->GetGUID() == sourceGuid || !SharedEligible(alt))
            continue;
        if (alt->GetSession()->GetAccountId() == accountId)
            ApplySharedCurrenciesTo(alt);
    }
}

// Seeds the pool the first time, then hands the character the pooled values.
//
// The seed is MAX across the account's characters, NOT the sum the original
// used, and not the stored value. That matters because this feature was dead
// for a while and characters drifted apart in the meantime:
//   - Trusting the stored value would have taken 182k copper off Muckfuppet
//     and 1.7 MILLION off Swayss, whose characters had earned well past the
//     last pooled figure. That is the same shape as bug #21 and not a mistake
//     worth making twice.
//   - Summing would mint gold from nothing, and worse on accounts that WERE
//     synced before, where every character already holds the same pool and the
//     sum multiplies it by the number of characters.
// MAX means nobody loses what their best character had, and nothing is
// invented. rev_living_gear_shared_reseed.sql clears shared_inited so every
// account re-seeds exactly once under this rule.
void EnsureSharedCurrencies(Player* player)
{
    if (!SharedEligible(player))
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadSharedCurrencies(accountId);
    if (!g_sharedInited[accountId])
    {
        uint32 gold = player->GetMoney();
        uint32 honor = player->GetHonorPoints();
        uint32 arena = player->GetArenaPoints();
        if (QueryResult result = CharacterDatabase.Query(
            "SELECT COALESCE(MAX(`money`), 0), COALESCE(MAX(`totalHonorPoints`), 0), "
            "COALESCE(MAX(`arenaPoints`), 0) FROM `characters` WHERE `account` = {}",
            accountId))
        {
            gold = std::max(gold, (*result)[0].Get<uint32>());
            honor = std::max(honor, (*result)[1].Get<uint32>());
            arena = std::max(arena, (*result)[2].Get<uint32>());
        }
        g_sharedGold[accountId] = gold;
        g_sharedHonor[accountId] = honor;
        g_sharedArena[accountId] = arena;
        g_sharedInited[accountId] = 1;
        SaveSharedCurrencies(accountId);
    }
    ApplySharedCurrenciesTo(player);
}
bool g_metaReady = false;
bool g_hasSpeedCapCol = false;
bool g_hasRidingCol = false;
bool g_hasClassBuffTable = false;
bool g_hasAccountMountTable = false;

void DetectNextSchema()
{
    if (g_metaReady)
        return;
    g_metaReady = true;
    if (QueryResult cols = CharacterDatabase.Query(
        "SELECT `COLUMN_NAME` FROM `information_schema`.`COLUMNS` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta'"))
    {
        do
        {
            std::string const name = (*cols)[0].Get<std::string>();
            if (name == "speed_cap")
                g_hasSpeedCapCol = true;
            else if (name == "riding_skill")
                g_hasRidingCol = true;
        } while (cols->NextRow());
    }
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_class_buff_unlock'"))
        g_hasClassBuffTable = (*tables)[0].Get<uint64>() > 0;
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_mount'"))
        g_hasAccountMountTable = (*tables)[0].Get<uint64>() > 0;
}

void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
}

bool HasPerk(Player* player, uint32 spellId)
{
    return player && player->HasSpell(spellId);
}

// Bug report #25, 2026-08-22. The Paladin perks in this file were gated on
// HasPerk() above, which asks "does the character KNOW this spell". For a class
// perk that is never true: UnlockPerk() in LivingGear_ClassPerks.cpp writes
// lg_account_perk and notifies the client, and deliberately never calls
// learnSpell. So HasPerk(SPELL_PALADIN_HOLY) was false for every Paladin who
// ever lived, and both Paladin perks were unreachable code.
//
// The selected spec lives in lg_char_class_perk and is what every other class
// file checks. This is also the multi-classing-friendly choice: it asks what
// the player picked, not what class they happen to be.
bool HasClassPerk(Player* player, uint32 perkId)
{
    return player && ::GetClassPerk(player) == perkId;
}

// 2026-08-21: every caller in this file (Class Buffs, Riding, Auto-Accept)
// is a pure account-perk flag with no CASTABLE_SPELLS/SkillLineAbility
// entry -- player->learnSpell() marked them "known" with nothing to
// categorize them, so the client dumped them into the General spellbook
// tab regardless of being excluded from CASTABLE_SPELLS client-side.
// HasPerk()'s g_perks[acc]/DB fallback (LivingGear_Perks.cpp) works fine
// without ever calling learnSpell, so this no longer does.
void UnlockPerk(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession() || !sSpellMgr->GetSpellInfo(spellId))
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
        accountId, spellId);
}

bool RankOf(SpellInfo const* info, uint32 firstId)
{
    if (!info)
        return false;
    uint32 const first = sSpellMgr->GetFirstSpellInChain(firstId);
    return first && sSpellMgr->GetFirstSpellInChain(info->Id) == first;
}

NextState& StateFor(Player* player)
{
    return g_state[player->GetGUID().GetCounter()];
}

uint32 SpeedCapPct(Player* player)
{
    if (!player)
        return SPEED_CAP_DEFAULT;
    uint32 pct = StateFor(player).speedCapPct;
    if (pct < 100)
        pct = 100;
    if (pct > SPEED_CAP_DEFAULT)
        pct = SPEED_CAP_DEFAULT;
    return pct;
}

void ApplySpeedCap(Player* player)
{
    if (!player || !player->IsInWorld())
        return;
    float const cap = float(SpeedCapPct(player)) / 100.0f;
    if (player->GetSpeedRate(MOVE_RUN) > cap)
        player->SetSpeed(MOVE_RUN, cap, true);
    if (player->GetSpeedRate(MOVE_SWIM) > cap)
        player->SetSpeed(MOVE_SWIM, cap, true);
    if (player->GetSpeedRate(MOVE_FLIGHT) > cap)
        player->SetSpeed(MOVE_FLIGHT, cap, true);
}

void LoadClassBuffUnlock(uint32 accountId)
{
    if (g_classBuffUnlock.find(accountId) != g_classBuffUnlock.end())
        return;
    DetectNextSchema();
    auto& set = g_classBuffUnlock[accountId];
    if (!g_hasClassBuffTable)
        return;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `class` FROM `lg_class_buff_unlock` WHERE `account_id` = {}", accountId))
    {
        do
            set.insert((*result)[0].Get<uint8>());
        while (result->NextRow());
    }
}

void NoteClassBuffUnlock(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint8 const cls = player->getClass();
    LoadClassBuffUnlock(accountId);
    if (g_classBuffUnlock[accountId].count(cls))
        return;
    g_classBuffUnlock[accountId].insert(cls);
    DetectNextSchema();
    if (g_hasClassBuffTable)
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `lg_class_buff_unlock` (`account_id`, `class`) VALUES ({}, {})",
            accountId, cls);
    UnlockPerk(player, SPELL_CLASS_BUFFS);
    ChatHandler(player->GetSession()).SendSysMessage(
        "|cff66ccff[Account Perks]|r Class Buffs unlocked for this class (Naxxramas 25).");
}

void ApplyClassBuffs(Player* player, bool apply)
{
    if (!player)
        return;
    NextState& st = StateFor(player);
    if (st.classBuffOn == apply)
        return;
    st.classBuffOn = apply;
    float const amt = apply ? CLASS_BUFF_PCT : -CLASS_BUFF_PCT;
    player->ApplyStatPctModifier(UNIT_MOD_STAT_STRENGTH, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_AGILITY, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_STAMINA, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_PCT, amt);
    player->ApplyStatPctModifier(UNIT_MOD_STAT_SPIRIT, TOTAL_PCT, amt);
    player->UpdateAllStats();
}

bool PlayerHasClassBuffUnlock(Player* player)
{
    if (!player || !player->GetSession())
        return false;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadClassBuffUnlock(accountId);
    return g_classBuffUnlock[accountId].count(player->getClass()) > 0
        && HasPerk(player, SPELL_CLASS_BUFFS);
}

bool ShouldHaveClassBuff(Player* player)
{
    if (!player || !player->IsAlive())
        return false;
    if (PlayerHasClassBuffUnlock(player))
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
        if (PlayerHasClassBuffUnlock(member))
            return true;
    }
    return false;
}

void TickClassBuffs(Player* player)
{
    if (!player || !player->GetSession())
        return;
    ApplyClassBuffs(player, ShouldHaveClassBuff(player));
}

void LoadAccountRiding(uint32 accountId)
{
    if (g_accountRiding.find(accountId) != g_accountRiding.end())
        return;
    DetectNextSchema();
    uint32 skill = 0;
    if (g_hasRidingCol)
        if (QueryResult result = CharacterDatabase.Query(
            "SELECT `riding_skill` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
            skill = (*result)[0].Get<uint32>();
    g_accountRiding[accountId] = skill;
}

void SaveAccountRiding(uint32 accountId, uint32 skill)
{
    if (skill > g_accountRiding[accountId])
        g_accountRiding[accountId] = skill;
    DetectNextSchema();
    if (!g_hasRidingCol)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT INTO `lg_account_meta` (`account_id`, `riding_skill`) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE `riding_skill` = GREATEST(`riding_skill`, {})",
        accountId, skill, skill);
}

void ApplyAccountRiding(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    LoadAccountRiding(accountId);
    uint32 const have = g_accountRiding[accountId];
    if (!have)
        return;
    UnlockPerk(player, SPELL_RIDING_SHARE);
    uint16 const cur = player->GetSkillValue(SKILL_RIDING);
    if (cur >= have)
        return;
    uint32 step = 1;
    uint32 maxv = 75;
    if (have >= 300)
    {
        step = 4;
        maxv = 300;
    }
    else if (have >= 225)
    {
        step = 3;
        maxv = 225;
    }
    else if (have >= 150)
    {
        step = 2;
        maxv = 150;
    }
    player->SetSkill(SKILL_RIDING, uint16(step), uint16(have), uint16(maxv));
}

void NoteRiding(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint16 const val = player->GetSkillValue(SKILL_RIDING);
    if (!val)
        return;
    SaveAccountRiding(player->GetSession()->GetAccountId(), val);
    UnlockPerk(player, SPELL_RIDING_SHARE);
}

// ---------------------------------------------------------------------
// Account-wide mounts and companions.
//
// Riding skill has been shared across the account for a while (above), but
// the mounts and pets themselves were not -- so an alt could ride at level 1
// and had nothing to ride. Same shape as the account Key Ring
// (LivingGear_Vault.cpp RecordAccountKey/GrantAccountKeys) and for the same
// reason: nothing in this codebase writes into an offline character's
// spellbook, so a learn is recorded account-wide here and handed to each
// other character on its own next login.
// ---------------------------------------------------------------------

// Skill line 777 (Mounts) / 778 (Companions) is what the 3.3.5 client itself
// uses to sort a spell into the spellbook's Pet tab, so it is exactly the set
// of spells a player would call "my mounts and pets" -- and it is a much
// tighter test than "has SPELL_AURA_MOUNTED", which also catches vehicle
// auras, taxi/quest-scripted rides and boss mechanics we have no business
// copying onto alts.
bool IsCollectionSpell(uint32 spellId)
{
    SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        if (itr->second->SkillLine == SKILL_MOUNTS || itr->second->SkillLine == SKILL_COMPANIONS)
            return true;
    return false;
}

void RecordAccountCollection(Player* player, uint32 spellId)
{
    if (!player || !player->GetSession() || !spellId)
        return;
    DetectNextSchema();
    if (!g_hasAccountMountTable || !IsCollectionSpell(spellId))
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_mount` (`account_id`, `spell_id`) VALUES ({}, {})",
        player->GetSession()->GetAccountId(), spellId);
}

// Records everything this character already knows. Without it the feature
// would only ever see mounts learned from the moment it shipped, and every
// collection already sitting in the DB would stay stranded on whichever
// character earned it.
void HarvestAccountCollection(Player* player)
{
    if (!player || !player->GetSession())
        return;
    DetectNextSchema();
    if (!g_hasAccountMountTable)
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    // One statement, not one per mount: a collector logging in would
    // otherwise fire a hundred-odd synchronous writes on the world thread
    // every single login, all of them no-ops after the first time.
    std::string values;
    for (auto const& [spellId, state] : player->GetSpellMap())
    {
        if (!state || state->State == PLAYERSPELL_REMOVED || !state->Active)
            continue;
        if (!IsCollectionSpell(spellId))
            continue;
        if (!values.empty())
            values += ',';
        values += Acore::StringFormat("({},{})", accountId, spellId);
    }
    if (values.empty())
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_mount` (`account_id`, `spell_id`) VALUES {}", values);
}

void GrantAccountCollection(Player* player)
{
    if (!player || !player->GetSession())
        return;
    DetectNextSchema();
    if (!g_hasAccountMountTable)
        return;
    QueryResult result = CharacterDatabase.Query(
        "SELECT `spell_id` FROM `lg_account_mount` WHERE `account_id` = {}",
        player->GetSession()->GetAccountId());
    if (!result)
        return;
    do
    {
        uint32 const spellId = (*result)[0].Get<uint32>();
        // The spell may have come from a character of the other faction or a
        // different race. That is the entire point -- learnSpell does not
        // enforce SkillLineAbility RaceMask, so the alt simply gets it.
        if (!player->HasSpell(spellId) && sSpellMgr->GetSpellInfo(spellId))
            player->learnSpell(spellId);
    } while (result->NextRow());
}

uint8 GroupMedianLevel(Group* group, bool humansOnly)
{
    std::vector<uint8> levels;
    if (!group)
        return 0;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member)
            continue;
        bool const bot = member->GetSession() && member->GetSession()->IsBot();
        if (humansOnly && bot)
            continue;
        levels.push_back(member->GetLevel());
    }
    if (levels.empty())
        return 0;
    std::sort(levels.begin(), levels.end());
    return levels[levels.size() / 2];
}

bool GroupIsMentored(Player* player)
{
    if (!player || !player->GetGroup())
        return false;
    Group* group = player->GetGroup();
    uint8 const humanMed = GroupMedianLevel(group, true);
    if (!humanMed)
        return false;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || !member->GetSession() || !member->GetSession()->IsBot())
            continue;
        uint8 orig = g_state[member->GetGUID().GetCounter()].botOrigLevel;
        if (!orig)
            orig = member->GetLevel();
        if (orig > humanMed)
            return true;
    }
    return false;
}

void ScaleMentorBots(Player* player)
{
    if (!player || !player->GetGroup() || !player->GetMap() || !player->GetMap()->IsDungeon())
        return;
    Group* group = player->GetGroup();
    uint8 const median = GroupMedianLevel(group, true);
    if (!median)
        return;
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* bot = itr->GetSource();
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot())
            continue;
        NextState& st = g_state[bot->GetGUID().GetCounter()];
        if (!st.botOrigLevel)
            st.botOrigLevel = bot->GetLevel();
        if (st.botOrigLevel <= median)
            continue;
        if (bot->GetLevel() != median)
            bot->SetLevel(median);
    }
}

void RestoreMentorBots(Player* player)
{
    if (!player || !player->GetGroup())
        return;
    for (GroupReference* itr = player->GetGroup()->GetFirstMember(); itr; itr = itr->next())
    {
        Player* bot = itr->GetSource();
        if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot())
            continue;
        NextState& st = g_state[bot->GetGUID().GetCounter()];
        if (st.botOrigLevel && bot->GetLevel() != st.botOrigLevel)
            bot->SetLevel(st.botOrigLevel);
    }
}

bool QuestRelatedCreature(Creature* creature)
{
    if (!creature)
        return false;
    CreatureQuestItemList const* items = sObjectMgr->GetCreatureQuestItemList(creature->GetEntry());
    if (items && !items->empty())
        return true;
    return false;
}

void CapQuestRespawn(Creature* creature)
{
    if (!creature || !QuestRelatedCreature(creature))
        return;
    if (creature->GetRespawnDelay() > QUEST_RESPAWN_CAP)
        creature->SetRespawnDelay(QUEST_RESPAWN_CAP);
}

void CapQuestGoRespawn(GameObject* go)
{
    if (!go || go->GetGoType() != GAMEOBJECT_TYPE_CHEST)
        return;
    GameObjectTemplate const* info = go->GetGOInfo();
    if (!info)
        return;
    bool quest = info->chest.questId != 0;
    GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
    if (items && !items->empty())
        quest = true;
    if (!quest)
        return;
    if (go->GetRespawnDelay() > QUEST_RESPAWN_CAP)
        go->SetRespawnDelay(QUEST_RESPAWN_CAP);
}

// Paladin perks, bug report #25. Every cast below is deferred and triggered,
// for the reason this module keeps relearning: OnPlayerSpellCast fires part-way
// through the triggering Spell::cast(), and starting more casts on a unit that
// is still mid-cast is the reentrant path into Unit::_AddAura that produced the
// recurring assert crashes.
void PaladinSplashImpl(ObjectGuid playerGuid, ObjectGuid firstTarget, uint32 spellId, float range)
{
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player || !player->IsInWorld() || !player->IsAlive())
        return;
    Unit* anchor = firstTarget ? ObjectAccessor::GetUnit(*player, firstTarget) : player;
    if (!anchor || !anchor->IsInWorld())
        anchor = player;

    std::list<Unit*> around;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(anchor, player, range);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(anchor, around, check);
    Cell::VisitObjects(anchor, searcher, range);
    for (Unit* u : around)
    {
        if (!u || !u->IsAlive() || u->GetGUID() == firstTarget)
            continue;
        if (!player->IsValidAttackTarget(u))
            continue;
        player->CastSpell(u, spellId, true);
    }
}

void PaladinSplash(Player* player, Unit* target, uint32 spellId, float range)
{
    if (!player || !spellId)
        return;
    ObjectGuid const playerGuid = player->GetGUID();
    ObjectGuid const targetGuid = target ? target->GetGUID() : ObjectGuid::Empty;
    player->m_Events.AddEventAtOffset([playerGuid, targetGuid, spellId, range]()
    {
        PaladinSplashImpl(playerGuid, targetGuid, spellId, range);
    }, std::chrono::milliseconds(1));
}

// Divine Storm "each press hits 4 times": three extra triggered copies on top
// of the one the player actually cast.
void DivineStormExtraHits(Player* player)
{
    if (!player)
        return;
    ObjectGuid const playerGuid = player->GetGUID();
    for (uint32 i = 0; i < DIVINE_STORM_EXTRA_HITS; ++i)
    {
        player->m_Events.AddEventAtOffset([playerGuid]()
        {
            Player* p = ObjectAccessor::FindPlayer(playerGuid);
            if (p && p->IsInWorld() && p->IsAlive())
                p->CastSpell(p, SPELL_DIVINE_STORM, true);
        }, std::chrono::milliseconds(120 + i * 120));
    }
}

// Called from the spell-cast hook. Splits by perk so a Paladin only ever gets
// the behaviour of the spec they actually chose.
void HandlePaladinPerkCast(Player* player, SpellInfo const* info, Unit* target)
{
    if (!player || !info)
        return;

    if (HasClassPerk(player, SPELL_PALADIN_HOLY) && RankOf(info, SPELL_HOLY_SHOCK))
    {
        // "hits enemies within 10 yards of the target" -- anchored on the
        // target, not the caster, which is what the wording says and also what
        // makes it useful at range.
        if (target && player->IsValidAttackTarget(target))
            PaladinSplash(player, target, info->Id, HOLY_SHOCK_SPLASH_RANGE);
        return;
    }

    if (!HasClassPerk(player, SPELL_PALADIN_RETRIBUTION))
        return;

    if (RankOf(info, SPELL_DIVINE_STORM))
    {
        DivineStormExtraHits(player);
        return;
    }
    // "While Retribution Aura is up, Crusader Strike also casts Exorcism on
    // nearby enemies." The aura condition is the player's own choice of aura,
    // so it stays a real check rather than being assumed.
    if (RankOf(info, SPELL_CRUSADER_STRIKE) && player->HasAura(SPELL_RETRIBUTION_AURA))
        PaladinSplash(player, target, SPELL_EXORCISM, EXORCISM_SPLASH_RANGE);
}

void RelocateConsecration(Player* player)
{
    if (!player || !player->GetMap() || !HasClassPerk(player, SPELL_PALADIN_HOLY))
        return;
    for (SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_CONSECRATION); info; info = info->GetNextRankSpell())
    {
        DynamicObject* dyn = player->GetDynObject(info->Id);
        if (!dyn)
            continue;
        player->GetMap()->DynamicObjectRelocation(dyn,
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        if (dyn->GetDuration() < 2000)
            dyn->SetDuration(8000);
    }
}

void ThrowExtraAvengers(Player* player, Spell* spell)
{
    if (!player || !spell || player->getClass() != CLASS_PALADIN)
        return;
    SpellInfo const* info = spell->GetSpellInfo();
    if (!RankOf(info, SPELL_AVENGERS_SHIELD))
        return;
    Unit* first = spell->m_targets.GetUnitTarget();
    if (!first)
        return;
    std::list<Unit*> targets;
    Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck check(player, player, 30.0f);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> searcher(player, targets, check);
    Cell::VisitObjects(player, searcher, 30.0f);
    uint32 extra = 0;
    for (Unit* target : targets)
    {
        if (!target || target == first || !target->IsAlive() || !player->IsValidAttackTarget(target))
            continue;
        player->CastSpell(target, info->Id, true);
        if (++extra >= 2)
            break;
    }
    int32 cd = int32(AS_CD_MS);
    float haste = player->GetRatingBonusValue(CR_HASTE_SPELL);
    if (haste < 0.0f)
        haste = 0.0f;
    cd = int32(float(cd) / (1.0f + haste / 100.0f));
    if (cd < 1000)
        cd = 1000;
    player->AddSpellCooldown(info->Id, 0, getMSTime() + uint32(cd), true);
}

void KeepHofOneTarget(Player* caster, Unit* target)
{
    if (!caster || !target || !HasPerk(caster, SPELL_PALADIN_RETRIBUTION))
        return;
    NextState& st = StateFor(caster);
    uint32 const newGuid = target->GetGUID().GetCounter();
    if (st.hofTarget && st.hofTarget != newGuid)
        if (Player* prev = ObjectAccessor::FindPlayerByLowGUID(st.hofTarget))
            prev->RemoveAurasDueToSpell(SPELL_HAND_OF_FREEDOM);
    st.hofTarget = newGuid;
    if (Aura* aura = target->GetAura(SPELL_HAND_OF_FREEDOM, caster->GetGUID()))
    {
        aura->SetMaxDuration(-1);
        aura->SetDuration(-1);
    }
    caster->RemoveSpellCooldown(SPELL_HAND_OF_FREEDOM, true);
}

void ApplyWeaponPeak(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    NextState& st = StateFor(player);
    if (st.weaponPeakOn)
    {
        player->HandleStatFlatModifier(UNIT_MOD_STAT_STRENGTH, TOTAL_VALUE, st.peakStr, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_AGILITY, TOTAL_VALUE, st.peakAgi, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_STAMINA, TOTAL_VALUE, st.peakSta, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_VALUE, st.peakInt, false);
        player->HandleStatFlatModifier(UNIT_MOD_STAT_SPIRIT, TOTAL_VALUE, st.peakSpi, false);
        st.weaponPeakOn = false;
        st.peakStr = st.peakAgi = st.peakSta = st.peakInt = st.peakSpi = 0.f;
    }
    Item* weapon = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!weapon)
        return;
    ItemTemplate const* proto = weapon->GetTemplate();
    if (!proto || proto->Class != ITEM_CLASS_WEAPON)
        return;
    float peak[5] = {};
    float mine[5] = {};
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `item_entry`, `str`, `agi`, `sta`, `intel`, `spi` FROM `lg_absorb` WHERE `account_id` = {}",
        accountId))
    {
        do
        {
            uint32 const entry = (*result)[0].Get<uint32>();
            ItemTemplate const* other = sObjectMgr->GetItemTemplate(entry);
            if (!other || other->Class != proto->Class || other->SubClass != proto->SubClass)
                continue;
            float vals[5] = {
                (*result)[1].Get<float>(), (*result)[2].Get<float>(), (*result)[3].Get<float>(),
                (*result)[4].Get<float>(), (*result)[5].Get<float>()
            };
            for (int i = 0; i < 5; ++i)
                if (vals[i] > peak[i])
                    peak[i] = vals[i];
            if (entry == proto->ItemId)
            {
                for (int i = 0; i < 5; ++i)
                    mine[i] = vals[i];
            }
        } while (result->NextRow());
    }
    st.peakStr = std::max(0.f, peak[0] - mine[0]);
    st.peakAgi = std::max(0.f, peak[1] - mine[1]);
    st.peakSta = std::max(0.f, peak[2] - mine[2]);
    st.peakInt = std::max(0.f, peak[3] - mine[3]);
    st.peakSpi = std::max(0.f, peak[4] - mine[4]);
    if (st.peakStr + st.peakAgi + st.peakSta + st.peakInt + st.peakSpi <= 0.01f)
        return;
    player->HandleStatFlatModifier(UNIT_MOD_STAT_STRENGTH, TOTAL_VALUE, st.peakStr, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_AGILITY, TOTAL_VALUE, st.peakAgi, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_STAMINA, TOTAL_VALUE, st.peakSta, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_VALUE, st.peakInt, true);
    player->HandleStatFlatModifier(UNIT_MOD_STAT_SPIRIT, TOTAL_VALUE, st.peakSpi, true);
    st.weaponPeakOn = true;
    player->UpdateAllStats();
}

bool HandleNextMessage(Player* player, std::string const& raw)
{
    std::string msg = raw;
    if (msg.rfind("LG\t", 0) == 0)
        msg = msg.substr(3);
    uint32 cap = 0;
    if (sscanf(msg.c_str(), "SCAP|%u", &cap) == 1)
    {
        if (cap < 100)
            cap = 100;
        if (cap > SPEED_CAP_DEFAULT)
            cap = SPEED_CAP_DEFAULT;
        StateFor(player).speedCapPct = cap;
        DetectNextSchema();
        if (g_hasSpeedCapCol && player->GetSession())
            CharacterDatabase.DirectExecute(
                "INSERT INTO `lg_account_meta` (`account_id`, `speed_cap`) VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE `speed_cap` = {}",
                player->GetSession()->GetAccountId(), cap, cap);
        ApplySpeedCap(player);
        SendLine(player, Acore::StringFormat("SCAP|{}", cap));
        return true;
    }
    return false;
}

// The speed cap is the only state this module pushes at login, and a
// client REQ never used to reach it, so the slider snapped back to its
// default on every /reload.
void SendNextSync(Player* player)
{
    if (!player || !player->GetSession())
        return;
    SendLine(player, Acore::StringFormat("SCAP|{}", SpeedCapPct(player)));
}

class NextPlayer : public PlayerScript
{
public:
    NextPlayer() : PlayerScript("LivingGearNextPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_MONEY_CHANGED,
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_UPDATE_SKILL,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_EQUIP,
        PLAYERHOOK_ON_UNEQUIP_ITEM,
        PLAYERHOOK_ON_PLAYER_QUEST_ACCEPT
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        uint32 const accountId = player->GetSession()->GetAccountId();
        DetectNextSchema();
        LoadClassBuffUnlock(accountId);
        LoadAccountRiding(accountId);
        EnsureSharedCurrencies(player);
        // Paladin Retribution promises "Learn Crusader Strike" (bug report
        // #25). Granted here rather than on selection so an existing
        // Retribution Paladin picks it up on their next login instead of
        // having to re-choose the perk.
        if (HasClassPerk(player, SPELL_PALADIN_RETRIBUTION) && !player->HasSpell(SPELL_CRUSADER_STRIKE)
            && sSpellMgr->GetSpellInfo(SPELL_CRUSADER_STRIKE))
            player->learnSpell(SPELL_CRUSADER_STRIKE);
        if (g_hasSpeedCapCol)
        {
            if (QueryResult result = CharacterDatabase.Query(
                "SELECT `speed_cap` FROM `lg_account_meta` WHERE `account_id` = {}", accountId))
            {
                uint32 cap = (*result)[0].Get<uint32>();
                if (cap)
                    StateFor(player).speedCapPct = cap;
            }
        }
        ApplyAccountRiding(player);
        // Harvest before grant: what this character already owns joins the
        // account pool in the same login it receives everyone else's.
        HarvestAccountCollection(player);
        GrantAccountCollection(player);
        if (g_classBuffUnlock[accountId].count(player->getClass()))
            UnlockPerk(player, SPELL_CLASS_BUFFS);
        ApplyWeaponPeak(player);
        SendLine(player, Acore::StringFormat("SCAP|{}", SpeedCapPct(player)));
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        // Only money changes push during play, so honor and arena earned this
        // session would otherwise sit unpooled until the next coin moved.
        // Cheap here and it closes that window.
        PushSharedCurrencies(player);
        ApplyClassBuffs(player, false);
        NextState& st = StateFor(player);
        if (st.weaponPeakOn)
            ApplyWeaponPeak(player);
        RestoreMentorBots(player);
        g_state.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override
    {
        if (!player || !player->IsInWorld())
            return;
        uint32 const now = getMSTime();
        NextState& st = StateFor(player);
        if (!st.lastTickMs || getMSTimeDiff(st.lastTickMs, now) >= 400)
        {
            st.lastTickMs = now;
            TickClassBuffs(player);
            ApplySpeedCap(player);
        }
        RelocateConsecration(player);
    }

    void OnPlayerQuestAccept(Player* player, Quest const* /*quest*/) override
    {
        UnlockPerk(player, SPELL_AUTO_ACCEPT);
    }

    // Deferred by a tick: this fires from inside the money change itself, and
    // ApplySharedCurrenciesTo calls SetMoney on other players, which is the
    // reentrancy this module has been bitten by repeatedly.
    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skip*/) override
    {
        if (!player || !spell)
            return;
        HandlePaladinPerkCast(player, spell->GetSpellInfo(), spell->m_targets.GetUnitTarget());
    }

    void OnPlayerMoneyChanged(Player* player, int32& /*amount*/) override
    {
        if (!SharedEligible(player))
            return;
        ObjectGuid const guid = player->GetGUID();
        player->m_Events.AddEventAtOffset([guid]()
        {
            if (Player* p = ObjectAccessor::FindPlayer(guid))
                PushSharedCurrencies(p);
        }, std::chrono::milliseconds(1));
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!player)
            return;
        if (player->GetMap() && player->GetMap()->IsDungeon())
            ScaleMentorBots(player);
        else
            RestoreMentorBots(player);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killer || !killed || killed->GetEntry() != NPC_KELTHUZAD)
            return;
        Map* map = killer->GetMap();
        if (!map || map->GetId() != MAP_NAXXRAMAS)
            return;
        if (map->GetDifficulty() != RAID_DIFFICULTY_25MAN_NORMAL
            && map->GetDifficulty() != RAID_DIFFICULTY_25MAN_HEROIC)
            return;
        NoteClassBuffUnlock(killer);
        if (Group* group = killer->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                if (Player* member = itr->GetSource())
                    if (member->IsInMap(killer) && member->getClass() == killer->getClass())
                        NoteClassBuffUnlock(member);
        }
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        NoteRiding(player);
        RecordAccountCollection(player, spellId);
    }

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/,
        uint32 /*step*/, uint32 /*newValue*/) override
    {
        if (skillId == SKILL_RIDING)
            NoteRiding(player);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (!player || !amount)
            return;
        if (GroupIsMentored(player))
            amount = uint32(float(amount) * MENTOR_XP);
    }

    void OnPlayerEquip(Player* player, Item* /*it*/, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/) override
    {
        ApplyWeaponPeak(player);
    }

    void OnPlayerUnequip(Player* player, Item* /*it*/) override
    {
        ApplyWeaponPeak(player);
    }

};

class NextSpell : public AllSpellScript
{
public:
    NextSpell() : AllSpellScript("LivingGearNextSpell", {
        ALLSPELLHOOK_ON_PREPARE,
        ALLSPELLHOOK_ON_SPELL_CHECK_CAST,
        ALLSPELLHOOK_ON_CAST
    }) { }

    void OnSpellPrepare(Spell* spell, Unit* caster, SpellInfo const* spellInfo) override
    {
        if (!spell || !caster || !spellInfo || !caster->IsPlayer())
            return;
        if (spellInfo->HasAura(SPELL_AURA_MOUNTED))
            spell->SetCastTime(0);
    }

    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& res) override
    {
        if (!spell)
            return;
        Unit* caster = spell->GetCaster();
        if (!caster || !caster->IsPlayer())
            return;
        Player* player = caster->ToPlayer();
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        if (info->HasAura(SPELL_AURA_MOUNTED)
            && (res == SPELL_FAILED_MOVING || res == SPELL_FAILED_LEVEL_REQUIREMENT
                || res == SPELL_FAILED_LOWLEVEL))
            res = SPELL_CAST_OK;
        if (HasClassPerk(player, SPELL_PALADIN_HOLY) && RankOf(info, SPELL_CONSECRATION)
            && player->GetDynObject(info->Id))
        {
            player->RemoveDynObject(info->Id);
            res = SPELL_FAILED_DONT_REPORT;
        }
        if (HasClassPerk(player, SPELL_PALADIN_RETRIBUTION) && info->Id == SPELL_HAND_OF_FREEDOM
            && res == SPELL_FAILED_NOT_READY)
            res = SPELL_CAST_OK;
    }

    void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        if (!spell || !caster || !spellInfo || !caster->IsPlayer())
            return;
        Player* player = caster->ToPlayer();
        if (RankOf(spellInfo, SPELL_AVENGERS_SHIELD) && !spell->IsTriggered())
            ThrowExtraAvengers(player, spell);
        if (spellInfo->Id == SPELL_HAND_OF_FREEDOM)
        {
            Unit* target = spell->m_targets.GetUnitTarget();
            if (!target)
                target = player;
            KeepHofOneTarget(player, target);
        }
    }
};

class NextUnit : public UnitScript
{
public:
    NextUnit() : UnitScript("LivingGearNextUnit", true, {
        UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
        UNITHOOK_ON_AURA_APPLY
    }) { }

    void ModifySpellDamageTaken(Unit* /*target*/, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (damage <= 0 || !attacker || !spellInfo)
            return;
        if (RankOf(spellInfo, SPELL_AVENGERS_SHIELD) && attacker->IsPlayer())
            damage = int32(float(damage) * AS_DAMAGE_MULT);

        // Paladin Holy (bug report #25): "Consecration damage +1000%" and
        // "Holy Shock damage +300%". Both are direct spell damage, so one hook
        // covers them. Gated on the perk so a Protection or Retribution
        // Paladin's Consecration is untouched.
        Player* player = attacker->ToPlayer();
        if (!player || !HasClassPerk(player, SPELL_PALADIN_HOLY))
            return;
        if (RankOf(spellInfo, SPELL_CONSECRATION))
            damage = int32(float(damage) * CONSECRATION_DAMAGE_MULT);
        else if (RankOf(spellInfo, SPELL_HOLY_SHOCK))
            damage = int32(float(damage) * HOLY_SHOCK_DAMAGE_MULT);
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!unit || !aura || aura->GetId() != SPELL_HAND_OF_FREEDOM)
            return;
        Unit* caster = aura->GetCaster();
        if (!caster || !caster->IsPlayer())
            return;
        KeepHofOneTarget(caster->ToPlayer(), unit);
    }
};

class NextCreature : public AllCreatureScript
{
public:
    NextCreature() : AllCreatureScript("LivingGearNextCreature") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        CapQuestRespawn(creature);
    }
};

class NextGameObject : public AllGameObjectScript
{
public:
    NextGameObject() : AllGameObjectScript("LivingGearNextGameObject") { }

    void OnGameObjectAddWorld(GameObject* go) override
    {
        CapQuestGoRespawn(go);
    }
};

} // namespace LivingGearNext

// Addon-command entry point, called by the dispatcher in LivingGear.cpp.
// The gate that used to live in NextPlayer::OnPlayerCanUseChat tested the
// RAW message against "SCAP|", but every client line arrives prefixed as
// "LG<tab>SCAP|..." -- so it never matched once, and the speed cap slider
// has never done anything. Routing through the dispatcher (which strips
// the prefix in exactly one place) removes the chance to get that wrong
// per module.
bool LivingGear_HandleNextCommand(Player* player, std::string const& msg)
{
    return LivingGearNext::HandleNextMessage(player, msg);
}

void LivingGear_SendNextSync(Player* player)
{
    LivingGearNext::SendNextSync(player);
}

void AddSC_LivingGearNext()
{
    new LivingGearNext::NextPlayer();
    new LivingGearNext::NextSpell();
    new LivingGearNext::NextUnit();
    new LivingGearNext::NextCreature();
    new LivingGearNext::NextGameObject();
}
