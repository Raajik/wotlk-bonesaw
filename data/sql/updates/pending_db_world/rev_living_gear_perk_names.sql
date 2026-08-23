-- Consistent perk spell names (2026-08-23).
--
-- The names had drifted into three styles at once. The originals abbreviated
-- (*Mine:, *Herb:, *Fish:, *Trade:, *Rep:), the 40 badges generated in 0.1.61
-- used the addon's full track names (*Fishing: Cast, *Cooking: 75), and the
-- result was *Fish: 150 and *Fishing: Cast sitting in the same list. Learning
-- a batch of them at login put the whole mess on screen at once, which is how
-- it got noticed.
--
-- Every name here is derived from the Account Perks window: "*<Track>: <Tick>"
-- using the track and tick text the player already reads there, so the two can
-- no longer disagree. The twenty ungrouped utility spells (*Mailbox, *Bank,
-- *Bind and friends) are not in a track and keep their bare names.
--
-- Names only. No behaviour, no ids, no perk membership changes.
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Reputation: 1 Exalted' WHERE `ID` = 910013;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Reputation: 5 Exalted' WHERE `ID` = 910014;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Reputation: 10 Exalted' WHERE `ID` = 910015;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Bloodsail' WHERE `ID` = 910016;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Darkmoon' WHERE `ID` = 910017;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Ravenholdt' WHERE `ID` = 910018;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Shendralar' WHERE `ID` = 910019;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Arathor' WHERE `ID` = 910020;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Defilers' WHERE `ID` = 910021;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Silverwing' WHERE `ID` = 910022;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Warsong' WHERE `ID` = 910023;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Stormpike' WHERE `ID` = 910024;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Factions: Frostwolf' WHERE `ID` = 910025;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Professions: 75' WHERE `ID` = 910026;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Professions: 150' WHERE `ID` = 910027;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Professions: 225' WHERE `ID` = 910028;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Professions: 300' WHERE `ID` = 910029;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Professions: 375' WHERE `ID` = 910030;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Professions: 450' WHERE `ID` = 910031;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Leveling: Wayfarer' WHERE `ID` = 910038;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Attune: Curator' WHERE `ID` = 910101;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Rogue: Shadow Dance' WHERE `ID` = 910102;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Leveling: Mounted Opener' WHERE `ID` = 910104;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Mining: 150' WHERE `ID` = 910109;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Mining: 300' WHERE `ID` = 910110;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Mining: 450' WHERE `ID` = 910111;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Mining: Reach 75' WHERE `ID` = 910112;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Mining: Reach 225' WHERE `ID` = 910113;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Mining: Reach 375' WHERE `ID` = 910114;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Herbalism: 150' WHERE `ID` = 910115;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Herbalism: 300' WHERE `ID` = 910116;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Herbalism: 450' WHERE `ID` = 910117;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Herbalism: Reach 75' WHERE `ID` = 910118;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Herbalism: Reach 225' WHERE `ID` = 910119;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Herbalism: Reach 375' WHERE `ID` = 910120;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Skinning: 150' WHERE `ID` = 910121;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Skinning: 300' WHERE `ID` = 910122;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Skinning: 450' WHERE `ID` = 910123;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Skinning: Reach 75' WHERE `ID` = 910124;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Skinning: Reach 225' WHERE `ID` = 910125;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Skinning: Reach 375' WHERE `ID` = 910126;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Fishing: 150' WHERE `ID` = 910127;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Fishing: 300' WHERE `ID` = 910128;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Fishing: 450' WHERE `ID` = 910129;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Fishing: Reach 75' WHERE `ID` = 910130;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Fishing: Reach 225' WHERE `ID` = 910131;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Fishing: Reach 375' WHERE `ID` = 910132;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Engineering: 150' WHERE `ID` = 910133;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Engineering: 300' WHERE `ID` = 910134;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Engineering: 450' WHERE `ID` = 910135;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Engineering: Reach 75' WHERE `ID` = 910136;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Engineering: Reach 225' WHERE `ID` = 910137;
UPDATE `spell_dbc` SET `Name_Lang_enUS` = '*Engineering: Reach 375' WHERE `ID` = 910138;

-- 53 name(s) corrected.
