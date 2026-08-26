-- Report #78: currency account-wide (Emblems of Triumph/Frost/etc).
--
-- The shared vault (lg_vault) is keyed by account_id, so anything deposited
-- into it is already account-wide -- any character can withdraw it. Emblems
-- are ITEM_CLASS_MISC currency-tab tokens in item_template, which means
-- IsReagentItem's class check (trade goods/gem/reagent) never claimed them
-- and they stayed in the looting character's bags forever.
--
-- Adding them to lg_vault_reagent sends them to the shared vault on loot,
-- the same explicit-list pattern proven by report #67 (ZG bijous/coins,
-- Relic of Ulduar). Consulted AFTER the quest guard, so nothing currently
-- on a quest is ever affected -- emblems are never quest items anyway.
--
-- Withdrawing an emblem from the vault at the emblem vendor works like any
-- other vault reagent.
--
-- Read at startup (LivingGear_Vault.cpp, LoadVaultReagentIds).

DELETE FROM `lg_vault_reagent` WHERE `item_entry` IN (25996, 40752, 40753, 45624, 47241, 49426);
INSERT INTO `lg_vault_reagent` (`item_entry`) VALUES
(25996),  -- Emblem of Perseverance
(40752),  -- Emblem of Heroism
(40753),  -- Emblem of Valor
(45624),  -- Emblem of Conquest
(47241),  -- Emblem of Triumph
(49426);  -- Emblem of Frost
