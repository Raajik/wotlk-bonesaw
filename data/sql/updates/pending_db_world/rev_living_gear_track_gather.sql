-- *Track Ore / *Track Herbs (910170/910171): pure badge/toggle-gate spells,
-- same as the gather yield/reach perks -- no real effect of their own.
-- Toggling them on makes the server periodically cast the real native
-- Blizzard tracking spells (Find Minerals 2580 / Find Herbs 2383) on the
-- player, which is what actually puts nodes on the minimap.

DELETE FROM `spell_dbc` WHERE `ID` IN (910170, 910171);

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910170, 16, 1, 101, 1, -1, -1, 3, 1, 406,
 '*Track Ore', 16712190,
 'Toggle. Shows nearby ore veins on the minimap.', 16712190, 1, 1, 1),
(910171, 16, 1, 101, 1, -1, -1, 3, 1, 960,
 '*Track Herbs', 16712190,
 'Toggle. Shows nearby herbs on the minimap.', 16712190, 1, 1, 1);
