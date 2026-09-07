"""
Close out player bug reports and feature requests, in the database and on GitHub.

Reports used to be write-once: they arrived and there was no point in the
process where one got closed. With twenty-odd open it stops being obvious which
are done, which were attempted, and which nobody has looked at.

  python tools/bug-reports/bug_resolve.py                      # what is open
  python tools/bug-reports/bug_resolve.py --all                # everything
  python tools/bug-reports/bug_resolve.py 21 fixed "guarded quest items"
  python tools/bug-reports/bug_resolve.py 15 attempted "instrumented, not solved"
  python tools/bug-reports/bug_resolve.py 4 wontfix "needs client DBC work"
  python tools/bug-reports/bug_resolve.py 21 open                # reopen it

Marking a report updates the row and the linked GitHub issue. Resolutions were
also mirrored into a Discord channel until 2026-09-07; that half was removed
along with the intake notifications.

Reaching the database needs the Docker daemon that runs ac-database. On lohk
that is local and nothing is required; from a workstation, export
DOCKER_HOST=ssh://lohk first.

Statuses: open, fixed, attempted, wontfix, duplicate.
`attempted` is the honest one and worth using. Bug #15 (solid chests) was
instrumented rather than solved; calling that "fixed" would be a lie that costs
someone an afternoon later.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from github_sync import GitHubCLI

ROOT = Path(__file__).resolve().parent

DB_CONTAINER = "ac-database"
DB_NAME = "acore_characters"
DB_USER = "root"
DB_PASSWORD = "password"

STATUSES = ("open", "fixed", "attempted", "wontfix", "duplicate")
SEP = "\x1f"


def run_sql(sql: str, db_password: str) -> str:
    proc = subprocess.run(
        ["docker", "exec", "-i", DB_CONTAINER, "mysql", "--user=%s" % DB_USER,
         "--password=%s" % db_password, "--default-character-set=utf8mb4",
         "--batch", "--raw", "--skip-column-names", DB_NAME],
        input=sql, capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        err = "\n".join(l for l in proc.stderr.splitlines()
                        if "Using a password on the command line" not in l).strip()
        raise RuntimeError("mysql failed (%d): %s" % (proc.returncode, err))
    return proc.stdout


def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("'", "''")


def listing(db_password: str, show_all: bool) -> int:
    where = "" if show_all else "WHERE status = 'open'"
    sql = ("SELECT CONCAT_WS('%s', id, status, character_name, "
           "LEFT(REPLACE(REPLACE(description, '\\n', ' '), '\\r', ' '), 88), "
           "COALESCE(resolution, '')) FROM lg_bug_report %s ORDER BY id;" % (SEP, where))
    rows = [l for l in run_sql(sql, db_password).splitlines() if l.strip()]
    if not rows:
        print("No open reports." if not show_all else "No reports.")
        return 0
    for line in rows:
        parts = line.split(SEP)
        if len(parts) < 5:
            continue
        rid, status, who, desc, res = parts[:5]
        flag = {"open": " ", "fixed": "+", "attempted": "~",
                "wontfix": "x", "duplicate": "="}.get(status, "?")
        print("%s #%-3s %-9s %-12s %s" % (flag, rid, status, who[:12], desc))
        if res:
            print("        -> %s" % res)
    print()
    print("%d report(s). Legend: + fixed  ~ attempted  x wontfix  = duplicate" % len(rows))
    return 0


def resolve(report_id: int, status: str, note: str, db_password: str) -> int:
    rows = run_sql("SELECT CONCAT_WS('%s', id, COALESCE(github_issue_number, 0)) "
                   "FROM lg_bug_report WHERE id = %d;" % (SEP, report_id), db_password).strip()
    if not rows:
        print("No report #%d." % report_id, file=sys.stderr)
        return 1
    parts = rows.split(SEP)
    github_issue = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0

    stamp = "UNIX_TIMESTAMP()" if status != "open" else "0"
    run_sql("UPDATE lg_bug_report SET status = '%s', resolution = %s, resolved_at = %s "
            "WHERE id = %d;"
            % (status, ("'%s'" % escape(note)) if note else "NULL", stamp, report_id),
            db_password)
    print("#%d -> %s%s" % (report_id, status, (" (%s)" % note) if note else ""))
    if github_issue:
        try:
            github = GitHubCLI()
            github.ensure_labels()
            github.resolve_issue(github_issue, status, note)
            print("  GitHub issue #%d updated" % github_issue)
        except RuntimeError as exc:
            print("  GitHub update failed: %s" % exc, file=sys.stderr)
            return 1
    else:
        print("  no GitHub issue linked")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("report", nargs="?", type=int, help="report id to mark")
    ap.add_argument("status", nargs="?", choices=STATUSES, help="new status")
    ap.add_argument("note", nargs="?", default="", help="what was done about it")
    ap.add_argument("--all", action="store_true", help="list every report, not just open ones")
    ap.add_argument("--db-password", default=DB_PASSWORD)
    args = ap.parse_args()

    try:
        if args.report is None:
            return listing(args.db_password, args.all)
        if not args.status:
            print("need a status: %s" % ", ".join(STATUSES), file=sys.stderr)
            return 2
        return resolve(args.report, args.status, args.note, args.db_password)
    except RuntimeError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
