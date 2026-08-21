-- Populate lg_zone_scale with real per-zone recommended-max-level data so
-- Zone Scale can be safely re-enabled (see Bonesaw.md "Zone Scale is
-- currently disabled" -- it was crushing damage/XP everywhere because this
-- table was always empty and AreaTable.dbc's area_level is unset for
-- nearly every classic/Outland zone). Zone IDs verified 2026-08-21 by
-- parsing the real AreaTable.dbc directly (var/mmap-output/dbc/AreaTable.dbc)
-- rather than trusted from memory -- do not add zone_id values here without
-- doing the same. Levels are standard WotLK-era (pre-Cataclysm) leveling
-- ranges; capital cities, instance-only areas, and non-quest zones (Onyxia's
-- Lair, Caverns of Time, GM Island, etc.) are intentionally omitted --
-- ZoneLevel() falls back to AreaTable.dbc's area_level (clamped to 1) for
-- anything not listed here, which is fine for zones nobody quests in.

DELETE FROM `lg_zone_scale` WHERE `zone_id` IN (
  1, 3, 4, 8, 10, 11, 12, 33, 38, 40, 41, 44, 45, 46, 47, 51, 28, 139, 85, 130, 267,
  14, 17, 141, 148, 215, 331, 406, 400, 405, 357, 15, 440, 16, 361, 490, 1377, 618,
  3483, 3521, 3519, 3518, 3522, 3523, 3520, 3524, 3525, 3430, 3433,
  65, 394, 3537
);

INSERT INTO `lg_zone_scale` (`zone_id`, `max_level`) VALUES
-- Eastern Kingdoms
(1, 12),    -- Dun Morogh
(3, 38),    -- Badlands
(4, 55),    -- Blasted Lands
(8, 45),    -- Swamp of Sorrows
(10, 30),   -- Duskwood
(11, 30),   -- Wetlands
(12, 10),   -- Elwynn Forest
(33, 45),   -- Stranglethorn Vale
(38, 20),   -- Loch Modan
(40, 20),   -- Westfall
(41, 60),   -- Deadwind Pass
(44, 25),   -- Redridge Mountains
(45, 40),   -- Arathi Highlands
(46, 55),   -- Burning Steppes
(47, 40),   -- The Hinterlands
(51, 50),   -- Searing Gorge
(28, 55),   -- Western Plaguelands
(139, 60),  -- Eastern Plaguelands
(85, 10),   -- Tirisfal Glades
(130, 20),  -- Silverpine Forest
(267, 30),  -- Hillsbrad Foothills
-- Kalimdor
(14, 10),   -- Durotar
(17, 25),   -- The Barrens
(141, 10),  -- Teldrassil
(148, 20),  -- Darkshore
(215, 10),  -- Mulgore
(331, 25),  -- Ashenvale
(406, 25),  -- Stonetalon Mountains
(400, 30),  -- Thousand Needles
(405, 40),  -- Desolace
(357, 50),  -- Feralas
(15, 45),   -- Dustwallow Marsh
(440, 50),  -- Tanaris
(16, 55),   -- Azshara
(361, 55),  -- Felwood
(490, 53),  -- Un'Goro Crater
(1377, 60), -- Silithus
(618, 60),  -- Winterspring
-- Outland
(3483, 63), -- Hellfire Peninsula
(3521, 64), -- Zangarmarsh
(3519, 65), -- Terokkar Forest
(3518, 67), -- Nagrand
(3522, 68), -- Blade's Edge Mountains
(3523, 70), -- Netherstorm
(3520, 70), -- Shadowmoon Valley
(3524, 10), -- Azuremyst Isle
(3525, 20), -- Bloodmyst Isle
(3430, 10), -- Eversong Woods
(3433, 20), -- Ghostlands
-- Northrend (only the few AreaTable.dbc leaves at 0 -- most already have a
-- real area_level baked into the DBC and don't need an override)
(65, 74),   -- Dragonblight
(394, 76),  -- Grizzly Hills
(3537, 72); -- Borean Tundra
