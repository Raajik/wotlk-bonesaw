-- Rename quest world perks for client/SQL parity.

UPDATE `spell_dbc` SET
 `Name_Lang_enUS` = '*Quests - Find',
 `Description_Lang_enUS` = 'Adds up to 5 available quests in your current zone, lowest level first. Unlocked by completing 50 quests.'
WHERE `ID` = 910088;

UPDATE `spell_dbc` SET
 `Name_Lang_enUS` = '*Quests - Finish',
 `Description_Lang_enUS` = 'Summon the questgivers for completed quests in your log for 60 sec. Turn in and take follow-ups from them. Unlocked by completing 1 quest.'
WHERE `ID` = 910090;
