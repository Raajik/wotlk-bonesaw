-- Persist the Pull Radius toggle. Default off.

SET @col_exists := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'lg_account_meta'
    AND `COLUMN_NAME` = 'pull_radius'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `lg_account_meta` ADD COLUMN `pull_radius` TINYINT UNSIGNED NOT NULL DEFAULT 0',
  'SELECT 1');
PREPARE `stmt` FROM @sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;
