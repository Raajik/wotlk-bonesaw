-- *Autoloot (910008): toggleable auto-loot-on-kill. Client names/icons come
-- from patch-Y.MPQ Spell.dbc, but the SERVER also needs its own spell_dbc
-- row (sSpellMgr->GetSpellInfo) before it can ever grant/know this spell --
-- that row was never added, so the entire autoloot feature (toggle button,
-- reset, actual auto-looting) was dead from the start regardless of any
-- other fix. Mirrors the existing 910090/910091 rows exactly.

DELETE FROM `spell_dbc` WHERE `ID` = 910008;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910008, 16, 1, 101, 1, -1, -1, 3, 1, 185,
 '*Autoloot', 16712190,
 'Toggle automatic looting. On by default.', 16712190, 1, 1, 1);
