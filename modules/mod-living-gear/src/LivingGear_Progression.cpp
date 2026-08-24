/*
 * Living Gear account-wide progression perks: Honor, Reputation, Trade
 * (profession skill-up rate), and Leveling (alt-count XP bonus).
 *
 * These were part of the original 14,303-line LivingGear.cpp and were
 * silently dropped when that file was split into LivingGear.cpp /
 * LivingGear_Next.cpp / LivingGear_Gather.cpp / LivingGear_Perks.cpp.
 * This file is a fresh implementation of the same player-facing spell IDs
 * (910010-910031, 910053-910062), written from scratch against the current
 * (non-corrupted) codebase. It does not share any code with, or get built
 * from, LivingGear.cpp.backup-20260818 -- that file is reference-only and
 * confirmed to contain duplicated/conflicting copies of this exact logic.
 *
 * Per-file convention (matches LivingGear_Perks.cpp / LivingGear_Next.cpp):
 * this file is self-contained and duplicates its own small helpers
 * (SendLine / HasPerk / UnlockPerk) rather than sharing a header.
 */

#include "AllBattlegroundScript.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Log.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Castable perks only get learned; badges do not. LivingGear_Perks.cpp.
bool LivingGear_PerkIsCastable(uint32 spellId);

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

// File scope on purpose: inside the namespace this mangles as
// LivingGearProgression::LivingGear_DiagBump and fails to link.
void LivingGear_DiagBump(Player* player, char const* key); // LivingGear_Support.cpp

