-- Feature #169: Archmage Lan'dalock's weekly raid quests ("... Must Die!")
-- award 250 of each emblem per turn-in instead of 5.
-- 49426 = Emblem of Frost, 47241 = Emblem of Triumph.

UPDATE `quest_template` SET `RewardAmount1` = 250, `RewardAmount2` = 250 WHERE `ID` BETWEEN 24579 AND 24590;
