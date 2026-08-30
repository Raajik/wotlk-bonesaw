-- Report #182: "currencies still not syncing account-wide [Stone Keeper's
-- Shard] and emblems of frost/triumph/etc". Design (user-approved): a shared
-- account currency pool -- honor-like, one balance per (account, currency
-- item) across every character. Currency-token items looted or rewarded are
-- deposited into this pool instead of the character's currency tab, and
-- vendor purchases that cost those tokens pay from the pool first.
--
-- Only currency TOKENS (BagFamily currency tokens: shards, emblems, marks)
-- pool here. Gold, honor, and arena points were already pooled account-wide
-- by the SharedCurrencies system in LivingGear_Next.cpp and are untouched.
--
-- Idempotent: CREATE TABLE IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS `lg_account_currency` (
  `account_id` INT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `count` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `item_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
