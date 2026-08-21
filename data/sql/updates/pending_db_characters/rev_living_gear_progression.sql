-- Account-wide exalted-faction tracking for the Honor/Reputation/Trade/Leveling
-- progression perks (910010-910031, 910053-910062) in LivingGear_Progression.cpp.
-- lg_account_meta / lg_account_perk already exist from earlier Living Gear
-- migrations; this only adds the new exalted-faction lookup table. Code
-- defensively probes for this table via information_schema before using it,
-- so it degrades gracefully (rep-count perks just never unlock) if this
-- migration hasn't been applied yet.

CREATE TABLE IF NOT EXISTS `lg_account_exalted_faction` (
  `account_id` INT UNSIGNED NOT NULL,
  `faction_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `faction_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
