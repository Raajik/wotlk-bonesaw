-- Achievement-funded Account Perks (design in chat, 2026-08-24).
--
-- Replaces the ad-hoc unlock conditions on the convenience perks with a
-- purchase model funded by achievements. Balance is DERIVED, never stored:
--     balance = earned(achievements) - SUM(lg_account_perk_purchase.paid)
-- so there is no counter to drift, double-spend is impossible, and a respec
-- is a DELETE rather than arithmetic.
--
-- Ownership deliberately stays in `lg_account_perk`, untouched. Every rank is
-- already its own spell id, so all existing HasPerk() reads keep working and
-- there is no ownership migration. A perk owned there with no row in
-- `lg_account_perk_purchase` was granted, not bought -- which is exactly how
-- every pre-existing unlock grandfathers in at zero cost, for free.
--
-- The five multiplier tracks (Honor, Reputation, Factions, Leveling,
-- Professions) are deliberately NOT priced here. They stay condition-gated so
-- they cannot be respecced into ahead of whatever activity you are about to
-- do; Honor and Reputation move to per-category achievement thresholds in the
-- C++ change that accompanies this file.
--
-- CURVE. Rank 1 of every track costs 1 and rank 2 costs 3, so a new account
-- can taste nearly everything immediately: rank 1 of all 11 tracks is 11
-- points, ranks 1-2 of every track is 40, and all 20 one-off unlocks together
-- are 81. A fresh level 80 who quested and ran dungeons has well over that.
-- The cost lives in the tail (rank 6 is 34, rank 9 is 64), so continuing to
-- earn is what buys mastery rather than access.
--
-- Budget: 1,113 skill points exist outside holiday content (Achievement.dbc,
-- CEIL(points/10), World Events excluded as a bonus rather than a plan).
-- The 72 purchasable nodes below total 835, so maxing literally everything
-- takes ~75% of all non-holiday achievements. A player with two gathering
-- professions who skips the other three tracks realistically needs ~630.

