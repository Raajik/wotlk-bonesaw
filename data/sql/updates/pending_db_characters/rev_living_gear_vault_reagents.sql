-- Extra reagent-vault item ids (bug reports #35, #65, #66, #67).
--
-- Dark Iron Residue, Dark Iron Scraps, Core of Elements, the darkmoon cards,
-- Hakkari bijous and coins, Relic of Ulduar and the Argent Dawn token are all
-- ITEM_CLASS_QUEST (12) in item_template rather than trade goods, so
-- IsReagentItem's class check never claimed any of them and every one stayed
-- in players' bags.
--
-- Admitting class 12 wholesale is not safe. The quest guard in IsReagentItem
-- only covers quests the player is currently ON, so a quest-starter item looted
-- before its quest was accepted would be filed into the vault where the player
-- cannot reach it -- which is report #21 from the other direction. An explicit
-- list keeps these repeatable turn-in currencies working without putting every
-- quest item in the game at risk.
--
-- Read at startup (LivingGear_Vault.cpp, LoadVaultReagentIds) and consulted
-- AFTER the quest guard, so an item wanted by a quest the player is actually on
-- still stays in their bags regardless of appearing here.
--
-- To cover more of the "various oddities" in report #66, add a row. That needs
-- a worldserver restart, not a rebuild.

CREATE TABLE IF NOT EXISTS `lg_vault_reagent` (
  `item_entry` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DELETE FROM `lg_vault_reagent` WHERE `item_entry` IN (1274, 12844, 18945, 19227, 19230, 19231, 19232, 19233, 19234, 19235, 19236, 19258, 19259, 19260, 19261, 19262, 19263, 19264, 19265, 19268, 19269, 19270, 19271, 19272, 19273, 19274, 19275, 19276, 19278, 19279, 19280, 19281, 19282, 19283, 19284, 19698, 19699, 19700, 19701, 19702, 19703, 19704, 19705, 19706, 19707, 19708, 19709, 19710, 19711, 19712, 19713, 19714, 19715, 21100, 22527, 22528, 22529, 42780);
INSERT INTO `lg_vault_reagent` (`item_entry`) VALUES
(1274),  -- Hops
(12844),  -- Argent Dawn Valor Token
(18945),  -- Dark Iron Residue
(19227),  -- Ace of Beasts
(19230),  -- Two of Beasts
(19231),  -- Three of Beasts
(19232),  -- Four of Beasts
(19233),  -- Five of Beasts
(19234),  -- Six of Beasts
(19235),  -- Seven of Beasts
(19236),  -- Eight of Beasts
(19258),  -- Ace of Warlords
(19259),  -- Two of Warlords
(19260),  -- Three of Warlords
(19261),  -- Four of Warlords
(19262),  -- Five of Warlords
(19263),  -- Six of Warlords
(19264),  -- Seven of Warlords
(19265),  -- Eight of Warlords
(19268),  -- Ace of Elementals
(19269),  -- Two of Elementals
(19270),  -- Three of Elementals
(19271),  -- Four of Elementals
(19272),  -- Five of Elementals
(19273),  -- Six of Elementals
(19274),  -- Seven of Elementals
(19275),  -- Eight of Elementals
(19276),  -- Ace of Portals
(19278),  -- Two of Portals
(19279),  -- Three of Portals
(19280),  -- Four of Portals
(19281),  -- Five of Portals
(19282),  -- Six of Portals
(19283),  -- Seven of Portals
(19284),  -- Eight of Portals
(19698),  -- Zulian Coin
(19699),  -- Razzashi Coin
(19700),  -- Hakkari Coin
(19701),  -- Gurubashi Coin
(19702),  -- Vilebranch Coin
(19703),  -- Witherbark Coin
(19704),  -- Sandfury Coin
(19705),  -- Skullsplitter Coin
(19706),  -- Bloodscalp Coin
(19707),  -- Red Hakkari Bijou
(19708),  -- Blue Hakkari Bijou
(19709),  -- Yellow Hakkari Bijou
(19710),  -- Orange Hakkari Bijou
(19711),  -- Green Hakkari Bijou
(19712),  -- Purple Hakkari Bijou
(19713),  -- Bronze Hakkari Bijou
(19714),  -- Silver Hakkari Bijou
(19715),  -- Gold Hakkari Bijou
(21100),  -- Coin of Ancestry
(22527),  -- Core of Elements
(22528),  -- Dark Iron Scraps
(22529),  -- Savage Frond
(42780);  -- Relic of Ulduar
