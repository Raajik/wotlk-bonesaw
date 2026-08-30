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
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "AllGameObjectScript.h"
#include "AreaDefines.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "GameObject.h"
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
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

#include <algorithm>
#include <cstdio>
#include <map>
#include <limits>
#include <string>
#include <unordered_map>

class Player;
void LivingGear_SendAddonLine(Player* player, std::string const& line); // LivingGear.cpp
bool LivingGear_PlayerNeedsItemForQuest(Player const* player, uint32 itemId); // LivingGear_Vault.cpp
uint32 GetClassPerk(Player* player); // LivingGear_ClassPerks.cpp

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

// Gold buyout for the cooldown. Read from config in LivingGear_SupportConfig().
// 500g base, per user request: this is meant to hurt if you lean on it. With
// the doubling below, a second buyout inside the hour is 1000g and a third is
// 2000g, which is the "costly in rapid succession" the sink is for.
uint32 g_questBypassBase = 5000000;     // copper
float  g_questBypassMult = 2.0f;        // per buyout inside the window
uint32 g_questBypassWindow = 3600;      // window length, seconds

// Bug report #12: quest items always drop. Bug report #11: every kill is worth
// something. Both are config-gated so they can be turned off without a rebuild.
bool g_questDropAlways = true;
bool g_killXpFloorEnabled = true;
// Percent of the CURRENT level's XP bar that a PLAYER kill is worth at
// minimum. Creature kills are floored by KillXpFor in LivingGear_Perks.cpp
// instead -- it is the only place that knows the effective level, the elite
// tier and the zone multiplier, and applying a second floor here would just
// be a worse copy of it.
//
// Raised from 1 to 2 alongside that funnel: 50 kills a level, and a
// battleground honorable kill is now worth the same as a mob, which is what
// makes BG levelling viable at all.
uint32 g_killXpFloorPct = 2;

// Bug report #3: Wintergrasp siege damage scales with how many people are
// actually there. WG_FULL_ROSTER is the population the stock building health
// is balanced around -- at or above it nothing changes at all. Below it,
// damage is divided by the shortfall, capped at WG_MAX_SIEGE_MULT.
//
// 20 and 10 together give exactly what was asked for: "if there's only a couple
// of players, make them do 10x normal damage" -- two players hit 20/2 = 10x.
bool g_wgSiegeScale = true;
uint32 g_wgFullRoster = 20;
float g_wgMaxSiegeMult = 10.0f;

// ---------------------------------------------------------------------
// Live diagnostics (.lg diag)
// ---------------------------------------------------------------------
//
// Three reports in a row -- #15 solid chests, #20 profession skill-ups, #23
// Hemorrhage -- read correctly in the source and did not work in game. Every
// one cost a round of static analysis that proved nothing, because the thing
// worth knowing is not "is the code right" but "did this line actually run for
// THIS player". That is not answerable by reading.
//
// So: cheap named counters, bumped at the points that matter, dumped on
// request. A count of 0 next to "hemo.hook" is a different bug from a count of
// 12 next to it and 0 next to "hemo.hit", and telling those apart is the whole
// job.
//
// Costs one hash lookup and an increment on paths that already do far more,
// and nothing at all until someone types the command.
std::unordered_map<uint32, std::map<std::string, uint32>> g_diag;

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
// Bug reports and feature requests
// ---------------------------------------------------------------------

// Report intake, 2026-08-29 redesign: one ".bug" entry point with a small
// addon form. The player picks bug / feature / other and ticks Critical and
// Recurring separately, so the kind no longer lives in which command they
// typed. .feature and .crit still work (mapped onto the flags below) so old
// muscle memory and old addon copies keep filing reports.
//
// is_recurring marks "still not working / keeps happening": feedback on a
// previous fix rather than a brand-new problem.
enum LgReportKind { LG_REPORT_BUG = 0, LG_REPORT_FEATURE = 1, LG_REPORT_OTHER = 2 };

// [item:12345] in the report text -> a real clickable item link.
std::string ExpandItemLinks(std::string text);

