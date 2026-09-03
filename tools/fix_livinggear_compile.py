#!/usr/bin/env python3
"""Surgical compile fixes for LivingGear.cpp - remove duplicate bodies, add missing constants."""

import re
import sys
from pathlib import Path

SRC = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp")
BACKUP = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp.backup-20260818")
EXPECTED_SHA = "8A949243827DDEE91B50469309FF22AB48CF418FCE43529622CE5D486EBEB475"
MIN_LINES = 13000

CONST_INSERT_AFTER = "uint32 const SPELL_LEVELING_10 = 910062;"
CONST_BLOCK = """\
uint32 const SPELL_LEVELING[] = { 910053, 910054, 910055, 910056, 910057, 910058, 910059, 910060, 910061, 910062 };
uint32 const LEVELING_TIERS = 10;
float const LEVELING_XP_BONUS = 0.10f;
"""

CONST_INSERT_AFTER2 = "uint32 const SPELL_TRAVEL_10 = 910082;"
CONST_BLOCK2 = """\
uint32 const SPELL_TRAVEL[] = { 910073, 910074, 910075, 910076, 910077, 910078, 910079, 910080, 910081, 910082 };
uint32 const TRAVEL_NEED[] = { 1, 2, 3, 4, 5, 10, 25, 50, 100, 250 };
uint32 const TRAVEL_TIERS = 10;
float const TRAVEL_REDUCE = 0.20f;
"""

FIRST_AID_MARKER = "uint32 const FISH_POOL_NEED = 500;"
FIRST_AID_CONST = "uint32 const FIRST_AID_MAX = 450;\n"
COOK_MARKER = "uint32 const SPELL_COOK[] = { 910063, 910064, 910065, 910066, 910067, 910068 };"
COOK_CONST = "uint32 const COOK_REGEN_MS = 1000;\n"


