-- *Call of the Wilds (910181): Survival Hunter "The Trap Engineer" castable.
-- A pure badge/trigger spell with no effect of its own, exactly like the
-- other 9101xx buttons -- the server's OnPlayerSpellCast hook recognizes the
-- cast and summons the 2 tank bears (clones of the pet, or Shardtooth Bears
-- when petless). The same row ships in the client DBC patch
-- (tools/client-patch/build_patch.py) so the spellbook button exists
-- client-side; icon 406 reused from *Track Ore, which is confirmed to
-- render in this build.

DELETE FROM `spell_dbc` WHERE `ID` = 910181;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910181, 16, 1, 101, 1, -1, -1, 3, 1, 406,
 '*Call of the Wilds', 16712190,
 'Summons 2 tank bears cloned from your pet (or the wilds themselves if you have no pet) at 50% health. They taunt nearby enemies and hold them while you shoot. Lasts 60 sec.', 16712190, 1, 1, 1);
