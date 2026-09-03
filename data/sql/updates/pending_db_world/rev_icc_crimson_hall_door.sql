-- Bug #213: the Crimson Hall Door never opens -- the trash gauntlet before
-- the Blood Prince Council is supposed to signal DATA_BPC_TRASH_DIED (4 kills
-- open the passage), but nothing in the scripts ever fires it, so the door
-- stays shut after the adds die. Remove the spawn: the instance script only
-- touches these door objects when they exist (GUID lookup in
-- OnGameObjectCreate / door state maps), so a missing spawn is null-safe,
-- same pattern as the Dire Maul and SFK door removals.

DELETE FROM `gameobject` WHERE `guid` = 150332 AND `id` = 201376 AND `map` = 631;
