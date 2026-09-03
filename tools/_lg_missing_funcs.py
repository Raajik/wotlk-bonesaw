from pathlib import Path
import re

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
calls = set(re.findall(r"\b([A-Z][A-Za-z0-9]+)\s*\(", t))
defined = set()
for m in re.finditer(
    r"^(?:static\s+)?(?:void|bool|uint32|uint8|uint16|int32|int|float|std::string|char const\*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    t,
    re.M,
):
    defined.add(m.group(1))
# skip known types/macros
skip = {
    "LOG_ERROR", "LOG_INFO", "LOG_WARN", "LOG_DEBUG", "PSendSysMessage", "StringFormat",
    "Query", "DirectExecute", "GetOption", "ToPlayer", "Acore", "Field", "ItemPosCountVec",
    "ChatHandler", "WorldPacket", "ObjectGuid", "Position", "TempSummon", "GameObjectWorker",
    "VisitObjects", "DealHeal", "CalculatePct", "CountPctFromMaxHealth",
}
missing = sorted(c for c in calls if c not in defined and c not in skip and not c.startswith("PLAYERHOOK") and not c.startswith("ALLMAP") and not c.startswith("WORLDHOOK") and not c.startswith("SERVERHOOK") and not c.startswith("MISCHOOK"))
print("undefined-looking calls", len(missing))
for n in missing:
    if n.startswith(("Save", "Announce", "Show", "Cast", "Apply", "Clear", "Try", "Check", "Patch", "Load", "Login", "Player", "Remove", "Assign", "Boost", "Tick")):
        print(n)
