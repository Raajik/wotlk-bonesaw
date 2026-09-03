#!/usr/bin/env python3
"""Remove duplicate global definitions from LivingGear.cpp after patch replay."""
from __future__ import annotations

import re
import sys
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "modules/mod-living-gear/src/LivingGear.cpp"

GLOBAL_PATTERNS = [
    re.compile(r"^std::unordered_set<uint32> g_autolootOff;\s*$"),
    re.compile(r"^LgConfig g_cfg;\s*$"),
    re.compile(r"^TempSummon\* SummonInvisibleHelper\("),
    re.compile(r"^uint32 ClassTrainerEntry\("),
]


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    seen_globals: set[str] = set()
    seen_const: set[str] = set()
    out: list[str] = []
    skip_until = -1
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.rstrip("\n")

        if i < skip_until:
            i += 1
            continue

        m = re.match(r"^(uint32|float|int32|uint8|char) const (\w+)", stripped)
        if m:
            if m.group(2) in seen_const:
                i += 1
                continue
            seen_const.add(m.group(2))

        if stripped == "std::unordered_set<uint32> g_autolootOff;":
            if "g_autolootOff" in seen_globals:
                i += 1
                continue
            seen_globals.add("g_autolootOff")

        if stripped == "LgConfig g_cfg;":
            if "g_cfg" in seen_globals:
                i += 1
                continue
            seen_globals.add("g_cfg")

        # Skip duplicate helper function blocks
        if stripped.startswith("TempSummon* SummonInvisibleHelper("):
            if "SummonInvisibleHelper" in seen_globals:
                depth = 0
                while i < len(lines):
                    if "{" in lines[i]:
                        depth += lines[i].count("{")
                    if "}" in lines[i]:
                        depth -= lines[i].count("}")
                    i += 1
                    if depth <= 0:
                        break
                continue
            seen_globals.add("SummonInvisibleHelper")

        if stripped.startswith("uint32 ClassTrainerEntry("):
            if "ClassTrainerEntry" in seen_globals:
                depth = 0
                while i < len(lines):
                    if "{" in lines[i]:
                        depth += lines[i].count("{")
                    if "}" in lines[i]:
                        depth -= lines[i].count("}")
                    i += 1
                    if depth <= 0:
                        break
                continue
            seen_globals.add("ClassTrainerEntry")

        out.append(line)
        i += 1

    result = "".join(out)
    TARGET.write_text(result, encoding="utf-8", newline="\n")
    print(f"Dedup globals: {len(lines)} -> {len(result.splitlines())} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
