-- Rogue perk descriptions reworded one entry per ABILITY rather than per
-- sentence, matching the Account Perks panel's new icon-bullet layout.
--
-- These strings exist in two places -- CLASS_PERKS in the client addon and
-- here -- and the panel and the spellbook tooltip are read side by side, so
-- they have to be changed together or they visibly contradict each other.
-- The addon side is modules/mod-living-gear/client_addon/LivingGear/LivingGear.lua.
--
-- A separate file because rev_living_gear_missing_perk_spells.sql has already
-- been imported and its ON DUPLICATE KEY UPDATE only refreshes Name_Lang_enUS,
-- so re-running it would never touch the description.

UPDATE `spell_dbc`
  SET `Description_Lang_enUS` = 'Envenom is learned for free and detonates every poison and bleed you own on all enemies within 15 yards, dealing their whole remaining duration at once, then puts them back to full. All your poisons deal +300% damage.'
  WHERE `ID` = 910035;

UPDATE `spell_dbc`
  SET `Description_Lang_enUS` = 'Adrenaline Rush is learned for free and becomes a toggle with no cooldown; while it is up your abilities cost no energy and Killing Spree has no cooldown. Blade Flurry is always on, and strikes everything within 15 yards while Adrenaline Rush is up.'
  WHERE `ID` = 910036;

UPDATE `spell_dbc`
  SET `Description_Lang_enUS` = 'Hemorrhage and Shadowstep are learned for free. Hemorrhage spreads itself, a boosted Ambush and the Garrote bleed to everything within 15 yards. Shadowstep has a 6 sec cooldown and pickpockets every humanoid within 20 yards of where you land. Eviscerate also applies Slice and Dice to you and Rupture to everything within 15 yards. Garrote and Rupture deal +2000% damage and tick faster with haste.'
  WHERE `ID` = 910037;