namespace LivingGearProgression
{
// ---------------------------------------------------------------------
// Spell IDs (see tools/client-patch/build_patch.py CUSTOM_SPELLS for the
// exact tooltip text these were written against).
// ---------------------------------------------------------------------
uint32 const SPELL_HONOR_DEFEAT = 910010;   // Honor gains +100%. Unlocked by losing a BG.
uint32 const SPELL_HONOR_VICTORY = 910011;  // Honor gains +200%. Unlocked by winning a BG.
uint32 const SPELL_HONOR_BLOODIED = 910012; // Honor gains +200%. Unlocked by 100 honorable kills.

uint32 const SPELL_REP_FIRST = 910013; // Rep gains +100%. Unlocked by 1 exalted faction (account-wide).
uint32 const SPELL_REP_FIVE = 910014;  // Rep gains +100%. Unlocked by 5 exalted factions.
uint32 const SPELL_REP_TEN = 910015;   // Rep gains +100%. Unlocked by 10 exalted factions.

uint32 const SPELL_REP_BLOODSAIL = 910016;
uint32 const SPELL_REP_DARKMOON = 910017;
uint32 const SPELL_REP_RAVENHOLDT = 910018;
uint32 const SPELL_REP_SHENDRALAR = 910019;
uint32 const SPELL_REP_ARATHOR = 910020;
uint32 const SPELL_REP_DEFILERS = 910021;
uint32 const SPELL_REP_SILVERWING = 910022;
uint32 const SPELL_REP_WARSONG = 910023;
uint32 const SPELL_REP_STORMPIKE = 910024;
uint32 const SPELL_REP_FROSTWOLF = 910025;

uint32 const FACTION_BLOODSAIL = 87;
uint32 const FACTION_DARKMOON = 909;
uint32 const FACTION_RAVENHOLDT = 349;
uint32 const FACTION_SHENDRALAR = 809;
uint32 const FACTION_ARATHOR = 509;
uint32 const FACTION_DEFILERS = 510;
uint32 const FACTION_SILVERWING = 890;
uint32 const FACTION_WARSONG = 889;
uint32 const FACTION_STORMPIKE = 730;
uint32 const FACTION_FROSTWOLF = 729;

struct SpecialRepPerk
{
    uint32 factionId;
    uint32 spellId;
};

SpecialRepPerk const SPECIAL_REP_PERKS[] = {
    { FACTION_BLOODSAIL, SPELL_REP_BLOODSAIL },
    { FACTION_DARKMOON, SPELL_REP_DARKMOON },
    { FACTION_RAVENHOLDT, SPELL_REP_RAVENHOLDT },
    { FACTION_SHENDRALAR, SPELL_REP_SHENDRALAR },
    { FACTION_ARATHOR, SPELL_REP_ARATHOR },
    { FACTION_DEFILERS, SPELL_REP_DEFILERS },
    { FACTION_SILVERWING, SPELL_REP_SILVERWING },
    { FACTION_WARSONG, SPELL_REP_WARSONG },
    { FACTION_STORMPIKE, SPELL_REP_STORMPIKE },
    { FACTION_FROSTWOLF, SPELL_REP_FROSTWOLF }
};

// 910026-910031: Trade 75/150/225/300/375/450 -- profession skill-up gain
// bonus, distinct from Craft 1-5 (910093-910097 in LivingGear_Perks.cpp,
// which reduces craft CAST TIME). Trade instead boosts the skill points
// gained per successful skill-up roll.
uint32 const SPELL_TRADE[] = { 910026, 910027, 910028, 910029, 910030, 910031 };
uint32 const TRADE_BREAKS[] = { 75, 150, 225, 300, 375, 450 };
uint32 const TRADE_TIERS = 6;

// Same 8-profession set LivingGear_Perks.cpp's CatchUpProfession() uses for
// the Craft (cast-speed) perks -- cooking has its own separate 75-450
// regen-tier system (SPELL_COOK) and is intentionally excluded here so the
// two systems don't double up on the same skill.
uint32 const TRADE_SKILLS[] = {
    SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_LEATHERWORKING, SKILL_TAILORING,
    SKILL_ENGINEERING, SKILL_ENCHANTING, SKILL_JEWELCRAFTING, SKILL_INSCRIPTION
};

// 910053-910062: Leveling 1-10. XP gains +50% per tier. Unlocked when the
// account has N characters at max level (this character included).
uint32 const SPELL_LEVELING[] = {
    910053, 910054, 910055, 910056, 910057, 910058, 910059, 910060, 910061, 910062
};
uint32 const LEVELING_TIERS = 10;
float const LEVELING_XP_BONUS = 0.5f; // +50% per unlocked tier, additive.

uint32 const HONOR_KILLS_MILESTONE = 100;

// ---------------------------------------------------------------------
// Small per-file helpers, matching LivingGear_Perks.cpp lines ~150-210.
// ---------------------------------------------------------------------
std::unordered_set<uint32> g_perkLoaded;
std::unordered_map<uint32, std::unordered_set<uint32>> g_perks;

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

void UnlockPerk(Player* player, uint32 spellId, char const* msg = nullptr)
{
    if (!player || !player->GetSession())
        return;
    // Bug report #22, 2026-08-22: "Do not have the leveling perk for having a
    // level 80 despite being on a level 80 currently."
    //
    // The perk logic was correct and the account genuinely qualified. Spells
    // 910053-910062 had no spell_dbc row, so this returned here and did it in
    // total silence -- not one account on the realm had ever earned a Leveling
    // perk. Forty of the 139 advertised perks were in that state; see
    // tools/perk_spell_audit.py, which now checks for it.
    //
    // Still refuse to proceed, because learnSpell on a missing spell is not
    // survivable, but say so. A perk that cannot be granted is a data bug and
    // should look like one instead of like a player being wrong about their
    // own character.
    if (!sSpellMgr->GetSpellInfo(spellId))
    {
        LOG_ERROR("module.livinggear",
            "Living Gear: perk {} is advertised but has no spell_dbc row, so it can never "
            "be granted. Run tools/perk_spell_audit.py.", spellId);
        return;
    }
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadPerks(acc);
    // Perks belong to the account, spells are learned per character. Returning
    // on "the account already owns this" skipped the learn entirely, so only
    // the first character on an account ever got the spell. See the long note
    // on UnlockPerk in LivingGear_Perks.cpp; ReconcilePerkSpells() there is the
    // login-time repair for characters that already missed out.
    bool const firstTime = g_perks[acc].insert(spellId).second;
    if (firstTime)
        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
            acc, spellId);
    // Castable perks only -- learning a badge spams chat and puts nothing in
    // the spellbook. See LivingGear_PerkIsCastable in LivingGear_Perks.cpp.
    if (LivingGear_PerkIsCastable(spellId) && !player->HasSpell(spellId))
        player->learnSpell(spellId);
    if (!firstTime)
        return;
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        ChatHandler(player->GetSession()).SendSysMessage(msg);
}

