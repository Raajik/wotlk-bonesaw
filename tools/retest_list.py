#!/usr/bin/env python3
"""Build the "Please test these things" section for Bonesaw patch notes.

Phase 6 of /bonesaw-ship appends this section to every notes file, just before
the update line. It keeps fixes that shipped but were never confirmed in front
of players instead of silently aging out on the tracker.

    python tools/retest_list.py --raw
        Print every open status:awaiting-retest issue (oldest first). Use this
        to pick and rephrase bullets for the notes.

    python tools/retest_list.py -a "does X behave?" -a "does Y?" --max 4
        Render a paste-ready section: your -a asks first (this ship's changes
        that only a player can verify), then the oldest awaiting-retest issues.

    python tools/retest_list.py --check tools/patch-notes/0.1.100.md
        Exit non-zero if the notes file is missing the section or leaks an
        internal [Report #N] reference into player-facing text.

Issue titles come from GitHub (Raajik/wotlk-bonesaw); the [Report #N] prefix is
stripped and long titles trimmed, but the rendered output is still a STARTER -
the notes author rephrases before it ships. Players cannot see the tracker, so
issue numbers never appear in the notes.

ASCII only: smart quotes and dashes are folded to ASCII, and any issue whose
title still contains non-ascii after that is skipped with a warning rather
than poisoning the notes (post_patch_notes.py hard-fails on non-ascii).
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = "Raajik/wotlk-bonesaw"
LABEL = "status:awaiting-retest"
HEADER = "**Please test these things**"

# Client-facing strings are ASCII only. Fold the characters players most often
# type into a bug report before falling back to skipping the issue entirely.
FOLD = {
    "\u2018": "'", "\u2019": "'",
    "\u201c": '"', "\u201d": '"',
    "\u2013": "-", "\u2014": "-",
    "\u2026": "...",
}
REPORT_PREFIX = re.compile(r"^\[Report\s*#\d+\]\s*")


def fetch_issues():
    cmd = ["gh", "issue", "list", "--repo", REPO, "--label", LABEL,
           "--state", "open", "--json", "number,title,createdAt",
           "--limit", "100"]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
    if proc.returncode != 0:
        sys.exit(f"gh issue list failed: {(proc.stderr or '').strip()[:300]}")
    issues = json.loads(proc.stdout or "[]")
    issues.sort(key=lambda i: i["createdAt"])  # longest-waiting first
    return issues


def clean_title(title):
    title = REPORT_PREFIX.sub("", title.strip())
    for bad, good in FOLD.items():
        title = title.replace(bad, good)
    return title


def is_ascii(text):
    return all(ord(c) < 128 for c in text)


def trim(title, limit=100):
    if len(title) <= limit:
        return title
    return title[:limit - 3].rstrip() + "..."


def render(asks, issues, max_issues):
    skipped = []
    bullets = []
    for issue in issues:
        title = clean_title(issue["title"])
        if not is_ascii(title):
            skipped.append(f"#{issue['number']} (non-ascii title)")
            continue
        bullets.append(trim(title))
        if len(bullets) >= max_issues:
            break

    out = [HEADER]
    if asks:
        out += ["", "Fresh from this patch - tell us if any of it misbehaves:"]
        out += [f"- {a}" for a in asks]
    if bullets:
        out += ["", "Still waiting on a report from you:"]
        out += [f"- {b}" for b in bullets]
    return "\n".join(out), skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-a", "--ask", action="append", default=[],
                    help="player-facing ask about THIS ship's changes; repeatable")
    ap.add_argument("--max", type=int, default=6,
                    help="max awaiting-retest bullets (default 6)")
    ap.add_argument("--only", default="",
                    help="comma-separated issue numbers to include instead of oldest-first")
    ap.add_argument("--raw", action="store_true",
                    help="print the raw awaiting-retest list and exit")
    ap.add_argument("--check", metavar="FILE",
                    help="verify a notes file carries the section; exits 1 if not")
    args = ap.parse_args()

    if args.check:
        text = Path(args.check).read_text(encoding="utf-8")
        problems = []
        if HEADER not in text:
            problems.append(f'missing the "{HEADER}" section')
        if "[Report #" in text:
            problems.append("leaks internal [Report #N] issue references")
        if problems:
            sys.exit("\n".join(problems))
        print(f"ok: {args.check} carries the section")
        return 0

    issues = fetch_issues()

    if args.raw:
        print(f"{len(issues)} open {LABEL} issue(s), oldest first:")
        for issue in issues:
            title = clean_title(issue["title"])
            print(f"  #{issue['number']}  {issue['createdAt'][:10]}  {trim(title)}")
        return 0

    if args.only:
        want = {int(n) for n in args.only.split(",") if n.strip()}
        issues = [i for i in issues if i["number"] in want]
        missing = want - {i["number"] for i in issues}
        if missing:
            print(f"warning: not open/labelled: {sorted(missing)}")

    section, skipped = render(args.ask, issues, args.max)
    for note in skipped:
        print(f"warning: skipped {note} - rephrase it by hand")
    print(section)
    print(f"\n[{len(section)} chars; post_patch_notes.py splits on blank lines "
          f"if the whole file exceeds one message]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
