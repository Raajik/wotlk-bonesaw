-- #########################################################
-- Living Gear - auto-attune filters (report #228, auto-attune
-- options thread)
--
-- The auto-attune path had been an empty stub since 2026-08-23
-- (auto-consuming looted gear before the player could decide).
-- It is revived behind real filters: the account master switch,
-- the existing per-quality opt-out mask, and a NEW minimum item
-- level. Because attuning consumes the item, the master switch
-- flips to DEFAULT 0 and every existing account is reset to 0
-- here so nothing starts consuming loot without an explicit
-- opt-in through the Auto-Attune toggle. auto_attune_ilvl holds
-- the minimum item level (0 = no minimum).
-- #########################################################

ALTER TABLE `lg_account_meta` MODIFY COLUMN `auto_attune_on` tinyint unsigned NOT NULL DEFAULT 0;
UPDATE `lg_account_meta` SET `auto_attune_on` = 0;
ALTER TABLE `lg_account_meta` ADD COLUMN `auto_attune_ilvl` smallint unsigned NOT NULL DEFAULT 0 AFTER `auto_attune_off`;
