SELECT ID, Effect_1, EffectMiscValue_1, EffectImplicitTargetA_1, DurationIndex, SummonSpellVisual FROM spell_dbc WHERE ID IN (2894, 2895);
SELECT entry, name, subname, minlevel, maxlevel, rank, spell1, spell2, spell6, VehicleId, AIName, ScriptName FROM creature_template WHERE entry IN (15438, 15439, 15454, 15483, 15455);
SELECT d.ID, d.Duration, d.DurationMax FROM spellduration_dbc d JOIN spell_dbc s ON s.DurationIndex = d.ID WHERE s.ID IN (2894, 2895);
