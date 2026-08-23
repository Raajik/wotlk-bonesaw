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
gear bank a slice of its own current stats on every level-up, ramping from 1% at
item level 1 to 100% at `Attune.CapLevel = 25`. Past 25 the item keeps growing
its worn stats up to `MaxLevel = 50` but stops adding to the account.

## The actual problem

The two systems were never really two systems. **Item level is the attunement
clock**, so levelling a piece both makes it stronger and banks it. That is why
"do I wear this or file it?" has no clean answer, and why the collection keeps
grabbing things the player wanted to equip.

The fix is to separate the *jobs*, not to retune the numbers:

- **Item leveling** — the piece you are wearing gets stronger. Per-instance,
  personal, immediate.
- **Attunement** — the permanent account-wide bonus, earned by acquiring and
  spending copies.

Two currencies, two jobs, no competition.

## Decisions

**1. Full decouple.** Item leveling no longer feeds attunement at all.
`Attune.CapLevel`, `Attune.IlvlBaseline` and `Attune.IlvlFloorScale` stop
governing anything and the level-up hook stops writing to `lg_absorb`.

**2. Attuning consumes the item.** Peloria's model. The item is destroyed when
it credits.

**3. Because it consumes, it must be deliberate.** This follows from (2) and is
the part that undoes the original complaint. Auto-attune on loot is removed:
with consumption, auto-attuning what you loot destroys gear before the player
has decided whether to wear it, which is strictly worse than the armory
round-trip it was meant to avoid. Loot goes to bags like anything else, and the
player attunes when they choose — the existing `ATTUNE|` command becomes a bulk
"attune everything eligible in my bags" action.

`lg_account_meta.auto_attune_on` / `auto_attune_off` are retired.

**4. Per-copy contribution scales with rarity.**

| Source | Per copy | Copies to 100% |
|---|---|---|
| Epic and above | 50% | 2 |
| Rare | 25% | 4 |
| Uncommon | 10% | 10 |
| Common / crafted | 25% | 4 |

A flat rate was considered and rejected. At a flat 5% a raid drop needs twenty
copies and a crafted belt needs twenty copies, but only one of those is
obtainable twenty times — so the best items in the game would sit permanently
near zero while cheap craftables capped out. Rarity scaling makes everything max
in a comparable number of *acquisitions*, and still lands crafted gear at four
copies, which was the target that made crafting attractive in the first place.

## Schema

`lg_absorb` keys on `(account_id, item_entry)` already, which is the right
grain. It needs one column:

```sql
ALTER TABLE `lg_absorb` ADD COLUMN `attune_pct` SMALLINT UNSIGNED NOT NULL DEFAULT 0;
```

`attune_pct` is 0–100. The stat columns become *the item's full stats*, and what
the account actually receives is `stats * attune_pct / 100`, computed at apply
time. Storing the full value rather than the scaled slice means topping up a
partially attuned item is a single addition to `attune_pct` and never a
re-derivation of the stats.

## Migrating the 20,549 existing rows

They were earned and must not evaporate. Existing rows already hold a *scaled*
slice — whatever the item had banked by its level. Set `attune_pct = 100` for
every existing row and leave the stats as they are: the player keeps exactly the
bonus they have today, and the item counts as fully attuned so they are never
asked to farm copies of something they already finished.

That is generous to a handful of accounts and it is the right call — the
alternative is silently reducing people's stats to fit a new formula.

## Open questions

- **Where does the bulk attune live?** A button in the Armory tab is the least
  work and needs no world object. A physical NPC or object is more evocative and
  matches Peloria, but is another errand.
- **Does the Curator still auto-file gear into the collection?** It should
  probably keep doing that — filing is not attuning any more, so it is no longer
  destructive and no longer conflicts with wanting to wear something.
- **Crafted detection.** "Crafted" is not an item flag; it means "some recipe
  produces this". Deriving it needs a startup index over the spell reagent /
  create-item data, the same shape as `BuildQuestItemIndex`. Until that exists,
  crafted items fall into their rarity band, which for most greens and blues is
  10% or 25% anyway.
