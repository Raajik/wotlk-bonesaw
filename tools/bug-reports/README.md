# In-game bugs and feature requests

Players file reports in game:

- `.bug <what went wrong>` — works for everyone, no addon needed.
- `/bugreport <what went wrong>` — the addon's slash command. Not `/bug`,
  which is a stock WoW command that opens Blizzard's own report frame and
  files into a table nobody reads.
- `.feature <what you would like>` — feature request without the addon.
- `/featurerequest <what you would like>` — the addon's feature-request command.

All four commands write a row into
`acore_characters.lg_bug_report`, capturing the description plus the
report type, reporter's name, level, map, zone, exact coordinates and current target.
That context is most of the value — "the chest doesn't open" is close to
unactionable on its own.

The worldserver never contacts GitHub. It only writes rows.

## GitHub tracking

A scheduled sync makes GitHub the work tracker. For each report it:

1. searches for an existing `[Report #N]` or `[Feature #N]` issue (safe for
   historical manually-mirrored reports),
2. creates one when none exists,
3. stores the issue number and URL on `lg_bug_report`.

Creation is idempotent: a retry searches before creating, and the database has
a unique issue-number key. Only `open` and `attempted` reports are backfilled;
resolved history is left alone unless it already has a matching issue.

Bug reports receive `bug`; feature requests receive `enhancement`. Workflow
labels are created/updated automatically:
`source:in-game`, `status:needs-triage`, `status:awaiting-retest`,
`status:verified`, and explicit resolution labels. Closing through
`bug_resolve.py` updates the database and the linked GitHub issue. A GitHub
outage never blocks in-game intake; the row remains unlinked and is retried on
the next run.

The repository is public. Issue bodies deliberately omit account IDs. They do
include character name, in-game coordinates and selected target because that
context is what makes an in-game report actionable.

## Delivering them

`bug_sync.py` files a GitHub issue for every `open` or `attempted` report that
does not have one yet.

```
python tools/bug-reports/bug_sync.py            # file anything unlinked
python tools/bug-reports/bug_sync.py --dry-run  # print, change nothing
```

Reaching the database means reaching the Docker daemon that runs
`ac-database`. On lohk that is local and nothing extra is needed; from a
workstation, `export DOCKER_HOST=ssh://lohk` first.

### No more Discord

Until 2026-09-07 the same job also mirrored every new report into a Discord
channel, and `bug_resolve.py` struck the message through when the report was
closed. Both halves were removed. GitHub is the canonical tracker, the
notifications were noise, and the delivery path had been dead since the realm
moved to lohk without anyone noticing — which is the argument against a
notification channel, not for one.

The `posted` and `discord_message_id` columns still exist on `lg_bug_report`.
Nothing reads them; they are left alone rather than migrated away, since the
table is append-only history.

### The scheduled job

The sync runs on **lohk**, not on a workstation, because that is the box that
is always up and holds the database. It is an Unraid User Scripts entry named
`bonesaw-bug-sync`, on a `*/15 * * * *` schedule, logging to
`/boot/config/bonesaw/bug_sync.log`.

The failure mode worth guarding against is the job quietly stopping: reports
keep accumulating and nobody notices, because an empty tracker looks exactly
like "no bugs today". That is not hypothetical — it is what happened between
2026-09-03 and 2026-09-07, and six reports sat unfiled. Check the log, which
records a timestamp and exit code for every run.

```
ssh lohk 'tail -20 /boot/config/bonesaw/bug_sync.log'
ssh lohk '/boot/config/bonesaw/run_bug_sync.sh'      # force a run
```

## Closing reports

Reports were write-once until 2026-08-22: they arrived, went to Discord, and
nothing ever marked one done. `bug_resolve.py` closes that loop.

```
python tools/bug-reports/bug_resolve.py                      # what is open
python tools/bug-reports/bug_resolve.py --all                # everything
python tools/bug-reports/bug_resolve.py 21 fixed "guarded quest items"
python tools/bug-reports/bug_resolve.py 15 attempted "instrumented, not solved"
python tools/bug-reports/bug_resolve.py 4 wontfix "needs client DBC work"
python tools/bug-reports/bug_resolve.py 21 open              # reopen
```

Marking a report updates the row and the linked GitHub issue: the status label
changes and the resolution note is added as a comment. A report with no linked
issue is still updated in the database, and the tool says so rather than
failing.

**Use `attempted` when that is the truth.** #15 (solid chests) was instrumented,
not solved. Calling that "fixed" is a lie that costs someone an afternoon when
it comes back.

Where this fits: resolve reports as part of the ship that carries the fix, so
the database and the GitHub issue match what players actually have. Use `attempted` for a shipped diagnostic/change awaiting proof; use
`fixed` only when that is the honest final state.

## Durability

Every active report exists in three places:

1. **`lg_bug_report`** — the authoritative copy. Rows are *never deleted*.
   This table can rebuild the GitHub tracker from scratch.
2. **GitHub** — the canonical triage and work tracker.
3. **The worldserver log** — a line is written at report time, so a report
   survives even the characters database being rolled back.

Delivery cannot lose a report. The issue number is stored on the row only
*after* GitHub confirms creation, so a failed run leaves reports queued for the
next one rather than dropping them, and a re-run searches before creating, so
it cannot file a duplicate.

## Credentials

Filing issues needs `gh` authenticated. On lohk the token lives at
`/boot/config/bonesaw/gh.token` (mode 600, flash-persistent) and the run script
exports it as `GH_TOKEN`. It is a fine-grained PAT scoped to `Issues: read and
write` on this repository only — deliberately not the workstation's OAuth
token, which carries `repo` and `workflow` across every repo you own.

Never commit a token, echo one into a log, or paste one into a chat transcript.
