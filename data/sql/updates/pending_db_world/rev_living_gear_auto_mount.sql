-- *Auto-Mount (910105): mount automatically when you leave combat. Toggle on World tab.
-- Client names/icons come from patch-Y.MPQ Spell.dbc.

DELETE FROM `spell_script_names` WHERE `spell_id` = 910105;
DELETE FROM `spell_dbc` WHERE `ID` = 910105;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910105, 16, 1, 101, 1, -1, -1, 3, 1, 132,
 '*Auto-Mount', 16712190,
 'Automatically mount when you leave combat. Toggle on the World tab or by casting this perk. Unlocked by learning a mount.', 16712190, 1, 1, 1);
