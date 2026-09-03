-- Jack in the Box (910102) had ImplicitTargetA_1 = 25 (TARGET_UNIT_TARGET_ANY),
-- which forces the client to require an existing unit target before it will
-- even let you cast the spell, and makes the dummy effect apply against
-- whatever's targeted instead of the caster. This is a self-cast, drop-a-
-- totem-at-my-feet ability -- SummonJackBox() only ever reads the caster's
-- own position and never looks at spell targets. Switch to
-- TARGET_UNIT_CASTER (1) so it casts with no target selected, matching
-- every other totem-style ability.
UPDATE `spell_dbc` SET `ImplicitTargetA_1` = 1 WHERE `ID` = 910102;
