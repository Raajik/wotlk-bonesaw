-- Speed cap, account riding, and per-class Naxx 25 buff unlock.

SET @col_exists := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'lg_account_meta'
    AND `COLUMN_NAME` = 'speed_cap'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `lg_account_meta` ADD COLUMN `speed_cap` INT UNSIGNED NOT NULL DEFAULT 500',
  'SELECT 1');
PREPARE `stmt` FROM @sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;

SET @col_exists := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'lg_account_meta'
    AND `COLUMN_NAME` = 'riding_skill'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `lg_account_meta` ADD COLUMN `riding_skill` INT UNSIGNED NOT NULL DEFAULT 0',
  'SELECT 1');
PREPARE `stmt` FROM @sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;

CREATE TABLE IF NOT EXISTS `lg_class_buff_unlock` (
  `account_id` INT UNSIGNED NOT NULL,
  `class` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `class`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
