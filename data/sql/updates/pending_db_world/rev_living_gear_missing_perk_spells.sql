-- Badge spells for perks the interface advertises but that had no
-- spell_dbc row, so nothing could ever grant them. See bug #22 and
-- tools/perk_spell_audit.py.
--
-- Shape copied from the badges that already work (910011, 910070):
-- Attributes 16 (passive), Effect_1 3 (dummy), ImplicitTargetA_1 1
-- (caster), RangeIndex 1, no duration. They carry no mechanics of
-- their own -- the module reads the id and does the work.

INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910043, 16, 1, 3, 1, 1, '*Fishing: Cast', 16712190, 'Train Fishing. After you cast Fishing, it recasts and catches for you.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910044, 16, 1, 3, 1, 1, '*Fishing: Pools', 16712190, 'Catch 250 fish. While autofishing, loot pools within 25 yards.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910045, 16, 1, 3, 1, 1, '*Fishing: Speed', 16712190, 'Earn the 500 Fish achievement. Bites come twice as fast.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910046, 16, 1, 3, 1, 1, '*First Aid: Instant', 16712190, 'Train First Aid. Bandages become instant HoTs.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910047, 16, 1, 3, 1, 1, '*First Aid: Restore', 16712190, 'Train First Aid. Bandages restore 1% HP per second at 1-75, 2% at 76-150, and so on.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910048, 16, 1, 3, 1, 1, '*First Aid: Cleanse', 16712190, 'Max First Aid. While bandaged, remove debuffs every second.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910053, 16, 1, 3, 1, 1, '*Leveling: 1', 16712190, 'Have 1 character at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910054, 16, 1, 3, 1, 1, '*Leveling: 2', 16712190, 'Have 2 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910055, 16, 1, 3, 1, 1, '*Leveling: 3', 16712190, 'Have 3 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910056, 16, 1, 3, 1, 1, '*Leveling: 4', 16712190, 'Have 4 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910057, 16, 1, 3, 1, 1, '*Leveling: 5', 16712190, 'Have 5 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910058, 16, 1, 3, 1, 1, '*Leveling: 6', 16712190, 'Have 6 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910059, 16, 1, 3, 1, 1, '*Leveling: 7', 16712190, 'Have 7 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910060, 16, 1, 3, 1, 1, '*Leveling: 8', 16712190, 'Have 8 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910061, 16, 1, 3, 1, 1, '*Leveling: 9', 16712190, 'Have 9 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910062, 16, 1, 3, 1, 1, '*Leveling: 10', 16712190, 'Have 10 characters at level 80. +50% XP.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910063, 16, 1, 3, 1, 1, '*Cooking: 75', 16712190, 'Reach Cooking 75. Out of combat, heal 1% of max health and mana every second.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910064, 16, 1, 3, 1, 1, '*Cooking: 150', 16712190, 'Reach Cooking 150. Out of combat, heal 2% of max health and mana every second.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910065, 16, 1, 3, 1, 1, '*Cooking: 225', 16712190, 'Reach Cooking 225. Out of combat, heal 3% of max health and mana every second.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910066, 16, 1, 3, 1, 1, '*Cooking: 300', 16712190, 'Reach Cooking 300. Out of combat, heal 4% of max health and mana every second.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910067, 16, 1, 3, 1, 1, '*Cooking: 375', 16712190, 'Reach Cooking 375. Out of combat, heal 5% of max health and mana every second.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910068, 16, 1, 3, 1, 1, '*Cooking: 450', 16712190, 'Reach Cooking 450. Out of combat, heal 6% of max health and mana every second.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910150, 16, 1, 3, 1, 1, '*Hunter: Marksmanship', 16712190, 'Chimera Shot has no cooldown and refreshes Serpent Sting to full duration. Ranged shots have a chance to grant a free, instant Aimed Shot.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910151, 16, 1, 3, 1, 1, '*Shaman: Elemental', 16712190, 'Thunderstorm has no cooldown. Lava Burst deals double damage. Chain Lightning has no target cap.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910152, 16, 1, 3, 1, 1, '*Deathknight: Unholy', 16712190, 'Summon Gargoyle has no cooldown. Army of the Dead has no cooldown and also summons a 5-ghoul group: 1 tank, 1 healer, 3 dps.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910153, 16, 1, 3, 1, 1, '*Hunter: Beast Mastery', 16712190, 'Bestial Wrath has no cooldown/focus cost. Call up to 4 more beasts from your stable to fight alongside your pet, each at 50% stats.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910154, 16, 1, 3, 1, 1, '*Hunter: Survival', 16712190, 'Explosive Shot deals double damage. Traps lose their cooldown and get a bigger blast radius. You are immune to your own trap damage.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910155, 16, 1, 3, 1, 1, '*Shaman: Enhancement', 16712190, 'Feral Spirit is a free toggle: your 2 spirit wolves never expire while it''s active and deal double damage. Stormstrike has no cooldown.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910156, 16, 1, 3, 1, 1, '*Shaman: Restoration', 16712190, 'Riptide has no cooldown and also jumps to 2 more injured allies within 15 yards. Chain Heal has no bounce cap.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910157, 16, 1, 3, 1, 1, '*Warlock: Affliction', 16712190, 'Your DoTs spread to enemies within 15 yards every 1 sec. DoT tick damage is increased by your haste.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910158, 16, 1, 3, 1, 1, '*Warlock: Demonology', 16712190, 'Metamorphosis has no cooldown or shard cost. Your demon pet''s damage is doubled.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910159, 16, 1, 3, 1, 1, '*Warlock: Destruction', 16712190, 'Chaos Bolt has no cooldown. Conflagrate also casts a free, instant Chaos Bolt.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910160, 16, 1, 3, 1, 1, '*Druid: Balance', 16712190, 'Starfall has no cooldown/mana cost. You are permanently in both Solar and Lunar Eclipse at once.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910161, 16, 1, 3, 1, 1, '*Druid: Feral', 16712190, 'Berserk is a free toggle. While active, Cat/Bear abilities cost no energy/rage and lose their cooldowns.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910162, 16, 1, 3, 1, 1, '*Druid: Restoration', 16712190, 'Wild Growth has no cooldown and heals up to 10 allies within 30 yards. Rejuvenation spreads to injured allies within 15 yards every 3 sec.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910163, 16, 1, 3, 1, 1, '*Priest: Discipline', 16712190, 'Penance has no cooldown and also applies Power Word: Shield to the target.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910164, 16, 1, 3, 1, 1, '*Priest: Holy', 16712190, 'Guardian Spirit has no cooldown and also applies to 2 more injured allies within 20 yards.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910165, 16, 1, 3, 1, 1, '*Priest: Shadow', 16712190, 'Shadowfiend has no cooldown. Mind Flay deals quadruple damage.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910166, 16, 1, 3, 1, 1, '*Deathknight: Blood', 16712190, 'Dancing Rune Weapon has no cooldown/runic cost. While active, melee hits heal you for 5% of the damage dealt.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `RangeIndex`, `Effect_1`,
    `ImplicitTargetA_1`, `SpellIconID`, `Name_Lang_enUS`, `Name_Lang_Mask`,
    `Description_Lang_enUS`, `Description_Lang_Mask`)
  VALUES (910167, 16, 1, 3, 1, 1, '*Deathknight: Frost', 16712190, 'Hungering Cold has no cooldown/runic cost. Frost Strike and Obliterate deal double damage.', 16712190)
  ON DUPLICATE KEY UPDATE `Name_Lang_enUS` = VALUES(`Name_Lang_enUS`);

-- 40 spell(s).
