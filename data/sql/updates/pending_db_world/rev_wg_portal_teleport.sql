-- Wintergrasp defender portals (report #163, 2026-08-30): using a portal cast
-- 54640, whose only script branch is the Strand of the Ancients teleport, and
-- this row linked it to 54643 "Teleport" -- which has no chain destination, so
-- it teleported the user to their own feet and stacked a 30s [Teleport] dummy
-- aura on them without moving anyone (same for Strand of the Ancients users).
-- The row is removed; SpellEffects.cpp handles the Wintergrasp case directly
-- and casts 59096 (the fortress teleport, coords already in
-- spell_target_position), which carries no aura.
DELETE FROM `spell_linked_spell`
WHERE `spell_trigger` = 54640 AND `spell_effect` = 54643;
