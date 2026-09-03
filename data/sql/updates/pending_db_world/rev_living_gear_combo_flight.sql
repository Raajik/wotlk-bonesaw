-- Kill Combo speed in the air (bug report #27, 2026-08-22).
--
-- "kill combo speed boost doesn't apply to flight speed" -- correct, and it
-- never could have. The spell carried MOD_SPEED_ALWAYS (129) and
-- MOD_MOUNTED_SPEED_ALWAYS (130), which between them cover running and ground
-- mounts and nothing else. Flight is a separate aura family entirely.
--
-- Effect 3 adds MOD_MOUNTED_FLIGHT_SPEED_ALWAYS (209). 209 rather than 207
-- (MOD_INCREASE_MOUNTED_FLIGHT_SPEED) to match the "_ALWAYS" variants already
-- on the other two effects; all of the flight aura types share one handler
-- (HandleAuraModIncreaseFlightSpeed) so the choice is about stacking
-- behaviour, not about whether it works.
--
-- EffectDieSides_3 = 0 so the base points passed by CastCustomSpell arrive
-- unmodified -- CalcValue adds DieSides to them otherwise.
--
-- The module passes the same per-stack percentage to all three effects now.
-- It previously handed effect 1 the raw stack count, so on-foot speed rose 1%
-- per stack while the tooltip and the mounted effect both said 5%.

UPDATE `spell_dbc` SET
  `Effect_3` = 6,
  `EffectAura_3` = 209,
  `EffectDieSides_3` = 0,
  `EffectBasePoints_3` = 0,
  `ImplicitTargetA_3` = 1,
  `Description_Lang_enUS` = 'Kills stack this, up to 10. Kill XP increased by 20% per stack. Movement speed increased by 5% per stack, on foot, mounted and flying. Refreshes to 10 minutes on every kill. Survives death and logging out.'
WHERE `ID` = 910089;
