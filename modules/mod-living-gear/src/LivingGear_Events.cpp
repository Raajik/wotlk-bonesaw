/*
 * Living Gear: keep the in-game calendar honest about the holiday rotation.
 *
 * The ten annual holidays run on a 5-day rotation driven entirely by
 * game_event (see rev_living_gear_holiday_rotation.sql). Nothing in the core
 * knows that, because the calendar is fed from Holidays.dbc: the server sends
 * each entry's packed dates in SMSG_CALENDAR_SEND_CALENDAR, and the client
 * draws exactly what it is told. Left alone it advertises the real-world dates
 * -- Brewfest in September, Winter Veil in December -- while the realm is
 * actually running something else entirely.
 *
 * GameEventMgr::LoadHolidayDates already rewrites that store in place at
 * startup for its own dynamic-date scheme, so mutating it is an established
 * move rather than a trick. This runs afterwards (OnStartup fires at the end of
 * world init) and overwrites the dates for the rotating holidays with the ones
 * the rotation will actually produce.
 *
 * game_event stays the single source of truth. The occurrences are derived from
 * the event's own Start/Occurence/Length, so retuning the rotation in SQL moves
 * the calendar with it and there is no second copy of the schedule to drift.
 */

#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldConfig.h"

#include <ctime>

namespace
{
    // Hours, matching Holidays.dbc Duration units.
    constexpr uint32 SECONDS_PER_HOUR = 3600;

    // The client only has 5 bits for the year offset from 2000, so anything at
    // or past 2032 packs to a wrong year rather than failing loudly.
    constexpr int MAX_PACKABLE_YEAR = 2031;

    uint32 PackCalendarDate(time_t when)
    {
        std::tm local = {};
#ifdef _WIN32
        localtime_s(&local, &when);
#else
        localtime_r(&when, &local);
#endif
        if (local.tm_year + 1900 > MAX_PACKABLE_YEAR)
        {
            return 0;
        }

        uint32 const yearOffset = static_cast<uint32>(local.tm_year + 1900 - 2000);
        uint32 const month = static_cast<uint32>(local.tm_mon);
        uint32 const day = static_cast<uint32>(local.tm_mday - 1);
        uint32 const weekday = static_cast<uint32>(local.tm_wday);
        uint32 const hour = static_cast<uint32>(local.tm_hour);
        uint32 const minute = static_cast<uint32>(local.tm_min);

        return (yearOffset << 24) | (month << 20) | (day << 14) | (weekday << 11) | (hour << 6) | minute;
    }

    // Writes the next MAX_HOLIDAY_DATES occurrences of one rotating event into
    // its Holidays.dbc entry. Starts from the occurrence currently in progress
    // rather than the next one, so a holiday that is live right now shows on the
    // calendar as live rather than as something upcoming.
    bool SyncHolidayToEvent(GameEventData const& event)
    {
        HolidaysEntry* entry = const_cast<HolidaysEntry*>(sHolidaysStore.LookupEntry(event.HolidayId));
        if (!entry)
        {
            return false;
        }

        time_t const occurrence = static_cast<time_t>(event.Occurence) * MINUTE;
        time_t const length = static_cast<time_t>(event.Length) * MINUTE;
        if (occurrence <= 0 || length <= 0 || event.Start <= 0)
        {
            return false;
        }

        time_t const now = GameTime::GetGameTime().count();
        time_t cursor = event.Start;
        if (now > event.Start)
        {
            cursor += ((now - event.Start) / occurrence) * occurrence;
        }

        for (uint8 i = 0; i < MAX_HOLIDAY_DATES; ++i)
        {
            entry->Date[i] = PackCalendarDate(cursor);
            cursor += occurrence;
        }

        // Duration is in hours and only the first stage is meaningful now that
        // holidayStage is zero -- the rotation gives every holiday one window.
        entry->Duration[0] = static_cast<uint32>(length / SECONDS_PER_HOUR);
        for (uint8 i = 1; i < MAX_HOLIDAY_DURATIONS; ++i)
        {
            entry->Duration[i] = 0;
        }

        return true;
    }

    class LivingGearEventsWorld : public WorldScript
    {
    public:
        LivingGearEventsWorld() : WorldScript("LivingGearEventsWorld") { }

