-- Living Gear support tables (2026-08-22).
--
-- lg_bug_report   : player bug reports from ".bug <text>" / the addon's /bug.
-- lg_quest_complete: per-character cooldown for the quest log Complete button.
-- lg_combo        : Kill Combo state, so a 10 minute buff survives a relog.

-- Bug reports. `posted` is what tools/bug-reports/bug_digest.py flips once a
-- row has been delivered to Discord, so a re-run cannot double-post and a
-- failed run leaves the row to be picked up next time. Reports are kept after
-- posting rather than deleted -- they are the record of what was reported.
CREATE TABLE IF NOT EXISTS `lg_bug_report` (
  `id`             INT UNSIGNED    NOT NULL AUTO_INCREMENT,
  `account_id`     INT UNSIGNED    NOT NULL,
  `character_guid` INT UNSIGNED    NOT NULL,
  `character_name` VARCHAR(24)     NOT NULL DEFAULT '',
  `reported_at`    INT UNSIGNED    NOT NULL DEFAULT 0,
  `map_id`         INT UNSIGNED    NOT NULL DEFAULT 0,
  `zone_id`        INT UNSIGNED    NOT NULL DEFAULT 0,
  `zone_name`      VARCHAR(100)    NOT NULL DEFAULT '',
  `pos_x`          FLOAT           NOT NULL DEFAULT 0,
  `pos_y`          FLOAT           NOT NULL DEFAULT 0,
  `pos_z`          FLOAT           NOT NULL DEFAULT 0,
  `player_level`   TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `target_entry`   INT UNSIGNED    NOT NULL DEFAULT 0,
  `target_name`    VARCHAR(100)    NOT NULL DEFAULT '',
  `description`    TEXT            NOT NULL,
  `posted`         TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_posted` (`posted`, `id`),
  KEY `idx_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Quest Complete cooldown. Persisted rather than kept in memory so relogging
-- is not a way to skip the 10 minute wait.
CREATE TABLE IF NOT EXISTS `lg_quest_complete` (
  `guid`      INT UNSIGNED NOT NULL,
  `last_used` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Kill Combo. `expires` is a wall-clock unix timestamp, not a remaining
-- duration, so time spent logged out still counts against the 10 minute
-- window -- a full stack cannot be parked overnight and picked back up.
CREATE TABLE IF NOT EXISTS `lg_combo` (
  `guid`    INT UNSIGNED     NOT NULL,
  `stacks`  TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `expires` INT UNSIGNED     NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
