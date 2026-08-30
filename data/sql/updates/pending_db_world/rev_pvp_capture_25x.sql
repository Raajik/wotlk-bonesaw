-- Feature #167: PvP objective capture speed ~25x across every data-driven
-- capture point. Speed = maxValue / Data16 (minTime seconds) in both the
-- Battlefield (Wintergrasp) and OutdoorPvP sliders, so this is a pure Data16
-- change: Eastern Plaguelands 480s->19s, Hellfire 240s->10s, Halaa/Zangar/
-- Venture Bay 300s->12s, Terokkar + Wintergrasp banners 60s->2s, Nagrand
-- towers 90s->4s. Eye of the Storm is code-driven (bar tick in
-- BattlegroundEY) and is NOT covered here.

UPDATE `gameobject_template` SET `Data16` = 19 WHERE `entry` IN (181899, 182096, 182097, 182098);
UPDATE `gameobject_template` SET `Data16` = 10 WHERE `entry` IN (182173, 182174, 182175);
UPDATE `gameobject_template` SET `Data16` = 12 WHERE `entry` IN (182210, 182522, 182523, 189310);
UPDATE `gameobject_template` SET `Data16` = 2 WHERE `entry` IN (183104, 183411, 183412, 183413, 183414, 190475, 190487, 192626, 192627, 194959, 194960, 194962, 194963);
UPDATE `gameobject_template` SET `Data16` = 4 WHERE `entry` IN (184080, 184081, 184082, 184083);
