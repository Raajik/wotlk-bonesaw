#!/usr/bin/env python3
"""Remove duplicate consecutive declarations and duplicate override methods in LivingGear.cpp."""
from __future__ import annotations

import re
import sys
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "modules/mod-living-gear/src/LivingGear.cpp"


def dedup_const_lines(text: str) -> str:
    seen: set[str] = set()
    out: list[str] = []
    for line in text.splitlines(keepends=True):
        m = re.match(r"^(uint32|float|int32|uint8|char) const (\w+)", line)
        if m:
            key = m.group(2)
            if key in seen:
                continue
            seen.add(key)
        out.append(line)
    return "".join(out)


def dedup_override_blocks(text: str) -> str:
    # Remove duplicate identical override method blocks (simple heuristic)
    pattern = re.compile(
        r"(    void OnPlayerCompleteQuest\(Player\* player, Quest const\* quest\) override\n"
        r"    \{\n"
        r"        AttuneQuestRewards\(player, quest\);\n"
        r"        CheckQuestSpeedPerk\(player\);\n"
        r"    \}\n)",
        re.MULTILINE,
    )
    seen = False

    def repl(m: re.Match[str]) -> str:
        nonlocal seen
        if seen:
            return ""
        seen = True
        return m.group(1)

    return pattern.sub(repl, text)


def dedup_forward_decls(text: str) -> str:
    seen: set[str] = set()
    out: list[str] = []
    for line in text.splitlines(keepends=True):
        m = re.match(r"^void (\w+)\(", line)
        if m and line.rstrip().endswith(";"):
            key = m.group(1)
            if key in seen:
                continue
            seen.add(key)
        out.append(line)
    return "".join(out)


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    before = len(text.splitlines())
    text = dedup_const_lines(text)
    text = dedup_forward_decls(text)
    text = dedup_override_blocks(text)
    after = len(text.splitlines())
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(f"Deduped {before} -> {after} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
