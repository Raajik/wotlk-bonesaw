#!/usr/bin/env python3
"""One-pass LivingGear.cpp compile fix from the 20260818 backup."""

from __future__ import annotations

import hashlib
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(r"A:\wow-bonesaw")
BACKUP = ROOT / r"modules\mod-living-gear\src\LivingGear.cpp.backup-20260818"
SRC = ROOT / r"modules\mod-living-gear\src\LivingGear.cpp"
EXPECTED_SHA = "8A949243827DDEE91B50469309FF22AB48CF418FCE43529622CE5D486EBEB475"

spec = importlib.util.spec_from_file_location(
    "fix_livinggear_compile", ROOT / r"tools\fix_livinggear_compile.py"
)
fix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fix)

CFG_FIELDS = """\
    bool collectionPassiveEnabled = true;
    uint32 collectionPassiveTickMs = 10000;
    uint8 collectionPassiveMinGap = 5;
    uint32 collectionPassiveXp = 1;
    bool dungeonRareAlwaysSpawn = false;
"""

CFG_LOAD = """\
        collectionPassiveEnabled = sConfigMgr->GetOption<bool>("LivingGear.CollectionPassive.Enable", true);
        collectionPassiveTickMs = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.TickMs", 10000);
        if (collectionPassiveTickMs < 1000)
            collectionPassiveTickMs = 1000;
        collectionPassiveMinGap = static_cast<uint8>(sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.MinGap", 5));
        collectionPassiveXp = sConfigMgr->GetOption<uint32>("LivingGear.CollectionPassive.Xp", 1);
        if (collectionPassiveXp < 1)
            collectionPassiveXp = 1;
        dungeonRareAlwaysSpawn = sConfigMgr->GetOption<bool>("LivingGear.DungeonRare.AlwaysSpawn", false);
"""

CONST_BLOCK = """\
uint32 const SPELL_LEVELING[] = { 910053, 910054, 910055, 910056, 910057, 910058, 910059, 910060, 910061, 910062 };
uint32 const LEVELING_TIERS = 10;
float const LEVELING_XP_BONUS = 0.10f;
uint32 const COOK_REGEN_MS = 1000;
uint32 const FIRST_AID_MAX = 450;
uint32 const FISH_POOL_NEED = 500;
uint32 const AUTO_QUEST_NEED = 100;
uint32 const ACHIEVEMENT_500_FISH = 1560;
uint32 const COLLECTION_PASSIVE_COUNT = 3;
uint32 const LOGIN_SETTLE_MS = 2000;
"""

TRAVEL_BLOCK = """\
uint32 const SPELL_TRAVEL[] = { 910073, 910074, 910075, 910076, 910077, 910078, 910079, 910080, 910081, 910082 };
uint32 const TRAVEL_NEED[] = { 1, 2, 3, 4, 5, 10, 25, 50, 100, 250 };
uint32 const TRAVEL_TIERS = 10;
float const TRAVEL_REDUCE = 0.20f;
"""

GLOBALS = """\
std::unordered_set<uint32> g_loginReady;
std::unordered_map<uint32, uint32> g_loginMs;
std::unordered_map<uint32, uint32> g_collectionPassiveMs;
"""

LOGIN_SETTLED = """\
bool LoginSettled(Player* player)
{
    if (!player)
        return false;
    uint32 const guid = player->GetGUID().GetCounter();
    if (!g_loginReady.count(guid))
        return false;
    auto const it = g_loginMs.find(guid);
    if (it == g_loginMs.end())
        return true;
    return GetMSTimeDiffToNow(it->second) >= LOGIN_SETTLE_MS;
}

"""


def insert_after_line(text: str, marker: str, block: str) -> str:
    if block.strip().split("\n")[0] in text and marker not in block:
        first = block.strip().split("\n")[0]
        if first in text:
            return text
    idx = text.find(marker)
    if idx == -1:
        raise SystemExit(f"marker not found: {marker!r}")
    end = text.find("\n", idx)
    return text[: end + 1] + block + text[end + 1 :]


