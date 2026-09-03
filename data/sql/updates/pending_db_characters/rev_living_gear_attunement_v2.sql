-- Attunement rebuilt: per unique item, plus milestones (2026-08-23).
--
-- See modules/mod-living-gear/ATTUNEMENT-REDESIGN.md for the full reasoning.
-- The short version: item level was the attunement clock, so levelling a piece
-- both made it stronger and banked it, and "wear it or file it" had no clean
-- answer. The two are now separate jobs. Levelling makes the worn piece
-- powerful; attunement is a permanent account bonus earned by spending items.
--
-- `attune_pct` is 0-100. The stat columns now hold the item's FULL stats and
-- the account receives stats * attune_pct / 100, computed at apply time.
-- Storing full values means a milestone raising the rate is a single global
-- change rather than a rewrite of twenty thousand rows.
--
-- Existing rows are set to 100. They were earned under the old system, which
-- granted up to 100% of an item's stats at item level 25, and recomputing them
-- under the new formula would silently cut stats people already have. Being
-- generous to 96 accounts is much the cheaper mistake.

ALTER TABLE `lg_absorb`
  ADD COLUMN `attune_pct` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `item_level`;

UPDATE `lg_absorb` SET `attune_pct` = 100 WHERE `attune_pct` = 0;

-- Milestones that raise the per-item rate. One row per account per milestone
-- earned; the rate is the base plus five points for each row.
--
-- `milestone` is the achievement id, or 0 for the ones Living Gear defines
-- itself (attune N distinct items, level an item to 50) which have no
-- achievement behind them -- those use the `internal_id` column instead.
CREATE TABLE IF NOT EXISTS `lg_attune_milestone` (
  `account_id` INT UNSIGNED NOT NULL,
  `milestone` INT UNSIGNED NOT NULL,
  `earned_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `milestone`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
