-- Reports #143/#148/#150: dungeon currencies (Emblem of Triumph and friends)
-- were auto-filed into the reagent vault, where the client currency tab
-- cannot see them. The code guard now keeps class-10 items in bags; this
-- migration hands back everything the vault is holding so nothing is lost.
--
-- lg_vault_reagent named the emblems as "extra reagent ids" (f9aa4449f).
-- They stop being vault material, so their rows there are removed too.
-- Idempotent: the hand-back DELETE only fires while vault rows exist, and
-- the lg_vault_reagent cleanup is a plain DELETE.

-- 1. Empty every vault row for the five emblem entries (all accounts).
DELETE FROM `lg_vault`
WHERE `kind` = 2
  AND `item_entry` IN (40752, 40753, 45624, 47241, 49426);

-- 2. Drop the emblem rows from the extra-reagent table.
DELETE FROM `lg_vault_reagent`
WHERE `item_entry` IN (40752, 40753, 45624, 47241, 49426);