// Context is the whole point. "The chest in Uldaman does not open" is close
// to unactionable; the same sentence with a map, a zone, exact coordinates,
// the reporter's level and the entry id of whatever they had targeted
// usually points straight at the row that needs fixing.
bool RecordSupportReport(Player* player, std::string const& description, uint8 kind,
    bool critical = false, bool recurring = false)
{
    if (!player || !player->GetSession())
        return false;

    static char const* const KIND_TYPE[3] = { "bug", "feature", "other" };

    uint32 const accountId = player->GetSession()->GetAccountId();
    uint32 const now = uint32(GameTime::GetGameTime().count());

    std::string text = Trim(description);
    if (text.size() < BUG_MIN_LENGTH)
    {
        ChatHandler(player->GetSession()).SendSysMessage(
            "|cff66ccff[Report]|r Say a little more about what went wrong or what you would like.");
        return false;
    }
    if (text.size() > BUG_MAX_LENGTH)
        text.resize(BUG_MAX_LENGTH);

    // [item:12345] becomes a real item link, so a report about a specific item
    // reads in game without the reporter having to own or target it. Unknown
    // ids are left as typed rather than dropped silently.
    text = ExpandItemLinks(text);

    uint32 const last = g_lastBugAt[accountId];
    if (last && now - last < BUG_COOLDOWN_SECONDS)
    {
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff66ccff[Report]|r Give it {} more second(s) before the next report.",
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
        "(`report_type`, `is_critical`, `is_recurring`, `account_id`, `character_guid`, `character_name`, `reported_at`, `map_id`, `zone_id`, "
        "`zone_name`, `pos_x`, `pos_y`, `pos_z`, `player_level`, `target_entry`, `target_name`, `description`) "
        "VALUES ('{}', {}, {}, {}, {}, '{}', {}, {}, {}, '{}', {}, {}, {}, {}, {}, '{}', '{}')",
        KIND_TYPE[kind], critical ? 1 : 0, recurring ? 1 : 0,
        accountId, player->GetGUID().GetCounter(), Escape(player->GetName()), now,
        player->GetMapId(), player->GetZoneId(), Escape(ZoneName(player)),
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
        uint32(player->GetLevel()), targetEntry, Escape(targetName), Escape(text));

    // Also written to the worldserver log, so a report survives the digest
    // script being broken or the characters DB being rolled back.
    LOG_INFO("module.livinggear", "{} report{}{} from {} (account {}): {} [map {} zone {} at {} {} {}]",
        critical ? "CRITICAL" : KIND_TYPE[kind], critical ? " [critical]" : "", recurring ? " [recurring]" : "",
        player->GetName(), accountId, text, player->GetMapId(), player->GetZoneId(),
        player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());

    ChatHandler(player->GetSession()).SendSysMessage(
        critical ? "|cffff3333[CRITICAL]|r Reported, thank you. Your location and target were included."
                  : "|cff66ccff[Report]|r Reported, thank you. Your location and target were included.");
    return true;
}

// [item:12345] -> clickable item link. Kept tolerant: whitespace and a
// missing id are left untouched so nothing the player typed disappears.
std::string ExpandItemLinks(std::string text)
{
    std::size_t pos = 0;
    while ((pos = text.find("[item:", pos)) != std::string::npos)
    {
        std::size_t const close = text.find(']', pos);
        if (close == std::string::npos)
            break;
        uint32 entry = 0;
        if (sscanf(text.c_str() + pos, "[item:%u]", &entry) != 1 || !entry)
        {
            pos += 6;
            continue;
        }
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        if (!proto)
        {
            pos = close + 1;
            continue;
        }
        std::string const name = proto->Name1;
        uint32 const quality = std::min<uint32>(proto->Quality, MAX_ITEM_QUALITY - 1);
        // |c<8 hex>|Hitem:entry:0:0:0:0:0:0:0:0|h[Name]|h|r -- colors from the
        // same table the client's own item links use (Hyperlinks.cpp).
        char link[256];
        snprintf(link, sizeof(link), "|cff%06x|Hitem:%u:0:0:0:0:0:0:0:0|h[%s]|h|r",
            ItemQualityColors[quality] & 0xFFFFFFu, entry, Escape(name).c_str());
        text.replace(pos, close - pos + 1, link);
        pos += strlen(link);
    }
    return text;
}

