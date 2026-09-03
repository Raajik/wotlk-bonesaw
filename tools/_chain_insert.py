import struct

# Parse the shipped Spell.dbc and emit the full spell_dbc INSERT for ranks
# 49238/49239/49240 with EffectChainTargets_2 = 3.
data = open(r"C:\Users\jeremy\AppData\Local\Temp\Spell.dbc", 'rb').read()
magic, recCount, fieldCount, recSize, strSize = struct.unpack_from("<4s4I", data, 0)
recs = data[20:20 + recCount*recSize]
def u(rec, w): return struct.unpack_from("<I", rec, w*4)[0]
wanted = {49238, 49239, 49240}
rows = {}
for i in range(recCount):
    rec = recs[i*recSize:(i+1)*recSize]
    sid = u(rec, 0)
    if sid in wanted:
        rows[sid] = [u(rec, w) for w in range(fieldCount)]

# spell_dbc column list from DESCRIBE (order matters; the SQL table is the
# DBC fields in order, with the ID column first and string fields last).
cols = "ID Category DispelType Mechanic Attributes AttributesEx AttributesEx2 AttributesEx3 AttributesEx4 AttributesEx5 AttributesEx6 AttributesEx7 ShapeshiftMask unk_320_2 ShapeshiftExclude unk_320_3 Targets TargetCreatureType RequiresSpellFocus FacingCasterFlags CasterAuraState TargetAuraState ExcludeCasterAuraState ExcludeTargetAuraState CasterAuraSpell TargetAuraSpell ExcludeCasterAuraSpell ExcludeTargetAuraSpell CastingTimeIndex RecoveryTime CategoryRecoveryTime InterruptFlags AuraInterruptFlags ChannelInterruptFlags ProcTypeMask ProcChance ProcCharges MaxLevel BaseLevel SpellLevel DurationIndex PowerType ManaCost ManaCostPerLevel ManaPerSecond ManaPerSecondPerLevel RangeIndex Speed ModalNextSpell CumulativeAura Totem_1 Totem_2 Reagent_1 Reagent_2 Reagent_3 Reagent_4 Reagent_5 Reagent_6 Reagent_7 Reagent_8 ReagentCount_1 ReagentCount_2 ReagentCount_3 ReagentCount_4 ReagentCount_5 ReagentCount_6 ReagentCount_7 ReagentCount_8 EquippedItemClass EquippedItemSubclass EquippedItemInvTypes Effect_1 Effect_2 Effect_3 EffectDieSides_1 EffectDieSides_2 EffectDieSides_3 EffectRealPointsPerLevel_1 EffectRealPointsPerLevel_2 EffectRealPointsPerLevel_3 EffectBasePoints_1 EffectBasePoints_2 EffectBasePoints_3 EffectMechanic_1 EffectMechanic_2 EffectMechanic_3 ImplicitTargetA_1 ImplicitTargetA_2 ImplicitTargetA_3 ImplicitTargetB_1 ImplicitTargetB_2 ImplicitTargetB_3 EffectRadiusIndex_1 EffectRadiusIndex_2 EffectRadiusIndex_3 EffectAura_1 EffectAura_2 EffectAura_3 EffectAuraPeriod_1 EffectAuraPeriod_2 EffectAuraPeriod_3 EffectMultipleValue_1 EffectMultipleValue_2 EffectMultipleValue_3 EffectChainTargets_1 EffectChainTargets_2 EffectChainTargets_3 EffectItemType_1 EffectItemType_2 EffectItemType_3 EffectMiscValue_1 EffectMiscValue_2 EffectMiscValue_3 EffectMiscValueB_1 EffectMiscValueB_2 EffectMiscValueB_3 EffectTriggerSpell_1 EffectTriggerSpell_2 EffectTriggerSpell_3 EffectPointsPerCombo_1 EffectPointsPerCombo_2 EffectPointsPerCombo_3 EffectSpellClassMaskA_1 EffectSpellClassMaskA_2 EffectSpellClassMaskA_3 EffectSpellClassMaskB_1 EffectSpellClassMaskB_2 EffectSpellClassMaskB_3 EffectSpellClassMaskC_1 EffectSpellClassMaskC_2 EffectSpellClassMaskC_3".split()

out = []
for sid in (49238, 49239, 49240):
    row = rows[sid]
    vals = list(row[:len(cols)])
    # EffectChainTargets is DBC 1-based fields 104-106 -> cols 103-105;
    # in the spell_dbc column order, find its index:
    idx = cols.index("EffectChainTargets_2")
    vals[idx] = 3
    out.append("({})".format(",".join(str(v) for v in vals)))

sql_cols = ", ".join(f"`{c}`" for c in cols)
body = "INSERT INTO `spell_dbc` ({}) VALUES\n{};".format(sql_cols, ",\n".join(out))
open(r"C:\Users\jeremy\AppData\Local\Temp\chain_insert.sql", "w").write(body)
print("wrote insert,", len(body), "chars,", len(cols), "columns")
