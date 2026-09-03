-- Durable GitHub linkage for in-game bug and feature reports.
--
-- The characters DB remains the intake/audit source; GitHub becomes the work
-- tracker. NULL means the bridge has not linked the report yet. A unique issue
-- number makes a retry safe after a partial failure.

ALTER TABLE `lg_bug_report`
  ADD COLUMN `report_type` VARCHAR(16) NOT NULL DEFAULT 'bug' AFTER `id`,
  ADD COLUMN `github_issue_number` INT UNSIGNED NULL AFTER `discord_message_id`,
  ADD COLUMN `github_issue_url` VARCHAR(255) NOT NULL DEFAULT '' AFTER `github_issue_number`,
  ADD COLUMN `github_synced_at` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `github_issue_url`,
  ADD COLUMN `github_sync_attempts` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `github_synced_at`,
  ADD COLUMN `github_sync_error` VARCHAR(500) NOT NULL DEFAULT '' AFTER `github_sync_attempts`;

ALTER TABLE `lg_bug_report`
  ADD UNIQUE KEY `uq_github_issue_number` (`github_issue_number`),
  ADD KEY `idx_github_sync` (`status`, `report_type`, `github_issue_number`, `id`);
