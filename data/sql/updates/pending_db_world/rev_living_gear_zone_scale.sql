-- Optional zone level overrides for Living Gear zone scaling (AreaTable zone id).
-- When empty, module uses AreaTableEntry.area_level from DBC.

CREATE TABLE IF NOT EXISTS `lg_zone_scale` (
  `zone_id` INT UNSIGNED NOT NULL COMMENT 'AreaTable zone id (parent zone, not sub-area)',
  `max_level` TINYINT UNSIGNED NOT NULL COMMENT 'Recommended max level for this zone',
  PRIMARY KEY (`zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Living Gear zone scale overrides';

-- Examples (comment out or edit before apply):
-- DELETE FROM `lg_zone_scale` WHERE `zone_id` IN (14, 17);
-- INSERT INTO `lg_zone_scale` (`zone_id`, `max_level`) VALUES
-- (14, 12),  -- Durotar
-- (17, 25);  -- The Barrens
