-- *Gear: Curator (910101): passive Living Gear XP on 5 lowest collection pieces at 1000 attuned items.

DELETE FROM `spell_script_names` WHERE `spell_id` = 910101;
DELETE FROM `spell_dbc` WHERE `ID` = 910101;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910101, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 0, 0, 0,
 0, 0, 249,
 '*Gear: Curator', 16712190,
 'Passively levels your 5 lowest collection pieces. Unlocked at 1000 attuned items.', 16712190,
 1, 1, 1);
