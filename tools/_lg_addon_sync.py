from pathlib import Path

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
for n in [
    "static void SendLivingItem",
    "static void SendBagLivingItems",
    "struct AddonSyncGuard",
    "static bool HandleTipRequest",
    "void TryUnlockAndLootItem",
    "static void SendAddonLine",
]:
    i = t.find(n)
    print(n, "MISSING" if i < 0 else f"line {t[:i].count(chr(10))+1}")
