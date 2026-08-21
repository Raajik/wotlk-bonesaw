-- Persist the Autoloot toggle and its "Attuned: Vendor/Disenchant" mode.
-- Default on (autoloot_on=1), matching the client's "On by default" tooltip.

SET @col_exists := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'lg_account_meta'
    AND `COLUMN_NAME` = 'autoloot_on'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `lg_account_meta` ADD COLUMN `autoloot_on` TINYINT UNSIGNED NOT NULL DEFAULT 1',
  'SELECT 1');
PREPARE `stmt` FROM @sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;

SET @col_exists := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'lg_account_meta'
    AND `COLUMN_NAME` = 'autoloot_de'
);
SET @sql := IF(@col_exists = 0,
  'ALTER TABLE `lg_account_meta` ADD COLUMN `autoloot_de` TINYINT UNSIGNED NOT NULL DEFAULT 0',
  'SELECT 1');
PREPARE `stmt` FROM @sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;
