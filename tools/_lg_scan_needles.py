from pathlib import Path

needles = [
    "bool IsCraftingSpell(SpellInfo",
    "float CraftTimeMult(",
    "void CheckCraftPerks(Player* player)\n{",
    "uint32 BagCountWithoutVault",
    "SPELL_LEVELING[]",
    "COOK_REGEN_MS",
    "SelectClassPerk",
    "bool TrackingResourceBit",
    "void EnableGatherTracking",
    "bool IsAccountRepFaction",
    "void SendPendingRepUpdates",
    "g_syncing",
    "LoginSettled",
    "HasProtDevotion",
]
files = [
    Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp"),
    Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp.bak-build"),
    Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp.built"),
]
for p in files:
    t = p.read_text(encoding="utf-8", errors="replace")
    print("====", p.name, "nl", t.count("\n"))
    for n in needles:
        print(f"  {n!r:40} {t.count(n)}")
