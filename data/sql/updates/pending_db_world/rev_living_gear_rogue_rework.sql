-- Rogue Assassination and Combat reworked (2026-08-23).
--
-- Both were flat passives. Assassination was two numbers ("poisons +300%, DoTs
-- spread 10 yards") and Combat was three ("Blade Flurry always on, +50% energy
-- regen, 30% free Killing Spree"). Neither gave the player a moment, which is
-- what makes Subtlety's Shadowstep feel good.
--
-- Assassination: Envenom becomes a detonator. Every poison and bleed the rogue
-- owns, on the target and everything within 15 yards, is cashed in for its
-- entire remaining duration as instant damage and then put back at full. Build
-- wide, then detonate.
--
-- Combat: Adrenaline Rush becomes a free permanent toggle -- the same shape as
-- Bladestorm, Berserk and Arcane Power, all of which already work here. While
-- up: no energy costs, Blade Flurry reaches 15 yards, Killing Spree has no
-- cooldown.
--
-- Descriptions only; the behaviour is in LivingGear_ClassPerks.cpp. Kept in
-- step with the addon in the same change, because a stale tooltip is how a
-- perk ends up promising what it does not do.

UPDATE `spell_dbc` SET `Description_Lang_enUS` =
  'Learn Envenom. Poisons deal +300% damage. Envenom detonates every poison and bleed you own on all enemies within 15 yards, dealing their whole remaining duration at once, then puts them back at full.'
  WHERE `ID` = 910035;

UPDATE `spell_dbc` SET `Description_Lang_enUS` =
  'Learn Adrenaline Rush as a free toggle with no cooldown. While it is up your abilities cost no energy, Blade Flurry strikes everything within 15 yards, and Killing Spree has no cooldown. Blade Flurry is always on.'
  WHERE `ID` = 910036;
