#!/usr/bin/env python3
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "modules/mod-living-gear/src/LivingGear.cpp"
block = """    std::unordered_set<uint32>& attuned = g_attuned[accountId];
    attuned.clear();
    if (QueryResult ar = CharacterDatabase.Query(
        "SELECT `item_entry` FROM `lg_absorb` WHERE `account_id` = {}", accountId))
    {
        do
            attuned.insert((*ar)[0].Get<uint32>());
        while (ar->NextRow());
    }
"""

def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    count = text.count(block)
    if count <= 1:
        print(f"attuned blocks: {count} (nothing to do)")
        return 0
    text = text.replace(block, block, 1)
    text = text.replace(block, "")
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(f"removed {count - 1} attuned dup blocks")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
