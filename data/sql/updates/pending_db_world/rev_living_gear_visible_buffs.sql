-- Visible buffs pass, 2026-08-22.
--
-- Three perks that were real mechanically but invisible in the UI. Every one
-- of them has been reported as "not working" at some point, because from the
-- player's side an effect with no icon, no tooltip and no timer is
-- indistinguishable from one that never turned on.
--
-- DurationIndex values used below come from SpellDuration.dbc:
--   6  = 600000ms (10 minutes)
--   21 = -1       (permanent, refreshed/removed by the module)

-- ---------------------------------------------------------------------
-- Kill Combo (910089): stop being a hidden aura.
-- ---------------------------------------------------------------------
-- Attributes was 80 = 0x10 (IS_ABILITY) | 0x40 (PASSIVE). PASSIVE is what
-- kept it out of the buff bar, which is why the addon drew its own "Combo x7"
-- HUD frame over the top of the screen. Cleared to 0 so the stock buff frame
-- renders it. AttributesEx3 keeps 0x100000 (ALLOW_AURA_WHILE_DEAD) so dying
-- does not cost the combo.
--
-- DurationIndex moves off 21 (permanent, which is why the module had to recast
-- it every single second) onto 6, a real 10 minute duration matching
-- COMBO_SECONDS. The module now sets the exact remaining time on the aura
-- after each cast and otherwise leaves it alone, so the client ticks a real
-- countdown down instead of it being reset to full once a second.
--
-- StackAmount 10 matches COMBO_MAX, so the icon carries the stack number the
-- HUD used to print.
UPDATE `spell_dbc` SET
  `Attributes` = 0,
  `DurationIndex` = 6,
  `StackAmount` = 10,
  `Description_Lang_enUS` = 'Each kill stacks this, up to 10. Kill XP +20% and move speed +5% per stack. Refreshes for 10 minutes on every kill, and survives logging out.'
WHERE `ID` = 910089;

-- ---------------------------------------------------------------------
-- *Shadow Dance (910173): the visible half of the Subtlety perk.
-- ---------------------------------------------------------------------
-- 910102 stays exactly as it is -- a badge/perk flag with nothing to cast.
-- This is the aura that actually carries the +10% attack power it grants to
-- the Rogue and their party. It used to be applied with a raw
-- Player::ApplyStatPctModifier call, which changed the stat correctly and
-- showed the player nothing whatsoever.
--
-- MOD_ATTACK_POWER_PCT (aura 166) with EffectDieSides 0 so the value is
-- exactly the 10 in EffectBasePoints rather than 10+1 -- CalcValue adds
-- DieSides to the base points.
--
-- Permanent duration (21): the module casts it when the buff should be up and
-- removes it when it should not, so a fixed duration would only produce an
-- icon that flickered off and back on every time it lapsed.
--
-- Icon 95 reused from 910102 so the perk and its buff read as the same thing.
DELETE FROM `spell_script_names` WHERE `spell_id` = 910173;
DELETE FROM `spell_dbc` WHERE `ID` = 910173;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910173, 0, 1048576, 1, 101, 1, 21,
 -1, -1,
 6, 0, 10,
 1, 166, 95,
 '*Shadow Dance', 16712190,
 'Attack power increased by 10%. Granted by a Subtlety Rogue in your party.', 16712190,
 1, 1, 1);

-- ---------------------------------------------------------------------
-- *Well Fed (910174): the cooking regeneration tiers.
-- ---------------------------------------------------------------------
-- Was a hand-rolled ModifyHealth/ModifyPower tick every second with no icon.
-- Now a real aura feeding the two modifiers the engine's own regeneration
-- already reads: MOD_REGEN (84) in Player::RegenerateHealth and
-- MOD_POWER_REGEN (85) in Player::Regenerate. Both are denominated per 5
-- seconds; the module sets the base points per cast from the player's current
-- max health and mana, which is why EffectDieSides is 0 on both effects --
-- the passed-in value must arrive unmodified.
--
-- EffectMiscValue_2 = 0 is POWER_MANA, which is what
-- GetTotalAuraModifierByMiscValue matches on for the power half.
--
-- Health regen from MOD_REGEN is only added out of combat by the engine, so
-- the old manual "bail if in combat" check is no longer needed and the
-- behaviour now matches natural regeneration exactly.
--
-- Icon 134 (a food/drink style icon in this build); swap the SpellIconID
-- alone if it renders wrong, icons cannot be previewed via MPQ tooling here.
DELETE FROM `spell_script_names` WHERE `spell_id` = 910174;
DELETE FROM `spell_dbc` WHERE `ID` = 910174;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`, `ImplicitTargetA_1`, `EffectAura_1`,
 `Effect_2`, `EffectDieSides_2`, `EffectBasePoints_2`, `ImplicitTargetA_2`, `EffectAura_2`, `EffectMiscValue_2`,
 `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910174, 0, 1048576, 1, 101, 1, 21,
 -1, -1,
 6, 0, 0, 1, 84,
 6, 0, 0, 1, 85, 0,
 134,
 '*Well Fed', 16712190,
 'Your cooking keeps you going. Restores 1% of your health and mana per second for each cooking tier you have unlocked, at 75, 150, 225, 300, 375 and 450 skill.', 16712190,
 1, 1, 1);
