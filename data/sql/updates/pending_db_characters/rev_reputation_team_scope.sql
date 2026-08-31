-- Team-scope repair for account-wide reputation (bug report #220).
--
-- The account-wide reputation pool stored standings computed with the
-- earning character's base reputation. One-team factions (the nine cities,
-- the BG teams, the expedition factions) put the OPPOSITE team at Hated
-- (-42000) through Faction.dbc base-reputation slots, so a value captured
-- from one team replayed onto the other as raw = total - base(other team):
-- a Horde alt ended up hostile with its own faction cities and exalted with
-- enemy ones, and both directions ratcheted the pool every round trip.
--
-- The code now gates every pool read/write on the faction being legal for
-- the character's team. This migration repairs what was already written:
--
--   * opposite-team rows are deleted outright, the same as never having
--     contacted the faction;
--   * own-team rows below neutral are floored at 0 -- no gameplay path
--     drops a character below neutral with a faction its own team owns,
--     so every negative here is sync damage;
--   * pool rows for one-team factions are dropped so the gated backfill
--     re-seeds them from correct-team characters at next login.
--
-- Races: alliance (1,3,4,7,11), horde (2,5,6,8,10). Faction lists are the
-- Faction.dbc classification from tools/_fac_parse.py:
--   alliance-only: 47,54,69,72,469,471,509,589,730,890,891,930,946,978,
--                  1050,1068,1082,1094,1126
--   horde-only:    67,68,76,81,510,530,729,889,892,911,922,941,947,1052,
--                  1064,1067,1085,1124

DELETE `cr` FROM `character_reputation` `cr`
JOIN `characters` `c` ON `c`.`guid` = `cr`.`guid`
WHERE `c`.`race` IN (1,3,4,7,11) AND `cr`.`faction` IN (67,68,76,81,510,530,729,889,892,911,922,941,947,1052,1064,1067,1085,1124);

DELETE `cr` FROM `character_reputation` `cr`
JOIN `characters` `c` ON `c`.`guid` = `cr`.`guid`
WHERE `c`.`race` IN (2,5,6,8,10) AND `cr`.`faction` IN (47,54,69,72,469,471,509,589,730,890,891,930,946,978,1050,1068,1082,1094,1126);

UPDATE `character_reputation` `cr`
JOIN `characters` `c` ON `c`.`guid` = `cr`.`guid`
SET `cr`.`standing` = 0
WHERE `c`.`race` IN (2,5,6,8,10) AND `cr`.`standing` < 0
AND `cr`.`faction` IN (67,68,76,81,510,530,729,889,892,911,922,941,947,1052,1064,1067,1085,1124);

UPDATE `character_reputation` `cr`
JOIN `characters` `c` ON `c`.`guid` = `cr`.`guid`
SET `cr`.`standing` = 0
WHERE `c`.`race` IN (1,3,4,7,11) AND `cr`.`standing` < 0
AND `cr`.`faction` IN (47,54,69,72,469,471,509,589,730,890,891,930,946,978,1050,1068,1082,1094,1126);

DELETE FROM `lg_account_reputation` WHERE `faction_id` IN (47,54,69,72,469,471,509,589,730,890,891,930,946,978,1050,1068,1082,1094,1126,67,68,76,81,510,530,729,889,892,911,922,941,947,1052,1064,1067,1085,1124);
