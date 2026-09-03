#!/usr/bin/env python3
"""Aggressive dedup for LivingGear.cpp after patch replay."""
from __future__ import annotations

import re
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "modules/mod-living-gear/src/LivingGear.cpp"


def dedup_includes(lines: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for line in lines:
        if line.startswith("#include"):
            key = line.strip()
            if key in seen:
                continue
            seen.add(key)
        out.append(line)
    return out


def dedup_consecutive(lines: list[str]) -> list[str]:
    out: list[str] = []
    prev: str | None = None
    for line in lines:
        if line == prev:
            continue
        out.append(line)
        prev = line
    return out


def dedup_global_decl(lines: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for line in lines:
        stripped = line.rstrip("\n")
        if re.match(r"^std::(unordered_map|unordered_set|vector|mutex)", stripped) or re.match(
            r"^LgMetaColumns g_metaCols;", stripped
        ):
            if stripped in seen:
                continue
            seen.add(stripped)
        out.append(line)
    return out


def dedup_struct_fields_in_lg_meta(lines: list[str]) -> list[str]:
    """Remove duplicate fields inside struct LgMetaColumns."""
    out: list[str] = []
    in_struct = False
    seen_fields: set[str] = set()
    for line in lines:
        if line.strip().startswith("struct LgMetaColumns"):
            in_struct = True
            seen_fields.clear()
            out.append(line)
            continue
        if in_struct:
            m = re.match(r"\s+bool (\w+) = false;", line)
            if m:
                if m.group(1) in seen_fields:
                    continue
                seen_fields.add(m.group(1))
            if line.strip() == "};":
                in_struct = False
        out.append(line)
    return out


def remove_orphan_struct_tail(lines: list[str]) -> list[str]:
    """Remove orphaned duplicate bool block after globals."""
    out: list[str] = []
    i = 0
    while i < len(lines):
        if (
            i + 4 < len(lines)
            and lines[i].strip() == "bool jumpMode = false;"
            and lines[i + 1].strip() == "bool uiScale = false;"
            and lines[i + 2].strip() == "bool sharedGold = false;"
            and lines[i + 3].strip() == "bool taxiMask = false;"
            and lines[i + 4].strip() == "};"
        ):
            i += 5
            continue
        out.append(lines[i])
        i += 1
    return out


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    before = len(text.splitlines())
    lines = text.splitlines(keepends=True)
    lines = dedup_includes(lines)
    lines = dedup_global_decl(lines)
    lines = dedup_struct_fields_in_lg_meta(lines)
    lines = remove_orphan_struct_tail(lines)
    lines = dedup_consecutive(lines)
    text = "".join(lines)
    after = len(text.splitlines())
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(f"Aggressive dedup: {before} -> {after} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