// ---------------------------------------------------------------------
// Account-wide "exalted faction" tracking. Mirrors the account-wide
// riding-skill pattern in LivingGear_Next.cpp (LoadAccountRiding /
// SaveAccountRiding / ApplyAccountRiding): a small table keyed by
// account, defensively probed via information_schema before use so a
// missing migration degrades gracefully instead of aborting the
// worldserver on an unknown table.
// ---------------------------------------------------------------------
bool g_schemaReady = false;
bool g_hasExaltedTable = false;

void DetectSchema()
{
    if (g_schemaReady)
        return;
    g_schemaReady = true;
    if (QueryResult tables = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `information_schema`.`TABLES` "
        "WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_exalted_faction'"))
        g_hasExaltedTable = (*tables)[0].Get<uint64>() > 0;
}

void NoteExaltedFaction(uint32 accountId, uint32 factionId)
{
    DetectSchema();
    if (!g_hasExaltedTable)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_exalted_faction` (`account_id`, `faction_id`) VALUES ({}, {})",
        accountId, factionId);
}

uint32 AccountExaltedCount(uint32 accountId)
{
    DetectSchema();
    if (!g_hasExaltedTable)
        return 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(DISTINCT `faction_id`) FROM `lg_account_exalted_faction` WHERE `account_id` = {}",
        accountId))
        return (*result)[0].Get<uint32>();
    return 0;
}

bool AccountHasExaltedFaction(uint32 accountId, uint32 factionId)
{
    DetectSchema();
    if (!g_hasExaltedTable)
        return false;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT 1 FROM `lg_account_exalted_faction` WHERE `account_id` = {} AND `faction_id` = {}",
        accountId, factionId))
        return true;
    return false;
}

// Scan this character's live reputation state for any faction already at
// Exalted and record it account-wide. Catches characters that reached
// Exalted before this module/table existed, and keeps every alt's login
// contributing to the account total (same "accumulates over logins" model
// as account riding).
void RecordCurrentExalted(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    for (auto const& entry : player->GetReputationMgr().GetStateList())
    {
        FactionState const& state = entry.second;
        if (ReputationMgr::ReputationToRank(state.Standing) >= REP_EXALTED)
            NoteExaltedFaction(accountId, state.ID);
    }
}

// ---------------------------------------------------------------------
// Honor perks (910010-910012)
// ---------------------------------------------------------------------
float HonorPerkMultiplier(Player* player)
{
    if (!player)
        return 1.0f;
    float bonus = 0.0f;
    if (HasPerk(player, SPELL_HONOR_DEFEAT))
        bonus += 1.0f;
    if (HasPerk(player, SPELL_HONOR_VICTORY))
        bonus += 2.0f;
    if (HasPerk(player, SPELL_HONOR_BLOODIED))
        bonus += 2.0f;
    return 1.0f + bonus;
}

void CheckHonorKillMilestone(Player* player)
{
    if (!player || !player->GetSession())
        return;
    if (HasPerk(player, SPELL_HONOR_BLOODIED))
        return;
    if (player->GetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS) >= HONOR_KILLS_MILESTONE)
        UnlockPerk(player, SPELL_HONOR_BLOODIED,
            "|cff66ccff[Account Perks]|r Honor: Bloodied unlocked.");
}

// ---------------------------------------------------------------------
// Reputation perks (910013-910025)
// ---------------------------------------------------------------------
float RepPerkMultiplier(Player* player)
{
    if (!player)
        return 1.0f;
    float bonus = 0.0f;
    if (HasPerk(player, SPELL_REP_FIRST))
        bonus += 1.0f;
    if (HasPerk(player, SPELL_REP_FIVE))
        bonus += 1.0f;
    if (HasPerk(player, SPELL_REP_TEN))
        bonus += 1.0f;
    for (SpecialRepPerk const& perk : SPECIAL_REP_PERKS)
        if (HasPerk(player, perk.spellId))
            bonus += 1.0f;
    return 1.0f + bonus;
}

// Faction name for the unlock message, out of the DBC rather than a hardcoded
// table -- ten more hand-written strings is ten more chances to be wrong.
std::string FactionName(uint32 factionId)
{
    if (FactionEntry const* entry = sFactionStore.LookupEntry(factionId))
        if (entry->name[LOCALE_enUS] && *entry->name[LOCALE_enUS])
            return entry->name[LOCALE_enUS];
    return "that faction";
}

void CheckReputationPerks(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 exalted = std::max<uint32>(
        player->GetReputationMgr().GetExaltedFactionCount(), AccountExaltedCount(accountId));
    if (exalted >= 1)
        UnlockPerk(player, SPELL_REP_FIRST, "|cff66ccff[Account Perks]|r Rep: First Exalted unlocked.");
    if (exalted >= 5)
        UnlockPerk(player, SPELL_REP_FIVE, "|cff66ccff[Account Perks]|r Rep: Five Exalted unlocked.");
    if (exalted >= 10)
        UnlockPerk(player, SPELL_REP_TEN, "|cff66ccff[Account Perks]|r Rep: Ten Exalted unlocked.");
    // Announced, like every other rep tier above. These ten used to unlock in
    // silence -- UnlockPerk with no message -- so the one reputation perk a
    // player had actually gone out of their way to earn was the only one that
    // never said so. Found by the non-class perk audit, 2026-08-24.
    for (SpecialRepPerk const& perk : SPECIAL_REP_PERKS)
        if (AccountHasExaltedFaction(accountId, perk.factionId)
            || player->GetReputationRank(perk.factionId) >= REP_EXALTED)
            UnlockPerk(player, perk.spellId, Acore::StringFormat(
                "|cff66ccff[Account Perks]|r Rep: {} exalted -- reputation gains up.",
                FactionName(perk.factionId)).c_str());
}

// ---------------------------------------------------------------------
// Trade perks (910026-910031): profession skill-up gain bonus.
// ---------------------------------------------------------------------
bool IsTradeSkill(uint32 skillId)
{
    for (uint32 sk : TRADE_SKILLS)
        if (sk == skillId)
            return true;
    return false;
}

uint32 AccountMaxTradeSkill(Player* player)
{
    if (!player || !player->GetSession())
        return 0;
    uint32 best = 0;
    for (uint32 sk : TRADE_SKILLS)
        best = std::max(best, uint32(player->GetSkillValue(sk)));
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT MAX(`value`) FROM `character_skills` cs "
        "INNER JOIN `characters` c ON c.`guid` = cs.`guid` "
        "WHERE c.`account` = {} AND cs.`skill` IN ({}, {}, {}, {}, {}, {}, {}, {})",
        player->GetSession()->GetAccountId(),
        TRADE_SKILLS[0], TRADE_SKILLS[1], TRADE_SKILLS[2], TRADE_SKILLS[3],
        TRADE_SKILLS[4], TRADE_SKILLS[5], TRADE_SKILLS[6], TRADE_SKILLS[7]))
        if (!result->Fetch()->IsNull())
            best = std::max(best, (*result)[0].Get<uint32>());
    return best;
}

void CheckTradeMilestones(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const maxSkill = AccountMaxTradeSkill(player);
    for (uint32 i = 0; i < TRADE_TIERS; ++i)
        if (maxSkill >= TRADE_BREAKS[i])
            UnlockPerk(player, SPELL_TRADE[i]);
}

float TradePerkMultiplier(Player* player)
{
    if (!player)
        return 1.0f;
    float bonus = 0.0f;
    for (uint32 spellId : SPELL_TRADE)
        if (HasPerk(player, spellId))
            bonus += 1.0f;
    return 1.0f + bonus;
}

// ---------------------------------------------------------------------
// Leveling perks (910053-910062): XP bonus scaling with max-level alts.
// ---------------------------------------------------------------------
float LevelingXpMultiplier(Player* player)
{
    if (!player)
        return 1.0f;
    float bonus = 0.0f;
    for (uint32 spellId : SPELL_LEVELING)
        if (HasPerk(player, spellId))
            bonus += LEVELING_XP_BONUS;
    return 1.0f + bonus;
}

void CheckLevelingPerks(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 const maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32 count = 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `characters` WHERE `account` = {} AND `level` >= {}",
        accountId, maxLevel))
        count = (*result)[0].Get<uint32>();
    // The currently-logged-in character's level may not be flushed to the
    // `characters` table yet (saves are periodic) -- count it directly so a
    // just-dinged max-level toon isn't missed until the next save.
    if (player->GetLevel() >= maxLevel)
    {
        bool countedInDb = false;
        if (QueryResult self = CharacterDatabase.Query(
            "SELECT `level` FROM `characters` WHERE `guid` = {}", player->GetGUID().GetCounter()))
            countedInDb = (*self)[0].Get<uint32>() >= maxLevel;
        if (!countedInDb)
            ++count;
    }
    if (count > LEVELING_TIERS)
        count = LEVELING_TIERS;
    for (uint32 i = 0; i < count; ++i)
        UnlockPerk(player, SPELL_LEVELING[i],
            i == 0 ? "|cff66ccff[Account Perks]|r Leveling perk unlocked." : nullptr);
}

// ---------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------
class ProgressionPlayer : public PlayerScript
{
public:
    ProgressionPlayer() : PlayerScript("LivingGearProgressionPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_LEVEL_CHANGED,
        PLAYERHOOK_ON_REWARD_HONOR,
        PLAYERHOOK_ON_PVP_KILL,
        PLAYERHOOK_ON_GIVE_REPUTATION,
        PLAYERHOOK_ON_REPUTATION_RANK_CHANGE,
        PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL,
        PLAYERHOOK_ON_UPDATE_GATHERING_SKILL
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;
        RecordCurrentExalted(player);
        CheckReputationPerks(player);
        CheckHonorKillMilestone(player);
        CheckTradeMilestones(player);
        CheckLevelingPerks(player);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        // Composes with LivingGearPerks' own OnPlayerGiveXP (zone scale,
        // the kill funnel, dungeon pace) -- ScriptMgr calls every registered
        // PlayerScript hook in registration order, and Perks registers first
        // (LivingGear_loader.cpp). So this multiplies a value the funnel has
        // already settled, and must not run before it: the funnel REPLACES
        // amount for a creature kill rather than scaling it.
        //
        // The grey-kill grant does not reach this hook at all -- Player::GiveXP
        // never fires it -- which is why GrantUnrewardedKillXp applies
        // LivingGear_LevelingXpMultiplier itself.
        if (!player || !amount)
            return;
        float const mult = LevelingXpMultiplier(player);
        if (mult <= 1.0f)
            return;
        amount = uint32(float(amount) * mult);
        if (!amount)
            amount = 1;
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        CheckLevelingPerks(player);
    }

    void OnPlayerRewardHonor(Player* player, float& honor) override
    {
        if (!player || honor <= 0.0f)
            return;
        honor *= HonorPerkMultiplier(player);
    }

    void OnPlayerPVPKill(Player* killer, Player* /*killed*/) override
    {
        CheckHonorKillMilestone(killer);
    }

    void OnPlayerGiveReputation(Player* player, int32 /*factionID*/, float& amount, ReputationSource /*repSource*/) override
    {
        if (!player || amount == 0.0f)
            return;
        amount *= RepPerkMultiplier(player);
    }

    void OnPlayerReputationRankChange(Player* player, uint32 factionID, ReputationRank newRank,
        ReputationRank /*oldRank*/, bool /*increased*/) override
    {
        if (!player || !player->GetSession() || newRank != REP_EXALTED)
            return;
        NoteExaltedFaction(player->GetSession()->GetAccountId(), factionID);
        CheckReputationPerks(player);
    }

    // Lockpicking is a profession in every way that matters here -- it is
    // trained, it grinds 1-400, and a Rogue works it exactly the way a
    // Blacksmith works Blacksmithing -- but it is not a crafting skill, so
    // it comes up UpdateGatherSkill() instead of UpdateCraftSkill() and the
    // Trade perks below never saw it. A Rogue with all six tiers earned was
    // still picking up single points at a time.
    //
    // Only the gain is boosted. Lockpicking deliberately stays out of
    // TRADE_SKILLS, so it cannot unlock the Trade tiers by itself -- a
    // Rogue who has never touched a profession still gets 1 point a pick.
    void OnPlayerUpdateGatheringSkill(Player* player, uint32 skillId, uint32 /*currentLevel*/,
        uint32 /*gray*/, uint32 /*green*/, uint32 /*yellow*/, uint32& gain) override
    {
        if (!player || !gain || skillId != SKILL_LOCKPICKING)
            return;
        float const mult = TradePerkMultiplier(player);
        if (mult <= 1.0f)
            return;
        uint32 const boosted = uint32(float(gain) * mult + 0.5f);
        gain = boosted < 1 ? 1 : boosted;
    }

    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* skill,
        uint32 /*current_level*/, uint32& gain) override
    {
        // Distinct from LivingGearPerks' Craft 1-5 (cast time on this same
        // hook, via a different mechanism: SetCastTime in OnSpellPrepare).
        // This only touches the skill-up gain amount, and only for the
        // non-cooking profession set (cooking has its own regen-tier perks
        // at the same 75-450 breakpoints).
        // Bug report #20, 2026-08-22: "professions 100% skill-up buffs for
        // hitting 75, 150, 225 etc don't work". Reading the code has not
        // explained it, and three plausible causes are already ruled out:
        //   - the reporter genuinely holds 4 of the 6 tiers, so the multiplier
        //     should be 5.0
        //   - his Tailoring is 73 against a rank cap of 150, so UpdateSkillPro's
        //     clamp to MaxValue is not eating the gain
        //   - the other OnPlayerUpdateCraftingSkill in LivingGear_Perks.cpp
        //     ignores `gain` entirely, so nothing is clobbering this
        // Log what actually happens rather than shipping a guess. This is a log
        // line, not a chat print -- no player sees it.
        if (!player || !skill)
            return;
        // Silent for non-trade skill lines. This used to log every one, and
        // over one uptime that produced 71 lines -- all of them bots casting
        // class spells (skill lines 237 Arcane and 354 Demonology reach this
        // hook through Spell::EffectCreateItem's siblings), and not one real
        // craft among them. Instrumentation that buries the case it was added
        // to catch is worse than none: #20 needs the line for an actual
        // profession craft to be findable when it finally happens.
        if (!IsTradeSkill(skill->SkillLine))
            return;
        if (!gain)
        {
            LOG_INFO("module.livinggear", "trade skill-up: {} skill {} arrived with gain 0",
                player->GetName(), skill->SkillLine);
            return;
        }
        LivingGear_DiagBump(player, "trade.skillup");
        CheckTradeMilestones(player);
        float const mult = TradePerkMultiplier(player);
        uint32 const before = gain;
        if (mult <= 1.0f)
        {
            LOG_INFO("module.livinggear", "trade skill-up: {} skill {} gain {} unboosted, multiplier {}",
                player->GetName(), skill->SkillLine, before, mult);
            return;
        }
        uint32 const boosted = uint32(float(gain) * mult + 0.5f);
        gain = boosted < 1 ? 1 : boosted;
        LOG_INFO("module.livinggear", "trade skill-up: {} skill {} gain {} -> {} (multiplier {})",
            player->GetName(), skill->SkillLine, before, gain, mult);
    }
};

class ProgressionBattleground : public AllBattlegroundScript
{
public:
    ProgressionBattleground() : AllBattlegroundScript("LivingGearProgressionBattleground", {
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_END_REWARD
    }) { }

    void OnBattlegroundEndReward(Battleground* /*bg*/, Player* player, TeamId winnerTeamId) override
    {
        if (!player || !player->GetSession())
            return;
        if (player->GetBgTeamId() == winnerTeamId)
            UnlockPerk(player, SPELL_HONOR_VICTORY, "|cff66ccff[Account Perks]|r Honor: Victory unlocked.");
        else
            UnlockPerk(player, SPELL_HONOR_DEFEAT, "|cff66ccff[Account Perks]|r Honor: Defeat unlocked.");
    }
};

} // namespace LivingGearProgression

// The Leveling perks apply to every XP source, including the one XP path that
// cannot use the hook: LivingGear_Perks.cpp grants grey-kill XP through
// Player::GiveXP directly, and Player::GiveXP does not fire OnPlayerGiveXP.
// Without this the +50%-per-max-level-alt bonus silently did nothing in exactly
// the low-level zones the scaling exists to keep relevant.
float LivingGear_LevelingXpMultiplier(Player* player)
{
    return LivingGearProgression::LevelingXpMultiplier(player);
}

void AddSC_LivingGearProgression()
{
    new LivingGearProgression::ProgressionPlayer();
    new LivingGearProgression::ProgressionBattleground();
}
