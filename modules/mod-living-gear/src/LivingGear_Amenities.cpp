/*
 * Living Gear amenity unlocks + Quest Wayfarer speed perk.
 *
 * Written fresh 2026-08-20 to replace functionality that existed in the
 * pre-split LivingGear.cpp (14k lines) but was lost when that file was
 * split down to fit a compile fix and never restored. Informed by, but not
 * copied from, the corrupted backup at LivingGear.cpp.backup-20260818 (that
 * file is a spliced "frankenfile" with duplicated/conflicting function
 * bodies -- see A:\obsidian\jeremy\wiki\Bonesaw.md).
 *
 * Amenity unlocks (910002-910009): free UI-access spells that open the
 * mailbox/auction house/class trainer/bank/stable/flight master, or bind
 * the hearthstone, from anywhere -- by summoning an invisible clone of the
 * real NPC and interacting with it, same trick the original used.
 *
 * Wayfarer (910038 + 910175/910176/910177): one perk that trades movement
 * speed against damage on a slider the player sets. See the block comment
 * above WayfarerCap() for the whole design.
 */

#include "AchievementMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Trainer.h"
#include "Unit.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Castable perks only get learned; badges do not. LivingGear_Perks.cpp.
bool LivingGear_PerkIsCastable(uint32 spellId);
void LivingGear_RefundIfPurchased(Player* player, uint32 spellId);

using namespace std::chrono_literals;

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_IsAddonSendInProgress(); // LivingGear.cpp

namespace LivingGearAmenities
{
uint32 const SPELL_MAILBOX = 910002;
uint32 const SPELL_AUCTION = 910003;
uint32 const SPELL_TRAINER = 910004;
uint32 const SPELL_BANK = 910005;
uint32 const SPELL_STABLE = 910006;
uint32 const SPELL_BIND = 910007;
uint32 const SPELL_FLIGHT = 910009;
// Wayfarer. 910038 carries the speed half (MOD_SPEED_ALWAYS on foot,
// MOD_MOUNTED_SPEED_ALWAYS and MOD_MOUNTED_FLIGHT_SPEED_ALWAYS at half value
// while mounted or flying); 910175 is a hidden companion carrying the damage
// half (MOD_DAMAGE_PERCENT_DONE, all schools). Two spells because spell_dbc
// has room for exactly three effects and the speed half needs all three --
// bug report #27 is the record of what happens when flight is left out.
//
// 910176 and 910177 are badges: they own no aura and only widen the range.
uint32 const SPELL_WAYFARER = 910038;
uint32 const SPELL_WAYFARER_FOCUS = 910175;   // retired -- removed on sight, see ApplyWayfarer
uint32 const SPELL_WAYFARER_WIDE = 910176;
uint32 const SPELL_WAYFARER_FULL = 910177;
uint32 const SPELL_WAYFARER_R4 = 910039;
uint32 const SPELL_WAYFARER_R5 = 910040;
uint32 const SPELL_AUTO_QUEST = 910090;

uint32 const NPC_AH_ALLIANCE = 8670;
uint32 const NPC_AH_HORDE = 8673;
uint32 const NPC_STABLE = 9896;
uint32 const NPC_FLIGHT_ALLIANCE = 352;
uint32 const NPC_FLIGHT_HORDE = 3310;
uint32 const DISPLAY_INVIS_BOOKSHELF = 17188;
uint32 const HELPER_SECS = 120;
uint32 const QUEST_SPEED_NEED = 100;
uint32 const FRIENDLY_FACTION = 35;

void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
}

void Say(Player* player, char const* msg)
{
    if (!player || !player->GetSession())
        return;
    ChatHandler(player->GetSession()).SendSysMessage(msg);
}

std::unordered_map<uint32, std::unordered_set<uint32>> g_perks;
std::unordered_set<uint32> g_perkLoaded;

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
    if (!player || !player->GetSession() || !sSpellMgr->GetSpellInfo(spellId))
        return;
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
    {
        // Already owned. If it was BOUGHT and a condition has now granted it
        // anyway, the points went on something that would have been free.
        LivingGear_RefundIfPurchased(player, spellId);
        return;
    }
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        Say(player, msg);
}

// -------------------------------------------------------------------------
// Report #179: account-wide flight points via exploration. With the Flight
// amenity owned, the first time any character on the account enters a map,
// every taxi node on that map unlocks for the whole account. Explored maps
// are stored in lg_account_taxi_maps (pending migration); on login the
// stored maps re-apply their nodes so alts inherit the network. The
// per-character taximask then persists the bits as usual.
// -------------------------------------------------------------------------

