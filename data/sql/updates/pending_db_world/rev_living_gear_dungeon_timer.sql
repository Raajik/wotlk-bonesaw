-- Living Gear dungeon clear timer: par times + clear reward auras (910099 speed, 910100 event pace).
-- Tier thresholds default in living_gear.conf (Bronze 100% / Silver 75% / Gold 50% of par).

DELETE FROM `spell_script_names` WHERE `spell_id` IN (910099, 910100);
DELETE FROM `spell_dbc` WHERE `ID` IN (910099, 910100);

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `AttributesEx3`, `CastingTimeIndex`, `ProcChance`, `RangeIndex`, `DurationIndex`,
 `EquippedItemClass`, `EquippedItemSubclass`,
 `Effect_1`, `EffectDieSides_1`, `EffectBasePoints_1`,
 `Effect_2`, `EffectDieSides_2`, `EffectBasePoints_2`,
 `ImplicitTargetA_1`, `ImplicitTargetA_2`, `EffectAura_1`, `EffectAura_2`, `SpellIconID`,
 `Name_Lang_enUS`, `Name_Lang_Mask`,
 `Description_Lang_enUS`, `Description_Lang_Mask`,
 `EffectBonusMultiplier_1`, `EffectBonusMultiplier_2`, `EffectBonusMultiplier_3`)
VALUES
(910099, 80, 1048576, 1, 101, 1, 32,
 -1, -1,
 6, 1, 9,
 6, 1, 9,
 1, 1, 31, 31, 348,
 '*Dungeon: Speed Clear', 16712190,
 'Run and mount speed bonus after a fast dungeon clear. Stacks with Kill Combo.', 16712190,
 1, 1, 1),
(910100, 264, 1048576, 1, 101, 1, 32,
 -1, -1,
 6, 1, 14,
 0, 0, 0,
 1, 0, 4, 0, 348,
 '*Dungeon: Event Pace', 16712190,
 'Kill XP and Living Gear XP bonus after a fast dungeon clear.', 16712190,
 1, 1, 1);

CREATE TABLE IF NOT EXISTS `lg_dungeon_par` (
  `map_id` INT UNSIGNED NOT NULL COMMENT 'Map.dbc id',
  `difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=normal, 1=heroic (instance difficulty)',
  `par_sec` INT UNSIGNED NOT NULL COMMENT 'Par clear time in seconds',
  PRIMARY KEY (`map_id`, `difficulty`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Living Gear dungeon par times';

-- Defaults for common 5-mans (seconds). Omit a row to use LivingGear.DungeonTimer.DefaultParSec (1800).
DELETE FROM `lg_dungeon_par` WHERE (`map_id`, `difficulty`) IN
((389, 0), (43, 0), (36, 0), (33, 0), (48, 0), (189, 0), (289, 0), (409, 0));
INSERT INTO `lg_dungeon_par` (`map_id`, `difficulty`, `par_sec`) VALUES
(389, 0, 600),
(43, 0, 1200),
(36, 0, 1800),
(33, 0, 1800),
(48, 0, 1800),
(189, 0, 3600),
(289, 0, 3600),
(409, 0, 3600);
