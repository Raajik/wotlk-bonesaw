# Handoff: make wotlk-bonesaw a repo that can be cloned and built

Written 2026-09-02. Everything below was measured on the Linux host, not
assumed. Start a fresh session with this file.

## The one-line problem

**The buildable source for the live realm exists only on this machine.** Git
tracks 400 files; the tree the server is built from has 30,806. A clone of
`Raajik/wotlk-bonesaw` cannot build a worldserver, and nothing records which
upstream AzerothCore commit the local tree came from.

## What was measured

| fact | value |
|---|---|
| `git ls-files` | 400 files |
| files on disk (excl. `.git`, `build*`, `env`, `var`) | 30,806 |
| untracked, not ignored | 710 |
| commits on `Playerbot` | 487 |
| first commit | `a23f80384 Add Living Gear module, client patch tools, and GitHub updater` |
| `git merge-base HEAD playerbots/master` | **NONE** — unrelated histories |

Tracked vs on-disk, for the things a build needs:

| path | on disk | tracked |
|---|---|---|
| `src/` | yes | **52** of ~600 |
| `apps/` | yes | 0 |
| `deps/` | yes | 0 |
| `conf/` | yes | 0 |
| `CMakeLists.txt` | yes | 0 |
| `docker-compose.yml` | yes | 0 |
| `modules/` | yes | 87 |
| `data/sql/` | yes | 100 |

`docker-compose.yml` builds worldserver/authserver/db-import with
`context: .` and `dockerfile: apps/docker/Dockerfile`, so the image is compiled
from the whole on-disk tree — most of which git has never seen.

This repo was never a fork. The first commit adds Bonesaw's own work, and it
shares no ancestry with `playerbots/master`. It has always been an *overlay*: a
handful of modified AzerothCore files living alongside a full working copy that
git ignores. Most of the tree is hidden by `.gitignore` rules (`/conf/*`,
`/modules/*`, `/build*/`); `src/` is not ignored, which is why 546 files there
show up as untracked noise and made `git status` useless for the ship
preflight's "working tree must be clean" check.

## Why this is worth fixing

1. **Single point of failure.** Lose this disk and you lose the exact source
   that produces the running realm. The 52 tracked `src/` files are not enough
   to rebuild it.
2. **No reproducibility.** No upstream SHA is recorded anywhere. I checked two
   core files against the last 400 upstream commits and neither matched, so the
   baseline is either older than that or locally modified — and there is no way
   to tell which.
3. **Changes are invisible.** The 52 tracked files are whole-file copies, not
   patches. You cannot see what Bonesaw changed versus what upstream shipped,
   and a future upstream bump would overwrite or be overwritten with no conflict
   to review.
4. **Onboarding is impossible.** Nobody else can build this.

## Options

**A. Become a real fork (recommended).** Track the whole tree, with upstream as
a remote you merge from. This is how AzerothCore forks normally work, and it is
the direct answer to "how should a project like this function".
*Cost:* repo grows to a few hundred MB; upstream updates become real merges with
real conflicts (which is the point — conflicts are the signal you currently
lack).

**B. Pinned upstream + scripted overlay.** Record the upstream SHA in a file and
ship a script that clones upstream at that SHA and copies the overlay on top.
*Cost:* keeps the repo small and the separation explicit, but it is a bespoke
workflow, and whole-file overlays still hide drift.

**C. Submodule + patch series.** Upstream as a pinned submodule, Bonesaw's core
changes as `.patch` files applied at build time.
*Cost:* the most honest representation of "our changes", but patch conflicts on
every upstream bump are genuinely painful for 52 files.

Recommendation: **A**. B and C both preserve the property that makes this
fragile — that the buildable tree is assembled rather than stored.

## Plan for A

### Phase 0 — safety net (do not skip)

- `tar` the entire working tree to external storage. This is the only copy of
  the buildable source.
- `git push origin Playerbot` and confirm `ship/0.1.132` is pushed.
- Record the running image: `docker images --no-trunc | grep worldserver` and
  `docker inspect ac-worldserver --format '{{.Image}}'`. This is the artifact to
  compare against.
