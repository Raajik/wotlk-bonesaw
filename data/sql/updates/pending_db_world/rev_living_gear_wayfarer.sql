-- Wayfarer: one perk that trades movement speed against damage on a slider.
--
-- Replaces *Quest: Wayfarer, which was a flat +40% movement speed for 100
-- completed quests. Same spell id (910038) so every account that already owns
-- the perk keeps it and simply gets the new behaviour.
--
-- 910038 carries the SPEED half and needs all three movement aura types:
--   129 MOD_SPEED_ALWAYS                  -- on foot
--   130 MOD_MOUNTED_SPEED_ALWAYS          -- ground mount
--   209 MOD_MOUNTED_FLIGHT_SPEED_ALWAYS   -- flying
-- 129/130 alone cover running and ground mounts and nothing else; bug report
-- #27 against Kill Combo is the record of what leaving 209 out feels like.
--
-- That uses up all three effect slots spell_dbc has, so the DAMAGE half lives
-- on a second, hidden spell (910175, below).
--
-- EffectDieSides = 0 on every effect so the base points CastCustomSpell passes
-- arrive unmodified -- SpellEffectInfo::CalcValue adds DieSides otherwise, and
-- the old row carried DieSides 1, which is why the flat +40% was stored as 39.
--
-- Attributes 80 = SPELL_ATTR0_PASSIVE (0x40) | 0x10, matching every other
-- permanent perk aura here: no buff-bar icon for something that is simply
-- always on.

UPDATE `spell_dbc` SET
  `Effect_1` = 6,
  `EffectAura_1` = 129,
  `EffectDieSides_1` = 0,
  `EffectBasePoints_1` = 0,
  `ImplicitTargetA_1` = 1,
  `Effect_2` = 6,
  `EffectAura_2` = 130,
  `EffectDieSides_2` = 0,
  `EffectBasePoints_2` = 0,
  `ImplicitTargetA_2` = 1,
  `Effect_3` = 6,
  `EffectAura_3` = 209,
  `EffectDieSides_3` = 0,
  `EffectBasePoints_3` = 0,
  `ImplicitTargetA_3` = 1,
  `DurationIndex` = 21,
  `Attributes` = 80,
  `Name_Lang_enUS` = '*Wayfarer',
  `Description_Lang_enUS` = 'Balance movement speed against damage. Everything spent on one comes out of the other. Mounted and flying speed gain half the movement share. Changing the balance takes 30 seconds and cannot be done in combat.'
WHERE `ID` = 910038;

-- The damage half. Hidden companion to 910038: no name a player ever sees in
-- a tooltip of its own, no icon, no spellbook entry.
--
-- 79 MOD_DAMAGE_PERCENT_DONE with EffectMiscValue 127 (every school, physical
-- included). Unit reads this aura in three places -- SpellDamageBonusDone,
-- MeleeDamageBonusDone and UpdateDamagePctDoneMods -- and each filters on the
-- school mask, so 127 is what makes it apply to melee, ranged and spells
-- alike rather than to one of them.

DELETE FROM `spell_script_names` WHERE `spell_id` = 910175;
DELETE FROM `spell_dbc` WHERE `ID` = 910175;
INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`, `EffectMiscValue_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910175, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 6, 0, 0, 127,
 1, 79, 5,
 '*Wayfarer: Focus', 16712190,
 'The damage half of Wayfarer.', 16712190,
 1, 1, 1);

-- The two badges that widen the slider. They own no aura at all -- WayfarerCap
-- in LivingGear_Amenities.cpp only ever asks whether the account holds them --
-- but they need a spell_dbc row to exist, because UnlockPerk refuses to grant
-- a perk whose spell it cannot look up (and says so in the log).

DELETE FROM `spell_script_names` WHERE `spell_id` IN (910176, 910177);
DELETE FROM `spell_dbc` WHERE `ID` IN (910176, 910177);
INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`,
 `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910176, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 0, 0, 0,
 1, 0, 5,
 '*Wayfarer: Wide', 16712190,
 'Wayfarer reaches +75% instead of +50%. Explore Eastern Kingdoms or Kalimdor.', 16712190,
 1, 1, 1),
(910177, 80, 1048576, 1, 101, 1, 21,
 -1, -1,
 0, 0, 0,
 1, 0, 5,
 '*Wayfarer: Full', 16712190,
 'Wayfarer reaches the full +100%. Explore Outland or Northrend.', 16712190,
 1, 1, 1);
