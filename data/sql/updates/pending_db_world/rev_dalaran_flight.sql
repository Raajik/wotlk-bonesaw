-- Feature #184: allow flying mounts in Dalaran.
-- The "Flightless" debuff (spell 58600) is applied purely through spell_area
-- rows for the Dalaran areas; with the rows gone there is nothing to apply
-- and nothing to dismount flyers. Wintergrasp's own restriction (58730) is
-- untouched.

DELETE FROM `spell_area` WHERE `spell` = 58600;
