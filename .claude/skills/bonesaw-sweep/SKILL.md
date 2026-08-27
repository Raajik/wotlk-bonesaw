---
name: bonesaw-sweep
description: Work the open GitHub bug/feature queue for Bonesaw. Use when asked to "sweep bugs", "work the queue", "look at open reports", "what needs fixing", or any bug/feature sweep. Read-only until a fix is chosen; shipping is /bonesaw-ship only.
---

# Bonesaw queue sweep

GitHub is the canonical tracker. The in-game DB (`acore_characters.lg_bug_report`)
is intake/audit; Discord is a notification mirror. Start from GitHub, not the
DB or Discord.

## Entry point

```
gh issue list --repo Raajik/wotlk-bonesaw --state open --label source:in-game --limit 100 \
  --json number,title,labels,updatedAt
```

- `bug` label = bug report, `enhancement` = feature request.
- `status:needs-triage` = new, never reproduced/inspected.
- `status:awaiting-retest` = change shipped as `attempted`; needs gameplay
  proof before it can close.
- No status label = historical manual mirror; triage as needed.

Issue bodies carry the captured context (reporter, level, zone, coords,
target entry) and a `<!-- bonesaw-report-id:N -->` marker mapping back to
`lg_bug_report.id`.

## Sweep order (house rule)

1. `status:awaiting-retest` first -- ask the user what their gameplay testing
   showed, or close out with evidence from logs.
2. `status:needs-triage` bugs second, features after bugs.
3. Oldest first within a tier unless the user picks one.

## Working an issue

- Reproduce or read worldserver logs BEFORE theorizing (see
  azerothcore-module-dev: decode SpellCastResult codes, bucket log lines).
- Never guess a fix; instrument and ask for retest when evidence is thin.
- Fix on `Playerbot`. Build to verify (`docker compose build ac-worldserver
  ac-db-import` -- both, always). Never deploy without /bonesaw-ship.

## Closing the loop

Use the resolver -- it updates DB + strikes the Discord message + updates the
GitHub issue in one step:

```
python tools/bug-reports/bug_resolve.py <id> fixed "Shipped in X.Y.Z - <what changed>"
python tools/bug-reports/bug_resolve.py <id> attempted "<instrumented, awaiting retest>"
python tools/bug-reports/bug_resolve.py <id> wontfix|duplicate "<reason>"
```

- `fixed` only after gameplay verification (or a deterministic fix with
  incontrovertible log evidence). Honest `attempted` beats optimistic `fixed`.
- `attempted` adds `status:awaiting-retest` on GitHub and keeps the issue open.
- The `<id>` is the DB report id (`bonesaw-report-id` marker in the issue),
  NOT the GitHub issue number. They differ for backfilled reports
  (e.g. DB #98 -> issue #97).
- GitHub-side discussion, assignment and milestones happen on the issue
  itself; only status transitions flow back through the resolver.

## Do not

- Do not create GitHub issues by hand for in-game reports -- the digest does
  it idempotently on its 15-minute run (or run
  `python tools/bug-reports/bug_digest.py` to force it).
- Do not edit the issue title's `[Report #N]` / `[Feature #N]` prefix; the
  router keys on it.
- Do not ship from this skill. Sweep -> fixes committed -> user says ship ->
  /bonesaw-ship.

## Related skills

- `bonesaw-status` answers what is committed/built/imported/published.
- `bonesaw-ship` owns the live realm.
- `azerothcore-module-dev` holds the debugging discipline for individual fixes.
