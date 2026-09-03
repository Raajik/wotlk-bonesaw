-- Wayfarer slider position.
--
-- Per CHARACTER, not per account, and that is on purpose: the unlock is
-- account-wide like every other perk, but a protection warrior and a hunter
-- want different balances and nobody wants to set it again on every alt. The
-- account_id column is what makes both true -- a character with no row of its
-- own inherits the account's most recently changed value (LoadWayfarer in
-- LivingGear_Amenities.cpp), so a new alt starts where its owner left off.
--
-- dmg_pct is the share of the dial spent on DAMAGE, 0-100. Movement speed
-- gets whatever is left.

CREATE TABLE IF NOT EXISTS `lg_wayfarer` (
  `guid` INT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `dmg_pct` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `changed_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`),
  KEY `idx_account` (`account_id`, `changed_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
