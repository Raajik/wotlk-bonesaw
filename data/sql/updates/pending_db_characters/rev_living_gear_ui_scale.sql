-- Account-wide UI scale (85-175%), for LivingGear.lua's addon window
-- scale menu. See LivingGear.cpp SendAddonSync/OnPlayerCanUseChat.

SET @col_exists := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'lg_account_meta'
    AND `COLUMN_NAME` = 'ui_scale'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `lg_account_meta` ADD COLUMN `ui_scale` INT UNSIGNED NOT NULL DEFAULT 100',
  'SELECT 1');
PREPARE `stmt` FROM @sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;
