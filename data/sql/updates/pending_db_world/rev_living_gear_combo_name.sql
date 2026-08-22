-- Kill Combo naming and description (bug report #14, 2026-08-22).
--
-- "kill combo buff should just be called 'Kill Combo' and have a description of
-- the actual effects".
--
-- The leading asterisk is this module's marker for perks the player casts from
-- the Account Perks panel. Kill Combo is not one of those -- it is a buff that
-- happens to you -- so it now reads as an ordinary buff and is named like one.
--
-- The server-side row governs behaviour; the client's own Spell.dbc entry in
-- patch-Y.MPQ is what the buff tooltip actually renders, and is updated to
-- match in tools/client-patch/build_patch.py. Both have to change or the
-- tooltip and the server disagree.
UPDATE `spell_dbc` SET
  `Name_Lang_enUS` = 'Kill Combo',
  `Description_Lang_enUS` = 'Kills stack this, up to 10. Kill XP increased by 20% per stack. Movement speed increased by 5% per stack. Refreshes to 10 minutes on every kill. Survives death and logging out.'
WHERE `ID` = 910089;
