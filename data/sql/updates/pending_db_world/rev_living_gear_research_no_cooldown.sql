-- Reports #108/#109/#110: remove the cooldown from the three profession
-- research spells. spell_cooldown_overrides is applied to the live SpellInfo
-- at startup (SpellMgr::LoadSpellInfoStore), so a zero row fully removes both
-- RecoveryTime and CategoryRecoveryTime. StartRecoveryTime (the GCD) is left
-- alone: the reports ask for the 20-hour research clock, not for a GCD-free
-- button.

DELETE FROM `spell_cooldown_overrides` WHERE `Id` IN (60893, 61177, 61288);
INSERT INTO `spell_cooldown_overrides` (`Id`, `RecoveryTime`, `CategoryRecoveryTime`, `StartRecoveryTime`, `StartRecoveryCategory`) VALUES
(60893, 0, 0, 0, 0),
(61177, 0, 0, 0, 0),
(61288, 0, 0, 0, 0);
