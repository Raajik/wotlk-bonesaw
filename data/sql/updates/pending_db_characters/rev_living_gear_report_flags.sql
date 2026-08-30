-- Report form flags (2026-08-29).
--
-- .bug/.feature/.crit collapsed into one ".bug" intake with a small UI
-- (report #193 of the tracker redesign): the player now picks bug / feature /
-- other, and ticks Critical and Recurring separately, instead of the kind
-- living in which command they typed. report_type keeps bug/feature and gains
-- 'other'; the two booleans are their own columns so the digest and the
-- GitHub sync can act on them independently of the kind.
--
-- is_critical replaces what '.crit' used to encode as report_type='critical'.
-- is_recurring marks "still not working / keeps happening" -- feedback on a
-- previous fix rather than a brand-new problem, which is how re-reports
-- already get treated downstream.

ALTER TABLE `lg_bug_report`
  ADD COLUMN `is_critical`  TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `report_type`,
  ADD COLUMN `is_recurring` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `is_critical`;

ALTER TABLE `lg_bug_report` ADD KEY `idx_priority` (`is_critical`, `posted`, `id`);
