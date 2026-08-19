-- Kill combo (910089): hidden stacked aura, +5% kill XP per stack, 30 sec.
-- *Auto-Quest (910090): summon turn-in NPCs for completed log quests.
-- *Attuned Armory (910091): copy an attuned item into bags.
-- Client names/icons come from patch-Y.MPQ Spell.dbc.

DELETE FROM `spell_script_names` WHERE `spell_id` IN (910089, 910090, 910091);
DELETE FROM `spell_dbc` WHERE `ID` IN (910089, 910090, 910091);

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910089, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 6, 1, 4,
 1, 4, 95,
 '*Kill Combo', 16712190,
 'Each kill stacks this. Kill XP +5% per stack. Lasts 30 seconds.', 16712190,
 1, 1, 1);

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910090, 16, 1, 101, 1, -1, -1, 3, 1, 141,
 '*Quests - Finish', 16712190,
 'Summon the questgivers for completed quests in your log for 60 sec. Turn in and take follow-ups from them. Unlocked by completing 1 quest.', 16712190, 1, 1, 1),
(910091, 16, 1, 101, 1, -1, -1, 3, 1, 249,
 '*Attuned Armory', 16712190,
 'Make a wearable copy of an item you have attuned. Select a slot, then equip. The attunement stays on the account.', 16712190, 1, 1, 1);

UPDATE `spell_dbc` SET
 `Description_Lang_enUS` = 'Learn Bladestorm. No rage cost, no cooldown, and it does not end. Recast to stop. You can use other abilities while spinning.'
WHERE `ID` = 910083;
