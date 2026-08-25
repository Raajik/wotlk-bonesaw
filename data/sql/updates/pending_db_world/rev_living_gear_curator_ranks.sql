-- Curator ranks 2-4 (910178, 910179, 910180).
--
-- Curator used to feed 1 item XP a minute to the five lowest-level pieces in
-- your bags and bank, pushing them up the 1%-to-100% attunement ramp. That was
-- roughly 7.6 hours of being logged in to finish a single endgame item, and it
-- required the design to explain why a tabard sitting in the bank was gaining
-- levels.
--
-- Each rank now states a share of your collection outright: 25% at rank 1, then
-- 50, 75 and 100. Everything in bags and bank counts for that share of its BASE
-- stats straight away, so there is no drip and no timer.
--
-- Base rather than grown stats is what keeps wearing gear worthwhile: equipped
-- items bank their GROWN stats through the level ramp, grown only ever exceeds
-- base, and the lg_absorb ratchet keeps whichever side is larger. Leaving a
-- piece in the bank can never beat wearing and levelling it.
--
-- Badge shape copied from the ones that already work (910011, 910070):
-- Attributes 16 (passive), Effect_1 3 (dummy), ImplicitTargetA_1 1 (caster),
-- RangeIndex 1, no duration. The module reads the id and does the work in
-- LivingGear_Perks.cpp (CuratorCoverage) and LivingGear.cpp
-- (LivingGear_BankCollection).

DELETE FROM `spell_dbc` WHERE `ID` IN (910178, 910179, 910180);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`) VALUES
(910178, 16, 1, 3, 1, 1, '*Attune: Curator 2', 16712190, 'Items in your bags and bank count for 50% of their value.', 16712190),
(910179, 16, 1, 3, 1, 1, '*Attune: Curator 3', 16712190, 'Items in your bags and bank count for 75% of their value.', 16712190),
(910180, 16, 1, 3, 1, 1, '*Attune: Curator 4', 16712190, 'Items in your bags and bank count for 100% of their value.', 16712190);