struct AccountTaxiState
{
    std::unordered_set<uint32> maps; // map ids already unlocked for this account
};
std::unordered_map<uint32, AccountTaxiState> g_accountTaxi;

bool LoadAccountTaxiMaps(uint32 accountId)
{
    if (g_accountTaxi.find(accountId) != g_accountTaxi.end())
        return true;
    QueryResult result = CharacterDatabase.Query(
        "SELECT `map_id` FROM `lg_account_taxi_maps` WHERE `account_id` = {}", accountId);
    AccountTaxiState& st = g_accountTaxi[accountId];
    if (result)
    {
        do
            st.maps.insert((*result)[0].Get<uint32>());
        while (result->NextRow());
    }
    return false; // first load this session
}

// Apply every taxi node on the map to this character. Returns how many new
// nodes the character itself gained.
uint32 ApplyTaxiNodesForMap(Player* player, uint32 mapId)
{
    uint32 granted = 0;
    for (uint32 i = 0; i < sTaxiNodesStore.GetNumRows(); ++i)
    {
        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(i);
        if (!node || node->map_id != mapId)
            continue;
        if (player->m_taxi.SetTaximaskNode(node->ID))
            ++granted;
    }
    return granted;
}

void UnlockTaxiMapForAccount(Player* player, uint32 mapId)
{
    if (!player || !player->GetSession())
        return;
    uint32 const acc = player->GetSession()->GetAccountId();
    LoadAccountTaxiMaps(acc);
    AccountTaxiState& st = g_accountTaxi[acc];
    if (st.maps.count(mapId))
        return;
    st.maps.insert(mapId);
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_taxi_maps` (`account_id`, `map_id`, `unlocked_at`) VALUES ({}, {}, {})",
        acc, mapId, uint32(GameTime::GetGameTime().count()));
    uint32 const granted = ApplyTaxiNodesForMap(player, mapId);
    Say(player, Acore::StringFormat(
        "|cff66ccff[Living Gear]|r Explored a new zone -- every flight path on this map is now unlocked for all your characters ({} new for this one).",
        granted).c_str());
    LOG_INFO("module.livinggear", "account taxi unlock: account {} map {} (+{} node(s))",
        acc, mapId, granted);
}

TempSummon* SummonInvisibleHelper(Player* player, uint32 entry, bool hideModel = true)
{
    if (!player)
        return nullptr;
    TempSummon* helper = player->SummonCreature(entry,
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
        player->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, HELPER_SECS * IN_MILLISECONDS,
        nullptr, true);
    if (helper && hideModel)
        helper->SetDisplayId(DISPLAY_INVIS_BOOKSHELF);
    return helper;
}

uint32 ClassTrainerEntry(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:      return 26332;
        case CLASS_PALADIN:      return 26327;
        case CLASS_HUNTER:       return 26325;
        case CLASS_ROGUE:        return 26329;
        case CLASS_PRIEST:       return 26328;
        case CLASS_DEATH_KNIGHT: return 29194;
        case CLASS_SHAMAN:       return 26330;
        case CLASS_MAGE:         return 26326;
        case CLASS_WARLOCK:      return 26331;
        case CLASS_DRUID:        return 26324;
        default:                 return 0;
    }
}

void OpenClassTrainer(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const trainerEntry = ClassTrainerEntry(player->getClass());
    if (!trainerEntry)
    {
        Say(player, "|cffff6666[Living Gear]|r No class trainer for your class.");
        return;
    }
    // Keep the real trainer model -- the 3.3.5 trainer UI ignores the list
    // packet if the unit is an invisible bookshelf.
    TempSummon* trainer = SummonInvisibleHelper(player, trainerEntry, false);
    if (!trainer)
    {
        Say(player, "|cffff6666[Living Gear]|r Could not open the class trainer.");
        return;
    }
    trainer->SetFaction(FRIENDLY_FACTION);
    trainer->ReplaceAllNpcFlags(NPCFlags(
        UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_TRAINER_CLASS));

    Trainer::Trainer const* data = sObjectMgr->GetTrainer(trainer->GetEntry());
    if (!data || !data->IsTrainerValidForPlayer(player))
    {
        Say(player, "|cffff6666[Living Gear]|r Trainer is not available for your class.");
        return;
    }

    player->SetSelection(trainer->GetGUID());
    ObjectGuid const playerGuid = player->GetGUID();
    ObjectGuid const trainerGuid = trainer->GetGUID();
    player->m_Events.AddEventAtOffset([playerGuid, trainerGuid]()
    {
        Player* caster = ObjectAccessor::FindPlayer(playerGuid);
        if (!caster || !caster->GetMap())
            return;
        Creature* npc = caster->GetMap()->GetCreature(trainerGuid);
        if (!npc)
            return;
        caster->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TALK);
        caster->PrepareGossipMenu(npc, npc->GetGossipMenuId(), true);
        caster->SendPreparedGossip(npc);
    }, 50ms);
}

void BindHearthHere(Player* player)
{
    if (!player)
        return;
    if (player->GetMap() && player->GetMap()->Instanceable())
    {
        Say(player, "|cffff6666[Living Gear]|r You can only bind in the open world.");
        return;
    }
    WorldLocation const homeLoc = player->GetWorldLocation();
    uint32 const areaId = player->GetAreaId();
    player->SetHomebind(homeLoc, areaId);

    WorldPacket bindPoint(SMSG_BINDPOINTUPDATE, 4 + 4 + 4 + 4 + 4);
    bindPoint << float(homeLoc.GetPositionX());
    bindPoint << float(homeLoc.GetPositionY());
    bindPoint << float(homeLoc.GetPositionZ());
    bindPoint << uint32(homeLoc.GetMapId());
    bindPoint << uint32(areaId);
    player->SendDirectMessage(&bindPoint);

    WorldPacket playerBound(SMSG_PLAYERBOUND, 8 + 4);
    playerBound << player->GetGUID();
    playerBound << uint32(areaId);
    player->SendDirectMessage(&playerBound);

    Say(player, "|cff66ccff[Living Gear]|r Your hearthstone is bound here.");
}

// ---------------------------------------------------------------------
// Wayfarer
// ---------------------------------------------------------------------
//
// One perk, one dial. Everything the player puts into damage comes out of
// movement speed and vice versa: at 0 it is +100% speed and nothing else, at
// 100 it is +100% damage and nothing else, and the middle is +50%/+50%. The
// total never changes, so there is no setting that is simply better than
// another -- only one that suits what you are doing.
//
// Replaces the old *Quest: Wayfarer, which was a flat +40% movement speed for
// completing 100 quests. That was a fine reward and a boring one, and 100
// quests is not "early" for something meant to stand in for not owning a
// mount yet.
//
// Three things stop it being a free win:
//
//   Mounted speed is HALVED. The speed half is meant to bridge the gap before
//   a mount, not to make an epic mount 100% faster on top of its own 100%.
//
//   Swapping costs WAYFARER_SWAP_COOLDOWN and cannot be done in combat.
//   Without that, everyone travels at full speed and flips to full damage on
//   the pull, which is not a choice, it is both halves for free.
//
//   The range starts at +/-50 and only opens to +/-100 through exploration
//   (see WAYFARER_TIERS). A level 6 character does not get +100% damage.
//
// Alt sharing: the UNLOCK is account-wide like every other perk here, but the
// slider position is per character, seeded from the last value used anywhere
// on the account. A protection warrior and a hunter genuinely want different
// settings; nobody wants to set it again on every alt.
uint32 const WAYFARER_SWAP_COOLDOWN = 30; // seconds

// Achievements that unlock and then widen the perk. Every id was read out of
// var/mmap-output/dbc/Achievement.dbc on 2026-08-23 by parsing the file and
// checking the name field (index 4 in this build), not from memory.
//
// Tier 1 is deliberately reachable at level 1 by two very different routes:
// walk every corner of your own starting zone, or survive a 65 yard fall.
// Both are deeds rather than time served, which is what "available early but
// not a freebie" has to mean.
struct WayfarerTier
{
    uint32 spellId;
    uint32 cap;
    char const* label;
    std::vector<uint32> anyOf;
};

std::vector<WayfarerTier> const WAYFARER_TIERS =
{
    { SPELL_WAYFARER, 20, "*Wayfarer",
      { 964,   // Going Down?           -- fall 65 yards and live
        776,   // Explore Elwynn Forest
        627,   // Explore Dun Morogh
        842,   // Explore Teldrassil
        860,   // Explore Azuremyst Isle
        728,   // Explore Durotar
        736,   // Explore Mulgore
        768,   // Explore Tirisfal Glades
        859 }  // Explore Eversong Woods
    },
    { SPELL_WAYFARER_WIDE, 40, "*Wayfarer 2",
      { 42,    // Explore Eastern Kingdoms
        43 }   // Explore Kalimdor
    },
    { SPELL_WAYFARER_FULL, 60, "*Wayfarer 3",
      { 44,    // Explore Outland
        45 }   // Explore Northrend
    },
    // Ranks 4 and 5 carry no achievement route on purpose -- they are bought
    // with skill points like the rest of the progression tracks. The three
    // above keep theirs so nobody loses a rank they already earned.
    { SPELL_WAYFARER_R4, 80, "*Wayfarer 4", { } },
    { SPELL_WAYFARER_R5, 100, "*Wayfarer 5", { } },
};

// Character guid -> percentage of the dial spent on DAMAGE (0-100).
std::unordered_map<uint32, uint32> g_wayfarerPct;
// Character guid -> unix seconds of the last swap, for the cooldown.
std::unordered_map<uint32, uint32> g_wayfarerSwap;

uint32 WayfarerCap(Player* player)
{
    uint32 cap = 0;
    for (WayfarerTier const& tier : WAYFARER_TIERS)
        if (HasPerk(player, tier.spellId) && tier.cap > cap)
            cap = tier.cap;
    return cap;
}

void SendWayfarerState(Player* player)
{
    if (!player)
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const last = g_wayfarerSwap[guid];
    uint32 const left = (last && now < last + WAYFARER_SWAP_COOLDOWN)
        ? last + WAYFARER_SWAP_COOLDOWN - now : 0;
    SendLine(player, Acore::StringFormat("WAY|{}|{}|{}",
        g_wayfarerPct[guid], WayfarerCap(player), left));
}

// Apply both halves with the amounts this character's dial currently asks for.
//
// AddAura (the path the old flat +40% used, and the only one proven to work on
// a PASSIVE spell here) to get the aura on, then ChangeAmount per effect to set
// the real values. The spell's own base points are all zero: every number comes
// from here, and ChangeAmount is what re-runs the effect handler, so it is also
// what makes a slider move take effect without a relog.
void ApplyWayfarer(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetSession())
        return;
    uint32 const cap = WayfarerCap(player);
    if (!cap || !sSpellMgr->GetSpellInfo(SPELL_WAYFARER) || !sSpellMgr->GetSpellInfo(SPELL_WAYFARER_FOCUS))
    {
        player->RemoveAurasDueToSpell(SPELL_WAYFARER);
        player->RemoveAurasDueToSpell(SPELL_WAYFARER_FOCUS);
        return;
    }

    uint32 const guid = player->GetGUID().GetCounter();
    uint32 damage = g_wayfarerPct[guid];
    if (damage > 100)
        damage = 100;
    // The dial is always the full 0-100 split; the tier caps how far either
    // END of it can actually reach, so a tier 1 character slides between
    // +50 speed / +50 damage rather than between +100 and 0.
    // Straight movement speed now: five ranks of 20%, applied equally on foot,
    // mounted and flying. The old design split a dial between speed and damage
    // and halved the mounted share, which meant the number on the tin was never
    // the number you moved at.
    (void)damage;
    int32 const speedPct = int32(cap);
    int32 const mountedPct = speedPct;

    Aura* speed = player->GetAura(SPELL_WAYFARER);
    if (!speed)
        speed = player->AddAura(SPELL_WAYFARER, player);
    if (speed)
    {
        if (AuraEffect* e = speed->GetEffect(EFFECT_0))
            e->ChangeAmount(speedPct);
        if (AuraEffect* e = speed->GetEffect(EFFECT_1))
            e->ChangeAmount(mountedPct);
        if (AuraEffect* e = speed->GetEffect(EFFECT_2))
            e->ChangeAmount(mountedPct);
        speed->SetMaxDuration(-1);
        speed->SetDuration(-1);
    }

    // The damage half is retired. Strip it from anyone still carrying it from
    // before this change rather than leaving a permanent aura nothing updates.
    player->RemoveAurasDueToSpell(SPELL_WAYFARER_FOCUS);
}

void LoadWayfarer(Player* player)
{
    if (!player || !player->GetSession())
        return;
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const accountId = player->GetSession()->GetAccountId();
    if (QueryResult mine = CharacterDatabase.Query(
        "SELECT `dmg_pct` FROM `lg_wayfarer` WHERE `guid` = {}", guid))
    {
        g_wayfarerPct[guid] = std::min<uint32>(100, (*mine)[0].Get<uint32>());
        return;
    }
    // No row yet: inherit whatever this account last chose anywhere, so a new
    // alt starts where its owner left off instead of at zero.
    uint32 seed = 0;
    if (QueryResult acc = CharacterDatabase.Query(
        "SELECT `dmg_pct` FROM `lg_wayfarer` WHERE `account_id` = {} ORDER BY `changed_at` DESC LIMIT 1",
        accountId))
        seed = std::min<uint32>(100, (*acc)[0].Get<uint32>());
    g_wayfarerPct[guid] = seed;
}

// Returns false (with a reason said to the player) when the swap is refused.
bool SetWayfarer(Player* player, uint32 damagePct)
{
    if (!player || !player->GetSession())
        return false;
    if (!WayfarerCap(player))
    {
        Say(player, "|cffff6666[Wayfarer]|r You have not unlocked Wayfarer yet.");
        return false;
    }
    if (damagePct > 100)
        damagePct = 100;

    uint32 const guid = player->GetGUID().GetCounter();
    if (g_wayfarerPct[guid] == damagePct)
        return true; // no-op, and no cooldown burned for one

    if (player->IsInCombat())
    {
        Say(player, "|cffff6666[Wayfarer]|r Not while you are in combat.");
        SendWayfarerState(player);
        return false;
    }
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const last = g_wayfarerSwap[guid];
    if (last && now < last + WAYFARER_SWAP_COOLDOWN)
    {
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff6666[Wayfarer]|r Settling. {} seconds left.",
            last + WAYFARER_SWAP_COOLDOWN - now);
        SendWayfarerState(player);
        return false;
    }

    g_wayfarerPct[guid] = damagePct;
    g_wayfarerSwap[guid] = now;
    CharacterDatabase.Execute(
        "REPLACE INTO `lg_wayfarer` (`guid`, `account_id`, `dmg_pct`, `changed_at`) VALUES ({}, {}, {}, {})",
        guid, player->GetSession()->GetAccountId(), damagePct, now);
    ApplyWayfarer(player);
    SendWayfarerState(player);
    uint32 const cap = WayfarerCap(player);
    ChatHandler(player->GetSession()).PSendSysMessage(
        "|cff66ccff[Wayfarer]|r +{}% movement speed, +{}% damage.",
        (100 - damagePct) * cap / 100, damagePct * cap / 100);
    return true;
}

// Checked on login and on every achievement earned. Each tier is any-of, so
// the faction-split exploration achievements each count on their own.
void CheckWayfarerPerks(Player* player)
{
    if (!player || !player->GetSession())
        return;
    // Cumulative: earning the achievement for a tier grants every tier below it
    // too, so the track is never sparse.
    //
    // The tiers are keyed on unrelated achievements -- exploring your home zone
    // for rank 1, Outland or Northrend for rank 3 -- and nothing makes a player
    // earn them in order. Granting only the matching tier left holes: rank 3
    // owned, ranks 1 and 2 not, and because the speed is the highest cap owned
    // rather than a count, buying those lower ranks afterwards would have cost
    // points and granted no speed at all.
    bool gained = false;
    uint32 highest = 0;
    for (uint32 i = 0; i < WAYFARER_TIERS.size(); ++i)
        for (uint32 achievement : WAYFARER_TIERS[i].anyOf)
            if (player->HasAchieved(achievement))
            {
                highest = i + 1;
                break;
            }

    for (uint32 i = 0; i < highest; ++i)
    {
        WayfarerTier const& tier = WAYFARER_TIERS[i];
        if (HasPerk(player, tier.spellId))
            continue;
        std::string const msg = Acore::StringFormat(
            "|cff66ccff[Account Perks]|r {} unlocked!", tier.label);
        UnlockPerk(player, tier.spellId, msg.c_str());
        gained = true;
    }
    if (gained)
    {
        ApplyWayfarer(player);
        SendWayfarerState(player);
    }
}

bool HandleWayfarerCommand(Player* player, std::string const& msg)
{
    uint32 pct = 0;
    if (std::sscanf(msg.c_str(), "WAYSET|%u", &pct) == 1)
    {
        SetWayfarer(player, pct);
        return true;
    }
    if (msg == "WAYREQ")
    {
        SendWayfarerState(player);
        return true;
    }
    return false;
}

struct LgAmenityConfig
{
    bool learnMail = true;
    bool learnAuction = true;
    bool learnTrainer = true;
    bool learnBank = true;
    bool learnStable = true;
    bool learnBind = true;
    bool learnFlight = true;
    bool autoTrain = true;

    void Load()
    {
        learnMail = sConfigMgr->GetOption<bool>("LivingGear.LearnMailSpell", true);
        learnAuction = sConfigMgr->GetOption<bool>("LivingGear.LearnAuctionSpell", true);
        learnTrainer = sConfigMgr->GetOption<bool>("LivingGear.LearnTrainerSpell", true);
        learnBank = sConfigMgr->GetOption<bool>("LivingGear.LearnBankSpell", true);
        learnStable = sConfigMgr->GetOption<bool>("LivingGear.LearnStableSpell", true);
        learnBind = sConfigMgr->GetOption<bool>("LivingGear.LearnBindSpell", true);
        learnFlight = sConfigMgr->GetOption<bool>("LivingGear.LearnFlightSpell", true);
        autoTrain = sConfigMgr->GetOption<bool>("LivingGear.AutoTrainClassSpells", true);
    }
};

LgAmenityConfig g_cfg;

// Bug report #1, 2026-08-22: "Not auto-training class spells on level up."
//
// There was never any auto-training to break -- the module only ever offered a
// class trainer WINDOW from anywhere (SPELL_TRAINER). This adds the thing the
// report is actually asking for: every ability your own class trainer would
// teach you is learned automatically, free, the moment you qualify.
//
// The trainer's own CanTeachSpell() decides what qualifies, so level gates,
// prerequisite ranks and skill requirements are all judged exactly as they
// would be at the NPC. Money is simply not consulted -- charging for something
// handed over automatically would be a tax on levelling, not a decision.
//
// Runs on level up and again at login, the second so existing characters catch
// up on everything they were owed before this shipped rather than having to
// gain a level first.
void AutoTrainClassSpells(Player* player)
{
    if (!g_cfg.autoTrain || !player || !player->GetSession())
        return;
    uint32 const entry = ClassTrainerEntry(player->getClass());
    if (!entry)
        return;
    Trainer::Trainer const* data = sObjectMgr->GetTrainer(entry);
    if (!data || !data->IsTrainerValidForPlayer(player))
        return;

    uint32 learned = 0;
    for (Trainer::Spell const& trainerSpell : data->GetSpells())
    {
        if (!trainerSpell.SpellId || player->HasSpell(trainerSpell.SpellId))
            continue;
        if (!data->CanTeachSpell(player, &trainerSpell))
            continue;
        player->learnSpell(trainerSpell.SpellId);
        ++learned;
    }
    if (learned)
        Say(player, Acore::StringFormat(
            "|cff66ccff[Living Gear]|r Learned {} new class ability(s).", learned).c_str());
}

void GrantAmenityPerks(Player* player)
{
    if (!player || !player->GetSession())
        return;
    if (g_cfg.learnMail)
        UnlockPerk(player, SPELL_MAILBOX);
    if (g_cfg.learnAuction)
        UnlockPerk(player, SPELL_AUCTION);
    if (g_cfg.learnTrainer)
        UnlockPerk(player, SPELL_TRAINER);
    if (g_cfg.learnBank)
        UnlockPerk(player, SPELL_BANK);
    if (g_cfg.learnStable)
        UnlockPerk(player, SPELL_STABLE);
    if (g_cfg.learnBind)
        UnlockPerk(player, SPELL_BIND);
    if (g_cfg.learnFlight)
        UnlockPerk(player, SPELL_FLIGHT);
}

class AmenitiesWorld : public WorldScript
{
public:
    AmenitiesWorld() : WorldScript("LivingGearAmenitiesWorld", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }
    void OnAfterConfigLoad(bool /*reload*/) override { g_cfg.Load(); }
};

class AmenitiesPlayer : public PlayerScript
{
public:
    AmenitiesPlayer() : PlayerScript("LivingGearAmenitiesPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_LEVEL_CHANGED,
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_ACHI_COMPLETE,
        PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
        PLAYERHOOK_ON_UPDATE_ZONE
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        GrantAmenityPerks(player);
        // Report #179: re-apply every account-unlocked map's taxi nodes so
        // alts inherit the explored network. Only when the Flight amenity is
        // owned -- the unlock trigger is owning that amenity and exploring.
        if (HasPerk(player, SPELL_FLIGHT))
        {
            uint32 const acc = player->GetSession()->GetAccountId();
            bool const firstLoad = !LoadAccountTaxiMaps(acc);
            for (uint32 mapId : g_accountTaxi[acc].maps)
                ApplyTaxiNodesForMap(player, mapId);
            (void)firstLoad;
        }
        LoadWayfarer(player);
        CheckWayfarerPerks(player);
        ApplyWayfarer(player);
        SendWayfarerState(player);
        AutoTrainClassSpells(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        uint32 const guid = player->GetGUID().GetCounter();
        g_wayfarerPct.erase(guid);
        g_wayfarerSwap.erase(guid);
    }

    // Every Wayfarer tier is an achievement, so this is the moment one can
    // become true without a relog.
    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* /*achievement*/) override
    {
        CheckWayfarerPerks(player);
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        AutoTrainClassSpells(player);
    }

    // Report #179: first visit to a map (with the Flight amenity owned)
    // unlocks every flight path on that map for the whole account.
    void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        if (!player || !player->GetSession())
            return;
        if (!HasPerk(player, SPELL_FLIGHT))
            return;
        UnlockTaxiMapForAccount(player, player->GetMapId());
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skip*/) override
    {
        if (!player || !spell)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return;
        uint32 const id = info->Id;

        if (id == SPELL_MAILBOX)
        {
            if (player->GetSession())
                player->GetSession()->SendShowMailBox(player->GetGUID());
            return;
        }
        if (id == SPELL_AUCTION)
        {
            uint32 const entry = player->GetTeamId() == TEAM_ALLIANCE ? NPC_AH_ALLIANCE : NPC_AH_HORDE;
            if (TempSummon* auctioneer = SummonInvisibleHelper(player, entry))
                if (player->GetSession())
                    player->GetSession()->SendAuctionHello(auctioneer->GetGUID(), auctioneer);
            return;
        }
        if (id == SPELL_TRAINER)
        {
            OpenClassTrainer(player);
            return;
        }
        if (id == SPELL_BANK)
        {
            if (player->GetSession())
                player->GetSession()->SendShowBank(player->GetGUID());
            return;
        }
        if (id == SPELL_STABLE)
        {
            if (TempSummon* stable = SummonInvisibleHelper(player, NPC_STABLE))
            {
                stable->SetFaction(FRIENDLY_FACTION);
                if (player->GetSession())
                    player->GetSession()->SendStablePet(stable->GetGUID());
            }
            return;
        }
        if (id == SPELL_BIND)
        {
            BindHearthHere(player);
            return;
        }
        if (id == SPELL_FLIGHT)
        {
            if (!sObjectMgr->GetNearestTaxiNode(player->GetWorldLocation(), player->GetTeamId(true)))
            {
                Say(player, "|cffff6666[Living Gear]|r No flight path on this map.");
                return;
            }
            uint32 const flightEntry = player->GetTeamId() == TEAM_ALLIANCE
                ? NPC_FLIGHT_ALLIANCE : NPC_FLIGHT_HORDE;
            if (TempSummon* flight = SummonInvisibleHelper(player, flightEntry))
            {
                flight->SetFaction(FRIENDLY_FACTION);
                if (player->GetSession())
                    player->GetSession()->SendTaxiMenu(flight);
            }
            return;
        }
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* /*quest*/) override
    {
        UnlockPerk(player, SPELL_AUTO_QUEST, "|cff66ccff[Account Perks]|r *Quests - Finish unlocked!");
    }
};
} // namespace LivingGearAmenities

using namespace LivingGearAmenities;

// Wayfarer's slider. Every addon command in this module goes through the one
// dispatcher in LivingGear.cpp -- see the note there about buttons that do
// nothing because nothing was listening.
bool LivingGear_HandleAmenitiesCommand(Player* player, std::string const& msg)
{
    return LivingGearAmenities::HandleWayfarerCommand(player, msg);
}

// Answers the client's REQ alongside every other sync. Login alone is not
// enough: the addon re-syncs on /reload and on entering the world, and a
// state line sent before the addon existed reaches nobody.
void LivingGear_SendWayfarerSync(Player* player)
{
    LivingGearAmenities::SendWayfarerState(player);
}

void AddSC_LivingGearAmenities()
{
    new AmenitiesWorld();
    new AmenitiesPlayer();
}
