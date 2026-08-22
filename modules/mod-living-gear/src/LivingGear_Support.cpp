/*
 * Living Gear player-support tools.
 *
 * Two things live here, both of which exist so a player can deal with a
 * problem themselves instead of waiting for someone with GM rights:
 *
 *   Bug reports (2026-08-22): ".bug <description>" in chat, or /bug from the
 *   addon, files a report into `lg_bug_report` along with where the player
 *   was standing and what they had targeted. A separate scheduled script
 *   (tools/bug-reports/bug_digest.py) posts new rows to Discord every 15
 *   minutes and marks them sent. Nothing in the worldserver talks to Discord --
 *   it only ever writes rows.
 *
 *   Quest Complete (2026-08-22): force-completes the quest the player has
 *   selected in their quest log, on a 10 minute cooldown. This is a repair
 *   tool for quests that have bugged out, not a way to skip content -- the
 *   cooldown is short enough that being stuck is a minor annoyance rather
 *   than a lost evening, and long enough that it cannot be used to chain
 *   through a zone. Deliberately has NO spell and NO spellbook entry: it is
 *   a button on the quest log frame, driven entirely over the addon channel.
 */

#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

#include <cstdio>
#include <string>
#include <unordered_map>

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp

namespace LivingGearSupport
{
// One report per minute per account. Generous for anyone actually reporting
// something, and enough to stop a stuck key or a frustrated player filling
// the table (and the Discord channel) in a single sitting.
uint32 const BUG_COOLDOWN_SECONDS = 60;
uint32 const BUG_MAX_LENGTH = 500;
uint32 const BUG_MIN_LENGTH = 5;

// Quest Complete's cooldown. Persisted, so relogging does not reset it.
uint32 const QUEST_COMPLETE_COOLDOWN = 600;

std::unordered_map<uint32, uint32> g_lastBugAt;      // account id -> unix seconds
std::unordered_map<uint32, uint32> g_questCooldown;  // character guid -> unix seconds

void SendLine(Player* player, std::string const& line)
{
    ::LivingGear_SendAddonLine(player, line);
}

std::string Escape(std::string s)
{
    CharacterDatabase.EscapeString(s);
    return s;
}

std::string ZoneName(Player* player)
{
    if (!player)
        return "";
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetAreaId());
    if (!area)
        return "";
    char const* name = area->area_name[LOCALE_enUS];
    return name ? name : "";
}

std::string Trim(std::string s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

// ---------------------------------------------------------------------
// Bug reports
// ---------------------------------------------------------------------

// Context is the whole point. "The chest in Uldaman does not open" is close
// to unactionable; the same sentence with a map, a zone, exact coordinates,
// the reporter's level and the entry id of whatever they had targeted
// usually points straight at the row that needs fixing.
bool RecordBugReport(Player* player, std::string const& description)
{
    if (!player || !player->GetSession())
        return false;

    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 const now = uint32(GameTime::GetGameTime().count());

    std::string text = Trim(description);
    if (text.size() < BUG_MIN_LENGTH)
    {
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cff66ccff[Bug]|r Say a little more about what went wrong.");
        return false;
    }
    if (text.size() > BUG_MAX_LENGTH)
        text.resize(BUG_MAX_LENGTH);

    uint32 const last = g_lastBugAt[accountId];
    if (last && now - last < BUG_COOLDOWN_SECONDS)
    {
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff66ccff[Bug]|r Give it {} more second(s) before the next report.",
            BUG_COOLDOWN_SECONDS - (now - last));
        return false;
    }
    g_lastBugAt[accountId] = now;

    uint32 targetEntry = 0;
    std::string targetName;
    if (Unit* target = player->GetSelectedUnit())
    {
        targetName = target->GetName();
        if (Creature* creature = target->ToCreature())
            targetEntry = creature->GetEntry();
    }

    CharacterDatabase.Execute(
        "INSERT INTO `lg_bug_report` "
        "(`account_id`, `character_guid`, `character_name`, `reported_at`, `map_id`, `zone_id`, "
        "`zone_name`, `pos_x`, `pos_y`, `pos_z`, `player_level`, `target_entry`, `target_name`, `description`) "
        "VALUES ({}, {}, '{}', {}, {}, {}, '{}', {}, {}, {}, {}, {}, '{}', '{}')",
        accountId, player->GetGUID().GetCounter(), Escape(player->GetName()), now,
        player->GetMapId(), player->GetZoneId(), Escape(ZoneName(player)),
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
        uint32(player->GetLevel()), targetEntry, Escape(targetName), Escape(text));

    // Also written to the worldserver log, so a report survives the digest
    // script being broken or the characters DB being rolled back.
    LOG_INFO("module.livinggear", "Bug report from {} (account {}): {} [map {} zone {} at {} {} {}]",
        player->GetName(), accountId, text, player->GetMapId(), player->GetZoneId(),
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());

    ChatHandler(player->GetSession()).SendSysMessage(
        "|cff66ccff[Bug]|r Reported, thank you. Your location and target were included.");
    return true;
}

