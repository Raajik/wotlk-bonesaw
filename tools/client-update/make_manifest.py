"""Hash the Bonesaw client files that the launcher is allowed to update.

Wow.exe is omitted by default. Do not attach Blizzard's client to a public release.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

CLIENT = Path(r"B:\Games\WoW 3.3.5\Bonesaw")
FILES = [
    ("Data/patch-Y.MPQ", "patch-Y.MPQ"),
    ("Data/enUS/patch-enUS-4.MPQ", "patch-enUS-4.MPQ"),
    ("Data/enGB/patch-enGB-4.MPQ", "patch-enGB-4.MPQ"),
]
EXE = ("Wow.exe", "Wow.exe")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--include-exe",
        action="store_true",
        help="Hash Wow.exe too. Do not use this for a public GitHub release.",
    )
    args = parser.parse_args()

    version = (CLIENT / "Bonesaw.version").read_text(encoding="utf-8").strip()
    files = []
    selected = list(FILES)
    if args.include_exe:
        selected.insert(0, EXE)
    for rel, asset in selected:
        p = CLIENT / rel.replace("/", "\\")
        if not p.exists():
            raise SystemExit(f"Missing {p}")
        files.append({"path": rel, "asset": asset, "sha256": sha256(p)})
        print(f"{rel}  {files[-1]['sha256']}")
    out = {"version": version, "files": files}
    dest = Path(__file__).resolve().parent / "Bonesaw.manifest.json"
    dest.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {dest}")


if __name__ == "__main__":
    main()