// ---------------------------------------------------------------------
// Quest Complete
// ---------------------------------------------------------------------

// Keyed on the ACCOUNT, not the character.
//
// I argued for per-character on the grounds that this is a repair tool and an
// account-wide cooldown just means fixing a quest on your main blocks your alt.
// That was the wrong read, and the user's is better: it is not only a repair
// tool, it is also "skip the escort quest I genuinely hate", and that half has
// to be a scarce account resource or ten minutes stops meaning anything.
// Rationing it per account is the point.
uint32 QuestAccountOf(Player* player)
{
    return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
}

uint32 LastQuestComplete(Player* player)
{
    uint32 const acc = QuestAccountOf(player);
    auto const cached = g_questCooldown.find(acc);
    if (cached != g_questCooldown.end())
        return cached->second;
    uint32 last = 0;
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `last_used` FROM `lg_quest_complete` WHERE `account_id` = {}", acc))
        last = (*result)[0].Get<uint32>();
    g_questCooldown[acc] = last;
    return last;
}

// What it costs to skip the rest of the wait.
//
// Requested as a "lazy man's gold sink", and a flat fee is not one -- it is
// rounding error to anyone who has been playing a while, and gold is pooled
// account-wide here. So the price escalates with every buyout inside a rolling
// window and decays back to base once the player stops leaning on it. Without
// that decay the price would ratchet upward forever and the feature would be
// dead after one bad afternoon.
//
// It also scales with the time actually being skipped, which is the part that
// makes it feel fair: buying out nine minutes should not cost the same as
// buying out twenty seconds.
uint32 QuestBypassCost(Player* player, uint32 secondsLeft)
{
    if (!secondsLeft)
        return 0;
    uint32 const acc = QuestAccountOf(player);
    uint32 count = 0, windowStart = 0;
    if (QueryResult r = CharacterDatabase.Query(
        "SELECT `bypass_count`, `window_start` FROM `lg_quest_complete` WHERE `account_id` = {}", acc))
    {
        count = (*r)[0].Get<uint32>();
        windowStart = (*r)[1].Get<uint32>();
    }
    uint32 const now = uint32(GameTime::GetGameTime().count());
    if (!windowStart || now - windowStart >= g_questBypassWindow)
        count = 0;      // window lapsed, price is back to base

    double cost = double(g_questBypassBase);
    cost *= double(secondsLeft) / double(QUEST_COMPLETE_COOLDOWN);
    for (uint32 i = 0; i < count && i < 16; ++i)   // clamped: 2^16 is already absurd
        cost *= g_questBypassMult;
    if (cost < 1.0)
        cost = 1.0;
    if (cost > double(std::numeric_limits<uint32>::max()))
        return std::numeric_limits<uint32>::max();
    return uint32(cost);
}