        void OnStartup() override
        {
            GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
            uint32 synced = 0;

            for (uint16 id = 1; id < events.size(); ++id)
            {
                GameEventData const& event = events[id];

                // Only the rotating holidays. A non-zero holidayStage means
                // SetHolidayEventTime owns this event's timing from the DBC, so
                // its game_event values are not what actually runs and syncing
                // the calendar to them would make things worse, not better.
                if (event.HolidayId == HOLIDAY_NONE || event.HolidayStage != 0)
                {
                    continue;
                }

                if (!SyncHolidayToEvent(event))
                {
                    continue;
                }

                auto itr = std::lower_bound(sGameEventMgr->ModifiedHolidays.begin(),
                    sGameEventMgr->ModifiedHolidays.end(), event.HolidayId);
                if (itr == sGameEventMgr->ModifiedHolidays.end() || *itr != event.HolidayId)
                {
                    sGameEventMgr->ModifiedHolidays.insert(itr, event.HolidayId);
                }

                ++synced;
            }

            LOG_INFO("server.loading", "Living Gear: calendar synced to {} rotating holiday event(s).", synced);
        }
    };
}

// ###############################################################################
//
// Wintergrasp bot fill (report #170): "can we start having playerbots join
// Wintergrasp when there's real players in it?"
//
// Wintergrasp is a Battlefield (BattlefieldMgr), not a Battleground, and
// playerbots only ever join Battlegrounds (BattleGroundJoinAction.cpp) --
// the two pipelines share nothing, so bots never showed up no matter how
// many real players queued. WG wars also start on a timer whether or not
// anyone queued, so the queue phase can be skipped entirely: once the war
// is live, a bot joins through the exact same calls the client accept
// button makes -- Battlefield::InvitePlayerToWar (registers the war
// invite) then Battlefield::PlayerAcceptInviteToWar (BF raid group,
// SendBfEntered, PlayersInWar insert). Both are public; no core patch.
//
// A world sweep every few seconds tops each faction up to PerRealPlayer x
// the number of REAL players engaged on either side, capped by MaxPerSide
// and the battlefield's own vacancy gate, so a war with real players in it
// becomes a war and an empty zone stays quiet. Bots materialize at their
// faction's home graveyard (coords from WGGraveyard[] in BattlefieldWG.h)
// and fight whatever their AI finds in front of them; the war-end kick
// hands them back to the bot manager like any other participant.
//
// ###############################################################################
namespace
{
    bool g_wgBotsEnable = true;
    uint32 g_wgBotsPerReal = 3;
    uint32 g_wgBotsMaxPerSide = 15;
    uint32 g_wgBotsSweepSecs = 5;
    time_t g_wgBotsLastSweep = 0;

    // Faction landing graveyards, copied from WGGraveyard[] in
    // src/server/game/Battlefield/Zones/BattlefieldWG.h (map 571): inside
    // the zone, next to the faction's spirit healer, facing the right half
    // of the map. Indexed by TeamId.
    constexpr uint32 WG_MAP_ID = 571;
    Position const WgBotSpawn[2] =
    {
        { 5140.790f, 2179.120f, 390.950f, 1.972220f }, // TEAM_ALLIANCE
        { 5032.454f, 3711.382f, 372.468f, 3.971623f }, // TEAM_HORDE
    };

    Battlefield* WgBattlefield()
    {
        return sBattlefieldMgr->GetBattlefieldByBattleId(BATTLEFIELD_BATTLEID_WG);
    }

