-- Complete Quest goes account-wide, and gains a gold buyout (2026-08-23).
--
-- The cooldown was keyed on `guid`, one per character. It moves to the account.
--
-- I had argued for keeping it per character, on the grounds that this is a
-- repair tool and an account-wide cooldown just means fixing a quest on your
-- main blocks your alt. That was the wrong read: it is not only a repair tool.
-- It is also "skip the escort quest I genuinely hate", and that half needs to
-- be a scarce account resource or the ten minutes stops meaning anything.
-- Rationing it per account is the point, not a side effect.
--
-- The gold buyout is the requested "lazy man's gold sink". The cost escalates
-- with use or it is not a sink -- a flat fee is rounding error to anyone who
-- has played a while, and gold is pooled account-wide here, so the wealthy
-- account is exactly who it should bite. `bypass_count` counts buyouts inside
-- the current window and `window_start` is when that window opened; without the
-- window the price would ratchet up forever and the feature would be dead after
-- one bad afternoon.
--
-- Rebuilt rather than altered in place because the primary key changes. Rows
-- are carried over as the newest `last_used` per account, so nobody's cooldown
-- is quietly reset by the migration and two characters on one account cannot
-- collide on the new key.

CREATE TABLE IF NOT EXISTS `lg_quest_complete_new` (
  `account_id` INT UNSIGNED NOT NULL,
  `last_used` INT UNSIGNED NOT NULL DEFAULT 0,
  `bypass_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `window_start` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `lg_quest_complete_new` (`account_id`, `last_used`)
SELECT c.`account`, MAX(q.`last_used`)
FROM `lg_quest_complete` q
JOIN `characters` c ON c.`guid` = q.`guid`
GROUP BY c.`account`
ON DUPLICATE KEY UPDATE `last_used` = VALUES(`last_used`);

DROP TABLE `lg_quest_complete`;
RENAME TABLE `lg_quest_complete_new` TO `lg_quest_complete`;
