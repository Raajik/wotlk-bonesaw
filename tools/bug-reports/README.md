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

The worldserver never contacts Discord or GitHub. It only writes rows.

## GitHub tracking

The same scheduled digest now makes GitHub the work tracker. Before posting a
new Discord notification it:

1. searches for an existing `[Report #N]` or `[Feature #N]` issue (safe for
   historical manually-mirrored reports),
2. creates one when none exists,
3. stores the issue number and URL on `lg_bug_report`, and
4. includes the GitHub link in the Discord notification.

Creation is idempotent: a retry searches before creating, and the database has
a unique issue-number key. Only `open` and `attempted` reports are backfilled;
resolved history is left alone unless it already has a matching issue.

Bug reports receive `bug`; feature requests receive `enhancement`. Workflow
labels are created/updated automatically:
`source:in-game`, `status:needs-triage`, `status:awaiting-retest`,
`status:verified`, and explicit resolution labels. Closing through
`bug_resolve.py` updates the database, edits the original Discord message, and
updates the linked GitHub issue. A GitHub outage never blocks in-game intake;
the row remains unlinked and is retried on the next run.

The repository is public. Issue bodies deliberately omit account IDs. They do
include character name, in-game coordinates and selected target because that
context is what makes an in-game report actionable.

## Delivering them

`bug_digest.py` posts anything with `posted = 0` to Discord and then marks
those rows posted.

```
python tools/bug-reports/bug_digest.py            # post anything new
python tools/bug-reports/bug_digest.py --dry-run  # print, change nothing
python tools/bug-reports/bug_digest.py --test     # post a test message only
```

`--dry-run` and `--test` never create or modify GitHub issues.

### The scheduled task

The task calls `run_digest_hidden.vbs`, not `run_digest.cmd` directly. That
shim exists purely to launch the batch file with window style 0 -- calling the
`.cmd` straight from the task threw a console window into the foreground every
fifteen minutes, on top of whatever was on screen. The task has to stay
"Interactive only" (the digest reaches the database through `docker exec`, so
it cannot work when nobody is logged on), and an interactive task showing a
console is Windows behaving as designed. The shim is the fix; do not repoint
the task back at the `.cmd`.

A Windows scheduled task named **`Bonesaw Bug Digest`** runs
`run_digest.cmd` every 15 minutes. That wrapper exists so the task has one
stable thing to call and so every run is logged to `bug_digest.log` beside
it.

```
schtasks /Query /TN "Bonesaw Bug Digest" /V /FO LIST     # check it
schtasks /Run   /TN "Bonesaw Bug Digest"                 # force a run
schtasks /Change /TN "Bonesaw Bug Digest" /DISABLE       # pause it
```

It runs as `jeremy` and only while that user is logged on, which is
correct: the digest reaches the database through `docker exec`, so it
cannot work when Docker Desktop is not running anyway.

The failure mode worth guarding against is the task quietly stopping.
Reports keep accumulating and nobody notices, because a silent Discord
channel looks exactly like "no bugs today". Check `bug_digest.log` — it
records a timestamp and exit code for every run, whether or not anything
was posted.

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

Marking a report **edits its original Discord message in place** — the
description is struck through and the resolution appended — rather than posting
a second message nobody would connect to the first. Webhooks can edit their own
messages, which is why `bug_digest.py` now posts one message per report and
records the id; batching several into one message would make striking a single
report impossible.

Reports posted before that change have no id on file. They are still updated in
the database, and the tool says so rather than failing.

**Use `attempted` when that is the truth.** #15 (solid chests) was instrumented,
not solved. Calling that "fixed" is a lie that costs someone an afternoon when
it comes back.

Where this fits: resolve reports as part of the ship that carries the fix, so
the database, GitHub issue and Discord channel match what players actually
have. Use `attempted` for a shipped diagnostic/change awaiting proof; use
`fixed` only when that is the honest final state.

## Durability

Every active report exists in four places, and losing Discord loses none of them:

1. **`lg_bug_report`** — the authoritative copy. Rows are *never deleted*,
   including after posting; `posted` only tracks delivery. This table can
   rebuild the Discord channel from scratch.
2. **GitHub** — the canonical triage and work tracker.
3. **Discord** — a notification and conversation mirror.
4. **The worldserver log** — a line is written at report time, so a report
   survives even the characters database being rolled back.

Delivery cannot lose a report. Rows are marked `posted = 1` only *after*
Discord returns success, so a failed run leaves them queued for the next
one rather than dropping them, and a re-run cannot double-post. Discord
rejects any message over 2000 characters with a bare HTTP 400, so batches
are split at 1800.

To re-post history into a fresh channel, clear the flag for the range you
want and let the next run pick it up:

```sql
UPDATE lg_bug_report SET posted = 0 WHERE id BETWEEN 1 AND 50;
```

## The webhook

Put the Discord webhook URL in `discord.webhook` in this directory, one
line, no quotes. See `discord.webhook.example`.

That filename is covered by the `*.webhook` rule in `.gitignore` and must
never be committed. If the file is absent the script exits quietly, so a
scheduled run on a machine without the secret does nothing rather than
failing.

Note this is a *different* webhook from `tools/client-update/discord.webhook`,
which posts patch notes. Do not cross them.
