#!/usr/bin/env python3
"""Replay LivingGear.cpp StrReplace patches in two phases to avoid duplicates."""
from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRANSCRIPT_DIR = Path(
    r"C:\Users\jeremy\.cursor\projects\a-wow-bonesaw\agent-transcripts"
    r"\c437e449-e62c-4f1f-b743-dda65c4342b3"
)
BASE = Path(
    r"C:\Users\jeremy\AppData\Roaming\Cursor\User\History\6d6dc002\EcrQ.cpp"
)
TARGET = ROOT / "modules/mod-living-gear/src/LivingGear.cpp"
BACKUP = ROOT / "modules/mod-living-gear/src/LivingGear.cpp.stub.bak"
MAIN = TRANSCRIPT_DIR / "c437e449-e62c-4f1f-b743-dda65c4342b3.jsonl"


def collect_patches(files: list[Path]) -> list[tuple[str, int, str, str]]:
    patches: list[tuple[str, int, str, str]] = []
    for jf in files:
        with jf.open(encoding="utf-8") as f:
            for i, line in enumerate(f):
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                content = obj.get("message", {}).get("content", [])
                if not isinstance(content, list):
                    continue
                for block in content:
                    if block.get("name") != "StrReplace":
                        continue
                    inp = block.get("input", {})
                    path = inp.get("path", "").replace("\\", "/")
                    if "LivingGear.cpp" not in path:
                        continue
                    old = inp.get("old_string")
                    new = inp.get("new_string")
                    if old is None or new is None:
                        continue
                    patches.append((jf.name, i, old, new))
    return patches


def apply_patches(text: str, patches: list[tuple[str, int, str, str]], label: str) -> str:
    applied = skipped = 0
    failed: list[str] = []
    for src, line_no, old, new in patches:
        if old not in text:
            skipped += 1
            continue
        count = text.count(old)
        if count != 1:
            failed.append(f"{src}:{line_no} count={count}")
            continue
        text = text.replace(old, new, 1)
        applied += 1
    print(f"{label}: applied {applied}, skipped {skipped}, failed {len(failed)}")
    if failed[:5]:
        print("  sample failures:", failed[:5])
    return text


def main() -> int:
    if TARGET.exists() and not BACKUP.exists():
        shutil.copy2(TARGET, BACKUP)

    shutil.copy2(BASE, TARGET)
    text = TARGET.read_text(encoding="utf-8")
    print(f"Base: {len(text.splitlines())} lines")

    main_patches = collect_patches([MAIN])
    text = apply_patches(text, main_patches, "phase1-main")
    print(f"  after phase1: {len(text.splitlines())} lines")

    sub_files = sorted(p for p in TRANSCRIPT_DIR.rglob("*.jsonl") if p.name != MAIN.name)
    sub_patches = collect_patches(sub_files)
    text = apply_patches(text, sub_patches, "phase2-subagents")
    print(f"  after phase2: {len(text.splitlines())} lines")

    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(f"Wrote {TARGET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
