# Bonesaw schema snapshot

`CREATE TABLE` statements for the 71 tables that exist in the live databases
and **cannot be created from anything else in this repository**.

Captured 2026-09-02 from the running realm.

## Why this exists

The `lg_*` (Living Gear), `ap_*` (attunement) and `z_archive_*` tables were
created by revision SQL files that were later deleted from the repo. What
remains under `data/sql/updates/pending_db_characters/` are only *incremental*
changes -- `ALTER TABLE ... ADD COLUMN` and friends -- which assume the tables
already exist. The module code only ever reads and writes them; it never
creates them.

The practical consequence was that these 71 table definitions existed in
exactly one place on Earth: the live MySQL data directory. Losing it would have
meant losing the schema for Living Gear, the bug-report system, the vault, and
the attunement tables, with no way to rebuild them from source.

Verified: importing into a fresh database gets 111 of 174 `acore_characters`
tables without this file, and all 174 with it.

## What this is NOT

This is a **recovery artifact**, not part of the update pipeline. Nothing
applies it automatically -- the updater only reads `data/sql/base/db_*` and the
configured update directories, and this directory is neither.

It is deliberately kept out of the automatic path because it is a snapshot of
the *final* structure, so replaying the incremental updates on top of it fails:

    ERROR 1060 (42S21): Duplicate column name 'auto_attune_ilvl'
    ... applying rev_auto_attune_min_ilvl.sql

That is the remaining piece of work. Standing up a realm from an empty database
needs the snapshot to be paired with rows in each database's `updates` table
marking the already-baked-in revisions as applied -- which is exactly what
AzerothCore's own `base/` + `updates/` split does. Until that is done, treat
this file as insurance rather than as an installer.

## Restoring by hand

    mysql -uroot -p acore_characters < acore_characters_schema.sql
    mysql -uroot -p acore_world      < acore_world_schema.sql

Every statement is `CREATE TABLE IF NOT EXISTS`, so running it against the live
database is a no-op.
