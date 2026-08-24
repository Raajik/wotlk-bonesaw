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

5. **Never guess. Verify, then theorise.** State a hypothesis as a
   hypothesis, then go and check it before acting on it or reporting it.
   The logout crash cost three wrong fixes this way: a crash dump read
   `X: -8377 Y: -2757`, that got called "Stormwind" without ever being
   checked, and an entire theory about a bot druid buffing across a city
   was built on top of it. The coordinates were Burning Steppes and the
   player was alone. Reading `Unit::CleanupsBeforeDelete` -- ten seconds of
   actual evidence -- showed `CombatStop()` runs two lines AFTER
   `m_cleanupDone` is set, which was the answer all along.

   The database, the source under `src/server/`, and the worldserver log
   are all right there. Use them. "I believe X" in a report must mean it
   was checked, and if it could not be checked, say so in the same breath.

6. **Ask how it shares to alts.** The account is the player's persona.
   Whenever a feature could sensibly be account-wide, the question gets
   asked out loud in the design, and the answer gets written down --
   even if the answer is "per character, on purpose".

   This is not hypothetical bookkeeping. Perks are owned by the account
   but spells are learned per character, and nobody asked which side of
   that line each piece belonged on; the result was that only the FIRST
   character on an account ever got a perk spell. One real account owned
   70 perks whose five characters knew 20, 14, 11, 14 and 2 of them.
   Bug reports #33, #36, #37, #39 and #40 were all that one unasked
   question.

   Progress on one character helping every other one is a reward for the
   grind, not a leak to be plugged. Default to sharing.

## Documentation map

- `modules/mod-living-gear/ARCHITECTURE.md` -- what each source file owns, the
  three kinds of perk id, the account/character split, and the invariants that
  keep getting violated. Read this before changing perks or attunement.
- `modules/mod-living-gear/ATTUNEMENT-REDESIGN.md` -- attunement and item
  leveling as built, including the wrong turns and why they were wrong.
- `modules/mod-living-gear/BACKLOG.md` -- ordered by importance. The
  spec-by-spec class audit is item 1.
- `modules/mod-living-gear/core-patches/*.core-patch` -- one per core-engine
  change, describing what and why.
- `tools/bug-reports/README.md` -- how player reports reach Discord.
- `tools/client-update/README.md` -- how a client patch reaches players.
- `A:/obsidian/jeremy/wiki/Bonesaw.md` -- durable learnings, appended every
  ship. The single densest source of "do not repeat this mistake".

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