def insert_before(text: str, marker: str, block: str) -> str:
    needle = block.strip().split("\n")[0]
    if needle in text:
        return text
    idx = text.find(marker)
    if idx == -1:
        raise SystemExit(f"before-marker not found: {marker!r}")
    return text[:idx] + block + text[idx:]


INTERRUPTED_STUB = re.compile(
    r"^static void SendAddonSync\(Player\* player, bool includeBags = true\)\n"
    r"\{\n"
    r"    if \(!player \|\| !g_cfg\.enabled \|\| !player->GetSession\(\)\)\n"
    r"        return;\n"
    r"    if \(player->isBeingLoaded\(\) \|\| !player->IsInWorld\(\)\)\n"
    r"        return;\n"
    r"    if \(g_syncing\)\n"
    r"        return;\n"
    r"    AddonSyncGuard guard;\n"
    r"\n"
    r"(?=static void |struct )",
    re.M,
)


def strip_interrupted_stubs(text: str) -> str:
    prev = None
    while prev != text:
        prev = text
        text = INTERRUPTED_STUB.sub("", text)
    return text


def widen_func_start() -> None:
    fix.FUNC_START = re.compile(
        r"^(?:static\s+)?"
        r"(?:struct\s+(\w+)|"
        r"(void|bool|uint32|uint16|uint8|int32|int|float|std::[\w:<>,\s*&]+?|char\s*(?:const\*|\*))\s+"
        r"([\w:]+)\s*\([^;]*\))\s*(?:\{|$)"
    )


def main() -> int:
    raw = BACKUP.read_bytes()
    sha = hashlib.sha256(raw).hexdigest().upper()
    if sha != EXPECTED_SHA:
        print(f"ERROR: backup SHA {sha} != {EXPECTED_SHA}", file=sys.stderr)
        return 1
    text = raw.decode("utf-8")
    if text.count("\n") != 14303:
        print(f"ERROR: backup lines {text.count(chr(10))}", file=sys.stderr)
        return 1

    if "collectionPassiveMinGap" not in text.split("void Load()")[0]:
        text = insert_after_line(text, "    bool zoneScaleNotify = true;", CFG_FIELDS)
    if "LivingGear.CollectionPassive.Enable" not in text:
        text = insert_after_line(text, '        zoneScaleNotify = sConfigMgr->GetOption<bool>("LivingGear.ZoneScale.Notify", true);', CFG_LOAD)

    text = insert_after_line(text, "uint32 const SPELL_LEVELING_10 = 910062;", CONST_BLOCK)
    text = insert_after_line(text, "uint32 const SPELL_TRAVEL_10 = 910082;", TRAVEL_BLOCK)
    text = insert_after_line(text, "std::unordered_map<uint32, uint32> g_lastAidCleanse;", GLOBALS)

    if "bool LoginSettled(" not in text:
        text = insert_before(text, "void PushSharedCurrencies(Player* source)", LOGIN_SETTLED)

    if "bool LoginSettled(Player* player);" not in text:
        text = insert_after_line(text, "void CheckCollectionPassivePerk(Player* player, bool scanAccount = false);", "bool LoginSettled(Player* player);\n")

    text = strip_interrupted_stubs(text)
    text = fix.dedupe_forward_decls(text)
    widen_func_start()
    text, n_removed = fix.remove_duplicate_functions(text)
    text = fix.fix_send_addon_sync_internals(text)

    if "void AddSC_LivingGear()" not in text:
        print("ERROR: AddSC_LivingGear missing after edit", file=sys.stderr)
        return 1
    if "} // namespace LivingGear" not in text:
        print("ERROR: namespace close missing", file=sys.stderr)
        return 1

    lines = text.count("\n") + (0 if text.endswith("\n") else 1)
    print(f"Removed {n_removed} duplicate function bodies")
    print(f"Output lines: {lines}")
    SRC.write_text(text, encoding="utf-8", newline="\n")
    out_sha = hashlib.sha256(SRC.read_bytes()).hexdigest()[:8].upper()
    print(f"Wrote {SRC} sha {out_sha}")
    if lines < 8000:
        print("ERROR: output suspiciously short", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
