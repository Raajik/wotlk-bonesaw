-- Account-wide mounts and companions (2026-08-22): riding skill has been
-- shared across the account since rev_living_gear_next_021.sql, but the
-- mounts and pets themselves were not -- so an alt could ride from level 1
-- and had nothing to ride. Any spell on skill line 777 (Mounts) or 778
-- (Companions) that any character on the account knows is recorded here and
-- learned by every other character on its own next login
-- (LivingGear_Next.cpp RecordAccountCollection/HarvestAccountCollection/
-- GrantAccountCollection). Same shape and same reasoning as lg_account_key:
-- nothing in this module writes into an offline character's spellbook.
CREATE TABLE IF NOT EXISTS `lg_account_mount` (
  `account_id` INT UNSIGNED NOT NULL,
  `spell_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
