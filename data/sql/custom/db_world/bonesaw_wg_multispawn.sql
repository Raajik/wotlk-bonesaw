-- Wintergrasp army multiplication rows for `creature_multispawn`.
--
-- These 36 rows exist on the live realm and in no SQL file anywhere: they were
-- applied by hand, or by a revision that was later deleted. A database built
-- from this repository came out at 984 rows where live has 1020, which is a
-- silent divergence in world content -- the Warsong/Wintergrasp champions
-- (entries 27107, 27108, 27110) simply would not multiply.
--
-- Captured from the live realm 2026-09-03. INSERT IGNORE so this is a no-op
-- against a database that already has them.

INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117631, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117631, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117632, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117632, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117637, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117637, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117638, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117638, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117639, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117639, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117640, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117640, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117642, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117642, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117643, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117643, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117644, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117644, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117645, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117645, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117716, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117716, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117724, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117724, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117730, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117730, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117734, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117734, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117755, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117755, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117756, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117756, 27110);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117820, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117820, 27108);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117822, 27107);
INSERT IGNORE INTO `creature_multispawn` (`spawnId`, `entry`) VALUES (117822, 27110);
