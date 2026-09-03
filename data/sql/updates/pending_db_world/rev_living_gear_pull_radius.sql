-- *Pull Radius (910168): toggleable, quadruples aggro/detection radius via
-- the native SPELL_AURA_MOD_DETECTED_RANGE (152) aura -- Creature::GetAttackDistance()
-- already adds player->GetTotalAuraModifier(SPELL_AURA_MOD_DETECTED_RANGE)
-- directly into its yardage calc, so this needs zero core changes. Flat +60
-- yards, which roughly 4x's the ~20-yard same-level baseline (varies with
-- level difference, same as vanilla aggro range). Same short DurationIndex
-- as Kill Combo (910089) -- the server refreshes it via CastSpell every 10s
-- while the account toggle is on (LivingGear_Perks.cpp OnPlayerUpdate).

DELETE FROM `spell_dbc` WHERE `ID` = 910168;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectBasePoints_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910168, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 6, 59,
 1, 152, 237,
 '*Pull Radius', 16712190,
 'Toggle. Quadruples how far enemies detect and aggro onto you. For pulling everything in an area on purpose.', 16712190,
 1, 1, 1);
