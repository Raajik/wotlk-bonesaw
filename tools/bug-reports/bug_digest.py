"""
Post new player bug reports and feature requests to Discord.

Reports are written into acore_characters.lg_bug_report by the worldserver
(.bug/.feature in chat, or /bugreport and /featurerequest from the addon).
The worldserver never talks to
Discord itself -- it only writes rows -- so this script is what actually
delivers them. Run it on a schedule; 15 minutes is the current cadence.

  python tools/bug-reports/bug_digest.py            # post anything new
  python tools/bug-reports/bug_digest.py --dry-run  # print, change nothing
  python tools/bug-reports/bug_digest.py --test     # post a test message only

The webhook URL lives in tools/bug-reports/discord.webhook, one line, no
quotes. That path is covered by the *.webhook rule in .gitignore and must
never be committed. If the file is missing the script exits quietly rather
than failing, so a scheduled run on a machine without the secret is harmless.

Rows are marked posted=1 only after Discord accepts them, so a failed run
leaves them to be picked up next time rather than losing them, and a re-run
cannot double-post.
"""
from __future__ import annotations

import argparse
import contextlib
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

from github_sync import GitHubCLI, SqlReportStore, sync_report

ROOT = Path(__file__).resolve().parent
WEBHOOK_FILE = ROOT / "discord.webhook"
GITHUB_SYNC_LOCK = Path(tempfile.gettempdir()) / "bonesaw_bug_github_sync.lock"

DB_CONTAINER = "ac-database"
DB_NAME = "acore_characters"
DB_USER = "root"
DB_PASSWORD = "password"  # docker-compose default; override with --db-password

# Discord hard-limits a message to 2000 characters. Stay under it with room to
# spare rather than having a long batch rejected wholesale.
MAX_MESSAGE = 1800
MAX_PER_RUN = 40

FIELD_SEP = "\x1f"
ROW_SEP = "\x1e"


def read_webhook() -> str | None:
    if not WEBHOOK_FILE.exists():
        return None
    url = WEBHOOK_FILE.read_text(encoding="utf-8").strip()
    return url or None