void NoteQuestBypass(Player* player)
{
    uint32 const acc = QuestAccountOf(player);
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 count = 0, windowStart = 0;
    if (QueryResult r = CharacterDatabase.Query(
        "SELECT `bypass_count`, `window_start` FROM `lg_quest_complete` WHERE `account_id` = {}", acc))
    {
        count = (*r)[0].Get<uint32>();
        windowStart = (*r)[1].Get<uint32>();
    }
    if (!windowStart || now - windowStart >= g_questBypassWindow)
    {
        count = 0;
        windowStart = now;
    }
    CharacterDatabase.Execute(
        "INSERT INTO `lg_quest_complete` (`account_id`, `last_used`, `bypass_count`, `window_start`) "
        "VALUES ({}, 0, {}, {}) ON DUPLICATE KEY UPDATE `bypass_count` = {}, `window_start` = {}",
        acc, count + 1, windowStart, count + 1, windowStart);
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

    g_questCooldown[QuestAccountOf(player)] = now;
    CharacterDatabase.Execute(
        "INSERT INTO `lg_quest_complete` (`account_id`, `last_used`) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE `last_used` = {}",
        QuestAccountOf(player), now, now);

    handler.PSendSysMessage("|cff66ccff[Quest]|r Completed that quest. Turn it in as normal.");
    LOG_INFO("module.livinggear", "Quest Complete used by {} on quest {}", player->GetName(), questId);

    // Two fields, same as SendQuestCompleteState. This was still sending one,
    // so the moment the button was used the client got no price and could
    // never offer the buyout -- the feature was unreachable in the only state
    // it exists for.
    SendLine(player, Acore::StringFormat("QDONECD|{}|{}", QUEST_COMPLETE_COOLDOWN,
        QuestBypassCost(player, QUEST_COMPLETE_COOLDOWN)));
    return true;
}

// Pay gold to skip the rest of the cooldown, then complete the quest.
//
// The money is taken BEFORE the completion is attempted and refunded if that
// attempt fails, rather than the other way round -- ForceCompleteQuest has a
// dozen reasons to refuse (quest not in the log, already complete, and so on)
// and charging for a no-op would be the worst possible bug in a feature whose
// entire job is taking gold.
void BuyOutQuestCooldown(Player* player, uint32 questId)
{
    if (!player || !player->GetSession())
        return;
    ChatHandler handler(player->GetSession());
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const last = LastQuestComplete(player);
    uint32 const left = (last && now - last < QUEST_COMPLETE_COOLDOWN)
        ? QUEST_COMPLETE_COOLDOWN - (now - last) : 0;
    if (!left)
    {
        handler.SendSysMessage("|cff66ccff[Quest]|r Complete Quest is already off cooldown -- no need to pay.");
        return;
    }

    uint32 const cost = QuestBypassCost(player, left);
    if (player->GetMoney() < cost)
    {
        handler.PSendSysMessage("|cff66ccff[Quest]|r Skipping the remaining {}m {}s costs {}g {}s. You do not have it.",
            left / 60, left % 60, cost / 10000, (cost % 10000) / 100);
        return;
    }

    player->ModifyMoney(-int32(cost));
    // Clear the cooldown so ForceCompleteQuest's own check passes, then let it
    // set a fresh one on success exactly as a normal use would.
    g_questCooldown[QuestAccountOf(player)] = 0;
    CharacterDatabase.Execute(
        "INSERT INTO `lg_quest_complete` (`account_id`, `last_used`) VALUES ({}, 0) "
        "ON DUPLICATE KEY UPDATE `last_used` = 0", QuestAccountOf(player));

    if (!ForceCompleteQuest(player, questId))
    {
        player->ModifyMoney(int32(cost));      // refund; it told the player why
        g_questCooldown[QuestAccountOf(player)] = last;
        CharacterDatabase.Execute(
            "INSERT INTO `lg_quest_complete` (`account_id`, `last_used`) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE `last_used` = {}", QuestAccountOf(player), last, last);
        return;
    }

    NoteQuestBypass(player);
    handler.PSendSysMessage("|cff66ccff[Quest]|r Paid {}g to skip the wait.", cost / 10000);
    LOG_INFO("module.livinggear", "Quest Complete cooldown bought out by {} for {} copper (quest {})",
        player->GetName(), cost, questId);
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
    // Second field is what a buyout would cost right now, so the client can
    // label the button without having to model the price curve itself.
    SendLine(player, Acore::StringFormat("QDONECD|{}|{}", left, QuestBypassCost(player, left)));
}

// ---------------------------------------------------------------------
// Wintergrasp: join from anywhere
// ---------------------------------------------------------------------

