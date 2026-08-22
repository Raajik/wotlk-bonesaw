-- Account-wide Key Ring (2026-08-21): any key item (ItemTemplate::Class ==
-- ITEM_CLASS_KEY) looted/received by any character on an account gets
-- recorded here, then granted into every other character's real, native
-- Key Ring the next time each of them logs in (LivingGear_Vault.cpp
-- RecordAccountKey/GrantAccountKeys). Matches the existing lg_account_perk
-- convention exactly (account-keyed side table, applied on next login --
-- there's no precedent anywhere in this module for writing directly into
-- an offline character's inventory, so this doesn't attempt to).
CREATE TABLE IF NOT EXISTS `lg_account_key` (
  `account_id` INT UNSIGNED NOT NULL,
  `item_entry` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
