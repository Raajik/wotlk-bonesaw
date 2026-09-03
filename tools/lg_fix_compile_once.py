#!/usr/bin/env python3
"""One-shot surgical LivingGear.cpp compile fix. Not a maintained repo tool."""
from __future__ import annotations

import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BACKUP = ROOT / "archive/living-gear-backups/LivingGear.cpp.20260818-2219"
TARGET = ROOT / "modules/mod-living-gear/src/LivingGear.cpp"

FUNC_SIG = re.compile(
    r"^((?:static\s+)?(?:void|bool|int32|uint32|float|uint8|LgStats|std::string)\s+(\w+)\s*\([^)]*\))\s*$",
    re.MULTILINE,
)
STRUCT_SIG = re.compile(r"^(struct\s+(\w+)\s*)\{\s*$", re.MULTILINE)


def brace_end(text: str, open_brace: int) -> int:
    depth = 0
    pos = open_brace
    while pos < len(text):
        ch = text[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                nl = text.find("\n", pos)
                return nl + 1 if nl != -1 else pos + 1
        pos += 1
    return len(text)


def func_block(text: str, sig_start: int, sig_end: int, max_lines: int = 400) -> tuple[int, int] | None:
    i = sig_end
    while i < len(text) and text[i] in " \t\r\n":
        i += 1
    if i >= len(text) or text[i] != "{":
        return None
    end = brace_end(text, i)
    if text[sig_start:end].count("\n") > max_lines:
        return None
    return sig_start, end


def collect_blocks(text: str) -> list[tuple[str, str, int, int]]:
    blocks: list[tuple[str, str, int, int]] = []
    for m in FUNC_SIG.finditer(text):
        span = func_block(text, m.start(), m.end())
        if span:
            blocks.append(("func", m.group(2), span[0], span[1]))
    for m in STRUCT_SIG.finditer(text):
        span = func_block(text, m.start(), m.end(), max_lines=20)
        if span:
            blocks.append(("struct", m.group(2), span[0], span[1]))
    blocks.sort(key=lambda x: x[2])
    return blocks


def fix_addon_sync_splice(text: str) -> str:
    notify_m = re.search(
        r"(static void NotifyVaultChange\(Player\* player, uint8 kind, uint32 itemEntry\)\s*\{.*?\n\})",
        text,
        re.DOTALL,
    )
    notify_body = notify_m.group(1) if notify_m else None

    living_idx = text.find("\nstatic void SendLivingItem(")
    if living_idx == -1:
        return text

    send_line = text.find("\nstatic void SendAddonLine(")
    guard_idx = text.find("struct AddonSyncGuard", send_line if send_line != -1 else 0)
    if guard_idx == -1 or guard_idx > living_idx:
        guard_idx = text.rfind("struct AddonSyncGuard", 0, living_idx)
    if guard_idx == -1:
        return text
    open_brace = text.find("{", guard_idx)
    guard_end = brace_end(text, open_brace)
    while guard_end < len(text) and text[guard_end] in "\r\n":
        guard_end += 1

    text = text[:guard_end] + "\n\n" + text[living_idx + 1 :]

    if notify_body and notify_body not in text:
        anchor = text.find('SendAddonLine(player, "END");')
        if anchor != -1:
            close = text.find("\n}", anchor)
            if close != -1:
                insert_at = close + 2
                text = text[:insert_at] + "\n\n" + notify_body + text[insert_at:]

    return text


def dedupe_blocks(text: str) -> str:
    blocks = collect_blocks(text)
    remove: list[tuple[int, int]] = []
    seen_func: dict[str, str] = {}
    seen_struct: set[str] = set()

    for kind, name, start, end in blocks:
        body = text[start:end]
        if kind == "struct":
            if name in seen_struct:
                remove.append((start, end))
            else:
                seen_struct.add(name)
            continue
        if name == "SendAddonSync" and 'SendAddonLine(player, "END")' not in body:
            remove.append((start, end))
            continue

        if name in seen_func:
            if body == seen_func[name]:
                remove.append((start, end))
        else:
            seen_func[name] = body

    remove.sort(key=lambda x: x[0], reverse=True)
    for start, end in remove:
        text = text[:start] + text[end:]
    text = text.replace("}lass ", "}\n\nclass ")
    return text


def clean_send_addon_sync(text: str) -> str:
    start = text.find("static void SendAddonSync(Player* player, bool includeBags = true)")
    if start == -1:
        return text
    open_brace = text.find("{", start)
    end = brace_end(text, open_brace)
    body = text[start:end]

    body = body.replace(
        "    if (player->isBeingLoaded() || !player->IsInWorld())\n"
        "        return;\n"
        "    if (player->isBeingLoaded() || !player->IsInWorld())\n"
        "        return;\n",
        "    if (player->isBeingLoaded() || !player->IsInWorld())\n"
        "        return;\n",
    )

    perk_block = """    uint32 perkCount = 0;
    if (uint32 const* list = ClassPerkList(player->getClass(), perkCount))
    {
        uint32 const selected = GetClassPerk(player);
        for (uint32 i = 0; i < perkCount; ++i)
            SendAddonLine(player, Acore::StringFormat("CPK|{}|{}",
                list[i], list[i] == selected ? 1 : 0));
    }

"""
    while body.count(perk_block) > 1:
        body = body.replace(perk_block, "", 1)

    alde_aa = """    SendAddonLine(player, Acore::StringFormat("ALDE|{}",
        g_attunedDe[accountId] ? 1 : 0));
    SendAddonLine(player, Acore::StringFormat("AA|{}|{}|{}",
        g_autoAttuneOn[accountId] ? 1 : 0,
        AttunedCount(accountId),
        g_autoAttuneOff[accountId]));
"""
    while body.count(alde_aa) > 1:
        body = body.replace(alde_aa, "", 1)

    return text[:start] + body + text[end:]


def insert_constants(text: str) -> str:
    inserts: list[tuple[str, str]] = [
        (
            "uint32 const SPELL_LEVELING_10 = 910062;",
            "uint32 const SPELL_LEVELING[] = { 910053, 910054, 910055, 910056, 910057, 910058, 910059, 910060, 910061, 910062 };\n"
            "uint32 const LEVELING_TIERS = 10;\n"
            "float const LEVELING_XP_BONUS = 0.10f;",
        ),
        (
            "uint32 const SPELL_TRAVEL_10 = 910082;",
            "uint32 const SPELL_TRAVEL[] = { 910073, 910074, 910075, 910076, 910077, 910078, 910079, 910080, 910081, 910082 };\n"
            "uint32 const TRAVEL_NEED[] = { 1, 2, 3, 4, 5, 10, 25, 50, 100, 250 };\n"
            "uint32 const TRAVEL_TIERS = 10;\n"
            "float const TRAVEL_REDUCE = 0.20f;",
        ),
        (
            "uint32 const SPELL_COOK[] = { 910063, 910064, 910065, 910066, 910067, 910068 };",
            "uint32 const COOK_REGEN_MS = 1000;",
        ),
        (
            "uint32 const QUEST_SPEED_NEED = 100;",
            "uint32 const AUTO_QUEST_NEED = 100;\n"
            "uint32 const FISH_POOL_NEED = 500;",
        ),
        (
            "uint32 const ACHIEVEMENT_100_HONORABLE_KILLS = 513;",
            "uint32 const ACHIEVEMENT_50_QUESTS = 482;\n"
            "uint32 const ACHIEVEMENT_500_FISH = 1447;\n"
            "uint32 const FIND_QUESTS_NEED = 50;",
        ),
    ]
    for anchor, block in inserts:
        if block.split("\n")[0].split("=")[0].strip() in text:
            continue
        idx = text.find(anchor)
        if idx == -1:
            continue
        line_end = text.find("\n", idx)
        text = text[: line_end + 1] + block + "\n" + text[line_end + 1 :]
    return text


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    before = len(text.splitlines())

    text = fix_addon_sync_splice(text)
    text = dedupe_blocks(text)
    text = clean_send_addon_sync(text)
    text = insert_constants(text)

    if not text.endswith("\n"):
        text += "\n"
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    after = len(text.splitlines())
    print(f"Fixed LivingGear.cpp: {before} -> {after} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
