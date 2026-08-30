-- Feature #159: delete all doors in Dire Maul.
-- Every door-type (type 0) gameobject spawn on map 429: the wing gates, the
-- Gordok courtyard and inner doors, the conservatory door, the crumble wall
-- and the pylon force fields. Templates stay; only the spawns go. The
-- instance script only touches these objects when they exist (entry switch in
-- OnGameObjectCreate), so missing spawns are already null-safe.

DELETE FROM `gameobject` WHERE `map` = 429 AND `id` IN (177211, 177212, 177213, 177215, 177217, 177219, 177220, 177221, 176907, 179503, 179506, 179549, 179550);