// ---------------------------------------------------------------------
// Quest Complete
// ---------------------------------------------------------------------

uint32 LastQuestComplete(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    auto const cached = g_questCooldown.find(guid);
    if (cached != g_questCooldown.end())
        return cached->second;
    uint32 last = 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `last_used` FROM `lg_quest_complete` WHERE `guid` = {}", guid))
        last = (*result)[0].Get<uint32>();
    g_questCooldown[guid] = last;
    return last;
}

// Mirrors the GM ".quest complete" command (cs_quest.cpp) step for step:
// hand over required items, credit every creature/GO objective, credit player
// kills, satisfy reputation objectives, take required money, then mark the
// quest complete. Doing less than all of that leaves the quest log showing
// "Creature slain 0/10" beside a quest the client believes is finished.
bool ForceCompleteQuest(Player* player, uint32 questId)
{
    if (!player || !player->GetSession())
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return false;

    ChatHandler handler(player->GetSession());
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const last = LastQuestComplete(player);
    if (last && now - last < QUEST_COMPLETE_COOLDOWN)
    {
        uint32 const left = QUEST_COMPLETE_COOLDOWN - (now - last);
        handler.PSendSysMessage("|cff66ccff[Quest]|r Complete Quest is on cooldown for another {}m {}s.",
            left / 60, left % 60);
        return false;
    }

    QuestStatus const status = player->GetQuestStatus(questId);
    if (status == QUEST_STATUS_NONE)
    {
        handler.SendSysMessage("|cff66ccff[Quest]|r That quest is not in your log.");
        return false;
    }
    if (status == QUEST_STATUS_COMPLETE)
    {
        handler.SendSysMessage("|cff66ccff[Quest]|r That quest is already complete -- go turn it in.");
        return false;
    }

    for (uint8 x = 0; x < QUEST_ITEM_OBJECTIVES_COUNT; ++x)
    {
        uint32 const id = quest->RequiredItemId[x];
        uint32 const count = quest->RequiredItemCount[x];
        if (!id || !count)
            continue;
        uint32 const have = player->GetItemCount(id, true);
        if (have >= count)
            continue;
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, id, count - have) == EQUIP_ERR_OK)
            if (Item* item = player->StoreNewItem(dest, id, true))
                player->SendNewItem(item, count - have, true, false);
    }

    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        int32 const creature = quest->RequiredNpcOrGo[i];
        uint32 const creatureCount = quest->RequiredNpcOrGoCount[i];
        if (creature > 0)
        {
            if (CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(creature))
                for (uint16 z = 0; z < creatureCount; ++z)
                    player->KilledMonster(info, ObjectGuid::Empty);
        }
        else if (creature < 0)
        {
            for (uint16 z = 0; z < creatureCount; ++z)
                player->KillCreditGO(creature);
        }
    }

    if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_PLAYER_KILL))
        if (uint32 const reqPlayers = quest->GetPlayersSlain())
            player->KilledPlayerCreditForQuest(reqPlayers, quest);

    if (uint32 const repFaction = quest->GetRepObjectiveFaction())
    {
        uint32 const repValue = quest->GetRepObjectiveValue();
        if (player->GetReputationMgr().GetReputation(repFaction) < int32(repValue))
            if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
                player->GetReputationMgr().SetReputation(factionEntry, float(repValue));
    }
    if (uint32 const repFaction2 = quest->GetRepObjectiveFaction2())
    {
        uint32 const repValue2 = quest->GetRepObjectiveValue2();
        if (player->GetReputationMgr().GetReputation(repFaction2) < int32(repValue2))
            if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction2))
                player->GetReputationMgr().SetReputation(factionEntry, float(repValue2));
    }

    int32 const reqOrRewMoney = quest->GetRewOrReqMoney(player->GetLevel());
    if (reqOrRewMoney < 0)
        player->ModifyMoney(-reqOrRewMoney);

    player->CompleteQuest(questId);

    g_questCooldown[player->GetGUID().GetCounter()] = now;
    CharacterDatabase.Execute(
        "REPLACE INTO `lg_quest_complete` (`guid`, `last_used`) VALUES ({}, {})",
        player->GetGUID().GetCounter(), now);

    handler.PSendSysMessage("|cff66ccff[Quest]|r Completed that quest. Turn it in as normal.");
    LOG_INFO("module.livinggear", "Quest Complete used by {} on quest {}", player->GetName(), questId);

    SendLine(player, Acore::StringFormat("QDONECD|{}", QUEST_COMPLETE_COOLDOWN));
    return true;
}

