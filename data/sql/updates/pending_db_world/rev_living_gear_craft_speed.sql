-- Craft speed world-tab perks: 5 stacked 20% faster ranks.
-- Client names/icons come from patch-Y.MPQ Spell.dbc.

DELETE FROM `spell_script_names` WHERE `spell_id` BETWEEN 910093 AND 910097;
DELETE FROM `spell_dbc` WHERE `ID` BETWEEN 910093 AND 910097;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910093, 16, 1, 101, 1, -1, -1, 3, 1, 326,
 '*Craft: 1', 16712190,
 'Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 75 in a crafting profession.', 16712190, 1, 1, 1),
(910094, 16, 1, 101, 1, -1, -1, 3, 1, 326,
 '*Craft: 2', 16712190,
 'Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 150 in a crafting profession.', 16712190, 1, 1, 1),
(910095, 16, 1, 101, 1, -1, -1, 3, 1, 326,
 '*Craft: 3', 16712190,
 'Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 225 in a crafting profession.', 16712190, 1, 1, 1),
(910096, 16, 1, 101, 1, -1, -1, 3, 1, 326,
 '*Craft: 4', 16712190,
 'Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 300 in a crafting profession.', 16712190, 1, 1, 1),
(910097, 16, 1, 101, 1, -1, -1, 3, 1, 326,
 '*Craft: 5', 16712190,
 'Tradeskill craft time 20% faster. Stacks with other Craft ranks. Unlocked by reaching skill 375 in a crafting profession.', 16712190, 1, 1, 1);
