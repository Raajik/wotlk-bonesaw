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

#include "Chat.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "Log.h"
#include "ScriptMgr.h"

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

void AddSC_LivingGearEvents()
{
    new LivingGearEventsWorld();
}
