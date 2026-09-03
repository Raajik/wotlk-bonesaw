-- Living Gear: per-item XP/levels and account absorb from sacrificed copies.

CREATE TABLE IF NOT EXISTS `lg_item` (
  `item_guid` INT UNSIGNED NOT NULL,
  `item_entry` INT UNSIGNED NOT NULL,
  `owner_guid` INT UNSIGNED NOT NULL,
  `xp` INT UNSIGNED NOT NULL DEFAULT 0,
  `level` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `roll_str` INT NOT NULL DEFAULT 0,
  `roll_agi` INT NOT NULL DEFAULT 0,
  `roll_sta` INT NOT NULL DEFAULT 0,
  `roll_int` INT NOT NULL DEFAULT 0,
  `roll_spi` INT NOT NULL DEFAULT 0,
  PRIMARY KEY (`item_guid`),
  KEY `idx_lg_item_owner` (`owner_guid`),
  KEY `idx_lg_item_entry` (`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `lg_absorb` (
  `account_id` INT UNSIGNED NOT NULL,
  `item_entry` INT UNSIGNED NOT NULL,
  `str` FLOAT NOT NULL DEFAULT 0,
  `agi` FLOAT NOT NULL DEFAULT 0,
  `sta` FLOAT NOT NULL DEFAULT 0,
  `intel` FLOAT NOT NULL DEFAULT 0,
  `spi` FLOAT NOT NULL DEFAULT 0,
  `armor` FLOAT NOT NULL DEFAULT 0,
  `item_level` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`account_id`, `item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