void ShowDiagnostics(Player* player)
{
    if (!player || !player->GetSession())
        return;
    ChatHandler handler(player->GetSession());
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const accountId = player->GetSession()->GetAccountId();

    // PSendSysMessage is fmt, not printf. Using %s/%u here printed the format
    // string literally to the player and lost every value -- which is exactly
    // what happened the first time this was used in anger.
    handler.PSendSysMessage("|cff66ccff[Diag]|r {}  char guid {}  account {}  level {}  class {}",
        player->GetName(), guid, accountId, uint32(player->GetLevel()),
        uint32(player->getClass()));

    uint32 const classPerk = ::GetClassPerk(player);
    if (classPerk)
        handler.PSendSysMessage("|cff66ccff[Diag]|r class perk selected: {}", classPerk);
    else
        handler.SendSysMessage("|cff66ccff[Diag]|r class perk selected: NONE -- every spec-gated feature is off for you");

    auto const found = g_diag.find(guid);
    if (found == g_diag.end() || found->second.empty())
    {
        handler.SendSysMessage("|cff66ccff[Diag]|r no counters recorded yet this session.");
        handler.SendSysMessage("|cff66ccff[Diag]|r Use the ability you are reporting, then run this again.");
        return;
    }
    handler.SendSysMessage("|cff66ccff[Diag]|r counters since this character logged in:");
    for (auto const& entry : found->second)
        handler.PSendSysMessage("    {:<28} {}", entry.first, entry.second);
}

// Bug report #4, 2026-08-22: "wintergrasp should be queuable via the blue
// button on the battlegrounds queue tab (the built-in blizzard ui one)".
//
// The stock battlegrounds tab cannot list Wintergrasp. It is driven by
// BattlemasterList.dbc, which has 13 rows in this client and no Wintergrasp
// among them -- WG is a Battlefield, not a Battleground, and was never
// queueable that way even on retail. Putting it in that tab would mean adding
// a client DBC row AND building Battlefield queue plumbing that does not
// exist. See the reply notes; that is a design job, not a fix.
//
// What this does instead reaches the same goal through Blizzard's own UI:
// InvitePlayerToWar sends SMSG_BATTLEFIELD_MGR_ENTRY_INVITE, which is the
// native battlefield invite popup with its Accept button -- the same dialog
// players already see when standing in the zone as a battle begins. Now they
// can raise it from anywhere in the world.
void JoinWintergrasp(Player* player)
{
    if (!player || !player->GetSession())
        return;
    ChatHandler handler(player->GetSession());

    Battlefield* wg = sBattlefieldMgr->GetBattlefieldByBattleId(BATTLEFIELD_BATTLEID_WG);
    if (!wg)
    {
        handler.SendSysMessage("|cff66ccff[Wintergrasp]|r Wintergrasp is not running on this realm.");
        return;
    }
    if (!wg->IsWarTime())
    {
        handler.SendSysMessage("|cff66ccff[Wintergrasp]|r The battle has not started yet. Try again when it does.");
        return;
    }
    if (wg->IsPlayerInBattlefield(player->GetGUID()))
    {
        handler.SendSysMessage("|cff66ccff[Wintergrasp]|r You are already in the battle.");
        return;
    }
    if (player->IsInCombat())
    {
        handler.SendSysMessage("|cff66ccff[Wintergrasp]|r Not while you are in combat.");
        return;
    }

    wg->InvitePlayerToWar(player);
    handler.SendSysMessage("|cff66ccff[Wintergrasp]|r Invite sent -- accept the popup to join the battle.");
}

// ---------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------

