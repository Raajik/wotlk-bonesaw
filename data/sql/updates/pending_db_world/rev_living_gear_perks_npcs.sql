-- Subtlety summons: Jack in the Box trap and Shadow Clone.
-- Live creature_template has no `scale` column (model scale is creature_template_model.DisplayScale).

DELETE FROM `creature_template` WHERE `entry` IN (910200, 910201);
INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `npcflag`,
 `speed_walk`, `speed_run`, `rank`, `unit_class`, `unit_flags`, `unit_flags2`,
 `BaseAttackTime`, `RangeAttackTime`, `type`, `type_flags`,
 `HealthModifier`, `ManaModifier`, `ArmorModifier`, `flags_extra`,
 `ScriptName`, `VerifiedBuild`)
VALUES
(910200, 'Jack in the Box', '', 80, 80, 35, 0,
 1, 1.14286, 0, 1, 33554432, 0,
 2000, 2000, 10, 0,
 1, 1, 1, 0,
 'npc_lg_jack_box', 12340),
(910201, 'Shadow Clone', '', 80, 80, 35, 0,
 1, 1.14286, 0, 4, 0, 0,
 2000, 2000, 7, 0,
 1, 1, 1, 0,
 'npc_lg_shadow_clone', 12340);

DELETE FROM `creature_template_model` WHERE `CreatureID` IN (910200, 910201);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES
(910200, 0, 15294, 1, 1, 12340),
(910201, 0, 11686, 1, 1, 12340);