def find_matching_brace(text: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    in_str = False
    in_char = False
    escape = False
    line_comment = False
    block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""

        if block_comment:
            if c == "*" and n == "/":
                block_comment = False
                i += 2
                continue
            i += 1
            continue
        if line_comment:
            if c == "\n":
                line_comment = False
            i += 1
            continue
        if in_str:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == "'":
                in_char = False
            i += 1
            continue

        if c == "/" and n == "/":
            line_comment = True
            i += 2
            continue
        if c == "/" and n == "*":
            block_comment = True
            i += 2
            continue
        if c == '"':
            in_str = True
            i += 1
            continue
        if c == "'":
            in_char = True
            i += 1
            continue

        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unmatched brace")


FUNC_START = re.compile(
    r"^(?:static\s+)?(?:struct\s+(\w+)|"
    r"(void|bool|uint32|int32|float|std::[\w:<>,\s*&]+?|char\s*(?:const\*|\*))\s+"
    r"([\w:]+)\s*\([^;]*\))\s*(?:\{|$)"
)


def func_signature(line: str) -> str | None:
    s = line.strip()
    if s.endswith(";"):
        return None
    m = FUNC_START.match(line)
    if not m:
        return None
    if m.group(1):
        return f"struct {m.group(1)}"
    return f"{m.group(2).strip()} {m.group(3)}".strip()


def remove_duplicate_functions(text: str) -> tuple[str, int]:
    """Remove duplicate top-level function/struct bodies; keep first occurrence."""
    removed = 0
    seen: dict[str, int] = {}
    out = []
    i = 0
    n = len(text)

    while i < n:
        line_start = text.rfind("\n", 0, i) + 1
        line_end = text.find("\n", i)
        if line_end == -1:
            line_end = n
        line = text[line_start:line_end]

        sig = func_signature(line)
        if sig and "{" in line:
            brace = line.index("{")
            abs_brace = line_start + brace
            end_brace = find_matching_brace(text, abs_brace)
            end = end_brace + 1
            while end < n and text[end] in "\r\n":
                end += 1
            chunk = text[line_start:end]
            if sig in seen:
                removed += 1
                i = end
                continue
            seen[sig] = line_start
            out.append(chunk)
            i = end
            continue

        if sig:
            # opening brace on next non-empty line
            j = line_end + 1
            while j < n and text[j] in " \t\r\n":
                j += 1
            if j < n and text[j] == "{":
                end_brace = find_matching_brace(text, j)
                end = end_brace + 1
                while end < n and text[end] in "\r\n":
                    end += 1
                chunk = text[line_start:end]
                if sig in seen:
                    removed += 1
                    i = end
                    continue
                seen[sig] = line_start
                out.append(chunk)
                i = end
                continue

        out.append(text[i:line_end + 1 if line_end < n else n])
        i = line_end + 1 if line_end < n else n

    return "".join(out), removed


def dedupe_forward_decls(text: str) -> str:
    lines = text.splitlines(keepends=True)
    seen_fwd: set[str] = set()
    out = []
    for line in lines:
        s = line.strip()
        if s.endswith(";") and not s.startswith("#") and not s.startswith("//"):
            if re.match(r"^(?:static\s+)?(?:bool|void|uint32|int32|float)\s+\w+\(", s):
                if s in seen_fwd:
                    continue
                seen_fwd.add(s)
        out.append(line)
    return "".join(out)


def fix_send_addon_sync_internals(text: str) -> str:
    """Remove duplicate ALDE/AA/perkCount blocks inside SendAddonSync."""
    # duplicate isBeingLoaded check
    text = text.replace(
        "    if (player->isBeingLoaded() || !player->IsInWorld())\n        return;\n"
        "    if (player->isBeingLoaded() || !player->IsInWorld())\n        return;\n",
        "    if (player->isBeingLoaded() || !player->IsInWorld())\n        return;\n",
        1,
    )

    alde_aa = (
        "    SendAddonLine(player, Acore::StringFormat(\"ALDE|{}\",\n"
        "        g_attunedDe[accountId] ? 1 : 0));\n"
        "    SendAddonLine(player, Acore::StringFormat(\"AA|{}|{}|{}\",\n"
        "        g_autoAttuneOn[accountId] ? 1 : 0,\n"
        "        AttunedCount(accountId),\n"
        "        g_autoAttuneOff[accountId]));\n"
    )
    while text.count(alde_aa) > 1:
        text = text.replace(alde_aa, "", 1)

    cpk = (
        "    uint32 perkCount = 0;\n"
        "    if (uint32 const* list = ClassPerkList(player->getClass(), perkCount))\n"
        "    {\n"
        "        uint32 const selected = GetClassPerk(player);\n"
        "        for (uint32 i = 0; i < perkCount; ++i)\n"
        "            SendAddonLine(player, Acore::StringFormat(\"CPK|{}|{}\",\n"
        "                list[i], list[i] == selected ? 1 : 0));\n"
        "    }\n\n"
    )
    while text.count(cpk) > 1:
        text = text.replace(cpk, "", 1)

    refresh_dup = (
        "    if (player->isBeingLoaded() || (player->GetSession() && player->GetSession()->PlayerLoading()))\n"
        "        return;\n"
    )
    count = text.count(refresh_dup)
    if count > 1:
        text = text.replace(refresh_dup, "", count - 1)

    return text


def insert_after(text: str, marker: str, block: str) -> str:
    if block.strip().split("\n")[0] in text:
        return text
    idx = text.find(marker)
    if idx == -1:
        raise SystemExit(f"marker not found: {marker!r}")
    end = text.find("\n", idx)
    return text[: end + 1] + block + text[end + 1 :]


def main() -> int:
    import hashlib

    text = BACKUP.read_text(encoding="utf-8")
    h = hashlib.sha256(text.encode()).hexdigest().upper()
    if h != EXPECTED_SHA:
        print(f"WARNING: backup SHA256 mismatch: {h}", file=sys.stderr)

    text = insert_after(text, CONST_INSERT_AFTER, CONST_BLOCK)
    text = insert_after(text, CONST_INSERT_AFTER2, CONST_BLOCK2)
    text = insert_after(text, FIRST_AID_MARKER, FIRST_AID_CONST)
    text = insert_after(text, COOK_MARKER, COOK_CONST)
    text = dedupe_forward_decls(text)
    text, n_removed = remove_duplicate_functions(text)
    text = fix_send_addon_sync_internals(text)

    lines = text.count("\n") + (0 if text.endswith("\n") else 1)
    print(f"Removed {n_removed} duplicate function bodies")
    print(f"Output: {lines} lines")

    if lines < MIN_LINES:
        print(f"ERROR: line count {lines} below minimum {MIN_LINES}", file=sys.stderr)
        return 1

    SRC.write_text(text, encoding="utf-8", newline="\n")
    print(f"Wrote {SRC}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
