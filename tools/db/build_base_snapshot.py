#!/usr/bin/env python3
"""Regenerate the Bonesaw half of the from-scratch database install.

A clone of this repository can build a worldserver, and upstream's data/sql
lets it build upstream's databases. What it cannot do on its own is create
Bonesaw's own 71 tables: the CREATE TABLE statements for Living Gear, the
attunement system and the bug tracker were in revision files that were later
deleted, and what survives under data/sql/updates/pending_* is only the
incremental ALTERs that assume the tables already exist.

This script writes that missing half, reading the live database as the source
of truth:

    data/sql/base/db_characters/bonesaw_base.sql
    data/sql/base/db_world/bonesaw_base.sql

AzerothCore's DBUpdater::Populate() applies every .sql file in the base
directory, but only when the database is empty. So these files run exactly
once, on a fresh install, and are inert against a database that already
exists. Nothing here can touch the live realm.

Each generated file contains three sections:

  1. CREATE TABLE IF NOT EXISTS for that database's Bonesaw tables, in the
     structure the live realm currently has.

  2. INSERT for the config tables only. A table is config if it is not keyed
     by account_id or guid -- perk prices, achievement overrides, the reagent
     whitelist, zone scaling, world content. Player data (lg_item's 336k rows,
     characters, bug reports) is deliberately not seeded; a new realm starts
     empty.

  3. Rows in `updates` marking every pending_* revision as already applied,
     hash-matched to the current file contents.

Section 3 is the part that is easy to mistake for a hack, so: it is load
bearing, and replaying those files instead is not an option. The updater
applies updates in alphabetical order, but these files were written -- and
applied to live -- in chronological order, and the two disagree.
rev_living_gear_report_flags.sql adds a column AFTER `report_type`, which is
created by rev_living_gear_support_github_sync.sql, and 'r' sorts before 's'.
A fresh database replaying them alphabetically fails. Because section 1
already carries the final structure those revisions would have produced, and
section 2 carries the data they would have inserted, marking them applied is
the accurate statement: their effects are present.

The pending files themselves are never modified. That matters -- editing one
changes its hash, which makes the updater re-apply it on the next ship, and
rev_auto_attune_min_ilvl.sql contains `UPDATE lg_account_meta SET
auto_attune_on = 0`. Re-applying that would silently switch auto-attune off
for every account on the realm.

Usage:
    python tools/db/build_base_snapshot.py                 # read live via docker
    python tools/db/build_base_snapshot.py --container X --password Y

Re-run this whenever Bonesaw's schema changes, i.e. after a ship that added or
altered an lg_* table. tools/db/bonesaw_tables.txt says how to refresh the
table list itself if new tables appear.
"""

import argparse
import hashlib
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
TABLE_LIST = REPO / "tools" / "db" / "bonesaw_tables.txt"

# database -> (base output file, pending updates directory)
TARGETS = {
    "acore_characters": (
        REPO / "data/sql/base/db_characters/bonesaw_base.sql",
        REPO / "data/sql/updates/pending_db_characters",
    ),
    "acore_world": (
        REPO / "data/sql/base/db_world/bonesaw_base.sql",
        REPO / "data/sql/updates/pending_db_world",
    ),
}

# `updates` is created by upstream's own base files, but base files are applied
# in directory order, which is not guaranteed. Create it defensively so section
# 3 cannot fail on ordering.
UPDATES_DDL = """CREATE TABLE IF NOT EXISTS `updates` (
  `name` varchar(200) NOT NULL,
  `hash` char(40) DEFAULT '',
  `state` enum('RELEASED','CUSTOM','MODULE','ARCHIVED','PENDING') NOT NULL DEFAULT 'RELEASED',
  `timestamp` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `speed` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
"""


def mysql(args, container, password, db=None, dump=False):
    cmd = ["docker", "exec", container, "mysqldump" if dump else "mysql",
           "-uroot", f"-p{password}"]
    if dump:
        cmd += ["--skip-comments", "--compact", "--single-transaction"]
    if db:
        cmd += [db] if dump else ["-N", "-e", args, db]
    if dump:
        cmd += args if isinstance(args, list) else [args]
    elif not db:
        cmd += ["-N", "-e", args]
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        sys.exit(f"mysql failed: {r.stderr.decode()[:400]}")
    return r.stdout.decode("utf-8", "replace")


def is_config(db, table, container, password):
    """Config tables are not keyed by a player or account identifier."""
    cols = mysql(
        "SELECT LOWER(COLUMN_NAME) FROM information_schema.COLUMNS "
        f"WHERE TABLE_SCHEMA='{db}' AND TABLE_NAME='{table}'",
        container, password).split()
    keyed = {"account_id", "guid", "account", "owner_guid", "char_guid", "player_guid"}
    return not (set(cols) & keyed)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--container", default="ac-database")
    ap.add_argument("--password", default="password")
    args = ap.parse_args()

    tables = {}
    for line in TABLE_LIST.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        db, table = line.split(".", 1)
        tables.setdefault(db, []).append(table)

    for db, (out_path, pending_dir) in TARGETS.items():
        names = sorted(tables.get(db, []))
        if not names:
            continue

        parts = [
            f"-- Bonesaw base install for `{db}`.",
            "--",
            "-- GENERATED by tools/db/build_base_snapshot.py -- do not hand-edit.",
            "-- Applied by DBUpdater::Populate() only when the database is empty,",
            "-- so this file is inert against an existing realm.",
            "",
            "-- ---------------------------------------------------------------",
            f"-- 1. Structure: {len(names)} tables that nothing else in the repo creates",
            "-- ---------------------------------------------------------------",
            "",
        ]

        structure = mysql(["--no-data", "--skip-add-drop-table"] + names,
                          args.container, args.password, db=db, dump=True)
        parts.append(structure.replace("CREATE TABLE `", "CREATE TABLE IF NOT EXISTS `"))

        config = [t for t in names if is_config(db, t, args.container, args.password)]
        parts += [
            "",
            "-- ---------------------------------------------------------------",
            f"-- 2. Seed data for {len(config)} config tables",
            "--    (player-owned tables are intentionally left empty)",
            "-- ---------------------------------------------------------------",
            "",
        ]
        if config:
            data = mysql(["--no-create-info", "--complete-insert"] + config,
                         args.container, args.password, db=db, dump=True)
            parts.append(data if data.strip() else "-- (all config tables empty)")

        pend = sorted(p for p in pending_dir.glob("*.sql")) if pending_dir.is_dir() else []
        parts += [
            "",
            "-- ---------------------------------------------------------------",
            f"-- 3. Mark {len(pend)} pending revisions as applied",
            "--    Their structure is in section 1 and their data in section 2.",
            "--    They cannot be replayed: the updater runs files alphabetically",
            "--    but these were written chronologically, and the orders differ.",
            "-- ---------------------------------------------------------------",
            "",
            UPDATES_DDL,
        ]
        for p in pend:
            sha = hashlib.sha1(p.read_bytes()).hexdigest().upper()
            parts.append(
                f"INSERT INTO `updates` (`name`, `hash`, `state`) VALUES "
                f"('{p.name}', '{sha}', 'PENDING') "
                f"ON DUPLICATE KEY UPDATE `hash` = VALUES(`hash`);")

        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("\n".join(parts) + "\n")
        print(f"{out_path.relative_to(REPO)}: {len(names)} tables, "
              f"{len(config)} seeded, {len(pend)} revisions marked applied")


if __name__ == "__main__":
    main()
