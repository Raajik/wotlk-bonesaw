#!/usr/bin/env python3
"""Answer "where are we?" for Bonesaw in one place.

The recurring failure mode is not that a change was written badly, it is that
nobody could see it had never reached players. Three things drift apart and
nothing used to compare them:

  1. the last version actually announced   -> the newest ship/* git tag
  2. what is committed                     -> HEAD
  3. what players are actually running     -> the ac-worldserver image build time

On 2026-08-22 all three were different -- the running server had been built one
minute BEFORE the last commit, so a fix everyone believed was live had never
been compiled. Nothing anywhere said so. That is what this reports.

Read-only. Safe to run at any time, including mid-session on a live realm.
Exit code is 0 when everything is shipped and consistent, 1 otherwise, so it
can gate a ship.
"""

import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORLDSERVER_IMAGE = "acore/ac-wotlk-worldserver:master"
PENDING_DIRS = {
    "acore_world": "data/sql/updates/pending_db_world",
    "acore_characters": "data/sql/updates/pending_db_characters",
    "acore_auth": "data/sql/updates/pending_db_auth",
}


def run(cmd, **kw):
    """Run a command, returning stripped stdout or None if it failed at all."""
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60, **kw)
    except (OSError, subprocess.SubprocessError):
        return None
    if p.returncode != 0:
        return None
    return p.stdout.strip()


def git(*args):
    return run(["git", "-C", REPO, *args])


def parse_iso(s):
    if not s:
        return None
    s = s.strip().replace("Z", "+00:00")
    # Docker prints nanoseconds; datetime only takes microseconds.
    s = re.sub(r"\.(\d{6})\d+", r".\1", s)
    try:
        d = datetime.fromisoformat(s)
    except ValueError:
        return None
    return d if d.tzinfo else d.replace(tzinfo=timezone.utc)


def fmt(dt):
    return dt.astimezone().strftime("%Y-%m-%d %H:%M") if dt else "unknown"


def last_ship_tag():
    """Newest ship/* tag by version number, not by tag date."""
    tags = git("tag", "--list", "ship/*") or ""
    best, best_key = None, None
    for t in tags.splitlines():
        m = re.match(r"^ship/(\d+)\.(\d+)\.(\d+)$", t.strip())
        if not m:
            continue
        key = tuple(int(x) for x in m.groups())
        if best_key is None or key > best_key:
            best, best_key = t.strip(), key
    return best


def sql_not_imported():
    """Pending SQL files the database updater has not recorded as applied."""
    missing = []
    for db, rel in PENDING_DIRS.items():
        d = os.path.join(REPO, rel)
        if not os.path.isdir(d):
            continue
        files = sorted(f for f in os.listdir(d) if f.endswith(".sql"))
        if not files:
            continue
        applied = run([
            "docker", "exec", "ac-database", "mysql", "-uroot", "-ppassword", "-N", "-B",
            "-e", "SELECT name FROM `%s`.`updates`" % db,
        ])
        if applied is None:
            return None  # database unreachable; do not guess
        have = set(applied.split())
        missing += ["%s/%s" % (rel, f) for f in files if f not in have]
    return missing


