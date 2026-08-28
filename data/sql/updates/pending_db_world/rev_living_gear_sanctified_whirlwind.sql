-- Sanctified Whirlwind (Paladin Retribution perk 910071, plan pass 2
-- 2026-08-28): the free permanent toggle button. Castable self-buff with an
-- infinite-duration dummy aura (DurationIndex 21 = -1 in SpellDuration.dbc,
-- verified); the module keeps it alive via observation and owns the off-switch
-- in the strict CheckCast pass -- same free-permanent-toggle shape as
-- Bladestorm/Starfall. Everything mechanical (Consecration follow, Divine
-- Storm cooldown clears, DS-window swing cleave) lives in C++ in
-- LivingGear_Next.cpp; this spell only carries the visible aura.
-- Icon 95 reused deliberately (same MPQ lookup limitation noted in
-- rev_living_gear_shadow_dance.sql -- confirmed rendering fine already).

DELETE FROM `spell_dbc` WHERE `ID` = 910182;

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`, `SchoolMask`,
 `Effect_1`, `EffectAura_1`, `EffectBasePoints_1`, `ImplicitTargetA_1`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`, `EffectBonusMultiplier_1`)
VALUES
(910182, 0, 1, 101, 1, 21, -1, -1, 1, 6, 4, 0, 1, 95,
 '*Sanctified Whirlwind', 16712190,
 'Toggle. Consecration follows you, and Divine Storm cooldown is cleared whenever you deal holy damage. During Divine Storm your melee swings also strike all enemies within 8 yards for 50%.', 16712190, 1);