def run_sql(sql: str, db_password: str) -> str:
    """Run a statement inside the database container and return raw stdout."""
    proc = subprocess.run(
        [
            "docker", "exec", "-i", DB_CONTAINER,
            "mysql", f"--user={DB_USER}", f"--password={db_password}",
            "--batch", "--raw", "--skip-column-names", DB_NAME,
        ],
        input=sql,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        stderr = proc.stderr.strip()
        # mysql prints this to stderr on every single invocation.
        stderr = "\n".join(
            line for line in stderr.splitlines()
            if "Using a password on the command line" not in line
        ).strip()
        raise RuntimeError(f"mysql failed ({proc.returncode}): {stderr}")
    return proc.stdout


def _fetch_reports(where: str, db_password: str) -> list[dict]:
    # Concatenated with explicit separators because a bug description is free
    # text and can contain tabs and newlines, which would otherwise break the
    # column/row splitting that --batch output relies on.
    sql = f"""
        SELECT CONCAT_WS('{FIELD_SEP}',
            id, character_name, player_level, reported_at,
            zone_name, map_id, zone_id,
            ROUND(pos_x, 1), ROUND(pos_y, 1), ROUND(pos_z, 1),
            target_entry, target_name,
            REPLACE(REPLACE(description, '\\n', ' '), '\\r', ' '), report_type,
            status, COALESCE(github_issue_number, 0), COALESCE(github_issue_url, ''),
            COALESCE(is_critical, 0), COALESCE(is_recurring, 0)
        ), '{ROW_SEP}'
        FROM lg_bug_report
        WHERE {where}
        ORDER BY id
        LIMIT {MAX_PER_RUN};
    """
    out = run_sql(sql, db_password)
    rows = []
    for line in out.split(ROW_SEP):
        line = line.strip("\r\n\t ")
        if not line:
            continue
        parts = line.split(FIELD_SEP)
        if len(parts) < 17:
            continue
        rows.append({
            "id": parts[0],
            "name": parts[1],
            "level": parts[2],
            "at": parts[3],
            "zone": parts[4],
            "map": parts[5],
            "zone_id": parts[6],
            "x": parts[7],
            "y": parts[8],
            "z": parts[9],
            "target_entry": parts[10],
            "target_name": parts[11],
            "description": parts[12],
            "report_type": parts[13],
            "status": parts[14],
            "github_issue_number": parts[15],
            "github_issue_url": parts[16],
            "is_critical": parts[17] if len(parts) > 17 else "0",
            "is_recurring": parts[18] if len(parts) > 18 else "0",
        })
    return rows


def fetch_unposted(db_password: str) -> list[dict]:
    return _fetch_reports("posted = 0", db_password)


def fetch_sync_candidates(db_password: str) -> list[dict]:
    return _fetch_reports(
        "status IN ('open', 'attempted') AND github_issue_number IS NULL",
        db_password,
    )


def mark_posted(ids: list[str], db_password: str) -> None:
    if not ids:
        return
    id_list = ",".join(str(int(i)) for i in ids)
    run_sql(f"UPDATE lg_bug_report SET posted = 1 WHERE id IN ({id_list});", db_password)


def format_report(row: dict) -> str:
    when = "unknown time"
    try:
        when = datetime.fromtimestamp(int(row["at"]), tz=timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    except (ValueError, OverflowError, OSError):
        pass

    where = row["zone"] or f"map {row['map']}"
    rtype = row.get("report_type")
    if rtype == "feature":
        heading = f"Feature #{row['id']}"
    else:
        heading = f"#{row['id']}"
    # is_critical replaces the old '.crit' report_type; either spelling means
    # the player flagged it game-breaking.
    if row.get("is_critical") in ("1", "true", "True"):
        heading = f"CRITICAL #{row['id']}"
    if row.get("is_recurring") in ("1", "true", "True"):
        heading += " (recurring)"
    lines = [
        f"**{heading}** - {row['name']} (level {row['level']}) - {when}",
        f"> {row['description']}",
        f"`{where}` at `{row['x']} {row['y']} {row['z']}` (map {row['map']}, zone {row['zone_id']})",
    ]
    if row["target_name"]:
        target = row["target_name"]
        if row["target_entry"] and row["target_entry"] != "0":
            target += f" (entry {row['target_entry']})"
        lines.append(f"Target: {target}")
    if row.get("github_issue_url"):
        lines.append(f"GitHub: {row['github_issue_url']}")
    return "\n".join(lines)


def store_message_id(report_id: str, message_id: str, db_password: str) -> None:
    """Remember which Discord message carries which report.

    Without this there is no way to go back and strike a report through when it
    is fixed -- the whole point of bug_resolve.py."""
    if not message_id:
        return
    safe = "".join(c for c in message_id if c.isdigit())
    if not safe:
        return
    run_sql("UPDATE lg_bug_report SET discord_message_id = '%s' WHERE id = %d;"
            % (safe, int(report_id)), db_password)


def should_sync_github(dry_run: bool, test_mode: bool) -> bool:
    return not dry_run and not test_mode


@contextlib.contextmanager
def github_sync_lock(path: Path = GITHUB_SYNC_LOCK):
    """Allow one GitHub producer while never blocking Discord delivery."""
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_RDWR)
    except OSError:
        yield False
        return
    acquired = False
    try:
        try:
            if os.path.getsize(path) == 0:
                os.write(descriptor, b"0")
            os.lseek(descriptor, 0, os.SEEK_SET)
        except OSError:
            yield False
            return
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(descriptor, msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            acquired = True
        except OSError:
            yield False
            return
        yield True
    finally:
        if acquired:
            try:
                os.lseek(descriptor, 0, os.SEEK_SET)
                if os.name == "nt":
                    import msvcrt
                    msvcrt.locking(descriptor, msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(descriptor, fcntl.LOCK_UN)
            except OSError:
                pass
        try:
            os.close(descriptor)
        except OSError:
            pass



def post(url: str, content: str) -> str:
    # ?wait=true makes Discord return the created message rather than an empty
    # 204, which is the only way to learn its id.
    if "?" not in url:
        url = url + "?wait=true"
    payload = json.dumps({"content": content, "allowed_mentions": {"parse": []}}).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json", "User-Agent": "bonesaw-bug-digest/1.0"},
    )
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                if 200 <= response.status < 300:
                    try:
                        return str(json.loads(response.read().decode()).get("id", ""))
                    except Exception:
                        return ""
                raise RuntimeError(f"Discord returned HTTP {response.status}")
        except urllib.error.HTTPError as exc:
            if exc.code == 429 and attempt < 2:
                retry_after = 5.0
                try:
                    retry_after = float(json.loads(exc.read().decode()).get("retry_after", 5))
                except Exception:
                    pass
                time.sleep(min(retry_after, 30))
                continue
            raise RuntimeError(f"Discord returned HTTP {exc.code}: {exc.reason}") from exc
        except urllib.error.URLError as exc:
            if attempt < 2:
                time.sleep(3)
                continue
            raise RuntimeError(f"could not reach Discord: {exc.reason}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="print what would be posted and leave the rows unmarked")
    parser.add_argument("--test", action="store_true",
                        help="post a single test message and exit, touching no reports")
    parser.add_argument("--db-password", default=DB_PASSWORD,
                        help="database root password (default: the docker-compose default)")
    args = parser.parse_args()

    # GitHub is the canonical work tracker. Reconcile before posting Discord so
    # each notification carries the issue link. A GitHub outage does not lose
    # or block the DB report; Discord delivery continues without a link and the
    # next scheduled run retries the sync.
    if should_sync_github(args.dry_run, args.test):
        with github_sync_lock() as acquired:
            if not acquired:
                print("GitHub sync already running; Discord delivery will continue.")
            else:
                github = GitHubCLI()
                store = SqlReportStore(run_sql, args.db_password)
                try:
                    github.ensure_labels()
                    for report in fetch_sync_candidates(args.db_password):
                        try:
                            sync_report(report, github, store)
                        except RuntimeError as exc:
                            print(f"GitHub sync failed for report #{report['id']}: {exc}", file=sys.stderr)
                except Exception as exc:
                    print(f"GitHub unavailable; reports remain queued: {exc}", file=sys.stderr)

    url = read_webhook()
    if not url and not args.dry_run:
        print(f"No webhook at {WEBHOOK_FILE} -- nothing to do.")
        return 0

    if args.test:
        post(url, "Bonesaw reports: this channel is wired up. Player bugs and feature requests "
                  "from `.bug`, `.feature`, `/bugreport`, and `/featurerequest` will arrive here.")
        print("Test message posted.")
        return 0

    try:
        rows = fetch_unposted(args.db_password)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not rows:
        print("No new reports.")
        return 0

    if args.dry_run:
        for row in rows:
            print(format_report(row))
            print("-" * 60)
        print("(dry run: %d report(s) left unmarked)" % len(rows))
        return 0

    # One message per report. Batching kept the message count down, but a
    # batched message cannot be struck through for a single report inside it,
    # and being able to close reports matters more than tidiness.
    done = []
    try:
        for row in rows:
            message_id = post(url, format_report(row))
            store_message_id(row["id"], message_id, args.db_password)
            done.append(row["id"])
            time.sleep(0.4)  # stay clear of the webhook rate limit
    except RuntimeError as exc:
        print("error posting to Discord: %s" % exc, file=sys.stderr)
        # Mark whatever genuinely landed, so a partial run does not repost the
        # ones that already made it.
        if done:
            mark_posted(done, args.db_password)
            print("Posted %d before failing; those are marked." % len(done))
        return 1

    mark_posted(done, args.db_password)
    print("Posted %d report(s)." % len(done))
    return 0


if __name__ == "__main__":
    sys.exit(main())