// Bug report #11: "make all mobs give N% current level xp per kill so you
// always get something from them."
//
// PLAYER victims only now. Creature kills go through KillXpFor in
// LivingGear_Perks.cpp, which floors them itself with the elite tier and the
// zone multiplier in hand; this hook only ever saw the subset of creature
// kills the engine already paid for anyway, since KillRewarder skips
// OnPlayerGiveXP entirely whenever its own number is zero.
//
// What is left here is the half that funnel cannot reach: a battleground
// honorable kill, which reaches this hook with a Player victim and no scaling
// to do. That is what makes "get a decent bit of XP from battlegrounds" true.
//
// Expressed against PLAYER_NEXT_LEVEL_XP so it stays meaningful at every level
// -- a flat XP number would be a fortune at level 5 and a rounding error at 75.
//
// Deliberately NOT applied to quest XP: LivingGearPerks::OnPlayerQuestComputeXP
// already has its own floor, and stacking a second one here would pay the
// kill floor on top of every turn-in.
class SupportKillXp : public PlayerScript
{
public:
    SupportKillXp() : PlayerScript("LivingGearSupportKillXp", {
        PLAYERHOOK_ON_GIVE_EXP
    }) { }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 xpSource) override
    {
        if (!g_killXpFloorEnabled || !g_killXpFloorPct || !player)
            return;
        if (xpSource != XPSOURCE_KILL)
            return;
        // Creature kills are floored by KillXpFor instead. Two floors on one
        // grant is how the elite tier would quietly get flattened back to the
        // trash rate.
        if (victim && victim->IsCreature())
            return;
        if (player->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            return;
        uint32 const forNextLevel = player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        uint32 const floor = uint32(uint64(forNextLevel) * g_killXpFloorPct / 100);
        if (amount < floor)
            amount = floor;
    }
};

// Bug report #12: "make quest items have a 100% drop rate."
//
// LootStoreItem::Roll() short-circuits to true the moment chance >= 100, ahead
// of the quality modifier and the drop-rate config, so setting it here is a
// genuine guarantee rather than a very good chance.
//
// Two separate cases, and both matter:
//   needs_quest      - the loot entry is flagged as a quest drop. The classic
//                      "kill 30 of these for one drop" case.
//   PlayerNeedsItem  - an ordinary item that happens to be a required objective
//                      for a quest this player is on right now. Gather quests
//                      asking for common trade goods live here, and they are the
//                      ones that actually feel broken.
// The second is per-player and checked against an index rather than a quest-log
// walk, so a player not on the quest sees completely normal drop rates.
class SupportLoot : public GlobalScript
{
public:
    SupportLoot() : GlobalScript("LivingGearSupportLoot", {
        GLOBALHOOK_ON_ITEM_ROLL
    }) { }

    bool OnItemRoll(Player const* player, LootStoreItem const* lootStoreItem, float& chance,
        Loot& /*loot*/, LootStore const& /*store*/) override
    {
        if (!g_questDropAlways || !player || !lootStoreItem || chance >= 100.0f)
            return true;
        if (lootStoreItem->needs_quest || LivingGear_PlayerNeedsItemForQuest(player, lootStoreItem->itemid))
            chance = 100.0f;
        return true;
    }
};

// Bug report #3, 2026-08-22: "make wintergrasp siege damage scale with the
// number of players -- if there's only a couple of players, make them do 10x
// normal damage."
//
// Wintergrasp's walls and towers are destructible GameObjects, so their damage
// does not go through any of the Unit damage hooks -- it arrives here, at
// GameObject::ModifyHealth. `change` is negative for damage and positive for
// repair; only damage is touched, so repairing is unaffected.
//
// Scoped to the Wintergrasp area, and counts only players actually in that
// area rather than everyone on the Northrend map, which would otherwise let
// half of Dalaran suppress the multiplier without ever setting foot in the
// battle.
class SupportWintergrasp : public AllGameObjectScript
{
public:
    SupportWintergrasp() : AllGameObjectScript("LivingGearSupportWintergrasp") { }

