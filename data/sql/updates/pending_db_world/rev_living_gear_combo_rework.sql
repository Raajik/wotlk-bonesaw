-- Kill Combo (910089): +1% run/mount speed and +3% kill XP per stack.
-- Each kill adds a stack lasting 3 minutes; stacks decay one at a time.
-- Stack count tracked in LivingGear.cpp; recast with custom BP (no SetStackAmount).

UPDATE `spell_dbc` SET
 `DurationIndex` = 32,
 `Effect_1` = 6,
 `Effect_2` = 6,
 `EffectDieSides_1` = 1,
 `EffectDieSides_2` = 1,
 `EffectBasePoints_1` = 0,
 `EffectBasePoints_2` = 0,
 `ImplicitTargetA_1` = 1,
 `ImplicitTargetA_2` = 1,
 `EffectAura_1` = 129,
 `EffectAura_2` = 130,
 `Description_Lang_enUS` = 'Party kills stack this. Kill XP +3% and movement speed +1% per stack. Each stack lasts 3 minutes and falls off one at a time. Stacks up to 100 times.'
WHERE `ID` = 910089;
