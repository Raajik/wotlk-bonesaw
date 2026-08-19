-- *Movement: Mounted Opener (910104): mounted jump boost + midair slam pull/Thunder Clap at level 40.
-- Client name/icon from patch-Y.MPQ Spell.dbc.

DELETE FROM `spell_script_names` WHERE `spell_id` = 910104;
DELETE FROM `spell_dbc` WHERE `ID` = 910104;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910104, 16, 1, 101, 1, -1, -1, 3, 1, 1299,
 '*Movement: Mounted Opener', 16712190,
 'While mounted: jump forward for a boosted leap (+50% forward momentum). Jump again midair to slam down, pull enemies within 20 yards, and Thunder Clap. Unlocked at level 40.', 16712190,
 1, 1, 1);
