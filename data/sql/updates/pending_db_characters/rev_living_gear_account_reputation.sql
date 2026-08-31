-- Account-wide reputation (reports #203/#216, 2026-08-30): every reputation
-- change earned by any character on an account is recorded here (faction id,
-- displayed standing) and pushed live onto every other online character of
-- that account; offline characters replay the stored values at their next
-- login (LivingGear_Next.cpp OnPlayerReputationChange /
-- EnsureAccountReputation). The stored number is the total the reputation tab
-- shows -- base reputation included -- which is exactly the value
-- ReputationMgr hands the OnPlayerReputationChange hook, so any alt is set to
-- the number the earning character sees regardless of base-rep differences.
CREATE TABLE IF NOT EXISTS `lg_account_reputation` (
  `account_id` INT UNSIGNED NOT NULL,
  `faction_id` INT UNSIGNED NOT NULL,
  `standing` INT NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `faction_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
