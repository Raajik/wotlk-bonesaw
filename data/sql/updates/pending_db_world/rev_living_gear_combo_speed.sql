-- Kill Combo (910089): +3% run/mount speed per stack, 60s from last kill, cap 50.
-- Stack count is tracked in LivingGear.cpp; recast with custom BP. XP +5%/stack stays.
-- Supersedes rev_living_gear_combo_party.sql and rev_living_gear_combo_rework.sql
-- (both applied earlier in the same release and fully overwritten by this file,
-- since AzerothCore applies pending SQL alphabetically by filename).

UPDATE `spell_dbc` SET
 `DurationIndex` = 3,
 `Effect_1` = 6,
 `Effect_2` = 6,
 `EffectDieSides_1` = 1,
 `EffectDieSides_2` = 1,
 `EffectBasePoints_1` = 2,
 `EffectBasePoints_2` = 2,
 `ImplicitTargetA_1` = 1,
 `ImplicitTargetA_2` = 1,
 `EffectAura_1` = 129,
 `EffectAura_2` = 130,
 `Description_Lang_enUS` = 'Party kills stack this. Kill XP +5% and movement speed +3% per stack. Lasts 60 seconds after the last party kill. Stacks up to 50 times.'
WHERE `ID` = 910089;
