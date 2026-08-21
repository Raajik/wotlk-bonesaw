-- Jack in the Box (910102): give it a real 4s recast cooldown server-side
-- (was 0, relying on nothing but the client's default GCD). The matching
-- client-side fix (build_patch.py RECOVERY_OVERRIDE_MS) makes the action
-- bar actually show/enforce this same 4000ms cooldown visually.
UPDATE `spell_dbc` SET `RecoveryTime` = 4000 WHERE `ID` = 910102;
