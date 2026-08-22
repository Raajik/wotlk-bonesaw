# CLAUDE.md

Read `AGENTS.md` first. It is the full set of project rules and this file does
not replace it. This file exists because `AGENTS.md` and `.cursor/rules/*.mdc`
are read by Cursor and **not** by Claude Code, so for a long time Claude
sessions ran against this repo without ever seeing any of it.

## The four that must never be missed

1. **Hold live until an explicit ship.** Writing code, pending SQL and client
   sources is the default. Nothing reaches the realm or players until the user
   says ship, deploy, push live or release. See `/bonesaw-ship`.

2. **Build to verify; never deploy to verify.**
   `docker compose build ac-worldserver ac-db-import` writes images and does
   not touch the running container. Do it at the end of every change set. Work
   that has never compiled is not ready to deploy.
   **Both images** -- `ac-db-import` bakes `data/sql/updates/pending_db_*` in
   with no bind mount, so rebuilding worldserver alone skips new migrations
   permanently and silently.

3. **Warn and save before replacing worldserver.**
   `powershell tools/restart_worldserver.ps1` announces in game, waits 45
   seconds, then `saveall`. Only then `docker compose up`. Players take
   sizeable rollbacks otherwise.

4. **Every ship gets a `ship/X.Y.Z` git tag.** It is the only record of what
   players actually have. `git log ship/<latest>..HEAD` is the pending list.

## Where things are

- Real module source: `modules/mod-living-gear/`. The `main` branch has only a
  stub -- work happens on `Playerbot`, checked out here at `A:/wow-bonesaw`.
- Core-engine edits live as `.core-patch` files in
  `modules/mod-living-gear/core-patches/` and as tracked copies of the patched
  files under `src/server/`.
- SQL: write only in `data/sql/updates/pending_db_*/`. Everything else under
  `data/sql/` is immutable.
- Client addon: `modules/mod-living-gear/client_addon/LivingGear/`.
  Client-facing strings are ASCII only.

## Commands

- `/bonesaw-status` -- what is committed, built, imported, published, pending.
  Read-only, safe any time. Run it when asked "did that ship?".
- `/bonesaw-ship` -- the only thing allowed to touch the live realm, and only
  on an explicit request.

## Working on the addon protocol

Client and server talk over addon whispers. Every "this button does nothing"
bug in this project has been a client command with no server handler. There is
one dispatcher, `DispatchAddonCommand` in `LivingGear.cpp`; add commands there
and nowhere else. It logs anything unhandled, so check the worldserver log
before assuming logic is wrong.