- Do **not** delete anything in this phase.

### Phase 1 — establish the baseline

The on-disk tree is the ground truth; treat it as such.

- Find the closest upstream commit: `git fetch playerbots`, then diff the
  on-disk tree against candidate commits and pick the one with the smallest
  diff. Check `git log playerbots/master --before=<date of first Bonesaw core
  edit>` for candidates.
- Write the chosen SHA down in the repo (e.g. `UPSTREAM_BASELINE`). Even if the
  match is imperfect, a recorded approximate baseline beats none.
- The diff between that commit and the on-disk tree **is** Bonesaw's real core
  change set. Expect it to be larger than the 52 tracked files — that gap is
  the thing this whole exercise exists to expose.

### Phase 2 — build the new branch

- `git checkout -b fork-migration playerbots/<baseline-sha>`
- Copy the on-disk tree over it (excluding `.git`, `build*`, `env`, `var`,
  `azerothcore/`, and the client MPQ artifacts already ignored).
- Commit as one "Import the live tree" commit. The diff against the baseline is
  now reviewable — read it, because it is the first time anyone has seen the
  full set of local modifications.
- `git merge --allow-unrelated-histories Playerbot` to bring the 487 Bonesaw
  commits and their history in. Resolve in favour of the imported tree for
  `src/`, and in favour of `Playerbot` for `tools/`, `data/sql/` and `modules/`.

### Phase 3 — fix .gitignore

Current rules hide build-critical files. `/conf/*` and `/modules/*` need
narrowing so that upstream sources are tracked while genuinely local artifacts
(`build*/`, `env/`, `var/`, `azerothcore/`, `data/mysql/`, the `.logs/` and
`.build*.log` rules added in 0.1.132) stay ignored.

Target state: `git status` on a clean tree is **empty**. That is what makes the
ship preflight meaningful again.

### Phase 4 — verify before cutting over

Non-negotiable, in order:

1. `git clone` the new branch into `/tmp` — a genuinely fresh clone, not a copy.
2. `docker compose build worldserver` in that clone must succeed.
3. Start it against a **copy** of the database, not the live one, and confirm
   the realm comes up and a character can log in.
4. `python tools/bonesaw_status.py` still reports correctly.
5. `python tools/client-patch/build_patch.py` and
   `python tools/launcher/build_launcher.py` still work (both are cross-platform
   as of 0.1.132 — StormLib via `libstorm.so`, launcher via
   `x86_64-pc-windows-gnu`).

Only after all five: make the new branch the default and update anything that
references `Playerbot` by name.

### Phase 5 — rollback

Nothing is deleted. `Playerbot` and every `ship/*` tag stay where they are, and
the Phase 0 tarball is the floor. If Phase 4 fails, stay on `Playerbot` and the
realm is unaffected.

## Decisions needed from Raajik first

1. **Which upstream baseline?** `mod-playerbots/azerothcore-wotlk` master at
   what point — latest, or the commit that best matches the on-disk tree?
2. **Branch name.** Keep `Playerbot`, or take the opportunity to move to `main`?
   The ship tooling and `bonesaw_status.py` reference the current branch.
3. **Repo size.** A full fork is a few hundred MB. Acceptable?
4. **Upstream cadence.** Should upstream merges become a routine thing, or is
   this a one-time baseline capture?

## Things not to break

- `tools/ship_bookkeeping.py`, `tools/bonesaw_status.py` and the `.claude/skills`
  all assume the current layout and the `Playerbot` branch.
- `tools/client-patch/build_patch.py` reads its bases from
  `tools/client-patch/cache/` (gitignored) and needs
  `--refresh-glue-bases <client>/Data` on a fresh clone.
- `tools/launcher/payload/` MPQs are what get baked into `Bonesaw.exe`; the exe
  is **not** byte-reproducible, so never rebuild it between hashing and
  uploading.
- The live realm is served from the running container. None of this work should
  touch it until Phase 4 passes.
