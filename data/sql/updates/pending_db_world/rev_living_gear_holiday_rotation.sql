-- Rotate the ten annual holidays on a fixed 5-day cycle.
--
-- These holidays are worth 207 skill points of achievements that a player
-- realistically never sees: they run once a year, for a realm that has not been
-- up a year. Rotating them turns that into ordinary reachable content.
--
-- The core already has the rotation built in. For a GAMEEVENT_NORMAL event,
-- CheckOneGameEvent is
--     Start < now AND now < End AND (now - Start) % (Occurence*60) < Length*60
-- so ten events sharing a 50-day Occurence, a 5-day Length, and start times
-- staggered 5 days apart means exactly one is live at any moment, forever, with
-- no scheduler and no saved state. It survives restarts because it is derived
-- from timestamps rather than stored.
--
-- holidayStage MUST be 0. GameEventMgr::SetHolidayEventTime overwrites Length,
-- Occurence and Start from Holidays.dbc for any event with a non-zero stage,
-- which silently discards every value set here. That field is read in exactly
-- one place in the entire core (GameEventMgr.cpp:1916), so zeroing it does
-- nothing except hand control back to this table.
--
-- Achievement criteria are unaffected: ACHIEVEMENT_CRITERIA_DATA_TYPE_HOLIDAY
-- resolves through IsHolidayActive(), which walks the active event list and
-- never consults the calendar.

-- slot 0: Midsummer Fire Festival
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-01-01 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 1;
-- slot 1: Brewfest
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-01-06 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 24;
-- slot 2: Hallow's End
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-01-11 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 12;
-- slot 3: Winter Veil
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-01-16 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 2;
-- slot 4: Lunar Festival
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-01-21 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 7;
-- slot 5: Love is in the Air
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-01-26 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 8;
-- slot 6: Noblegarden
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-01-31 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 9;
-- slot 7: Children's Week
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-02-05 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 10;
-- slot 8: Harvest Festival
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-02-10 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 11;
-- slot 9: Pilgrim's Bounty
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200, `start_time` = '2026-02-15 00:00:00', `end_time` = '2038-01-01 00:00:00' WHERE `eventEntry` = 26;

-- Winter Veil's presents live in a separate event with no holiday id of its own,
-- so it does not rotate with its parent unless told to. Same slot as Winter Veil
-- (slot 3), or the tree is up with nothing under it.
UPDATE `game_event` SET `holidayStage` = 0, `occurence` = 72000, `length` = 7200,
    `start_time` = '2026-01-16 00:00:00', `end_time` = '2038-01-01 00:00:00'
WHERE `eventEntry` = 52;

-- Brewfest's two construction events spawn scaffolding in the days BEFORE the
-- festival. On a 5-day rotation there is no room for a build-up phase, and
-- running them alongside the festival spawns both the scaffolding and the
-- finished tents in the same spot. Parked in the future so they never fire.
UPDATE `game_event` SET `holidayStage` = 0, `start_time` = '2037-01-01 00:00:00'
WHERE `eventEntry` IN (70, 91);
