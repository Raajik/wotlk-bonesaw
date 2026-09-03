-- World-tab ticks are unlocks, not spellbook skills. (0.1.21 gather perks — split from
-- rev_living_gear_track_only.sql because AzerothCore's updater re-hashes rather than
-- re-executes an already-applied filename, so realms on 0.1.20 never got these deletes.)

DELETE FROM `character_spell` WHERE `spell` IN (
    910109, 910110, 910111, 910112, 910113, 910114,
    910115, 910116, 910117, 910118, 910119, 910120,
    910121, 910122, 910123, 910124, 910125, 910126,
    910127, 910128, 910129, 910130, 910131, 910132,
    910133, 910134, 910135, 910136, 910137, 910138
);
