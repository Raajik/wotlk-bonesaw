"""
File new player bug reports and feature requests as GitHub issues.

Reports are written into acore_characters.lg_bug_report by the worldserver
(.bug/.feature in chat, or /bugreport and /featurerequest from the addon). The
worldserver never talks to GitHub itself -- it only writes rows -- so this
script is what actually delivers them. Run it on a schedule; 15 minutes is the
current cadence.

  python tools/bug-reports/bug_sync.py            # file anything unlinked
  python tools/bug-reports/bug_sync.py --dry-run  # print, change nothing

Reports used to be mirrored into a Discord channel as well. That half was
removed on 2026-09-07: GitHub is the canonical tracker, the notifications were
noise, and the delivery path had been silently dead since the realm moved to
lohk anyway.

Only 'open' and 'attempted' reports without an issue number are filed, so
resolved history is left alone. The issue number is stored back on the row,
which is what makes this idempotent: a report that already has one is never
filed twice, and a run that dies partway leaves the rest to the next run.

Reaching the database needs the Docker daemon that runs ac-database. On lohk
that is local and nothing is required; from a workstation, export
DOCKER_HOST=ssh://lohk first.
"""
from __future__ import annotations

import argparse
import contextlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from github_sync import GitHubCLI, SqlReportStore, sync_report

ROOT = Path(__file__).resolve().parent
GITHUB_SYNC_LOCK = Path(tempfile.gettempdir()) / "bonesaw_bug_github_sync.lock"

DB_CONTAINER = "ac-database"
DB_NAME = "acore_characters"
DB_USER = "root"
DB_PASSWORD = "password"  # docker-compose default; override with --db-password

MAX_PER_RUN = 40

FIELD_SEP = "\x1f"
ROW_SEP = "\x1e"


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


def fetch_sync_candidates(db_password: str) -> list[dict]:
    return _fetch_reports(
        "status IN ('open', 'attempted') AND github_issue_number IS NULL",
        db_password,
    )


def describe(row: dict) -> str:
    kind = "Feature" if row.get("report_type") == "feature" else "Report"
    flags = ""
    if row.get("is_critical") in ("1", "true", "True"):
        flags += " [critical]"
    if row.get("is_recurring") in ("1", "true", "True"):
        flags += " [recurring]"
    return f"{kind} #{row['id']}{flags} - {row['name']}: {row['description'][:80]}"


@contextlib.contextmanager
def github_sync_lock(path: Path = GITHUB_SYNC_LOCK):
    """Keep overlapping scheduled runs from filing the same report twice."""
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
                import fcntl
                fcntl.flock(descriptor, fcntl.LOCK_UN)
            except OSError:
                pass
        try:
            os.close(descriptor)
        except OSError:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="print what would be filed and create nothing")
    parser.add_argument("--db-password", default=DB_PASSWORD,
                        help="database root password (default: the docker-compose default)")
    args = parser.parse_args()

    try:
        candidates = fetch_sync_candidates(args.db_password)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not candidates:
        print("No reports awaiting a GitHub issue.")
        return 0

    if args.dry_run:
        for row in candidates:
            print(describe(row))
        print(f"(dry run: {len(candidates)} report(s) left unfiled)")
        return 0

    with github_sync_lock() as acquired:
        if not acquired:
            print("Another sync is already running; nothing to do.")
            return 0

        github = GitHubCLI()
        store = SqlReportStore(run_sql, args.db_password)
        try:
            github.ensure_labels()
        except Exception as exc:
            print(f"GitHub unavailable; reports remain queued: {exc}", file=sys.stderr)
            return 1

        filed = 0
        failed = 0
        for report in candidates:
            try:
                sync_report(report, github, store)
                filed += 1
            except RuntimeError as exc:
                failed += 1
                print(f"GitHub sync failed for report #{report['id']}: {exc}", file=sys.stderr)

    print(f"Filed {filed} report(s)." + (f" {failed} failed and stay queued." if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
