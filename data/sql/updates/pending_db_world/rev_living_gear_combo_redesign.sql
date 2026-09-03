-- Kill Combo (910089) redesign 2026-08-21: was 100 stacks/3% xp/independent
-- 3-min-per-stack decay -- and was never actually granted to anyone, so
-- nobody ever saw it. New spec: 10-stack cap, 20% kill XP per stack (applied
-- in C++, OnPlayerGiveXP), 5% move speed per stack via a real native
-- MOD_INCREASE_SPEED aura effect (effect 2, added here) so speed is granted
-- by the engine itself rather than hand-rolled. Single 10-minute timer that
-- refreshes in full on every kill.

UPDATE `spell_dbc` SET
  `Effect_2` = 6,
  `EffectAura_2` = 31,
  `EffectBasePoints_2` = 0,
  `EffectBonusMultiplier_2` = 1,
  `Description_Lang_enUS` = 'Each kill stacks this, up to 10. Kill XP +20% and move speed +5% per stack. Refreshes for 10 minutes on every kill.'
WHERE `ID` = 910089;
