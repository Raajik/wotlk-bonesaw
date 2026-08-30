-- Correction to rev_chain_lightning_chaintarget.sql (report #184: Lightning
-- Bolt hitting for over 1 billion). The original full-row INSERT copied the
-- DAMAGE EFFECT's values (effect index 2 in the zero-based DBC) into the
-- spell_dbc effect-1 columns -- correct for the effect slot, but the
-- EffectRealPointsPerLevel value went in as the raw float BIT PATTERN
-- (1082130000 ~= float 4.0) instead of the float itself. Damage for rank N
-- became 714 + 1.08 billion per level over BaseLevel, hence billion bolts.
-- The ChainTarget=3 also landed on effect 2 (no effect) instead of effect 1
-- (the damage effect), so the original chain fix never actually engaged.
--
-- True shipped-DBC values, re-verified by struct-parsing Spell.dbc:
--   49238: bp 714, DieSides 101, RealPointsPerLevel 4.0
--   49239: bp 296, DieSides 43,  RealPointsPerLevel 1.5
--   49240: bp 356, DieSides 51,  RealPointsPerLevel 2.0
--
-- Fixes: RealPointsPerLevel_1 gets the real float, ChainTarget moves to
-- effect 1. Idempotent: UPDATE with WHERE guard on the broken value.
-- NOTE: the broken values are FLOATs (the raw int bit pattern reinterpreted)
-- and don't compare exactly -- guard with a > 1e9 range check.

UPDATE `spell_dbc` SET
  `EffectRealPointsPerLevel_1` = 4.0,
  `EffectChainTargets_1` = 3,
  `EffectChainTargets_2` = 0
WHERE `ID` = 49238 AND `EffectRealPointsPerLevel_1` > 1000000000;

UPDATE `spell_dbc` SET
  `EffectRealPointsPerLevel_1` = 1.5,
  `EffectChainTargets_1` = 3,
  `EffectChainTargets_2` = 0
WHERE `ID` = 49239 AND `EffectRealPointsPerLevel_1` > 1000000000;

UPDATE `spell_dbc` SET
  `EffectRealPointsPerLevel_1` = 2.0,
  `EffectChainTargets_1` = 3,
  `EffectChainTargets_2` = 0
WHERE `ID` = 49240 AND `EffectRealPointsPerLevel_1` > 1000000000;
