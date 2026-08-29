-- Report #128/#147 family, Swayss (guid 1055): Living Bomb rank 3 (55360)
-- sat in character_spell with specMask=1 while every other learned spell --
-- and the other two LB ranks -- carry specMask=0. specMask=0 means "active
-- in every spec"; specMask=1 means "active only in spec slot 1", so
-- Player::HasSpell(55360) returned false for this mage outside spec 1 and
-- BestOwned() (auto-apply AND the spread tick both use it) capped at rank 2.
-- Clearing the mask makes the rank the mage learned the rank the perk casts.
--
-- Scoped to the exact (guid, spell) row that was wrong; idempotent by PK.

UPDATE `character_spell` SET
  `specMask` = 0
WHERE `guid` = 1055
  AND `spell` = 55360
  AND `specMask` <> 0;
