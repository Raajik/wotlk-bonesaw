# Attunement and item leveling: the redesign

Decided 2026-08-23. Not built yet — this is the spec.

## The power fantasy this serves

**Soloing every dungeon and raid.** Rough at the start, snowballing to
"I can solo anything" over days-to-weeks of play.

This is the design intent, not a side effect, and it is written at the top
because everything below only makes sense in its light. In particular: the
runaway growth further down is the *goal*. Anyone reading this later and
reaching for a nerf because attuned stats dwarf equipped stats should stop and
check this section first — that is the system working.

What the shape needs to be:

- **Rough start.** Early attunement is worth little, so the first dungeons are
  played honestly.
- **Accelerating middle.** Each milestone raises the rate on *everything already
  banked*, so clearing content retroactively multiplies the whole collection.
  That is the snowball, and it is why milestone pacing — not the 5% — is the
  main tuning dial.
- **Plateau at "solo anything."** Not unbounded for its own sake; unbounded
  until the fantasy is delivered.

Soloing a 25-man boss means roughly 10–25x a normal player's throughput and
survivability. Item leveling to ilvl 284 supplies about 2x of that on its own,
so attunement has to carry the rest — which is the arithmetic that justifies
both the uncapped rate and the milestone multiplier.

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

### Uncapped, because the snowball is the product

Flat 5% scales with *your* gear quality: 200 attuned epics is 10 epics' worth of
stats before milestones multiply it. At a 25% rate that is 50 epics' worth.

That is not a bug to be capped. See the top of this document — the point is to
reach "solo any raid", and 10–25x throughput is what that costs. An earlier
draft of this spec proposed a guardrail here; it was wrong for this game and the
correction is recorded so the same instinct does not resurface as a nerf.

**Milestones are the accelerator.** Each one raises the rate on everything
already banked, so a player who collected broadly and then cleared all heroics
gets an across-the-board jump rather than a trickle. Collect wide early, then
each clear retroactively multiplies the lot. Tune pacing there first.

What to actually watch, and it is a *schedule* question rather than a danger one:

- **Too slow** if a dedicated player cannot solo heroic 5-mans within a week.
- **Too fast** if raids fall before the player has seen most of the content —
  the snowball should arrive at the end of the journey, not replace it.

Both are answerable with one query against `lg_absorb` plus a look at what that
account has actually cleared.

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

## Alts ride the snowball, on purpose

Attunement is keyed on `account_id`, so it already works this way — a new
character inherits the whole collection the moment it is created. Recorded here
as **intent** rather than an accident, because it is the reward for the grind:
progress on one character helping every other one is the point, not a leak.

This is also the honest answer to "rough at the start". The rough start is a
thing you experience **once, per account**. An alt on an established account
should feel enormously powerful immediately, and that is correct.

## The milestone list

Base rate 5% per attuned item. Each milestone below adds **+5%**, to a ceiling
of 100%.

That ceiling is not arbitrary: 100% per item is exactly what the *current*
continuous system already grants an item that reaches level 25. So a
fully-decorated account ends up where today's ceiling already sits — what
changes is that you get there by clearing content rather than by grinding every
individual item up a curve.

### Tier 1 — the first week

| Milestone | Why |
|---|---|
| Reach level 80 on any character | The obvious entry fee |
| Clear every WotLK normal dungeon | Teaches the dungeon set |
| Attune 50 distinct items | Rewards the system's own loop |
| Max any one gathering profession | Cheap, and it pairs with the reagent bank |

### Tier 2 — committed

| Milestone | Why |
|---|---|
| Clear every WotLK heroic dungeon | The named one; the natural next rung |
| Attune 150 distinct items | Breadth is the main axis, so it earns a rung |
| Max two crafting professions | Gives crafting the pull it lost with copy-counting |
| Exalted with every WotLK faction | Long but never blocked |
| Level a single item to 50 | Makes the other half of the redesign matter |

### Tier 3 — dedicated

| Milestone | Why |
|---|---|
| Clear every WotLK 10-man raid | The named one |
| Clear every Classic raid (MC, BWL, AQ40, Naxx40) | Old content becomes worth entering |
| Clear every TBC raid | Same |
| Attune 300 distinct items | The point where the collection is genuinely deep |
| Ambassador — every capital exalted | Classic time sink, no skill wall |
| Explore Azeroth, Outland and Northrend | Pure time, zero difficulty, pleasant |

### Tier 4 — the long haul, and the wild ones

| Milestone | Why |
|---|---|
| Clear every WotLK 25-man raid | The real end of the PvE ladder |
| Every battleground reputation exalted | Brutal, and it drags PvP into the system |
| Salty | Fishing title; absurd, beloved, entirely doable |
| The Insane | The signature "you did WHAT" achievement |
| Collect 100 mounts | Broad, slow, touches everything |
| Loremaster | The largest quest grind in the game |
| Glory of the Raider, any tier | Skill rather than time, unlike most of this list |

Twenty-two milestones at +5% reaches 115%, so the 100% ceiling means the last
few are for pride rather than power. That is deliberate — a player should be
able to skip the two or three they hate.

**Design note on the mix.** Most of these are *time*, not *skill*, on purpose:
the fantasy is a snowball you can see coming, not a wall. Glory of the Raider is
the one real skill check and it sits at the very end where it is optional.

## Open questions

- **Armory redesign.** Bulk attune belongs on the first page of the Armory,
  since it will be used constantly. The Armory's current first page was not laid
  out with a primary action button in mind, so this needs a pass — probably the
  same treatment the perks panel got.
- **Achievement IDs.** Every milestone above is an achievement lookup and none of
  the IDs are gathered yet. They come from `achievement_dbc` / the DBC, and per
  the rank-id lesson they get read out of the data rather than recalled.
