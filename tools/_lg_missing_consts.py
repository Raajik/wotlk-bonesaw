from pathlib import Path
import re

t = Path(r"A:/wow-bonesaw/modules/mod-living-gear/src/LivingGear.cpp").read_text(encoding="utf-8")
# likely missing CAP/MS/LEVEL consts: ALL_CAPS identifiers used but never defined as const
used = set(re.findall(r"\b([A-Z][A-Z0-9_]{4,})\b", t))
defined = set()
for m in re.finditer(
    r"(?:uint32|uint16|uint8|int32|int|float|bool|auto) const\s+([A-Z][A-Z0-9_]+)",
    t,
):
    defined.add(m.group(1))
# enum members
for m in re.finditer(r"\n\s+([A-Z][A-Z0-9_]+)\s*(?:=|,|})", t):
    defined.add(m.group(1))
missing = sorted(n for n in used if n not in defined and not n.startswith("SPELL_") and n not in {
    "SELECT", "FROM", "WHERE", "INSERT", "UPDATE", "DELETE", "VALUES", "COUNT", "ORDER", "LIMIT",
    "TRUE", "FALSE", "NULL", "REPLACE", "INTO", "AND", "OR", "ON", "SET", "KEY", "IGNORE",
    "EQUIPMENT_SLOT_START", "EQUIPMENT_SLOT_END", "INVENTORY_SLOT_BAG_0", "INVENTORY_SLOT_ITEM_START",
    "INVENTORY_SLOT_ITEM_END", "INVENTORY_SLOT_BAG_START", "INVENTORY_SLOT_BAG_END",
    "MAX_QUEST_LOG_SIZE", "QUEST_STATUS_INCOMPLETE", "EQUIP_ERR_OK", "LANG_ADDON",
    "ITEM_CLASS_RECIPE", "GAMEOBJECT_TYPE_FISHINGHOLE", "TEMPSUMMON_TIMED_DESPAWN",
    "POWER_MANA", "SPEC_MASK_ALL", "NULL_BAG", "NULL_SLOT", "IN_MILLISECONDS",
    "BASE_VALUE", "UNIT_MOD_STAT_START", "UNIT_MOD_ARMOR", "PLAYERHOOK_ON_LOGIN",
})
print("missing sample")
for n in missing:
    if any(x in n for x in ("ATTUNE", "FURY", "KILL_COMBO", "COOK", "TRAVEL", "LEVELING", "JUMP", "LOGIN", "EXTRA")):
        i = t.find(n)
        print(n, "line", t[:i].count("\n")+1, "count", t.count(n))
