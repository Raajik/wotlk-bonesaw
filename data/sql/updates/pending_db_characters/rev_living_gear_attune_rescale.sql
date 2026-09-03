-- Bring existing attunement rows onto the new rate (2026-08-23).
--
-- 0.1.68 migrated every existing row to attune_pct = 100 so nobody lost stats.
-- That instinct was right about not silently nerfing people and wrong about
-- everything after: the milestone update only ever RAISES a row, so 96 accounts
-- ended up permanently above any achievable rate. Milestones could never move
-- them, and anything they attuned afterwards arrived at 5-10% next to a 100%
-- row, which reads as a downgrade.
--
-- The grandfathering made the entire progression inert for exactly the people
-- who had been playing longest.
--
-- So every row is set to its account's real rate: 5, plus 5 per milestone
-- earned, capped at 100. This visibly reduces banked stats for existing
-- accounts -- for account 108 that is 909 items dropping from 100% to 10% --
-- and that is accepted deliberately, because only two real players are
-- affected and the alternative is a system that does nothing for them ever
-- again.
--
-- After this the invariant holds: every row equals its account's current rate,
-- and CheckAttuneMilestones keeps it there by raising rows below it.

UPDATE `lg_absorb` a
SET a.`attune_pct` = LEAST(100,
    5 + (SELECT COUNT(*) FROM `lg_attune_milestone` m WHERE m.`account_id` = a.`account_id`) * 5);