    void OnGameObjectModifyHealth(GameObject* go, Unit* attackerOrHealer, int32& change,
        SpellInfo const* /*spellInfo*/) override
    {
        if (!g_wgSiegeScale || !go || change >= 0 || !attackerOrHealer)
            return;
        if (go->GetAreaId() != AREA_WINTERGRASP && go->GetZoneId() != AREA_WINTERGRASP)
            return;
        Map* map = go->GetMap();
        if (!map)
            return;

        uint32 present = 0;
        for (auto const& pair : map->GetPlayers())
            if (Player* p = pair.GetSource())
                if (p->IsInWorld() && (p->GetZoneId() == AREA_WINTERGRASP || p->GetAreaId() == AREA_WINTERGRASP))
                    ++present;

        if (present >= g_wgFullRoster)
            return;
        float mult = float(g_wgFullRoster) / float(std::max<uint32>(present, 1));
        if (mult > g_wgMaxSiegeMult)
            mult = g_wgMaxSiegeMult;
        if (mult <= 1.0f)
            return;

        double const scaled = double(change) * double(mult);
        change = scaled <= double(std::numeric_limits<int32>::min())
            ? std::numeric_limits<int32>::min() : int32(scaled);
    }
};

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
        {
            g_questCooldown.erase(player->GetGUID().GetCounter());
            g_diag.erase(player->GetGUID().GetCounter());
        }
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
            { "feature", HandleFeature, rbac::RBAC_PERM_COMMAND_HELP, Console::No },
            { "crit", HandleCritical, rbac::RBAC_PERM_COMMAND_HELP, Console::No },
            { "wg",  HandleWintergrasp, rbac::RBAC_PERM_COMMAND_HELP, Console::No },
        };
        return commandTable;
    }

    static bool HandleBug(ChatHandler* handler, Tail description)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        std::string text = std::string(description);
        // Bare ".bug" opens the report form; ".bug <text>" files directly, so
        // the old one-line habit keeps working without the addon.
        if (LivingGearSupport::Trim(std::string(text)).size() < LivingGearSupport::BUG_MIN_LENGTH)
        {
            LivingGearSupport::SendLine(player, "REPORTUI|");
            return true;
        }
        LivingGearSupport::RecordSupportReport(player, std::string(text), LivingGearSupport::LG_REPORT_BUG);
        return true;
    }

    // Legacy spellings kept working after the single-.bug redesign: they map
    // onto the form's flags rather than being kinds of their own any more.
    static bool HandleFeature(ChatHandler* handler, Tail description)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        LivingGearSupport::RecordSupportReport(player, std::string(description), LivingGearSupport::LG_REPORT_FEATURE);
        return true;
    }

    // Report #189: .crit files with CRITICAL priority. Now just .bug with the
    // Critical box ticked, kept as its own command so #189's muscle memory and
    // the addon's /crit slash both keep working.
    static bool HandleCritical(ChatHandler* handler, Tail description)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        LivingGearSupport::RecordSupportReport(player, std::string(description),
            LivingGearSupport::LG_REPORT_BUG, /*critical=*/true);
        return true;
    }

    static bool HandleWintergrasp(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        JoinWintergrasp(player);
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
        LivingGearSupport::RecordSupportReport(player, msg.substr(4), LivingGearSupport::LG_REPORT_BUG);
        return true;
    }
    if (msg.rfind("FEATURE|", 0) == 0)
    {
        LivingGearSupport::RecordSupportReport(player, msg.substr(8), LivingGearSupport::LG_REPORT_FEATURE);
        return true;
    }
    if (msg.rfind("CRIT|", 0) == 0)
    {
        LivingGearSupport::RecordSupportReport(player, msg.substr(5),
            LivingGearSupport::LG_REPORT_BUG, /*critical=*/true);
        return true;
    }
    // New form intake: REPORT|kind|critical|recurring|text. kind is 0 bug,
    // 1 feature, 2 other; crit/rec are 0 or 1.
    if (msg.rfind("REPORT|", 0) == 0)
    {
        uint32 kind = 0, critical = 0, recurring = 0;
        if (sscanf(msg.c_str(), "REPORT|%u|%u|%u|", &kind, &critical, &recurring) == 3)
        {
            std::size_t const third = msg.find('|', msg.find('|', msg.find('|', 7) + 1) + 1);
            if (kind <= 2 && third != std::string::npos)
                LivingGearSupport::RecordSupportReport(player, msg.substr(third + 1),
                    uint8(kind), critical != 0, recurring != 0);
        }
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
    if (msg.rfind("QDONEBUY|", 0) == 0)
    {
        uint32 questId = 0;
        if (sscanf(msg.c_str(), "QDONEBUY|%u", &questId) == 1 && questId)
            LivingGearSupport::BuyOutQuestCooldown(player, questId);
        return true;
    }
    if (msg == "WGJOIN")
    {
        LivingGearSupport::JoinWintergrasp(player);
        return true;
    }
    return false;
}

