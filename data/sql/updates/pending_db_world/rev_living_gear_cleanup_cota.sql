-- Remove failed Call of the Archmage activator items/spells from world.

DELETE FROM `item_template` WHERE `entry` IN (900020, 900021);
DELETE FROM `spell_dbc` WHERE `ID` IN (900030, 900031);
