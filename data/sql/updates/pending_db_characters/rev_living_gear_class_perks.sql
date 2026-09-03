-- Living Gear class-spec perk selection (Mage, Warrior, Paladin Protection,
-- Rogue Combat -- see LivingGear_ClassPerks.cpp). Stores which of a class's
-- perk spells is the character's currently active pick. Character-scoped
-- (not account-scoped): the pick can differ per alt.

CREATE TABLE IF NOT EXISTS `lg_char_class_perk` (
  `guid` INT UNSIGNED NOT NULL,
  `spell_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
