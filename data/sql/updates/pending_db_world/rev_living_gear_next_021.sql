-- Class Buffs (910106), Riding (910107), Auto-Accept (910108).
-- Client names/icons come from patch-Y.MPQ Spell.dbc.

DELETE FROM `spell_script_names` WHERE `spell_id` IN (910106, 910107, 910108);
DELETE FROM `spell_dbc` WHERE `ID` IN (910106, 910107, 910108);

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910106, 16, 1, 101, 1, -1, -1, 3, 1, 149,
 '*Class Buffs', 16712190,
 'After you clear Naxxramas 25 on a class, that class applies 10% primary stats to you and nearby party.', 16712190, 1, 1, 1),
(910107, 16, 1, 101, 1, -1, -1, 3, 1, 132,
 '*Riding', 16712190,
 'Riding skill is account-wide. Alts can mount from level 1 once anyone trained riding.', 16712190, 1, 1, 1),
(910108, 16, 1, 101, 1, -1, -1, 3, 1, 141,
 '*Auto-Accept', 16712190,
 'Auto-accept quests when you talk to an NPC. Hold Shift to skip. Does not accept on login.', 16712190, 1, 1, 1);