// Pushed at login and on request so the quest log button starts out showing
// the right remaining cooldown rather than always looking ready.
void SendQuestCompleteState(Player* player)
{
    if (!player)
        return;
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const last = LastQuestComplete(player);
    uint32 const left = (last && now - last < QUEST_COMPLETE_COOLDOWN)
        ? QUEST_COMPLETE_COOLDOWN - (now - last) : 0;
    SendLine(player, Acore::StringFormat("QDONECD|{}", left));
}

// ---------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------

class SupportPlayer : public PlayerScript
{
public:
    SupportPlayer() : PlayerScript("LivingGearSupportPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        SendQuestCompleteState(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (player)
            g_questCooldown.erase(player->GetGUID().GetCounter());
    }
};

using namespace Acore::ChatCommands;

class SupportCommands : public CommandScript
{
public:
    SupportCommands() : CommandScript("LivingGearSupportCommands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "bug", HandleBug, rbac::RBAC_PERM_COMMAND_HELP, Console::No },
        };
        return commandTable;
    }

    static bool HandleBug(ChatHandler* handler, Tail description)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        RecordBugReport(player, std::string(description));
        return true;
    }
};

} // namespace LivingGearSupport

// Addon-command entry point, called by the dispatcher in LivingGear.cpp.
bool LivingGear_HandleSupportCommand(Player* player, std::string const& msg)
{
    if (!player)
        return false;

    if (msg.rfind("BUG|", 0) == 0)
    {
        LivingGearSupport::RecordBugReport(player, msg.substr(4));
        return true;
    }
    if (msg.rfind("QDONE|", 0) == 0)
    {
        uint32 questId = 0;
        if (sscanf(msg.c_str(), "QDONE|%u", &questId) == 1 && questId)
            LivingGearSupport::ForceCompleteQuest(player, questId);
        return true;
    }
    if (msg == "QDONEREQ")
    {
        LivingGearSupport::SendQuestCompleteState(player);
        return true;
    }
    return false;
}

void AddSC_LivingGearSupport()
{
    new LivingGearSupport::SupportPlayer();
    new LivingGearSupport::SupportCommands();
}
