-- Report #209: repair the LIVE spell_dbc rows broken by the pre-correction
-- rev_chain_lightning_chaintarget.sql (float columns carried raw float bit
-- patterns; ChainTarget sat on the empty effect slot). The correction of the
-- INSERT source does not re-run against an already-broken DB, so fix the rows
-- in place. Idempotent: unconditional SET to the true DBC values.

UPDATE `spell_dbc` SET
  `Speed` = 20.0,
  `EffectRealPointsPerLevel_1` = 4.0,
  `EffectChainTargets_1` = 3,
  `EffectChainTargets_2` = 0
WHERE `ID` = 49238;

UPDATE `spell_dbc` SET
  `Speed` = 20.0,
  `EffectRealPointsPerLevel_1` = 1.5,
  `EffectChainTargets_1` = 3,
  `EffectChainTargets_2` = 0
WHERE `ID` = 49239;

UPDATE `spell_dbc` SET
  `Speed` = 20.0,
  `EffectRealPointsPerLevel_1` = 2.0,
  `EffectChainTargets_1` = 3,
  `EffectChainTargets_2` = 0
WHERE `ID` = 49240;
