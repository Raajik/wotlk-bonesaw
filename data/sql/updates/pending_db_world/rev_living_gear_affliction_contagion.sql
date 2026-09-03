-- Warlock: Affliction (910157) is contagious now: every infected enemy spreads
-- to its own neighbours each second, instead of only the selected target
-- seeding a fixed 15-yard ring. The description has to say so.
--
-- A separate file because rev_living_gear_missing_perk_spells.sql has already
-- been imported and its ON DUPLICATE KEY UPDATE only refreshes Name_Lang_enUS,
-- so re-running it would never touch the description.

UPDATE `spell_dbc`
  SET `Description_Lang_enUS` = 'Your DoTs spread every 1 sec from every infected enemy to others within 15 yards, creeping outward for as long as there are enemies to reach. DoT tick damage is increased by your haste.'
  WHERE `ID` = 910157;
