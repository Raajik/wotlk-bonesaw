# Handoff: the realm now runs on lohk, and the Warsong Hold CPU bug

Written 2026-09-06. Every number below was measured on the host named, not
assumed. Read this before touching the server or wondering where it went.

## The one-line summary

**The realm moved off this desktop (`zahir`) to the Unraid box (`lohk`).** Two
things turned up on the way that matter more than the move itself:

- a stock AzerothCore content bug burning ~23% of worldserver CPU on a fight
  nobody could see (Warsong Hold), since fixed; and
- **`MapUpdate.Threads = 1`** — AzerothCore's untuned default, and the actual
  performance ceiling all along. Raising it to 3 took the realm from a
  saturated single core at 211 bots to 2.2 cores at **349** bots with *better*
  diff. This applies to any AzerothCore install, zahir included.

## Where things live now

| thing | location |
|---|---|
| running realm | `lohk:/mnt/boot/appdata/bonesaw` |
| source tree (build here) | `zahir:/home/muckfup/wow-bonesaw` |
| compose runner on lohk | Compose Manager Plus plugin (`docker compose v5.5.0`) |
| realm address | `100.89.249.40:8215` (`acore_auth.realmlist` id 1) — IP, not a name, see below |
| client launcher | `zahir:~/.local/bin/bonesaw` |

Only these are copied to lohk — **not** the source tree, `var/`, `tools/`, or
the build logs:

```
docker-compose.yml  docker-compose.override.yml  .env
conf/dist/env.ac    env/dist/etc/   env/dist/logs/
lua_scripts/        modules/mod-playerbots/
data/mysql/         data/client-data/
```

## Ports

8085 is sabnzbd on lohk and 7878 is radarr, so both moved. **Neither is
client-visible**: the world port comes from `acore_auth.realmlist`, not from
`realmlist.wtf`. Only the auth port and the realm address matter to a client,
and auth was free.

| service | zahir | lohk |
|---|---|---|
| auth | 3724 | 3724 (unchanged — client-facing) |
| world | 8085 | **8215** |
| SOAP | 7878 | **7979** |
| mysql | 3306 | 3306 |

## The Warsong Hold bug

**Symptom.** `Aura: 45577 / 45587 ... could not find empty unit visible slot`
at ~2.5/sec — 100% of log output, ~216k lines/day. Appeared only after ~3 days
of uptime, which is why it looked intermittent.

**What it actually was.** Not a leak. Game event 92 *"What the Cold Wind
Brings..."* runs 30 minutes of every hour, forever (`occurence 60`,
`length 30`). During it:

- the Warsong Captain (25446) gets `Set Invincibility Hp 1`, so the fight can
  never resolve, and
- Ith'rix (25453) summons Nerub'ar Sky Darkeners (25451) as
  `TEMPSUMMON_CORPSE_TIMED_DESPAWN` (type 6), whose timer only starts **after
  death**.

With no players in Borean Tundra nothing died and nothing despawned, so
summons accumulated every hour until one Captain held 255 live auras —
`MAX_AURAS`, the whole `uint8` slot space.

**How it was proven**, rather than guessed — gdb on the live worldserver,
breakpoint at `SpellAuras.cpp:88`:

```
target           Warsong Captain (25446), map 571, TYPEID_UNIT
visible auras    255      (map full)
stale (removed)  0        <- NOT a slot leak; all 255 live
distinct spells  2        -> 45577 x190, 45587 x65
distinct casters 188      (all entry 25451)
max per caster   2
```

188 casters x up to 2 debuffs = exactly 255. Visible slots are keyed per
(spell, caster), so every extra attacker consumes another slot.

