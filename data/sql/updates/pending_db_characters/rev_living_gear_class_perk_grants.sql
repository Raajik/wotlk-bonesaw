-- What a class perk gave you, so switching specs can take it back (2026-08-23).
--
-- Bug reports #33, #36, #37, #39 and #40 are all one hole: selecting a class
-- perk does not teach the spells that perk modifies, and switching away does
-- not remove the ones it did teach. Of roughly thirty specs, three granted
-- anything at all and one revoked anything.
--
-- #37 is why this table has to exist rather than the grant list being consulted
-- both ways: "Crusader Strike was taught on login, but does not go away when
-- switching to another class perk". Removing "everything the previous spec
-- grants" is wrong, because some of those spells are ALSO trainable. A paladin
-- can train Consecration normally; taking it away because they tried Holy and
-- then switched would be stealing something they earned.
--
-- So we record exactly what we handed out, per character, and give back exactly
-- that. Anything the player learned by any other route is untouched.
--
-- Rows are deleted as the spells are revoked, so this stays small -- at most a
-- handful per character.

CREATE TABLE IF NOT EXISTS `lg_char_class_grant` (
  `guid` INT UNSIGNED NOT NULL,
  `spell_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guid`, `spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
