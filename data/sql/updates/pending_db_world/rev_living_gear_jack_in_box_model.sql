-- Jack in the Box's creature_template_model used CreatureDisplayID 15294,
-- which is shared with known invisible scripted-trigger mobs (Heart of
-- Hakkar, Dirt Mound, Ahn'Qiraj Trigger) -- effectively a "no model" ID.
-- The totem spawned and its AI ran (attacked, dealt damage) but was
-- completely invisible to the player. Swap to the real, stock Searing
-- Totem display ID (used by creature entries 2523/34687 already in this
-- database) so it actually renders.
UPDATE `creature_template_model` SET `CreatureDisplayID` = 4589 WHERE `CreatureID` = 910200;
