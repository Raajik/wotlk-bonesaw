-- Kill Combo (910089): party kills share stacks. Tooltip only; aura still recast in C++.
UPDATE `spell_dbc` SET
 `Description_Lang_enUS` = 'Party kills stack this. Kill XP +5% and movement speed +3% per stack. Lasts 60 seconds after the last party kill. Stacks up to 50 times.'
WHERE `ID` = 910089;
