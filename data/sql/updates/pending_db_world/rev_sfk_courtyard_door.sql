-- Feature #133: remove the Shadowfang Keep courtyard door.
-- The door (entry 18895) sits between the entrance courtyard and the keep and
-- exists only to gate the route behind an unlock; with zone-scaled content any
-- level character can be walking in here and the door is pure friction.
-- Removing the spawn opens the path permanently; the template stays.

DELETE FROM `gameobject` WHERE `guid` = 20835 AND `id` = 18895;