def main():
    problems = []
    out = []

    version_file = os.path.join(REPO, "tools", "client-update", "Bonesaw.version")
    version = open(version_file).read().strip() if os.path.exists(version_file) else "?"

    head = git("rev-parse", "--short", "HEAD") or "?"
    head_at = parse_iso(git("log", "-1", "--format=%cI") or "")
    branch = git("rev-parse", "--abbrev-ref", "HEAD") or "?"

    out.append("BONESAW STATUS")
    out.append("")
    out.append("  branch            %s" % branch)
    out.append("  Bonesaw.version   %s" % version)

    # --- 1. what was last announced -------------------------------------
    tag = last_ship_tag()
    if tag:
        tag_sha = git("rev-parse", "--short", tag + "^{}") or "?"
        tag_at = parse_iso(git("log", "-1", "--format=%cI", tag) or "")
        out.append("  last ship         %s  (%s, %s)" % (tag, tag_sha, fmt(tag_at)))
        unshipped = (git("log", "--oneline", "%s..HEAD" % tag) or "").splitlines()
    else:
        out.append("  last ship         NO ship/* TAG -- run /bonesaw-ship to start tagging")
        problems.append("no ship tag: cannot tell what players have")
        unshipped = (git("log", "--oneline", "-20") or "").splitlines()

    # --- 2. what is committed -------------------------------------------
    out.append("  git HEAD          %s  (%s)" % (head, fmt(head_at)))
    out.append("")
    if unshipped:
        problems.append("%d commit(s) not shipped" % len(unshipped))
        out.append("  UNSHIPPED (%d commit%s)" % (len(unshipped), "" if len(unshipped) == 1 else "s"))
        for line in unshipped:
            out.append("    %s" % line[:100])
    else:
        out.append("  UNSHIPPED         none -- git matches the last ship")
    out.append("")

    dirty = [l for l in (git("status", "--porcelain") or "").splitlines()
             if l and not l.startswith("??")]
    if dirty:
        problems.append("%d uncommitted file(s)" % len(dirty))
        out.append("  UNCOMMITTED (%d)" % len(dirty))
        for l in dirty[:10]:
            out.append("    %s" % l)
        if len(dirty) > 10:
            out.append("    ... and %d more" % (len(dirty) - 10))
    else:
        out.append("  uncommitted       none")

    # --- 3. what players are running ------------------------------------
    built = parse_iso(run(["docker", "image", "inspect", "-f", "{{.Created}}", WORLDSERVER_IMAGE]) or "")
    running = run(["docker", "inspect", "-f", "{{.State.Status}}", "ac-worldserver"])
    if built is None:
        out.append("  worldserver       image not found (docker down, or never built)")
        problems.append("no worldserver image")
    else:
        after = [l for l in (git("log", "--format=%h %cI %s",
                                 "--since", built.astimezone(timezone.utc).isoformat()) or "").splitlines()]
        state = "container %s" % (running or "not running")
        out.append("  worldserver       image built %s, %s" % (fmt(built), state))
        if after:
            problems.append("running build predates %d commit(s)" % len(after))
            out.append("                    BEHIND: %d commit(s) landed after this build" % len(after))
            for l in after[:5]:
                out.append("                      %s" % l.split(" ", 2)[-1][:80])
        else:
            out.append("                    up to date with HEAD")

    # --- 4. database ------------------------------------------------------
    missing = sql_not_imported()
    if missing is None:
        out.append("  pending SQL       database unreachable, not checked")
    elif missing:
        problems.append("%d SQL file(s) not imported" % len(missing))
        out.append("  pending SQL       %d file(s) NOT imported" % len(missing))
        for m in missing[:10]:
            out.append("                      %s" % m)
    else:
        out.append("  pending SQL       all imported")

    # --- 5. client --------------------------------------------------------
    rel = run(["gh", "release", "view", "--json", "tagName,publishedAt"], cwd=REPO)
    if rel:
        try:
            j = json.loads(rel)
            published = j.get("tagName", "?").lstrip("v")
            ok = "OK" if published == version else "MISMATCH -- players pull %s" % published
            out.append("  client release    latest GitHub release %s  %s" % (j.get("tagName", "?"), ok))
            if published != version:
                problems.append("GitHub release %s != Bonesaw.version %s" % (published, version))
        except (ValueError, AttributeError):
            out.append("  client release    could not parse gh output")
    else:
        out.append("  client release    gh unavailable, not checked")

    out.append("")
    if problems:
        out.append("  NOT SHIPPED CLEAN:")
        for p in problems:
            out.append("    - %s" % p)
    else:
        out.append("  Everything committed, built, imported and published. Nothing pending.")

    print("\n".join(out))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
