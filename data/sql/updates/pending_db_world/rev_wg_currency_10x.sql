-- Feature #145 (report id 145 / GitHub issue #143): increase Stone Keeper's
-- Shard and Wintergrasp Mark of Honor gains by 10x.
--
-- Where the gains actually come from, checked against the live acore_world on
-- 2026-08-29:
--
--   * Stone Keeper's Shard (43228): two grant paths.
--       1. The 28 Wintergrasp quests below, every one rewarding exactly
--          RewardItem1 = 43228 x10. These are the only quest_template rows
--          rewarding either currency (43589 appears in no quest, loot table,
--          vendor or mail template anywhere in the DB).
--       2. Dungeon/raid boss and object loot (107 creature_loot_template +
--          8 gameobject_loot_template rows). Those tables are owned by the
--          loot workstream and are deliberately NOT touched here.
--   * Wintergrasp Mark of Honor (43589): zero sources. Nothing in quests,
--      loot, vendors or mail grants it and no core code references the item,
--      so there is no gain to multiply. The WG reward path hands out shards
--      (43228), which this file does boost on the quest side.
--
-- Fixed values, not arithmetic, so re-importing the file is a no-op.

UPDATE `quest_template` SET `RewardAmount1` = 100 WHERE `ID` IN (
  236, 13153, 13154, 13156, 13177, 13178, 13179, 13180, 13181, 13183,
  13185, 13186, 13191, 13192, 13193, 13194, 13195, 13196, 13197, 13198,
  13199, 13200, 13201, 13202, 13222, 13223, 13538, 13539
) AND `RewardItem1` = 43228;
-- All 28 are Wintergrasp quests (Fueling the Demolishers, Warding the Warriors,
-- Bones and Arrows, A Rare Herb, No Mercy for the Merciless, Slay them all!,
-- Victory in Wintergrasp, Stop the Siege, Healing with Roses, Jinxing the
-- Walls, Defend the Siege, Southern Sabotage, Toppling the Towers and their
-- faction variants), each previously rewarding 10 shards.
