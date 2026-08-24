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
-- The 72 purchasable nodes below total 837, so maxing literally everything
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
ALTER TABLE `lg_account_meta` ADD COLUMN `last_respec` INT UNSIGNED NOT NULL DEFAULT 0;

-- Price-epoch stamp. When LivingGear.Perks.CostEpoch is raised past the value
-- stored here, the account is fully refunded on its next login and re-spends
-- at the new prices. This is what makes rebalancing a rank safe: without it,
-- accounts keep whatever price they bought in at forever.
ALTER TABLE `lg_account_meta` ADD COLUMN `perk_epoch` INT UNSIGNED NOT NULL DEFAULT 0;

DELETE FROM `lg_perk_cost` WHERE `spell_id` IN (910105, 910106, 910107, 910172, 910108, 910003, 910090, 910008, 910005, 910007, 910009, 910088, 910002, 910006, 910004, 910101, 910063, 910064, 910065, 910066, 910067, 910068, 910093, 910094, 910095, 910096, 910097, 910046, 910047, 910048, 910043, 910044, 910045, 910127, 910128, 910129, 910130, 910131, 910132, 910133, 910134, 910135, 910136, 910137, 910138, 910115, 910116, 910117, 910118, 910119, 910120, 910038, 910176, 910177, 910109, 910110, 910111, 910112, 910113, 910114, 910121, 910122, 910123, 910124, 910125, 910126, 910098, 910073, 910074, 910075, 910076, 910077, 910092, 910168, 910170, 910171, 910091);
-- Armory, Solo Queue, Pull Radius, Track Ore and Track Herbs are absent on
-- purpose: CatchUpProfession grants all five unconditionally at every login
-- (they have no unlock condition at all), so every account already owns them
-- before the panel can open. Pricing them would be dead rows. They stay in
-- the DELETE above so a re-import clears any that were priced earlier.
INSERT INTO `lg_perk_cost` (`spell_id`, `cost`, `requires_spell_id`) VALUES
(910105, 6, 0),  -- Auto-Mount
(910106, 6, 0),  -- Class Buffs
(910107, 6, 0),  -- Riding
(910172, 6, 0),  -- CC Reduction
(910108, 3, 0),  -- Auto-Accept
(910003, 1, 0),  -- Auction
(910090, 3, 0),  -- Quests - Finish
(910008, 6, 0),  -- Autoloot
(910005, 1, 0),  -- Bank
(910007, 1, 0),  -- Bind Hearthstone
(910009, 1, 0),  -- Flight
(910088, 3, 0),  -- Quests - Find
(910002, 1, 0),  -- Mailbox
(910006, 1, 0),  -- Stable
(910004, 1, 0),  -- Trainer
(910101, 1, 0),  -- Attune 1
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
(910046, 1, 0),  -- First Aid 1
(910047, 3, 910046),  -- First Aid 2
(910048, 7, 910047),  -- First Aid 3
(910043, 1, 0),  -- Fishing 1
(910044, 3, 910043),  -- Fishing 2
(910045, 7, 910044),  -- Fishing 3
(910127, 13, 910045),  -- Fishing 4
(910128, 22, 910127),  -- Fishing 5
(910129, 34, 910128),  -- Fishing 6
(910130, 44, 910129),  -- Fishing 7
(910131, 54, 910130),  -- Fishing 8
(910132, 64, 910131),  -- Fishing 9
(910133, 1, 0),  -- Engineering 1
(910134, 3, 910133),  -- Engineering 2
(910135, 7, 910134),  -- Engineering 3
(910136, 13, 910135),  -- Engineering 4
(910137, 22, 910136),  -- Engineering 5
(910138, 34, 910137),  -- Engineering 6
(910115, 1, 0),  -- Herbalism 1
(910116, 3, 910115),  -- Herbalism 2
(910117, 7, 910116),  -- Herbalism 3
(910118, 13, 910117),  -- Herbalism 4
(910119, 22, 910118),  -- Herbalism 5
(910120, 34, 910119),  -- Herbalism 6
(910038, 1, 0),  -- Movement 1
(910176, 3, 910038),  -- Movement 2
(910177, 7, 910176),  -- Movement 3
(910109, 1, 0),  -- Mining 1
(910110, 3, 910109),  -- Mining 2
(910111, 7, 910110),  -- Mining 3
(910112, 13, 910111),  -- Mining 4
(910113, 22, 910112),  -- Mining 5
(910114, 34, 910113),  -- Mining 6
(910121, 1, 0),  -- Skinning 1
(910122, 3, 910121),  -- Skinning 2
(910123, 7, 910122),  -- Skinning 3
(910124, 13, 910123),  -- Skinning 4
(910125, 22, 910124),  -- Skinning 5
(910126, 34, 910125),  -- Skinning 6
(910098, 1, 0),  -- Travel 1
(910073, 3, 910098),  -- Travel 2
(910074, 7, 910073),  -- Travel 3
(910075, 13, 910074),  -- Travel 4
(910076, 22, 910075),  -- Travel 5
(910077, 34, 910076);  -- Travel 6
