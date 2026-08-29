-- Report #179: "account-wide flight points ... may be easier to just have a
-- perk that grants all flight points after doing something easy like
-- exploring a zone". Design (user-approved): with the Flight amenity owned,
-- the first time any character on the account enters a map, every taxi node
-- on that map unlocks for the whole account. This table records which maps
-- each account has explored; on login the stored maps re-apply their nodes
-- to the newly-loaded character (the per-character taximask then persists
-- them as usual).
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS `lg_account_taxi_maps` (
  `account_id` INT UNSIGNED NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `unlocked_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
