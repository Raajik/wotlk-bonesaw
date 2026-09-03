-- Report #200: emblem/badge rewards from random dungeons increased 10x.
-- These are the quest rewards LFGMgr::FinishDungeon silently grants for
-- every random dungeon (lfg_dungeon_rewards.firstQuestId / otherQuestId),
-- one row per level bracket + heroic daily/weekly variants.

UPDATE `quest_template` SET `RewardAmount1` = 20 WHERE `ID` IN (24788, 24789, 24790) AND `RewardItem1` IN (47241, 49426) AND `RewardAmount1` = 2;
UPDATE `quest_template` SET `RewardAmount1` = 10 WHERE `ID` IN (24881, 24882, 24883, 24884, 24885, 24886, 24887, 24888, 24889, 24890, 24891, 24892, 24893, 24894, 24895, 24896, 25482, 25483, 25484, 25485) AND `RewardAmount1` = 1;
