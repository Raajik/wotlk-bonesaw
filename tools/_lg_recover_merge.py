from pathlib import Path

src = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src")
built = (src / "LivingGear.cpp.built").read_text(encoding="utf-8")
cur = (src / "LivingGear.cpp").read_text(encoding="utf-8")
marker = "static void FlushBotAttune"
bi = built.find(marker)
ci = cur.find(marker)
if bi < 0 or ci < 0:
    raise SystemExit(f"marker built={bi} cur={ci}")
merged = built[:bi] + cur[ci:]
(src / "LivingGear.cpp").write_text(merged, encoding="utf-8", newline="\n")
print("merged nl", merged.count("\n"), "bytes", len(merged.encode()))
print("starts", merged.split("\n", 1)[0][:60])
print("craft", "bool IsCraftingSpell(SpellInfo" in merged)
print("include Player", '#include "Player.h"' in merged)
