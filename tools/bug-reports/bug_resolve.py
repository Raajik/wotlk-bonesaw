"""
Close out player bug reports, and strike them through in Discord.

Reports used to be write-once: they arrived, went to Discord, and there was no
point in the process where one got closed. With twenty-odd open it stops being
obvious which are done, which were attempted, and which nobody has looked at.

  python tools/bug-reports/bug_resolve.py                      # what is open
  python tools/bug-reports/bug_resolve.py --all                # everything
  python tools/bug-reports/bug_resolve.py 21 fixed "guarded quest items"
  python tools/bug-reports/bug_resolve.py 15 attempted "instrumented, not solved"
  python tools/bug-reports/bug_resolve.py 4 wontfix "needs client DBC work"
  python tools/bug-reports/bug_resolve.py 21 open                # reopen it

Marking a report edits its original Discord message in place -- the description
is struck through and the resolution appended -- rather than posting a second
message nobody would connect to the first. That only works for reports posted
after the digest started recording message ids; older ones are updated in the
database and reported here as "no Discord message on file", which is a note,
not a failure.

Statuses: open, fixed, attempted, wontfix, duplicate.
`attempted` is the honest one and worth using. Bug #15 (solid chests) was
instrumented rather than solved; calling that "fixed" would be a lie that costs
someone an afternoon later.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WEBHOOK_FILE = ROOT / "discord.webhook"

DB_CONTAINER = "ac-database"
DB_NAME = "acore_characters"
DB_USER = "root"
DB_PASSWORD = "password"

STATUSES = ("open", "fixed", "attempted", "wontfix", "duplicate")
MARK = {
    "open": "",
    "fixed": "FIXED",
    "attempted": "ATTEMPTED",
    "wontfix": "WONTFIX",
    "duplicate": "DUPLICATE",
}
SEP = "\x1f"


def run_sql(sql: str, db_password: str) -> str:
    proc = subprocess.run(
        ["docker", "exec", "-i", DB_CONTAINER, "mysql", "--user=%s" % DB_USER,
         "--password=%s" % db_password, "--batch", "--raw", "--skip-column-names", DB_NAME],
        input=sql, capture_output=True, text=True)
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


def edit_discord(message_id: str, report_id: str, status: str, note: str) -> str:
    """Rewrite the original message with the description struck through."""
    if not WEBHOOK_FILE.exists():
        return "no webhook file"
    url = WEBHOOK_FILE.read_text(encoding="utf-8").strip()
    if not url or not message_id:
        return "no Discord message on file"

    try:
        with urllib.request.urlopen(
                urllib.request.Request(
                    "%s/messages/%s" % (url.split("?")[0], message_id),
                    headers={"User-Agent": "bonesaw-bug-resolve/1.0"}),
                timeout=20) as response:
            original = json.loads(response.read().decode()).get("content", "")
    except urllib.error.HTTPError as exc:
        return "could not read message (HTTP %d)" % exc.code
    except Exception as exc:
        return "could not read message (%s)" % exc

    # Strip any previous resolution footer so re-resolving does not stack them,
    # and un-strike so a reopen genuinely restores the original.
    body = original.split("\n-# ")[0]
    body = body.replace("~~", "")

    if status == "open":
        content = body
    else:
        # Only the quoted description line is struck through. The context
        # lines stay readable, because they are exactly what you need if a
        # report has to be reopened.
        struck = []
        for line in body.split("\n"):
            if line.startswith("> "):
                struck.append("> ~~%s~~" % line[2:])
            else:
                struck.append(line)
        stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
        footer = "**%s**%s - %s" % (MARK[status], (" " + note) if note else "", stamp)
        content = "\n".join(struck) + "\n-# " + footer

    payload = json.dumps({"content": content, "allowed_mentions": {"parse": []}}).encode()
    request = urllib.request.Request(
        "%s/messages/%s" % (url.split("?")[0], message_id), data=payload, method="PATCH",
        headers={"Content-Type": "application/json", "User-Agent": "bonesaw-bug-resolve/1.0"})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                if 200 <= response.status < 300:
                    return "Discord updated"
                return "Discord returned HTTP %d" % response.status
        except urllib.error.HTTPError as exc:
            if exc.code == 429 and attempt < 2:
                time.sleep(5)
                continue
            return "Discord edit failed (HTTP %d)" % exc.code
        except Exception as exc:
            return "Discord edit failed (%s)" % exc
    return "Discord edit failed"


def resolve(report_id: int, status: str, note: str, db_password: str) -> int:
    rows = run_sql("SELECT CONCAT_WS('%s', id, discord_message_id) FROM lg_bug_report "
                   "WHERE id = %d;" % (SEP, report_id), db_password).strip()
    if not rows:
        print("No report #%d." % report_id, file=sys.stderr)
        return 1
    message_id = rows.split(SEP)[1] if SEP in rows else ""

    stamp = "UNIX_TIMESTAMP()" if status != "open" else "0"
    run_sql("UPDATE lg_bug_report SET status = '%s', resolution = %s, resolved_at = %s "
            "WHERE id = %d;"
            % (status, ("'%s'" % escape(note)) if note else "NULL", stamp, report_id),
            db_password)
    print("#%d -> %s%s" % (report_id, status, (" (%s)" % note) if note else ""))
    print("  %s" % edit_discord(message_id, str(report_id), status, note))
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