// CRASH GUARD, 2026-08-22. Second "_AddAura / !m_cleanupDone" of the night,
// this time on a Rogue logging out.
//
// Player::CleanupsBeforeDelete sets m_cleanupDone during logout, and adding any
// aura after that asserts and takes the whole realm down. m_cleanupDone is
// protected with no accessor, but WorldSession::PlayerLogout() is public and
// covers the same window, and IsDuringRemoveFromWorld() covers the rest.
//
// This got much easier to hit in 0.1.54: cooking regen and Shadow Dance both
// used to be raw stat modifiers and were rewritten as real auras cast from the
// per-player update tick. That turned a rare race into a reachable one, because
// there are now several self-casts per second per player rather than none.
//
// Anything in this module that casts on a player -- from a tick or from a
// deferred event -- checks this first.
bool LivingGear_SafeToCastOn(Player* player)
{
    if (!player || !player->IsInWorld() || !player->IsAlive())
        return false;
    if (player->IsDuringRemoveFromWorld())
        return false;
    WorldSession* session = player->GetSession();
    return session && !session->PlayerLogout();
}

// Called from the instrumented paths across the module. Deliberately takes a
// plain literal rather than a formatted string: this runs on hot paths, and a
// counter name that needs building is a counter name not worth having.
void LivingGear_DiagBump(Player* player, char const* key)
{
    if (!player || !key)
        return;
    ++LivingGearSupport::g_diag[player->GetGUID().GetCounter()][key];
}

void LivingGear_ShowDiagnostics(Player* player)
{
    LivingGearSupport::ShowDiagnostics(player);
}

void AddSC_LivingGearSupport()
{
    LivingGearSupport::g_questDropAlways =
        sConfigMgr->GetOption<bool>("LivingGear.QuestDropAlways", true);
    LivingGearSupport::g_questBypassBase =
        sConfigMgr->GetOption<uint32>("LivingGear.QuestBypass.BaseCost", 5000000);
    LivingGearSupport::g_questBypassMult =
        sConfigMgr->GetOption<float>("LivingGear.QuestBypass.Multiplier", 2.0f);
    LivingGearSupport::g_questBypassWindow =
        sConfigMgr->GetOption<uint32>("LivingGear.QuestBypass.WindowSeconds", 3600);
    LivingGearSupport::g_killXpFloorEnabled =
        sConfigMgr->GetOption<bool>("LivingGear.KillXpFloor.Enable", true);
    LivingGearSupport::g_killXpFloorPct =
        sConfigMgr->GetOption<uint32>("LivingGear.KillXpFloor.Pct", 2);
    if (LivingGearSupport::g_killXpFloorPct > 100)
        LivingGearSupport::g_killXpFloorPct = 100;

    LivingGearSupport::g_wgSiegeScale =
        sConfigMgr->GetOption<bool>("LivingGear.Wintergrasp.SiegeScale", true);
    LivingGearSupport::g_wgFullRoster =
        std::max<uint32>(1, sConfigMgr->GetOption<uint32>("LivingGear.Wintergrasp.FullRoster", 20));
    LivingGearSupport::g_wgMaxSiegeMult =
        sConfigMgr->GetOption<float>("LivingGear.Wintergrasp.MaxSiegeMult", 10.0f);

    new LivingGearSupport::SupportPlayer();
    new LivingGearSupport::SupportWintergrasp();
    new LivingGearSupport::SupportKillXp();
    new LivingGearSupport::SupportLoot();
    new LivingGearSupport::SupportCommands();
}
