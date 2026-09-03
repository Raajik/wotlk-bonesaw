-- Death Knight Unholy class perk (910152): "one-man raid group" ghouls
-- summoned alongside the real Army of the Dead. Model 26079 is copied from
-- the real Army of the Dead Ghoul (creature_template entry 24207) -- all
-- three roles share it for this first pass (see LivingGear_ClassPerks.cpp
-- comment above SpawnArmyGhoul for why).

DELETE FROM `creature_template` WHERE `entry` IN (910202, 910203, 910204);
INSERT INTO `creature_template`
(`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `npcflag`,
 `speed_walk`, `speed_run`, `rank`, `unit_class`, `unit_flags`, `unit_flags2`,
 `BaseAttackTime`, `RangeAttackTime`, `type`, `type_flags`,
 `HealthModifier`, `ManaModifier`, `ArmorModifier`, `flags_extra`,
 `ScriptName`, `VerifiedBuild`)
VALUES
(910202, 'Risen Tank', '', 80, 80, 35, 0,
 1, 1.14286, 0, 1, 0, 0,
 2000, 2000, 6, 0,
 1, 1, 1, 0,
 'npc_lg_army_ghoul_tank', 12340),
(910203, 'Risen Healer', '', 80, 80, 35, 0,
 1, 1.14286, 0, 1, 0, 0,
 2000, 2000, 6, 0,
 1, 1, 1, 0,
 'npc_lg_army_ghoul_healer', 12340),
(910204, 'Risen Warrior', '', 80, 80, 35, 0,
 1, 1.14286, 0, 1, 0, 0,
 2000, 2000, 6, 0,
 1, 1, 1, 0,
 'npc_lg_army_ghoul_dps', 12340);

DELETE FROM `creature_template_model` WHERE `CreatureID` IN (910202, 910203, 910204);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES
(910202, 0, 26079, 1, 1, 12340),
(910203, 0, 26079, 1, 1, 12340),
(910204, 0, 26079, 1, 1, 12340);
