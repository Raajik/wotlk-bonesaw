#!/usr/bin/env python3
"""Remove duplicate LoadClassPerk/GetClassPerk/ApplyClassPerkSpells blocks."""
from __future__ import annotations

import re
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "modules/mod-living-gear/src/LivingGear.cpp"

BLOCK_START = re.compile(r"^void LoadClassPerk\(uint32 guid\)\s*$", re.MULTILINE)


def find_block_end(text: str, start: int) -> int:
    """End after ApplyClassPerkSpells function closes."""
    pos = start
    depth = 0
    started = False
    while pos < len(text):
        if text[pos] == "{":
            depth += 1
            started = True
        elif text[pos] == "}":
            depth -= 1
            if started and depth == 0:
                # skip to newline after closing brace of ApplyClassPerkSpells
                nl = text.find("\n", pos)
                return nl + 1 if nl != -1 else pos + 1
        pos += 1
    return len(text)


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    before = len(text.splitlines())
    matches = list(BLOCK_START.finditer(text))
    if len(matches) <= 1:
        print(f"LoadClassPerk blocks: {len(matches)}")
        return 0
    # Keep first block; remove subsequent ones (each block is 3 functions)
    remove_ranges = []
    for m in matches[1:]:
        start = m.start()
        # walk back to preceding blank line if any
        while start > 0 and text[start - 1] in "\n":
            start -= 1
        end = start
        # find ApplyClassPerkSpells after this LoadClassPerk
        idx = text.find("void ApplyClassPerkSpells", m.start())
        if idx == -1:
            continue
        end = find_block_end(text, text.find("{", idx))
        remove_ranges.append((start, end))
    for start, end in reversed(remove_ranges):
        text = text[:start] + text[end:]
    after = len(text.splitlines())
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(f"Removed {len(remove_ranges)} duplicate class-perk blocks: {before} -> {after}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
