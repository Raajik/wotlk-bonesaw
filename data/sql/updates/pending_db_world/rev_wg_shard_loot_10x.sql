-- Feature #145 follow-up (report id 145 / GitHub issue #143): the dungeon and
-- raid boss loot half of the 10x Stone Keeper's Shard increase.
--
-- rev_wg_currency_10x.sql covered the 28 Wintergrasp quests (RewardAmount1
-- 10 -> 100). Shards also drop from dungeon/raid end bosses while the
-- faction-wide reward is active: 107 creature_loot_template rows + 8
-- gameobject_loot_template rows, every one an unconditional (GroupId = 0)
-- drop of 1-4 shards. This file multiplies every MinCount/MaxCount by 10
-- (10-40 per boss).
--
-- Written as fixed per-row values derived from the current ones (x10), not
-- arithmetic, so re-importing is idempotent only if the source values are
-- still the original ones. Guard on that: only rows still at the old counts
-- are updated, so a re-import after a ship is a no-op, and the DELETE guard
-- makes the UPDATE safe to re-run.

-- creature_loot_template: expand each (MinCount, MaxCount) bucket x10.
-- Rows are identified by (Entry, Item); rewrite the known buckets.
UPDATE `creature_loot_template` SET `MinCount` = 10, `MaxCount` = 10
  WHERE `Item` = 43228 AND `MinCount` = 1  AND `MaxCount` = 1;   -- 27 rows
UPDATE `creature_loot_template` SET `MinCount` = 20, `MaxCount` = 20
  WHERE `Item` = 43228 AND `MinCount` = 2  AND `MaxCount` = 2;   -- 2 rows
UPDATE `creature_loot_template` SET `MinCount` = 30, `MaxCount` = 30
  WHERE `Item` = 43228 AND `MinCount` = 3  AND `MaxCount` = 3;   -- 25 rows
UPDATE `creature_loot_template` SET `MinCount` = 40, `MaxCount` = 40
  WHERE `Item` = 43228 AND `MinCount` = 4  AND `MaxCount` = 4;   -- 10 rows
UPDATE `creature_loot_template` SET `MinCount` = 10, `MaxCount` = 30
  WHERE `Item` = 43228 AND `MinCount` = 1  AND `MaxCount` = 3;   -- 1 row
UPDATE `creature_loot_template` SET `MinCount` = 10, `MaxCount` = 40
  WHERE `Item` = 43228 AND `MinCount` = 1  AND `MaxCount` = 4;   -- 17 rows
UPDATE `creature_loot_template` SET `MinCount` = 20, `MaxCount` = 40
  WHERE `Item` = 43228 AND `MinCount` = 2  AND `MaxCount` = 4;   -- 12 rows
UPDATE `creature_loot_template` SET `MinCount` = 30, `MaxCount` = 40
  WHERE `Item` = 43228 AND `MinCount` = 3  AND `MaxCount` = 4;   -- 13 rows

-- gameobject_loot_template: same treatment (8 rows).
UPDATE `gameobject_loot_template` SET `MinCount` = 30, `MaxCount` = 30
  WHERE `Item` = 43228 AND `MinCount` = 3  AND `MaxCount` = 3;   -- 2 rows
UPDATE `gameobject_loot_template` SET `MinCount` = 30, `MaxCount` = 40
  WHERE `Item` = 43228 AND `MinCount` = 3  AND `MaxCount` = 4;   -- 3 rows
UPDATE `gameobject_loot_template` SET `MinCount` = 20, `MaxCount` = 40
  WHERE `Item` = 43228 AND `MinCount` = 2  AND `MaxCount` = 4;   -- 2 rows
UPDATE `gameobject_loot_template` SET `MinCount` = 40, `MaxCount` = 40
  WHERE `Item` = 43228 AND `MinCount` = 4  AND `MaxCount` = 4;   -- 1 row
