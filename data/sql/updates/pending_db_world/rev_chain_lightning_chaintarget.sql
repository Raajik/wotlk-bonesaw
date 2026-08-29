-- Report #166: Chain Lightning "doesn't work at all, unusable" for a
-- Shaman Elemental player. Two stacked causes; this migration fixes the
-- data half. The DBC's rank ladder for Chain Lightning is inconsistent:
-- rank 1 (421) carries ChainTarget=3, the WotLK-end rank (49271) carries 3,
-- but the per-level ranks 49238/49239/49240 (learned in between) carry
-- ChainTarget=0, so the spell engine skips chain target selection
-- entirely (maxTargets=0 -> "if (maxTargets > 1)" is false) and the spell
-- hits exactly one target for anyone who learned those ranks.
--
-- Restore ChainTarget=3 on effect 2 (the damage effect) for the three
-- broken ranks. Idempotent by primary key; only rows still at 0 change.
--
-- The code half (SPELLVALUE_MAX_TARGETS now honored by
-- Spell::SelectImplicitChainTargets) is recorded in core-patch 0046.

UPDATE `spell_dbc` SET
  `EffectChainTargets_2` = 3
WHERE `Id` IN (49238, 49239, 49240)
  AND `EffectChainTargets_2` = 0;
