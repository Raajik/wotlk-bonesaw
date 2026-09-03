#!/usr/bin/env python3
"""Remove orphaned SummonInvisibleHelper / ClassTrainerEntry bodies in LivingGear.cpp."""
from __future__ import annotations

import re
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "modules/mod-living-gear/src/LivingGear.cpp"

SUMMON_BODY = re.compile(
    r"\{\s*\n"
    r"\s*if \(!player\)\s*\n"
    r"\s*return nullptr;\s*\n"
    r"\s*TempSummon\* helper = player->SummonCreature\(entry,",
    re.MULTILINE,
)

CLASS_BODY = re.compile(
    r"\{\s*\n"
    r"\s*switch \(cls\)\s*\n"
    r"\s*\{\s*\n"
    r"\s*case CLASS_WARRIOR:\s+return 26332;",
    re.MULTILINE,
)


def strip_orphan_blocks(text: str) -> str:
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        # Orphan block: bare { not after function signature
        if line.strip() == "{" and (not out or not re.search(r"\)\s*$", out[-1].rstrip())):
            chunk = "".join(lines[i : i + 20])
            if SUMMON_BODY.search(chunk) or CLASS_BODY.search(chunk):
                depth = 0
                while i < len(lines):
                    depth += lines[i].count("{") - lines[i].count("}")
                    i += 1
                    if depth <= 0:
                        break
                continue
        out.append(line)
        i += 1
    return "".join(out)


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    before = len(text.splitlines())
    text = strip_orphan_blocks(text)
    after = len(text.splitlines())
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(f"Removed orphan bodies: {before} -> {after} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
