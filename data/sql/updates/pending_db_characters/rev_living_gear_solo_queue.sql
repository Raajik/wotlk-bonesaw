-- Persist Solo Queue world-tab toggle.

SET @col_exists := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'lg_account_meta'
    AND `COLUMN_NAME` = 'solo_queue'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `lg_account_meta` ADD COLUMN `solo_queue` TINYINT UNSIGNED NOT NULL DEFAULT 0',
  'SELECT 1');
PREPARE `stmt` FROM @sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;
