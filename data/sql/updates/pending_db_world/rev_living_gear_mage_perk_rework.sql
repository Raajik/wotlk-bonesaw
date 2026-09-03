-- Mage class perk rework (2026-08-23).
--
-- The three mage perks all had the same shape of problem: something happened
-- on its own while the player watched, and none of them carried a damage
-- multiplier. Assassination gets +300% poisons, DK Frost x2, Priest Shadow x4
-- Mind Flay; Fire spread Living Bomb to eight enemies at a multiplier of
-- exactly one, which spreads noise.
--
-- Fire keeps the spread Swayss liked and gains a button: Fire Blast detonates
-- every Living Bomb in range at once and each blast re-seeds the effect, so a
-- pull chain-reacts outward.
--
-- Frost's Blizzard damage is now driven by the module rather than by the
-- spell's own channel. Blizzard is a channeled persistent area aura, and
-- making it linger like Death and Decay meant clearing
-- SPELL_ATTR1_IS_CHANNELED -- which is exactly the flag the engine checks
-- before syncing such an aura's lifetime. It placed its circle and did
-- nothing, which is what was reported.
--
-- Arcane trades passive Mirror Images for Arcane Power as a free permanent
-- toggle, the same shape as Arms' Bladestorm and Feral's Berserk.
--
-- Descriptions only. The behaviour lives in LivingGear_ClassPerks.cpp; these
-- rows are what the player reads in the tooltip, and leaving them stale is how
-- a perk ends up promising something it does not do (see
-- tools/perk_promise_audit.py).

UPDATE `spell_dbc` SET `Description_Lang_enUS` =
  'Arcane Power is a free toggle with no cooldown. While it is up your Arcane damage is quadrupled. In combat, Mirror Images appear and chain-cast, and linger 60 sec after combat.'
  WHERE `ID` = 910032;

UPDATE `spell_dbc` SET `Description_Lang_enUS` =
  'Fire spells apply Living Bomb, which spreads every 1 sec and deals +300% damage. Fire Blast detonates every Living Bomb within 15 yards at once, and each blast re-applies Living Bomb around it.'
  WHERE `ID` = 910033;

UPDATE `spell_dbc` SET `Description_Lang_enUS` =
  'Blizzard is instant, no cooldown, and lingers like Death and Decay, damaging everything inside it every 1 sec. Frost damage quadrupled. Ice Lance hits nearby enemies every 2 sec.'
  WHERE `ID` = 910034;
