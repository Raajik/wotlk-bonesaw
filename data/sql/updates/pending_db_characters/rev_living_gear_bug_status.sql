-- Bug report resolution tracking (2026-08-22).
--
-- Reports were write-once: they arrived, went to Discord, and then there was
-- no point in the process where one got closed. With twenty-odd open it stops
-- being obvious which are done, which were attempted, and which nobody has
-- looked at.
--
-- `status` is the state, `resolution` says what was done about it, and
-- `discord_message_id` is what lets tools/bug-reports/bug_resolve.py go back
-- and strike the original message through rather than posting a second one
-- nobody will connect to the first.
--
-- Status values, kept as a plain string rather than an enum so adding one
-- later does not need a migration:
--   open      - nobody has dealt with it yet (default)
--   fixed     - believed fixed, shipped or pending ship
--   attempted - changed something, not confident it is fixed. #15 solid chests
--               is the honest example: instrumented, not solved.
--   wontfix   - deliberately not doing it, with a reason in `resolution`
--   duplicate - already covered by another report

ALTER TABLE `lg_bug_report`
  ADD COLUMN `status` VARCHAR(16) NOT NULL DEFAULT 'open' AFTER `posted`,
  ADD COLUMN `resolution` TEXT NULL AFTER `status`,
  ADD COLUMN `resolved_at` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `resolution`,
  ADD COLUMN `discord_message_id` VARCHAR(32) NOT NULL DEFAULT '' AFTER `resolved_at`;

ALTER TABLE `lg_bug_report` ADD KEY `idx_status` (`status`, `id`);

-- Everything already in the table predates this and is genuinely still open,
-- so the default is correct for the backfill and nothing else is needed.
