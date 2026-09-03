"""One-time repair: rewrite truncated in-game report titles on GitHub.

github_sync.py capped titles at 180 chars for its whole life, so every report
whose "[Report #N] " + description ran longer got a "..." title -- while the
full text stayed intact in acore_characters.lg_bug_report (the server stores
up to 500 chars). The cap is now 256 (GitHub's own limit), so this script
pushes the missing tail back into the existing issue titles.

  python tools/bug-reports/backfill_titles.py            # dry run: show changes
  python tools/bug-reports/backfill_titles.py --apply    # actually edit titles
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from github_sync import GitHubCLI, MAX_TITLE_LENGTH, clean_wow_text  # noqa: E402

DB_CONTAINER = "ac-database"
DB_NAME = "acore_characters"
DB_USER = "root"
DB_PASSWORD = "password"

TITLE_RE = re.compile(r"^\[(Report|Feature) #(\d+)\] ")
run_cli = GitHubCLI()


def run_sql(sql: str) -> str:
    proc = subprocess.run(
        ["docker", "exec", "-i", DB_CONTAINER, "mysql", "--user=%s" % DB_USER,
         "--password=%s" % DB_PASSWORD, "--batch", "--skip-column-names", DB_NAME],
        input=sql, capture_output=True, text=True)
    if proc.returncode != 0:
        err = "\n".join(l for l in proc.stderr.splitlines()
                        if "Using a password on the command line" not in l).strip()
        raise RuntimeError("mysql failed (%d): %s" % (proc.returncode, err))
    return proc.stdout


def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("'", "''")


def full_title(report_id: int, kind: str, description: str) -> str:
    """Same construction github_sync.build_issue uses, against stored text."""
    prefix = f"[{kind} #{report_id}] "
    title = prefix + clean_wow_text(description)
    if len(title) > MAX_TITLE_LENGTH:
        title = title[: MAX_TITLE_LENGTH - 3].rstrip() + "..."
    return title


def fetch_description(report_id: int) -> str | None:
    rows = run_sql(
        "SELECT description FROM lg_bug_report WHERE id = %d" % report_id
    ).strip()
    return rows or None


def truncated_issues() -> list[dict]:
    raw = run_cli._run([
        "issue", "list", "--state", "all", "--limit", "500",
        "--json", "number,title",
    ])
    issues = run_cli._json(raw, "listing issues")
    out = []
    for issue in issues:
        match = TITLE_RE.match(issue.get("title", ""))
        if match and issue["title"].endswith("..."):
            out.append({
                "number": int(issue["number"]),
                "title": issue["title"],
                "kind": match.group(1),
                "report_id": int(match.group(2)),
            })
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true",
                        help="edit titles (default is a dry run)")
    args = parser.parse_args()

    issues = truncated_issues()
    if not issues:
        print("no truncated report titles found")
        return 0

    changed = 0
    for issue in issues:
        description = fetch_description(issue["report_id"])
        if description is None:
            print("#%-5d report #%d: no DB row, leaving as-is" % (
                issue["number"], issue["report_id"]))
            continue
        new_title = full_title(issue["report_id"], issue["kind"], description)
        if new_title == issue["title"]:
            print("#%-5d report #%d: DB text already reflected in title" % (
                issue["number"], issue["report_id"]))
            continue
        print("#%-5d report #%d:\n  old: %s\n  new: %s" % (
            issue["number"], issue["report_id"], issue["title"], new_title))
        if args.apply:
            run_cli._run(["issue", "edit", str(issue["number"]),
                          "--title", new_title])
        changed += 1

    print("%d title(s) %s" % (
        changed, "updated" if args.apply else "would be updated (dry run)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
