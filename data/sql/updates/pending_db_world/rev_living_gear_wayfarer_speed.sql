-- Wayfarer ranks 4 and 5 (910039, 910040).
--
-- Wayfarer stopped being a dial balancing movement speed against damage and
-- became a straight speed track: five ranks of 20%, applied equally on foot,
-- mounted and flying. The old design halved the mounted share and split the
-- total against a damage bonus, so the number on the tin was never the number
-- you actually moved at.
--
-- Ranks 1-3 keep their existing ids and their exploration unlocks, so nobody
-- loses a rank they already earned. These two are new and are bought with
-- skill points like the other progression tracks.
--
-- Badge shape copied from the ones that already work (910011, 910070):
-- Attributes 16 (passive), Effect_1 3 (dummy), ImplicitTargetA_1 1 (caster),
-- RangeIndex 1, no duration. They carry no mechanics -- the module reads the
-- id and does the work in LivingGear_Amenities.cpp.

DELETE FROM `spell_dbc` WHERE `ID` IN (910039, 910040);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`) VALUES
(910039, 16, 1, 3, 1, 1, '*Wayfarer 4', 16712190, 'Movement speed +80% on foot, mounted and flying.', 16712190),
(910040, 16, 1, 3, 1, 1, '*Wayfarer 5', 16712190, 'Movement speed +100% on foot, mounted and flying.', 16712190);
