-- Jack in the Box dropped entirely (2026-08-21) in favor of a Subtlety
-- rework: Shadow Dance (permanent perk, two effects handled entirely in
-- C++/core-patch -- lets stealth-only openers be used without stealth,
-- and grants +10% attack power to the whole party/raid) and an AoE
-- Garrote+Pickpocket application on Hemorrhage. Reuses 910102's freed
-- spell ID rather than allocating a new one -- pure badge/perk-flag spell,
-- same minimal shape as the other non-castable account perks (Dummy
-- effect, no real aura, nothing to actually cast).

-- Icon 95 (Kill Combo's) reused deliberately -- confirmed rendering fine in
-- this build already; a real shadow-dance-themed icon couldn't be looked up
-- via MPQ tooling (same issue noted against the old Jack in the Box entry).
UPDATE `spell_dbc` SET
    `Name_Lang_enUS` = '*Shadow Dance',
    `Description_Lang_enUS` = 'Permanent. Stealth-only abilities (Ambush, Garrote, Cheap Shot, etc.) can be used without being stealthed. +10% attack power to your party/raid.',
    `SpellIconID` = 95
WHERE `ID` = 910102;

-- Jack in the Box's totem NPC is no longer summoned by anything -- remove
-- it outright rather than leaving an orphaned creature_template around.
DELETE FROM `creature_template` WHERE `entry` = 910200;
DELETE FROM `creature_template_model` WHERE `CreatureID` = 910200;
