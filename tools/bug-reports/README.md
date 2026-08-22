# Bug reports

Players file reports in game:

- `.bug <what went wrong>` — works for everyone, no addon needed.
- `/bugreport <what went wrong>` — the addon's slash command. Not `/bug`,
  which is a stock WoW command that opens Blizzard's own report frame and
  files into a table nobody reads.

Either way the worldserver writes a row into
`acore_characters.lg_bug_report`, capturing the description plus the
reporter's name, level, map, zone, exact coordinates and current target.
That context is most of the value — "the chest doesn't open" is close to
unactionable on its own.

The worldserver never contacts Discord. It only writes rows.

## Delivering them

`bug_digest.py` posts anything with `posted = 0` to Discord and then marks
those rows posted. Run it on a schedule — six hours is what it was built
for:

```
python tools/bug-reports/bug_digest.py            # post anything new
python tools/bug-reports/bug_digest.py --dry-run  # print, change nothing
python tools/bug-reports/bug_digest.py --test     # post a test message only
```

Rows are marked posted only after Discord accepts the message, so a failed
run repeats rather than loses. Reports are kept after posting; the table is
the record of what was reported.

## The webhook

Put the Discord webhook URL in `discord.webhook` in this directory, one
line, no quotes. See `discord.webhook.example`.

That filename is covered by the `*.webhook` rule in `.gitignore` and must
never be committed. If the file is absent the script exits quietly, so a
scheduled run on a machine without the secret does nothing rather than
failing.