    // War participants on one team (accepted plus still-pending invites),
    // counted separately for real players and bots.
    uint32 WgSideCount(Battlefield* bf, TeamId team, bool real)
    {
        uint32 count = 0;
        for (ObjectGuid const& guid : bf->GetPlayersInWarSet(team))
        {
            Player const* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && player->GetSession()->IsBot() != real)
            {
                ++count;
            }
        }
        for (auto const& invite : bf->GetInvitedPlayersMap(team))
        {
            Player const* player = ObjectAccessor::FindConnectedPlayer(invite.first);
            if (player && player->GetSession()->IsBot() != real)
            {
                ++count;
            }
        }
        return count;
    }

    // Bots eligible to be pulled into the war: alive, out of instances and
    // battlegrounds, ungrouped (never yank a bot out of a dungeon run), and
    // at or above the WG minimum level. A random slice of the matching pool,
    // so the same handful does not fight every war.
    std::vector<Player*> WgBotCandidates(TeamId team, uint32 need, uint32 minLevel)
    {
        std::vector<Player*> pool;
        pool.reserve(need * 8);
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (pool.size() >= need * 8)
            {
                break;
            }
            if (!bot || !bot->IsInWorld() || bot->GetTeamId() != team)
            {
                continue;
            }
            if (bot->GetLevel() < minLevel || bot->isDead() || bot->IsInFlight())
            {
                continue;
            }
            if (bot->InBattleground() || bot->GetGroup())
            {
                continue;
            }
            Map* botMap = bot->GetMap();
            if (!botMap || botMap->IsDungeon() || botMap->IsRaid())
            {
                continue;
            }
            pool.push_back(bot);
        }

        std::vector<Player*> picked;
        picked.reserve(need);
        while (!pool.empty() && picked.size() < need)
        {
            uint32 const idx = urand(0, uint32(pool.size() - 1));
            picked.push_back(pool[idx]);
            pool.erase(pool.begin() + idx);
        }
        return picked;
    }

    void WgBotJoinWar(Battlefield* bf, Player* bot)
    {
        Position const& dest = WgBotSpawn[bot->GetTeamId()];
        bot->TeleportTo(WG_MAP_ID, dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), dest.GetOrientation());
        bf->InvitePlayerToWar(bot);
        bf->PlayerAcceptInviteToWar(bot);
    }

    void WgBotSweep()
    {
        Battlefield* bf = WgBattlefield();
        if (!bf || !bf->IsEnabled() || !bf->IsWarTime())
        {
            return;
        }

        uint32 real[2];
        uint32 bots[2];
        for (uint8 team = 0; team < 2; ++team)
        {
            real[team] = WgSideCount(bf, TeamId(team), true);
            bots[team] = WgSideCount(bf, TeamId(team), false);
        }

        // Bots only join a war a real player is fighting; both sides fill to
        // the same target so an attacker never faces an empty fortress, and
        // Wintergrasp's own tenacity absorbs whatever imbalance remains.
        uint32 const engaged = std::max(real[0], real[1]);
        if (!engaged)
        {
            return;
        }

        uint32 const target = std::min(g_wgBotsMaxPerSide, g_wgBotsPerReal * engaged);
        uint32 const minLevel = sWorld->getIntConfig(CONFIG_WINTERGRASP_PLR_MIN_LVL);

        for (uint8 team = 0; team < 2; ++team)
        {
            if (bots[team] >= target)
            {
                continue;
            }

            for (Player* bot : WgBotCandidates(TeamId(team), target - bots[team], minLevel))
            {
                if (!bf->HasWarVacancy(TeamId(team)))
                {
                    break;
                }
                WgBotJoinWar(bf, bot);
                LOG_INFO("module.livinggear.wgbots", "Wintergrasp fill: {} joins the war ({})",
                    bot->GetName(), team ? "Horde" : "Alliance");
            }
        }
    }

    class LivingGearWgBotsWorld : public WorldScript
    {
    public:
        LivingGearWgBotsWorld() : WorldScript("LivingGearWgBotsWorld") { }

        void OnAfterConfigLoad(bool /*reload*/) override
        {
            g_wgBotsEnable = sConfigMgr->GetOption<bool>("LivingGear.WGBots.Enable", true);
            g_wgBotsPerReal = sConfigMgr->GetOption<uint32>("LivingGear.WGBots.PerRealPlayer", 3);
            g_wgBotsMaxPerSide = sConfigMgr->GetOption<uint32>("LivingGear.WGBots.MaxPerSide", 15);
            g_wgBotsSweepSecs = std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("LivingGear.WGBots.SweepSeconds", 5));
        }

        void OnUpdate(uint32 /*diff*/) override
        {
            if (!g_wgBotsEnable)
            {
                return;
            }
            time_t const now = GameTime::GetGameTime().count();
            if (now - g_wgBotsLastSweep < g_wgBotsSweepSecs)
            {
                return;
            }
            g_wgBotsLastSweep = now;
            WgBotSweep();
        }
    };
}

void AddSC_LivingGearEvents()
{
    new LivingGearEventsWorld();
    new LivingGearWgBotsWorld();
}
