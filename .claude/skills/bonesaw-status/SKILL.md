---
name: bonesaw-status
description: Report what is committed, built, imported and published for Bonesaw, and what is still pending. Use when asked "what's pending", "what's live", "did that ship", "where are we", or before starting a ship. Read-only and safe on a live realm.
---

# Bonesaw status

Run:

```
python tools/bonesaw_status.py
```

Report its output to the user, then interpret it. Exit code 0 means everything
is shipped and consistent; 1 means something is pending.

## What the four lines mean

The tool compares things that drift apart independently. They are not the same
question and they fail separately:

| Line | Question it answers |
|---|---|
| `last ship` | What version did players actually get told about? (newest `ship/*` tag) |
| `UNSHIPPED` | What is committed but has never gone out? |
| `worldserver` | What are players actually running right now? |
| `pending SQL` | What migrations does the database still not have? |

`worldserver ... BEHIND: N commits` is the one that has bitten this project
most. It means the running binary was compiled before those commits landed, so
the code exists in git, reads correctly, and is not running. On 2026-08-22 the
image had been built one minute *before* the newest commit.

`pending SQL ... NOT imported` is the second. See the warning in
`bonesaw-ship` about `ac-db-import`: SQL is baked into that image, so a
worldserver-only rebuild leaves new migrations permanently unapplied. A perk
whose `spell_dbc` row never landed cannot unlock, and until recently that
failed silently.

## Answering follow-ups

- What changed since the last ship: `git log ship/<version>..HEAD`
- What a specific ship contained: `git log ship/0.1.47..ship/0.1.48`
- Whether one file changed since a ship: `git log ship/<version>..HEAD -- <path>`

## Do not

Do not fix anything from this skill. It reports. If the user wants the pending
work shipped, that is `/bonesaw-ship`.
