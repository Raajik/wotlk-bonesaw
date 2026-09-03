#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path

TARGET = Path(r"A:\wow-bonesaw\modules\mod-living-gear\src\LivingGear.cpp")

DEF_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<sig>"
    r"(?:static\s+)?(?:class|struct|enum)\s+(?P<tname>\w+)(?:\s*:[^{]+)?\s*"
    r"|"
    r"(?:static\s+)?(?:void|bool|int32|uint32|float|uint8|int|char const\*|std::string)\s+(?P<fname>\w+)\s*\([^\n;]*\)\s*"
    r")(?:\{)?\s*$",
    re.MULTILINE,
)
ARRAY_RE = re.compile(
    r"^(?P<indent>[ \t]*)(?P<sig>(?:static\s+)?(?:\w+\s+)+const\s+(?P<aname>\w+)\s*\[\s*\]\s*=\s*\{)\s*$",
    re.MULTILINE,
)


def brace_end(text: str, open_brace: int) -> int:
    depth = 0
    pos = open_brace
    n = len(text)
    while pos < n:
        ch = text[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                nl = text.find("\n", pos)
                return nl + 1 if nl != -1 else pos + 1
        pos += 1
    return n


def collect(text: str) -> list[tuple[str, str, str, int, int]]:
    blocks: list[tuple[str, str, str, int, int]] = []
    for m in DEF_RE.finditer(text):
        if m.group("indent"):
            continue
        open_brace = text.find("{", m.start())
        if open_brace == -1 or open_brace > m.end() + 8:
            i = m.end()
            while i < len(text) and text[i] in " \t\r\n":
                i += 1
            if i >= len(text) or text[i] != "{":
                continue
            open_brace = i
        end = brace_end(text, open_brace)
        # Skip tiny forward-looking struct tags without members if needed.
        name = m.group("tname") or m.group("fname")
        if not name:
            continue
        sig = re.sub(r"\s+", " ", m.group("sig")).strip()
        kind = "type" if m.group("tname") else "func"
        blocks.append((kind, name, sig, m.start(), end))
    for m in ARRAY_RE.finditer(text):
        if m.group("indent"):
            continue
        open_brace = text.find("{", m.start())
        if open_brace == -1:
            continue
        end = brace_end(text, open_brace)
        name = m.group("aname")
        blocks.append(("array", name, name, m.start(), end))
    blocks.sort(key=lambda x: x[3])
    return blocks


def dedupe(text: str) -> str:
    blocks = collect(text)
    keep: dict[str, tuple[str, int, int]] = {}
    remove: list[tuple[int, int]] = []
    for kind, name, sig, start, end in blocks:
        key = sig if kind == "func" else name
        body = text[start:end]
        if key not in keep:
            keep[key] = (body, start, end)
            continue
        old_body, old_start, old_end = keep[key]
        if len(body) > len(old_body):
            remove.append((old_start, old_end))
            keep[key] = (body, start, end)
        else:
            remove.append((start, end))
    remove.sort(key=lambda x: x[0], reverse=True)
    for start, end in remove:
        text = text[:start] + text[end:]
    return text


def collapse_blank(text: str) -> str:
    text = re.sub(r"\n{3,}", "\n\n", text)
    text = re.sub(
        r"^(bool AccountHasPerk\(uint32 accountId, uint32 spellId\);\n"
        r"static bool IsRandomAiBot\(Player\* player\);\n)+",
        "bool AccountHasPerk(uint32 accountId, uint32 spellId);\n"
        "static bool IsRandomAiBot(Player* player);\n",
        text,
        flags=re.MULTILINE,
    )
    text = re.sub(
        r"^(static bool SacrificeItem\(Player\* player, Item\* item, ChatHandler\* handler\);\n)+",
        "static bool SacrificeItem(Player* player, Item* item, ChatHandler* handler);\n",
        text,
        flags=re.MULTILINE,
    )
    text = re.sub(
        r"^(static bool IsEligible\(ItemTemplate const\* proto\);\n)+",
        "static bool IsEligible(ItemTemplate const* proto);\n",
        text,
        flags=re.MULTILINE,
    )
    return text


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    before = len(text.splitlines())
    text = dedupe(text)
    text = collapse_blank(text)
    if not text.endswith("\n"):
        text += "\n"
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    after = len(text.splitlines())
    print(f"Surgical dedupe: {before} -> {after} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
