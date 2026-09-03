#!/usr/bin/env python3
"""Generate a session handoff document for the Bonesaw project.

Folds durable state (git, DB bug reports, ship status) plus the rolling
agent notes file (.hermes/HANDOFF.md) into one markdown document so any
future session - human or agent - starts with full context.

Usage:
  python tools/handoff.py                    # print to stdout
  python tools/handoff.py --write            # also write .hermes/HANDOFF-latest.md
  python tools/handoff.py --note "text"      # append a dated line to the notes section
"""
import argparse
import datetime
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NOTES_FILE = ROOT / ".hermes" / "HANDOFF.md"
OUT_FILE = ROOT / ".hermes" / "HANDOFF-latest.md"


def sh(cmd: str, timeout: int = 30) -> str:
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                           timeout=timeout, cwd=ROOT)
        return (r.stdout or r.stderr or "").strip()
    except Exception as e:
        return f"(error: {e})"


def mysql(sql: str) -> str:
    cmd = ('docker exec ac-database mysql -uroot -ppassword -N -e "' + sql + '"')
    return sh(cmd, timeout=20).replace("\t", " | ")


def section(title: str, body: str) -> str:
    return f"\n## {title}\n\n{body.rstrip()}\n"


def build() -> str:
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    out = [f"# Bonesaw handoff -- generated {now}\n"]

    out.append(section("Ship status (bonesaw_status)",
                       "```\n" + sh("python tools/bonesaw_status.py") + "\n```"))

    out.append(section("Unshipped commits (patch-notes source)",
                       sh("git log --oneline $(git describe --tags --match 'ship/*' --abbrev=0)..HEAD 2>/dev/null || git log --oneline -5")))

    out.append(section("Uncommitted changes", sh("git status --short") or "(clean)"))

    bug_sql = ("SELECT id, status, character_name, LEFT(description,100) "
               "FROM acore_characters.lg_bug_report "
               "WHERE status IN ('open','attempted') ORDER BY id DESC LIMIT 25")
    out.append(section("Open/attempted bug reports (latest 25)", mysql(bug_sql) or "(none)"))

    feat_sql = ("SELECT id, status, LEFT(description,100) "
                "FROM acore_characters.lg_bug_report "
                "WHERE report_type='feature' AND status IN ('open','attempted') "
                "ORDER BY id DESC LIMIT 15")
    out.append(section("Open feature requests (latest 15)", mysql(feat_sql) or "(none)"))

    out.append(section("Recent pending SQL",
                       sh("ls -t data/sql/updates/pending_db_world/*.sql 2>/dev/null | head -5")))

    gh = sh("gh issue list -R Raajik/wotlk-bonesaw --state open --limit 15 "
            "--json number,title --jq '.[] | \"\\(.number) \\(.title)\"'", timeout=40)
    out.append(section("Open GitHub issues", gh or "(none)"))

    if NOTES_FILE.exists():
        out.append(section("Agent notes (rolling, most recent last)",
                           NOTES_FILE.read_text(encoding="utf-8").strip()))
    else:
        out.append(section("Agent notes (rolling, most recent last)",
                           "(no .hermes/HANDOFF.md yet - agents append multi-step "
                           "findings here as they work)"))

    return "\n".join(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="write .hermes/HANDOFF-latest.md")
    ap.add_argument("--note", help="append a line to the rolling notes before generating")
    args = ap.parse_args()

    if args.note:
        NOTES_FILE.parent.mkdir(parents=True, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
        with NOTES_FILE.open("a", encoding="utf-8") as f:
            f.write(f"- [{stamp}] {args.note}\n")

    doc = build()
    print(doc)
    if args.write:
        OUT_FILE.write_text(doc, encoding="utf-8")
        print(f"\n(written to {OUT_FILE})", file=sys.stderr)


if __name__ == "__main__":
    main()