**The fix.** Upstream had already found and fixed this:
[issue #27203](https://github.com/azerothcore/azerothcore-wotlk/issues/27203),
fixed by [PR #27207](https://github.com/azerothcore/azerothcore-wotlk/pull/27207)
(merged 2026-08-19). Our fork tracks `mod-playerbots/azerothcore-wotlk`, which
had picked up the event (upstream PR #24351, 2026-08-04) but not the fix.

Backported verbatim at upstream's own path and filename so it dedupes on a
future merge:

```
data/sql/updates/pending_db_world/rev_1787007899449162800.sql
```

**Measured effect on zahir**, before vs after, both at steady state:

| | before | after |
|---|---|---|
| worldserver CPU (mean) | 91.8% | 70.5% |
| RSS | 5.17 GiB | 3.61 GiB |
| `Update time diff` >100ms | median 116, p90 204 | none |
| log growth | ~216k error lines/day | none |

Note `MinRecordUpdateTimeDiff = 100` — the server only *logs* diff above
100ms. "No diff entries" means diff is under 100ms, not that diff is unmeasured.

## The realm address must be an IP, not a MagicDNS name

`acore_auth.realmlist.address` is resolved **by the authserver**, inside its
container, on lohk. It cannot be a `*.ts.net` name there:

```
Could not resolve address lohk.tail5dde0e.ts.net for realm "AzerothCore" id 1
```

Unraid's Tailscale plugin runs with `--accept-dns=false` (`CorpDNS: false`), so
lohk's `/etc/resolv.conf` points at the router and MagicDNS is never consulted
— even though MagicDNS is enabled tailnet-wide. Containers inherit that. The
desktop *does* accept Tailscale DNS, so the name resolves there, which makes
this look like a client problem when it is entirely server-side.

Realm 1 therefore holds the **tailnet IP `100.89.249.40`**, which needs no DNS
at all. `tailscale set --accept-dns=true` on lohk would also work, but changes
the resolver for every other container on that host to fix one database string
— not worth it.

The client's `realmlist.wtf` still uses the **name** (`lohk.tail5dde0e.ts.net`)
and that is fine: the client resolves the *auth* address itself, on a host that
does accept Tailscale DNS. The two are independent. If a player's machine
cannot resolve MagicDNS, point them at `100.89.249.40` instead.

Symptom to recognise: client config looks perfectly correct, realm list is
empty or unreachable. Check `docker logs ac-authserver` before touching the
client.

## Gotchas that cost time, so they are written down

**Containers run as uid 1000 (`acore`).** Any bind-mount directory created by
root over SSH is root-owned and the container cannot write it. This silently
broke `env/dist/logs` — no `Server.log` at all, and greps against it returned
empty, which reads exactly like "no errors". The container *does* warn at
startup (`cannot touch .write-test: Permission denied`), so read more than the
last few lines of a startup. Fix: `chown -R 1000:1000 env/dist/logs`.

**`data/mysql` is uid 999 with 750 subdirs.** Copying it needs `sudo` to read
and `--numeric-ids` to preserve ownership, or MySQL cannot read its own files
on the far end:

```
sudo rsync -a --numeric-ids -e "ssh -i ~/.ssh/id_ed25519_lohk" \
  data/mysql/ root@10.0.0.10:/mnt/boot/appdata/bonesaw/data/mysql/
```

Stop the stack first. Copying a live InnoDB directory gives a corrupt database.
Worldserver and authserver first, then the database; confirm `Shutdown
complete` in `docker logs ac-database` before copying.

**`docker compose build` replaces the `:master` tag**, and the previously
tagged image can then be garbage-collected even while a container still runs
it. The running container survives, but it can no longer be recreated from
that image. Check `docker inspect ac-worldserver --format '{{.Image}}'` against
the tag before any `compose up`.

**`ac-db-import` does not bind-mount the source tree** — it applies SQL baked
into its image. A new file in `data/sql/updates/` is invisible to it until that
image is rebuilt. Both hosts currently carry a `db-import` image predating the
Warsong fix; harmless while the DB is copied wholesale, but a from-scratch
world DB rebuild would reintroduce the bug. `docker compose build ac-db-import`
closes it.

**Image IDs differ between hosts after `save`/`load`** and that is not a
corruption signal — zahir uses the containerd snapshotter, lohk uses Btrfs, and
they compute IDs differently. Compare content instead:

```
docker run --rm --entrypoint sh acore/ac-wotlk-worldserver:master \
  -c 'sha256sum /azerothcore/env/dist/bin/worldserver'
```

## Deploying a code change

Build on zahir, ship the image. **Do not compile on lohk** — it is a 15W
i5-1145G7 and this is an hours-long build.

```
cd ~/wow-bonesaw
docker compose build ac-worldserver
docker save acore/ac-wotlk-worldserver:master | ssh -C lohk 'docker load'
ssh lohk 'cd /mnt/boot/appdata/bonesaw && docker compose up -d ac-worldserver'
```

Transfer of all four custom images took 85s over LAN. `mysql:8.4` pulls from
Docker Hub; every other image must be hand-carried, because the upstream
`db-import` image would try to apply upstream SQL to our fork's database.

## Clients

`realmlist.wtf` must say `set realmlist lohk.tail5dde0e.ts.net`. The launcher
writes it on every launch, to **every** `Data/*/realmlist.wtf` — the client
ships both enGB and enUS and a stale one makes it silently connect nowhere.

```
bonesaw --set-realm lohk.tail5dde0e.ts.net   # stored in ~/.config/bonesaw/realm
BONESAW_REALM=... bonesaw                    # one-off override
```

The spare client on `/run/media/muckfup/bok/Games/WoW 3.3.5/Bonesaw` was not
updated; point `--set-dir` at it and the launcher will fix it.

## Performance: the real bottleneck was `MapUpdate.Threads = 1`

**AzerothCore updates every map on a single thread by default, and nobody had
ever changed it.** That one line was the whole performance story. On the
7600X's fast core it was survivable; on a 15W mobile i5 it saturates, which is
why the move initially looked like "this hardware is too slow".

Measured on lohk, full bot population (`MinRandomBots 150` / `MaxRandomBots 350`):

| | `Threads = 1` | `Threads = 3` |
|---|---|---|
| bots online | 211 | **349** (at the cap) |
| total CPU | 98.9% (~1 core, saturated) | 213.9% (~2.2 cores) |
| busiest thread | ~100% | **84.5%** |
| per-thread | — | 69.6 / 84.5 / 69.6 |
| diff (sampled headline) | 136-201ms | 116-163ms |
| diff mean / median | — | **16ms / 2ms** |
| diff p95 / p99 | — | **~52ms / ~62ms** |
| RSS | 4.79 GiB | 4.83 GiB |
| host load (8 threads) | — | 2.62 |

65% more bots, slightly better typical diff, nothing saturated. Staying at 3:
bots are already capped, so a 4th thread would only shave diff a little while
widening the concurrency surface across custom module code.

### How it was found

Stack-sampling the busy thread — the same poor-man's-profiler trick that
settled the Warsong bug. Find the hot thread on the host, map it into the
container, then attach gdb a few times and look at where it actually is:

```
cat /proc/<host-tid>/status | grep NSpid      # host TID -> container LWP
docker exec -u 0 ac-worldserver gdb -p 1 -batch -nx -x /tmp/prof.gdb
```

Five samples with zero players and zero bots: two idle, one in
`ScriptMgr::OnCreatureUpdate` iterating the `AllCreatureScript` map, two in
VMAP ray intersection (`BIH::intersectRay` -> `WorldModel::GetLocationInfo`)
reached from `WorldObject::UpdatePositionData` <- `Map::CreatureRelocation` <-
`Unit::UpdateSplineMovement`. All inside a single `MapUpdater::WorkerThread`.
Creature movement doing full terrain/collision queries, all on one thread.

### Two traps in measuring this

**CPU% is not a headroom metric here.** `botActiveAloneSmartScale` raises bot
activity until diff climbs off its 50ms floor, so the worldserver trends toward
saturating whatever it is given. Extra capacity shows up as **more active
bots at the same diff**, not as lower diff or spare CPU. Judge by diff.

**Bot population is not the lever it looks like.** Cutting 211 -> 100 bots
bought 3% CPU, because the constraint was a thread, not a population. That
experiment is why `MinRandomBots`/`MaxRandomBots` are back at 150/350.

### Read the whole diff block, not the headline

`Update time diff: NNNms` is **one instantaneous tick**, logged only when it
exceeded `MinRecordUpdateTimeDiff` (100ms), at most once per
`RecordUpdateTimeDiffInterval` (5 min). It is the worst tick at each
checkpoint by construction — a biased sample, useless as a health measure.

`UpdateTime.cpp:169` prints four more lines immediately after it, which are the
actual distribution:

```
Update time diff: 124ms with 1 players online
Last 500 diffs summary:
|- Mean: 16ms
|- Median: 2ms
|- Percentiles (95, 99, max): 50ms, 60ms, 124ms
```

Measured across 33 checkpoints at 349 bots on lohk with `Threads = 3`:
**mean 12-20ms, median 1-2ms, p95 33-66ms, p99 47-81ms.** That is a healthy
world loop. p95 sitting at ~50ms is `smartScaleDiffLimitfloor` — the scaler
holding the distribution exactly where configured.

The three 400-600ms values in the log are single ticks out of ~16,000 sampled,
and none has recurred in the 18 checkpoints since. They were never a problem;
they were the tail of a good distribution, read without its context.

`grep -aA4 "Update time diff:"` — always take the whole block.

### Open

- **Thread safety is unproven.** 25 minutes clean proves nothing; races in the
  `AllCreatureScript` path (Living Gear runs there) surface on timing. An
  unexplained worldserver restart should suspect `MapUpdate.Threads` first —
  revert with `env/dist/etc/worldserver.conf.bak-mapthreads`.


## Rollback

The zahir stack is stopped but complete — containers, images, all 22GB of
`data/`. Nothing was deleted.

```
cd ~/wow-bonesaw && docker compose start
bonesaw --set-realm 127.0.0.1
# and set acore_auth.realmlist id 1 back to zahir.tail5dde0e.ts.net:8085
```

## Open items

- **`MapUpdate.Threads = 3` is on trial.** Clean for 25 minutes under full load
  at time of writing, which proves very little. Watch for unexplained
  worldserver restarts over the following days and revert first if one appears.
- The 400-600ms diff values turned out to be single ticks in a healthy distribution (median 2ms) - resolved, see the performance section.
- The whole session's changes were applied straight to the live realm without
  `tools/restart_worldserver.ps1`'s warn/save (nobody was connected, verified 0
  in `acore_auth.account`) and **no `ship/X.Y.Z` tag was cut** for any of it.
- `46bab7ff7d` (the SQL fix) sits on `fix/warsong-sky-darkener-despawn`, not
  merged to `main`.
- `db-import` images stale on both hosts, as above.
- lohk has **no array and no cache pool**: one 238GB NVMe that is also the boot
  device, holding docker.img and every container's appdata, with no parity. The
  realm's database now lives there too. Deliberate per the owner, recorded here
  because it is a single point of failure with no redundancy.
