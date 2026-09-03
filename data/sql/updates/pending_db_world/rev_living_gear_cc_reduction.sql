-- *CC Reduction (910172): passive account perk, cuts the duration of every
-- crowd control effect that lands on you by 95% -- stuns, roots, fears,
-- charms, sleeps, polymorphs, snares and silences.
--
-- Pure badge/perk-flag row, same minimal shape as *Gear: Curator (910101)
-- and *Shadow Dance (910102): Dummy effect, no real aura, nothing to cast.
-- The effect itself is applied in C++ (LivingGear_Perks.cpp
-- ReduceCrowdControl, off the OnAuraApply hook), NOT by this spell. It has
-- to be, because the native way to express this -- SPELL_AURA_MECHANIC_DURATION_MOD
-- (143) -- carries exactly one mechanic per spell effect, and a spell has
-- three effects, so there is no way to cover the ~14 control mechanics with
-- a spell_dbc row at all.
--
-- Icon 253 (a shackle/chain-break style icon in this build) chosen to read
-- as "breaking free"; icons cannot be previewed via MPQ tooling here, so
-- if it renders wrong swap the SpellIconID and nothing else.

DELETE FROM `spell_script_names` WHERE `spell_id` = 910172;
DELETE FROM `spell_dbc` WHERE `ID` = 910172;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910172, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 0, 0, 0,
 0, 0, 253,
 '*CC Reduction', 16712190,
 'Passive. Stuns, roots, fears, snares and other crowd control last 95% less on you.', 16712190,
 1, 1, 1);
