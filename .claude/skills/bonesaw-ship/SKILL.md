---
name: bonesaw-ship
description: Ship accumulated Bonesaw work to the live realm and to players - build, warn, save, replace worldserver, import SQL, publish the client, tag, and post Discord notes. Only ever invoked by the user typing /bonesaw-ship.
disable-model-invocation: true
---

# Ship Bonesaw

The user has explicitly asked to ship -- `disable-model-invocation` means this
cannot start any other way. This is the only skill allowed to touch the live
realm. Work through the phases in order and **stop at the first
failure** - do not carry on to a later phase to "get most of it out".

Announce which phase you are entering as you go, so an interrupted ship can be
resumed from a known point.

## Phase 0 - preflight

```
python tools/bonesaw_status.py
```

- Working tree must be clean. If it is not, commit first (do not stash - the
  stash stack is shared with other worktrees).
- Show the user the unshipped commit list. That list *is* the patch notes
  source; do not invent items that are not in it.
- If `UNSHIPPED` is empty and the server is not behind, there is nothing to
  ship. Say so and stop.

Decide the version: bump the patch number in `tools/client-update/Bonesaw.version`.
**Never skip a number**, including for server-only ships - the launcher and the
Discord thread both key off it.

## Phase 1 - build before anything live moves

```
docker compose build ac-worldserver ac-db-import
```

**Both images. This is not optional and it is the bug that keeps recurring.**
`ac-db-import` bakes `data/sql/updates/pending_db_*` *into the image* - there is
no bind mount. Rebuilding only `ac-worldserver` means every new migration is
silently skipped forever: on 2026-08-22 `rev_living_gear_account_keys.sql` had
been sitting unapplied since 08-21 because of exactly this, so the account key
ring was dead while its code read perfectly.

If the build fails, **stop here**. Nothing live has been touched yet, which is
the entire reason the build comes first. Fix the compile error and restart the
ship.

## Phase 2 - warn and save

Skip entirely if `ac-worldserver` is not running.

**First, count REAL players online.** The countdown exists to stop players
taking a rollback; with nobody to roll back, five minutes of announcements to an
empty realm is just five minutes. Playerbots do not count - they have no
progress a restart can cost them.

```
docker compose exec -T ac-database mysql -uroot -ppassword -N -e "SELECT COUNT(*) FROM acore_characters.characters c JOIN acore_auth.account a ON a.id = c.account WHERE c.online = 1 AND a.username NOT LIKE 'RNDBOT%';"
```

Bot accounts are all named `RNDBOT*` (144 of 147 accounts at the time of
writing), so excluding them is what makes this count mean "humans". Do NOT use
the worldserver console's `server info` for this - its "Connected players"
includes bots, so it reads non-zero on an empty realm.

**If that count is 0:** save and restart immediately, no announcements.

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/save_world.ps1
```

The save still runs. It costs about three seconds and it is not a warning - it
protects world and bot state, and a stale `online` flag left by an earlier crash
is the one case where this count could be wrong in the dangerous direction. If
it is wrong the other way (a human logs in during the restart) they get a clean
reconnect, not a rollback.

**If that count is greater than 0:** the full countdown, no shortcuts.

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/restart_worldserver.ps1
```

Announces in game on a staged countdown (5 minutes, then 2, 1, 30s, 10s), then
`saveall`. **Wait for its final line**, `OK: players warned ... and saved;
restart is allowed`. An exit code of 0 is not the same thing: on 2026-08-23 that
script was killed partway, exited 0, and had never reached the saveall - the
missing final line was the only evidence. Never use AzerothCore
`server shutdown` for a docker replace - it races `saveall` on its own timer.

Run it in the background rather than in the foreground, or a tool timeout will
kill it mid-countdown, which is exactly how that happened.

## Phase 3 - replace the server

```
docker compose up -d ac-db-import
docker compose up -d ac-worldserver
```

db-import first and let it complete; worldserver depends on it. Then confirm
the module actually loaded and no migration errored:

```
docker logs --tail 200 ac-worldserver | grep -iE "living gear|error|Applying"
```

Re-run `python tools/bonesaw_status.py` - `pending SQL` must now read
`all imported`. If it does not, the db-import image did not pick the file up;
go back to Phase 1.

## Phase 4 - client, if client files changed

Needed when anything under `modules/mod-living-gear/client_addon/`,
`tools/client-patch/` or `tools/launcher/` changed since the last ship:

```
git diff --stat ship/<previous>..HEAD -- modules/mod-living-gear/client_addon tools/client-patch tools/launcher
```

If it is empty, skip to Phase 5 - a server-only ship still bumps the version
and still gets Discord notes.

Otherwise follow `tools/client-update/README.md` under "Ship to all players".
That file is the source of truth for these steps; do not reproduce them from
memory. The order that matters: build the launcher, create the GitHub release,
then `--verify`, then push the manifest. The exe is not byte-reproducible, so
rebuilding between hashing and uploading strands every player.

Push **only** to origin `Raajik/wotlk-bonesaw`, never to the `playerbots`
remote. Never force-push `main`.

## Phase 5 - record the ship

```
git commit -am "Ship Bonesaw X.Y.Z: <one line>"
git tag ship/X.Y.Z
```

The tag is what makes the next `/bonesaw-status` able to say what players have.
Without it the whole thing goes blind again.

## Phase 6 - tell people

Post to Discord. Title `Bonesaw X.Y.Z - patch notes`, built from the Phase 0
commit list, following `.cursor/rules/discord-patch-notes.mdc`: player-facing
only, ASCII, plain `-` bullets, grouped by theme, no file paths or spell IDs,
only what actually shipped. Extra jump is disabled - do not advertise it.

If the client changed, tell players to close Wow and run `Bonesaw.exe`.

Then append durable learnings to `A:\obsidian\jeremy\wiki\Bonesaw.md` -
crashes, UI rules, deploy gotchas, spell IDs, do-not-repeat mistakes. Part of
shipping, not after it.

## Finally

Re-run `python tools/bonesaw_status.py`. It should exit 0 with
"Everything committed, built, imported and published." If it does not, say
exactly what is still outstanding rather than calling the ship done.
