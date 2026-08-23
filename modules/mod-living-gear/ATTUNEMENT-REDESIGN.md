# Attunement and item leveling: the redesign

Decided 2026-08-23. Not built yet — this is the spec.

## What exists today

Verified against the live database, not recalled:

| System | Table | State |
|---|---|---|
| Item leveling | `lg_item` | **178,728 item instances**, 958 owners, top level 42 of 50 |
| Attunement | `lg_absorb` | 20,549 rows, 96 accounts, ilvl 1–30 |
| Sacrifice attune | — | legacy; `AbsorbPct = 0.10`, nothing in the client sends it |

Continuous attunement (the 2026-08-20 redesign) has equipped and Curator-tracked
gear bank a slice of its own stats on every level-up, ramping 1% at item level 1
to 100% at `Attune.CapLevel = 25`.

## The problem

The two systems were never really two systems. **Item level is the attunement
clock**, so levelling a piece both makes it stronger and banks it. That is why
"wear it or file it" has no clean answer, and why the collection kept grabbing
gear the player wanted to equip.

The fix is to separate the *jobs*:

- **Item leveling** — the piece you wear becomes powerful. Per-instance.
- **Attunement** — a small permanent account bonus, per unique item, once.

They stop competing and become a genuine **choice**: level this item into
something strong, or spend it for a permanent slice. Once leveling can carry an
item to ilvl 284, wearing is properly attractive, so the decision has teeth.

---

## Part 1 — item leveling scales item level

Today growth is a flat multiplier: `(base + roll) * (1 + (level-1) * 0.10)`,
reaching 5.9x at level 50. That does not map to anything a player recognises.

Instead, levelling raises an **effective item level**:

```
effective_ilvl(level) = base_ilvl + (level - 1) * IlvlPerLevel
```

At `IlvlPerLevel ≈ 5.4` a level-1 quest green reaches 284 by level 50. The
formula keeps going past that for anyone who wants to push further.

### Stats come from a budget table, not a multiplier

Scaling stats by `effective_ilvl / base_ilvl` is wrong, and it is worth writing
down why: an ilvl 20 green with 8 stamina would reach 23x = 184 stamina, while
an ilvl 200 epic with 60 stamina reaches 2.3x = 138. The green would beat the
epic. Ratio scaling inverts gear value.

So the item's stats are set to **the budget for its effective ilvl**, distributed
in the item's own proportions. Real numbers, chest slot, from `item_template`:

| ilvl | 20 | 60 | 187 | 200 | 226 | 264 |
|---|---|---|---|---|---|---|
| avg primary stats | 2.3 | 33.4 | 129.9 | 164.6 | 214.4 | 316.1 |

**That curve accelerates.** ~0.8 stats/ilvl at the bottom, ~2.4 at the top. A
linear slope would badly undervalue high-ilvl gear, so the budget table is built
at startup from `item_template` itself — median primary-stat total per ilvl per
inventory type — and extrapolated past 284 using the top slope. Same shape as
`BuildQuestItemIndex`. Self-calibrating, no magic numbers, correct per slot.

### Convergence is intended

`IlvlPerLevel` is the same for everything, so a quest green and a raid epic
approach the same ceiling; the epic simply starts closer. An item you love stays
viable forever, which is the point of the heirloom comparison. The loot ladder
still matters for *how fast* you get there, not for the destination.

### XP cost

Already quadratic above `attuneCapLevel` (`level² / 2`). That stays, and the cap
level constant is repurposed as the point where the curve steepens rather than
the point attunement stops. Late levels should be a real investment.

---

## Part 2 — attunement is per unique item, plus milestones

**Every unique item gives 5% of its stats, once.** Milestones raise that rate by
+5% each — all WotLK heroics cleared, all 10-man raids, and so on.

### Why not copy-counting

The previous draft of this spec had rarity-scaled copies (epic 50%, rare 25%,
uncommon 10%). It is recorded here because the reason it was dropped matters:

**one-and-done items break it.** Quest rewards are greens you can acquire
exactly once. So are BoP raid drops. Under copy-counting a player holds gear
they can *never* finish attuning, which is the worst feeling a collection system
can produce. Exempting quest rewards at 100% patches the one case named and
leaves every other unique item broken, while adding a rule players must learn.

Flat-per-unique dissolves it. Nothing is ever unfinishable, there is no "should
I save this?", no farming treadmill, and the whole rarity rate table disappears.

It also changes what the system rewards — **breadth** (how much distinct gear
you have seen) and **achievement** (what you have cleared) instead of repetition.

### Attuning consumes the item

Kept from the earlier decision, and it now reads better than it did. Since each
item entry credits only once, **duplicates are the natural attune fodder** — the
second Frostmourne is worth nothing to wear and 5% forever if spent. That quietly
preserves a farming and crafting incentive without needing a third axis.

Consumption means attuning must be **deliberate**. Auto-attune on loot is
removed: destroying gear before the player has decided whether to wear it is
strictly worse than the armory round-trip it was meant to avoid. The existing
`ATTUNE|` command becomes a bulk "attune eligible items in my bags" action.
`lg_account_meta.auto_attune_on` / `auto_attune_off` are retired.

### No cap, deliberately — and what to watch

Flat 5% scales with *your* gear quality: 200 attuned epics is 10 epics' worth of
free stats before milestones multiply it. At a 25% rate that is 50 epics' worth.
This ships uncapped on purpose, to get real data rather than guess a ceiling.

**Revisit when any account passes roughly 300 attuned entries, or when attuned
stats exceed equipped stats.** Both are one query. Clawing stats back later is
noticed by players, so the trigger for looking is written down here rather than
left to memory.

---

## Schema

`lg_absorb` already keys on `(account_id, item_entry)`, which is the right grain.

```sql
ALTER TABLE `lg_absorb` ADD COLUMN `attune_pct` SMALLINT UNSIGNED NOT NULL DEFAULT 0;
```

Stat columns hold the item's **full** stats; the account receives
`stats * attune_pct / 100`, computed at apply time. Storing full values means the
milestone rate change is a single global multiplier, not a rewrite of 20,549 rows.

Milestone progress needs its own small table keyed by account.

## Migrating the existing 20,549 rows

They were earned. Set `attune_pct` to the current milestone rate and keep the
stored stats as the item's full value.

The alternative — recomputing everyone under the new formula — silently reduces
stats people already have. Being generous to 96 accounts is the cheaper mistake.

## Open questions

- **Where the bulk attune lives.** A button in the Armory tab is cheapest and
  needs no world object; an NPC or altar matches Peloria and reads better but is
  another errand. Still undecided.
- **Which milestones, and in what order.** "All WotLK heroics" and "all 10-man
  raids" are the two named so far. Needs a full list before building, since each
  one is an achievement-ID lookup.
