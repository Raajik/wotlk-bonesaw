-- *Windblown: dummy spell for the Living Gear spellbook entry.
-- Client name/icon come from patch-Y.MPQ Spell.dbc. Server validates the cast.

DELETE FROM `spell_script_names` WHERE `spell_id` = 910001;
DELETE FROM `spell_dbc` WHERE `ID` = 910001;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910001, 16, 1, 101, 1,
 -1, -1,
 3, 1, 67,
 '*Windblown', 16712190,
 'Open Living Gear.', 16712190,
 1, 1, 1);
