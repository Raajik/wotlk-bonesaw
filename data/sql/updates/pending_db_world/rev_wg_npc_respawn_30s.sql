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
-- Why this is not a single tight box around the fortress: the combat
-- templates span x 3715-4626, y 904-3542, z 66-185 (the Anub'ar digsite
-- and the Valley of Fallen Heroes sit WEST and SOUTH of the old box), and
-- the WG zone rows in creature.zoneId are all 0 (the column is
-- unpopulated in this DB), so the area cannot be selected by zone either.
-- The first two cuts (tight box x 4100-5600 / y 1800-3600, and a z>0
-- variant) applied to 0 and 158 rows respectively -- the z filter was
-- silently eating the digsite, and the box its western half. The update
-- is therefore template-list IN the wide WG rectangle x 3700-5600,
-- y 900-3600 (every row of the listed templates inside it is a WG combat
-- spawn; Dalaran-adjacent flyers/healers at z 500-800 are different
-- templates and never match). Verified live: touches 143 creatures.

UPDATE `creature` SET
  `spawntimesecs` = 30
WHERE `map` = 571
  AND `position_x` BETWEEN 3700 AND 5600
  AND `position_y` BETWEEN 900 AND 3600
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
