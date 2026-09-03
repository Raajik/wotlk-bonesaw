-- Report #112: ThrowExtraAvengers (LivingGear_Next.cpp) passed
-- getMSTime() + cooldown into Player::AddSpellCooldown, whose third parameter
-- is a DURATION (Player::_AddSpellCooldown stores GameTime::GetGameTimeMS() +
-- end_time), so every hand-cast Avenger's Shield left a cooldown row that
-- outlived the cast by roughly the server's whole uptime -- Scrug's row sat
-- at 3h46m remaining when he filed the report. The code bug is fixed
-- separately; these rows persist across restarts, so delete them.

DELETE FROM `character_spell_cooldown` WHERE `spell` IN (31935, 48826, 48827);

-- Reports #108/#109/#110: the research cooldowns are being removed outright
-- via spell_cooldown_overrides (pending_db_world). Rows already persisted here
-- keep the old clocks running until their stored expiry (one has 2.7 days
-- left on the alchemy research), so clear them too and the removal is visible
-- on next login.

DELETE FROM `character_spell_cooldown` WHERE `spell` IN (60893, 61177, 61288);
