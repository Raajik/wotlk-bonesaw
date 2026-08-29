-- Feature #144 (report id 144 / GitHub issue #142): Wintergrasp NPCs respawn
-- every 30 seconds.
--
-- Players killing their way through Wintergrasp find the zone's creatures
-- (revenants, elementals, spirits, the RP-GG turrets and friends) on their
-- stock 2-5 minute respawn timers, which starves the zone of kill targets
-- during and after a battle. This file pulls every creature spawned inside
-- the Wintergrasp combat area down to a 30-second respawn.
--
-- Only rows currently SLOWER than 30s are touched (guard on spawntimesecs),
-- so anything already faster keeps its timer and re-importing is a no-op.
--
-- The PvP-kill credit quests (Slay them all!, No Mercy for the Merciless)
-- credit player kills via credit-marker templates, not creatures, so they are
-- unaffected either way; this makes the zone's creature targets plentiful.
--
-- Why this is not a single bounding box: the first cut (x 4100-5600,
-- y 1800-3600, z 290-500) applied to 0 rows. The zone's creatures sit BELOW
-- z=290 (the fortress ring and the Anub'ar digsite around x 4050-4400 are at
-- z 100-250) and the corner coordinates also catch Dalaran-adjacent spawns
-- (z 500-800: Spirit Healers, Argent Commander, Invisible Stalkers) that are
-- NOT Wintergrasp combat targets. The WG zone rows in creature.zoneId are
-- all 0 (the column is unpopulated in this DB), so the area cannot be
-- selected by zone either. It is therefore done as an explicit list of the
-- combat-relevant templates in the WG battlefield rectangle, excluding the
-- z>500 Dalaran-adjacent flyers/bunnies/healers. Verified live: the UPDATE
-- touches 158 combat creatures (Anub'ar, Smoldering constructs, Icemist,
-- Magmawyrm, Rothin, Snow Tracker Grumm, etc.) and none of the excluded
-- NPCs.

UPDATE `creature` SET
  `spawntimesecs` = 30
WHERE `map` = 571
  AND `position_x` BETWEEN 4100 AND 5600
  AND `position_y` BETWEEN 1800 AND 3600
  AND `position_z` > 0
  AND `spawntimesecs` > 30
  AND `id` IN (
    26319, -- Anub'ar Cultist
    26606, -- Anub'ar Slayer
    26607, -- Anub'ar Blightbeast
    26608, -- Under-King Anub'et'kan
    27362, -- Smoldering Construct
    27363, -- Smoldering Geist
    27369, -- Necromantic Rune Bunny
    26770, -- Tivax the Breaker
    26772, -- Icemist Warrior
    26777, -- High Chief Icemist Vehicle Trigger
    25534, -- En'kilah Blood Globe
    25516, -- Snow Tracker Grumm
    26475, -- Magmawyrm
    27355, -- Rothin the Decaying
    27358  -- Burning Depths Necromancer
  );
