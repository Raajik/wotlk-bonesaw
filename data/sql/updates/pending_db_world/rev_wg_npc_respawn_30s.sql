-- Feature #144 (report id 144 / GitHub issue #142): Wintergrasp NPCs respawn
-- every 30 seconds.
--
-- Players killing their way through Wintergrasp find the zone's creatures
-- (revenants, elementals, spirits, the RP-GG turrets and friends) on their
-- stock 2-5 minute respawn timers, which starves the zone of kill targets
-- during and after a battle. This file pulls every creature spawned inside
-- the Wintergrasp zone (map 571, the zone's bounding box: x 4100-5600,
-- y 1800-3600, z 290-500 -- covers the fortress, the towers and the
-- workshops; Dalaran at x ~5800 stays out) down to a 30-second respawn.
--
-- Only rows currently SLOWER than 30s are touched (guard on spawntimesecs),
-- so anything already faster keeps its timer and re-importing is a no-op.
--
-- The PvP-kill credit quests (Slay them all!, No Mercy for the Merciless)
-- credit player kills via credit-marker templates, not creatures, so they are
-- unaffected either way; this makes the zone's creature targets plentiful.

UPDATE `creature` SET `spawntimesecs` = 30
WHERE `map` = 571
  AND `position_x` BETWEEN 4100 AND 5600
  AND `position_y` BETWEEN 1800 AND 3600
  AND `position_z` BETWEEN 290 AND 500
  AND `spawntimesecs` > 30;
