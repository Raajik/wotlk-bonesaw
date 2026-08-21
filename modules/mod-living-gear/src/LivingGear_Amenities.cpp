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
 * Quest Wayfarer (910038): +40% movement speed, permanently, once the
 * account has 100 rewarded quests across any character.
 */

#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Trainer.h"
#include "Unit.h"
#include "WorldSession.h"

#include <chrono>
#include <unordered_set>

using namespace std::chrono_literals;

namespace LivingGearAmenities
{
uint32 const SPELL_MAILBOX = 910002;
uint32 const SPELL_AUCTION = 910003;
uint32 const SPELL_TRAINER = 910004;
uint32 const SPELL_BANK = 910005;
uint32 const SPELL_STABLE = 910006;
uint32 const SPELL_BIND = 910007;
uint32 const SPELL_FLIGHT = 910009;
uint32 const SPELL_QUEST_SPEED = 910038;

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
    if (!player || !player->GetSession())
        return;
    player->Whisper(std::string("LG\t") + line, LANG_ADDON, player);
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
    if (!g_perks[acc].insert(spellId).second)
        return;
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO `lg_account_perk` (`account_id`, `spell_id`) VALUES ({}, {})",
        acc, spellId);
    if (!player->HasSpell(spellId))
        player->learnSpell(spellId);
    SendLine(player, Acore::StringFormat("PK|{}|1", spellId));
    if (msg)
        Say(player, msg);
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

void ApplyQuestSpeedAura(Player* player)
{
    if (!player || !player->IsAlive() || !player->GetSession())
        return;
    if (!HasPerk(player, SPELL_QUEST_SPEED))
        return;
    if (player->HasAura(SPELL_QUEST_SPEED))
        return;
    if (Aura* aura = player->AddAura(SPELL_QUEST_SPEED, player))
    {
        aura->SetDuration(-1);
        aura->SetMaxDuration(-1);
    }
}

void CheckQuestSpeedPerk(Player* player)
{
    if (!player || !player->GetSession())
        return;
    if (HasPerk(player, SPELL_QUEST_SPEED))
        return;
    if (player->GetRewardedQuestCount() >= QUEST_SPEED_NEED)
    {
        UnlockPerk(player, SPELL_QUEST_SPEED, "|cff66ccff[Account Perks]|r *Quest: Wayfarer unlocked!");
        return;
    }
    uint32 const accountId = player->GetSession()->GetAccountId();
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT COALESCE(MAX(`cnt`), 0) FROM ("
        "SELECT COUNT(*) AS `cnt` FROM `character_queststatus_rewarded` `r` "
        "INNER JOIN `characters` `c` ON `c`.`guid` = `r`.`guid` "
        "WHERE `c`.`account` = {} GROUP BY `r`.`guid`) `t`",
        accountId))
    {
        if ((*result)[0].Get<uint32>() >= QUEST_SPEED_NEED)
            UnlockPerk(player, SPELL_QUEST_SPEED, "|cff66ccff[Account Perks]|r *Quest: Wayfarer unlocked!");
    }
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

    void Load()
    {
        learnMail = sConfigMgr->GetOption<bool>("LivingGear.LearnMailSpell", true);
        learnAuction = sConfigMgr->GetOption<bool>("LivingGear.LearnAuctionSpell", true);
        learnTrainer = sConfigMgr->GetOption<bool>("LivingGear.LearnTrainerSpell", true);
        learnBank = sConfigMgr->GetOption<bool>("LivingGear.LearnBankSpell", true);
        learnStable = sConfigMgr->GetOption<bool>("LivingGear.LearnStableSpell", true);
        learnBind = sConfigMgr->GetOption<bool>("LivingGear.LearnBindSpell", true);
        learnFlight = sConfigMgr->GetOption<bool>("LivingGear.LearnFlightSpell", true);
    }
};

LgAmenityConfig g_cfg;

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
        PLAYERHOOK_ON_SPELL_CAST,
        PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        GrantAmenityPerks(player);
        ApplyQuestSpeedAura(player);
        CheckQuestSpeedPerk(player);
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
        CheckQuestSpeedPerk(player);
    }
};
} // namespace LivingGearAmenities

using namespace LivingGearAmenities;

void AddSC_LivingGearAmenities()
{
    new AmenitiesWorld();
    new AmenitiesPlayer();
}
