-- *Travel: Swim (910098): swim speed +500% at level 10. Client name/icon from patch-Y.MPQ Spell.dbc.

DELETE FROM `spell_script_names` WHERE `spell_id` = 910098;
DELETE FROM `spell_dbc` WHERE `ID` = 910098;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910098, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 6, 1, 499,
 1, 58, 348,
 '*Travel: Swim', 16712190,
 'Swim speed +500%. Unlocked at level 10.', 16712190,
 1, 1, 1);