CREATE TABLE IF NOT EXISTS `lg_achievement_value` (
  `achievement_id` INT UNSIGNED NOT NULL,
  `skill_points` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`achievement_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `lg_perk_cost` (
  `spell_id` INT UNSIGNED NOT NULL,
  `cost` INT UNSIGNED NOT NULL,
  `requires_spell_id` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `lg_account_perk_purchase` (
  `account_id` INT UNSIGNED NOT NULL,
  `spell_id` INT UNSIGNED NOT NULL,
  `paid` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Respec cooldown, account-wide. Plain ALTER is safe here because the module
-- probes information_schema before reading the column (LivingGear.cpp:975,
-- the ui_scale pattern), so a worldserver running ahead of this migration
-- degrades to "no cooldown" instead of throwing.
-- Guarded rather than a bare ALTER. A file under pending_db_* is re-executed
-- in full whenever its contents change, and MySQL 8 has no
-- ADD COLUMN IF NOT EXISTS -- so editing this file to add a perk rank made the
-- second run die on "Duplicate column name 'last_respec'", which aborted the
-- rest of the file AND stopped the updater before it reached the world DB.
SET @c := (SELECT COUNT(*) FROM `information_schema`.`COLUMNS` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta' AND `COLUMN_NAME` = 'last_respec');
SET @s := IF(@c = 0, 'ALTER TABLE `lg_account_meta` ADD COLUMN `last_respec` INT UNSIGNED NOT NULL DEFAULT 0', 'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Price-epoch stamp. When LivingGear.Perks.CostEpoch is raised past the value
-- stored here, the account is fully refunded on its next login and re-spends
-- at the new prices. This is what makes rebalancing a rank safe: without it,
-- accounts keep whatever price they bought in at forever.
SET @c := (SELECT COUNT(*) FROM `information_schema`.`COLUMNS` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'lg_account_meta' AND `COLUMN_NAME` = 'perk_epoch');
SET @s := IF(@c = 0, 'ALTER TABLE `lg_account_meta` ADD COLUMN `perk_epoch` INT UNSIGNED NOT NULL DEFAULT 0', 'DO 0');
PREPARE stmt FROM @s;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Armory, Solo Queue, Pull Radius, Track Ore and Track Herbs are absent on
-- purpose: CatchUpProfession grants all five unconditionally at every login
-- (they have no unlock condition at all), so every account already owns them
-- before the panel can open. Pricing them would be dead rows. They stay in
-- the DELETE above so a re-import clears any that were priced earlier.

-- Yield and reach are INDEPENDENT chains within each gathering profession.
-- They are listed as one track client-side, which is how an earlier pass came
-- to chain them end to end -- that made all three yield ranks a prerequisite
-- for any reach at all. They share nothing but a panel row.
--
-- Travel's Swim and Fishing's Cast/Pools/Speed are likewise not ranks of the
-- ladder they sit next to, and First Aid's three are separate effects rather
-- than tiers, so each stands alone.

DELETE FROM `lg_perk_cost` WHERE `spell_id` IN (910178, 910179, 910180, 910039, 910040, 910106, 910172, 910107, 910105, 910008, 910088, 910108, 910090, 910002, 910003, 910004, 910005, 910006, 910007, 910009, 910101, 910063, 910064, 910065, 910066, 910067, 910068, 910093, 910094, 910095, 910096, 910097, 910046, 910047, 910048, 910038, 910176, 910177, 910098, 910073, 910074, 910075, 910076, 910077, 910109, 910110, 910111, 910112, 910113, 910114, 910115, 910116, 910117, 910118, 910119, 910120, 910121, 910122, 910123, 910124, 910125, 910126, 910133, 910134, 910135, 910136, 910137, 910138, 910043, 910044, 910045, 910127, 910128, 910129, 910130, 910131, 910132, 910091, 910092, 910168, 910170, 910171);
INSERT INTO `lg_perk_cost` (`spell_id`, `cost`, `requires_spell_id`) VALUES
(910106, 24, 0),  -- Class Buffs
(910172, 20, 0),  -- CC Reduction
(910107, 18, 0),  -- Riding
(910105, 12, 0),  -- Auto-Mount
(910008, 12, 0),  -- Autoloot
(910088, 8, 0),  -- Quests - Find
(910108, 6, 0),  -- Auto-Accept
(910090, 6, 0),  -- Quests - Finish
(910002, 1, 0),  -- Mailbox
(910003, 1, 0),  -- Auction
(910004, 1, 0),  -- Trainer
(910005, 1, 0),  -- Bank
(910006, 1, 0),  -- Stable
(910007, 1, 0),  -- Bind Hearthstone
(910009, 1, 0),  -- Flight
(910063, 1, 0),  -- Cooking 1
(910064, 3, 910063),  -- Cooking 2
(910065, 7, 910064),  -- Cooking 3
(910066, 13, 910065),  -- Cooking 4
(910067, 22, 910066),  -- Cooking 5
(910068, 34, 910067),  -- Cooking 6
(910093, 1, 0),  -- Craft 1
(910094, 3, 910093),  -- Craft 2
(910095, 7, 910094),  -- Craft 3
(910096, 13, 910095),  -- Craft 4
(910097, 22, 910096),  -- Craft 5
(910046, 6, 0),  -- First Aid
(910047, 6, 0),  -- First Aid
(910048, 6, 0),  -- First Aid
(910098, 6, 0),  -- Travel
(910073, 1, 0),  -- Travel 1
(910074, 3, 910073),  -- Travel 2
(910075, 7, 910074),  -- Travel 3
(910076, 13, 910075),  -- Travel 4
(910077, 22, 910076),  -- Travel 5
(910109, 2, 0),  -- Mining Yield 1
(910110, 15, 910109),  -- Mining Yield 2
(910111, 55, 910110),  -- Mining Yield 3
(910112, 1, 0),  -- Mining Reach 1
(910113, 6, 910112),  -- Mining Reach 2
(910114, 16, 910113),  -- Mining Reach 3
(910115, 2, 0),  -- Herbalism Yield 1
(910116, 15, 910115),  -- Herbalism Yield 2
(910117, 55, 910116),  -- Herbalism Yield 3
(910118, 1, 0),  -- Herbalism Reach 1
(910119, 6, 910118),  -- Herbalism Reach 2
(910120, 16, 910119),  -- Herbalism Reach 3
(910121, 2, 0),  -- Skinning Yield 1
(910122, 15, 910121),  -- Skinning Yield 2
(910123, 55, 910122),  -- Skinning Yield 3
(910124, 1, 0),  -- Skinning Reach 1
(910125, 6, 910124),  -- Skinning Reach 2
(910126, 16, 910125),  -- Skinning Reach 3
(910133, 2, 0),  -- Engineering Yield 1
(910134, 15, 910133),  -- Engineering Yield 2
(910135, 55, 910134),  -- Engineering Yield 3
(910136, 1, 0),  -- Engineering Reach 1
(910137, 6, 910136),  -- Engineering Reach 2
(910138, 16, 910137),  -- Engineering Reach 3
(910043, 2, 0),  -- Fishing Cast
(910044, 8, 910043),  -- Fishing Pools
(910045, 18, 910044),  -- Fishing Speed
(910127, 2, 0),  -- Fishing Yield 1
(910128, 15, 910127),  -- Fishing Yield 2
(910129, 55, 910128),  -- Fishing Yield 3
(910130, 1, 0),  -- Fishing Reach 1
(910131, 6, 910130),  -- Fishing Reach 2
(910132, 16, 910131),  -- Fishing Reach 3
(910038, 2, 0),  -- Movement 1
(910176, 6, 910038),  -- Movement 2
(910177, 14, 910176),  -- Movement 3
(910039, 26, 910177),  -- Movement 4
(910040, 44, 910039),  -- Movement 5
(910101, 4, 0),  -- Attune 1
(910178, 12, 910101),  -- Attune 2
(910179, 26, 910178),  -- Attune 3
(910180, 48, 910179);  -- Attune 4

-- Realm First achievements are worth 10 skill points each.
--
-- Every one of them awards 0 points in Achievement.dbc, so without a row here
-- the hardest content on the realm pays nothing at all. They are also the only
-- part of Feats of Strength worth seeding: the other 115 are Collector's
-- Edition pets, BlizzCon promos and removed vanilla PvP ranks, which would pay
-- people for purchase history and for deleted systems rather than for playing.
--
-- This sits OUTSIDE the 1,113 point baseline the prices above are tuned
-- against, the same way holiday content does. One account per realm can ever
-- hold any of these, so it is a bragging bonus and not a budget line.
DELETE FROM `lg_achievement_value` WHERE `achievement_id` IN (3259, 1402, 1416, 3117, 4576, 1419, 4078, 1415, 1420, 1414, 1417, 1418, 1421, 1423, 1424, 1425, 1422, 1426, 1427, 457, 1405, 461, 1406, 466, 1407, 1413, 1404, 1408, 462, 460, 1409, 1410, 465, 464, 458, 467, 1411, 1412, 463, 459, 1400, 1463, 456);
INSERT INTO `lg_achievement_value` (`achievement_id`, `skill_points`) VALUES
(3259, 10),  -- Realm First! Celestial Defender
(1402, 10),  -- Realm First! Conqueror of Naxxramas
(1416, 10),  -- Realm First! Cooking Grand Master
(3117, 10),  -- Realm First! Death's Demise
(4576, 10),  -- Realm First! Fall of the Lich King
(1419, 10),  -- Realm First! First Aid Grand Master
(4078, 10),  -- Realm First! Grand Crusader
(1415, 10),  -- Realm First! Grand Master Alchemist
(1420, 10),  -- Realm First! Grand Master Angler
(1414, 10),  -- Realm First! Grand Master Blacksmith
(1417, 10),  -- Realm First! Grand Master Enchanter
(1418, 10),  -- Realm First! Grand Master Engineer
(1421, 10),  -- Realm First! Grand Master Herbalist
(1423, 10),  -- Realm First! Grand Master Jewelcrafter
(1424, 10),  -- Realm First! Grand Master Leatherworker
(1425, 10),  -- Realm First! Grand Master Miner
(1422, 10),  -- Realm First! Grand Master Scribe
(1426, 10),  -- Realm First! Grand Master Skinner
(1427, 10),  -- Realm First! Grand Master Tailor
(457, 10),  -- Realm First! Level 80
(1405, 10),  -- Realm First! Level 80 Blood Elf
(461, 10),  -- Realm First! Level 80 Death Knight
(1406, 10),  -- Realm First! Level 80 Draenei
(466, 10),  -- Realm First! Level 80 Druid
(1407, 10),  -- Realm First! Level 80 Dwarf
(1413, 10),  -- Realm First! Level 80 Forsaken
(1404, 10),  -- Realm First! Level 80 Gnome
(1408, 10),  -- Realm First! Level 80 Human
(462, 10),  -- Realm First! Level 80 Hunter
(460, 10),  -- Realm First! Level 80 Mage
(1409, 10),  -- Realm First! Level 80 Night Elf
(1410, 10),  -- Realm First! Level 80 Orc
(465, 10),  -- Realm First! Level 80 Paladin
(464, 10),  -- Realm First! Level 80 Priest
(458, 10),  -- Realm First! Level 80 Rogue
(467, 10),  -- Realm First! Level 80 Shaman
(1411, 10),  -- Realm First! Level 80 Tauren
(1412, 10),  -- Realm First! Level 80 Troll
(463, 10),  -- Realm First! Level 80 Warlock
(459, 10),  -- Realm First! Level 80 Warrior
(1400, 10),  -- Realm First! Magic Seeker
(1463, 10),  -- Realm First! Northrend Vanguard
(456, 10);  -- Realm First! Obsidian Slayer
