-- Report #111: attunement banked only the six primary columns, so every
-- secondary stat an item carried -- spell power above all -- was dropped the
-- moment the item was spent. A Truesilver Healing Ring attuned for its int/spi
-- and lost exactly the stat it existed for. lg_absorb now carries the item's
-- secondary stats next to its primaries, and the apply path grants them with
-- the same attune_pct scaling, through the same ApplySecondary path.
--
-- Defense, dodge, parry and block rating were dropped even by the ITEM
-- LEVELLING pipeline (ReadBaseStats never matched them), so this also makes a
-- defense-rating item gain its defense as it levels.
--
-- Weapon damage (the LG_SEC_DMG_MIN/DMG_MAX pair) stays OUT of the bank on
-- purpose: damage is a property of the held weapon, not an account stat, and
-- banking it would make the main-hand slot permanently solved by any attuned
-- weapon. Everything else banks.
--
-- Existing rows keep zero here: attunement destroys the item at spend time,
-- so the secondaries of already-spent items are gone and can only be re-earned
-- by attuning another copy of the same entry. Copies are common and cheap, and
-- the login-time under-bank repair only ever raises rows, never rewrites a
-- full one.

ALTER TABLE `lg_absorb`
  ADD COLUMN `sec_crit` FLOAT NOT NULL DEFAULT 0 AFTER `armor`,
  ADD COLUMN `sec_hit` FLOAT NOT NULL DEFAULT 0 AFTER `sec_crit`,
  ADD COLUMN `sec_haste` FLOAT NOT NULL DEFAULT 0 AFTER `sec_hit`,
  ADD COLUMN `sec_expertise` FLOAT NOT NULL DEFAULT 0 AFTER `sec_haste`,
  ADD COLUMN `sec_armor_pen` FLOAT NOT NULL DEFAULT 0 AFTER `sec_expertise`,
  ADD COLUMN `sec_resilience` FLOAT NOT NULL DEFAULT 0 AFTER `sec_armor_pen`,
  ADD COLUMN `sec_attack_power` FLOAT NOT NULL DEFAULT 0 AFTER `sec_resilience`,
  ADD COLUMN `sec_spell_power` FLOAT NOT NULL DEFAULT 0 AFTER `sec_attack_power`,
  ADD COLUMN `sec_defense` FLOAT NOT NULL DEFAULT 0 AFTER `sec_spell_power`,
  ADD COLUMN `sec_dodge` FLOAT NOT NULL DEFAULT 0 AFTER `sec_defense`,
  ADD COLUMN `sec_parry` FLOAT NOT NULL DEFAULT 0 AFTER `sec_dodge`,
  ADD COLUMN `sec_block` FLOAT NOT NULL DEFAULT 0 AFTER `sec_parry`,
  ADD COLUMN `sec_mp5` FLOAT NOT NULL DEFAULT 0 AFTER `sec_block`,
  ADD COLUMN `sec_health_regen` FLOAT NOT NULL DEFAULT 0 AFTER `sec_mp5`,
  ADD COLUMN `sec_spell_pen` FLOAT NOT NULL DEFAULT 0 AFTER `sec_health_regen`,
  ADD COLUMN `sec_block_value` FLOAT NOT NULL DEFAULT 0 AFTER `sec_spell_pen`;
