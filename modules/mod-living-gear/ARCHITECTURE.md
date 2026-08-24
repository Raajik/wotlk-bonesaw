# mod-living-gear: how it fits together

Written 2026-08-23, current as of ship 0.1.70. ~14,600 lines of C++ across nine
files, one 5,000-line addon, 20 core patches and four audit tools.

Read `../../CLAUDE.md` first for the rules. This file is the map.

---

## The source files, and what each one owns

| File | Lines | Owns |
|---|---|---|
| `LivingGear_ClassPerks.cpp` | 3369 | The 30 class specs. `CLASS_PERK_GRANTS`, spec selection, per-spec behaviour |
| `LivingGear_Perks.cpp` | 3116 | Account perks: Kill Combo, cooking, fishing, Rogue Subtlety, world toggles |
| `LivingGear.cpp` | 1997 | Item leveling, the stat budget, attunement, milestones, the addon dispatcher's stat half |
| `LivingGear_Vault.cpp` | 1754 | Reagent bank, autoloot rule engine, pickpocket and corpse autoloot |
| `LivingGear_Next.cpp` | 1327 | Paladin perks, shared account currency, class buffs, mentor bots |
| `LivingGear_Gather.cpp` | 1095 | Gathering perks, chest autoloot, lockpicking |
| `LivingGear_Support.cpp` | 870 | `.bug` reports, Complete Quest + gold buyout, kill XP floor, Wintergrasp |
| `LivingGear_Progression.cpp` | 623 | Reputation, honour, trade and Leveling perk tracks |
| `LivingGear_Amenities.cpp` | 484 | Summoned mailbox, bank, vendor and friends |

The split is historical rather than principled. Two things follow from that and
both have caused real bugs:

- **Helpers are duplicated per file on purpose.** `HasPerk`, `BestOwned`,
  `RankOf` and friends exist several times over. That convention is fine right
  up until two copies disagree — see the invariants below.
- **Ownership is not always where you would guess.** Rogue Assassination and
  Subtlety live in `Perks.cpp`, not `ClassPerks.cpp`. Paladin lives in
  `Next.cpp`. Grep before assuming.

---

## Perks: three different kinds of id

Everything in the `910xxx` range looks alike and behaves very differently.

**Castable perks (15).** Real spellbook buttons. The only ones with a
`SkillLineAbility.dbc` row, which is what makes a learned spell appear at all.
The list lives in three places and they must agree: `CASTABLE_SPELLS` in
`tools/client-patch/build_patch.py`, `PerkIsCastable()` in `Perks.cpp`, and
`CASTABLE` in `tools/perk_grant_audit.py`.

**Badges (~130).** Flags the module reads. No `SkillLineAbility` row, so they
appear nowhere and learning one only spams chat. **Never learn a badge.**

**Class spec perks (30).** Selected one at a time per character, stored in
`lg_char_class_perk`. Grant real game spells through `CLASS_PERK_GRANTS`.

### The account/character split

This is the single most productive source of bugs in the project.

- Perks are owned by the **account** (`lg_account_perk`)
- Spells are learned per **character** (`character_spell`)

Anything that conflates the two breaks in a way that is invisible in code and
only shows up as "it works on my main". Reports #25, #33, #36, #37, #39 and #40
were all this. `CLAUDE.md` rule 6 exists because of it.

### The four audits

Each answers a different question, and none of them subsumes another:

```
tools/perk_audit.py          does any code READ this perk id?
tools/perk_promise_audit.py  does the module reference the ability it names?
tools/perk_spell_audit.py    does a spell with this id EXIST?
tools/perk_grant_audit.py    did the character actually GET it?   (asks the DB)
```

Run all four. The last one is the only one that asks the database, and it is
the only one that could have caught 40 perks having no spell.

---

## Attunement and item leveling

Fully described in `ATTUNEMENT-REDESIGN.md`, including the wrong turns. In
brief:

- **Item leveling** raises an item's *effective item level* toward
  `IlvlCeiling` (284), and its stats become the budget for that level. The
  budget is measured from `item_template` at startup, per inventory type,
  because the real curve accelerates and any fixed slope misprices one end.
- **Attunement** is per unique item, once, consuming it. Rate is 5% plus 5 per
  milestone, and `lg_absorb` stores the item's **full** stats plus a percentage
  so a milestone is one `UPDATE` rather than a rewrite.
- **28 milestones**, all achievement ids read out of
  `var/mmap-output/dbc/Achievement.dbc`.

---

## Core patches

20 of them in `core-patches/`, each a `.core-patch` file describing what
changed and **why**, plus a tracked copy of the patched file under
`src/server/`. The `.core-patch` file is not applied by anything — it is the
record. Keep writing them; they are how the next person understands why a core
file diverges from upstream.

The ones most likely to surprise you:

- **0017 / 0021** — creature immunity to damage schools and to bleed/disease
  mechanics is softened. Crowd-control immunity is deliberately untouched.
- **0020** — weapon, positioning and reagent requirements are waived for player
  casts. **Crafting still pays its reagents**; profession spells list materials
  as reagents, so a blanket waiver would delete the crafting economy.
- **0022** — banked profession tools satisfy `TotemCategory` requirements in
  place, without leaving the reagent bank.

---

## The addon protocol

Client and server talk over addon whispers. **One dispatcher**,
`DispatchAddonCommand` in `LivingGear.cpp`. Add commands there and nowhere
else. It logs anything unhandled.

Every "this button does nothing" bug in this project has been a client command
with no server handler. Check the worldserver log before assuming the logic is
wrong.

---

## Invariants that keep getting violated

Each of these cost at least one shipped bug.

1. **Guard the aura TARGET, not just the caster.** `CombatStop()` runs *after*
   `m_cleanupDone` is set, so leave-combat hooks fire on torn-down players.
   `LivingGear_SafeToCastOn` for the caster, `SafeAuraTarget` for the target.
2. **Cooldowns are applied when a cast FINISHES.** Clearing one from
   `OnPlayerSpellCast` clears something that does not exist yet. Use
   `ClearCooldownAfterCast`.
3. **Never hardcode a spell rank.** `BestOwnedOrFirst` — the best rank the
   player has actually learned, falling back to the granted rank.
4. **Multipliers compose, they do not stack.** 11x under 21x is 231x.
5. **Collect before you mutate.** Removing auras, destroying items or dealing
   killing damage inside an iteration invalidates it. Gather GUIDs first.
6. **Anything a perk grants must go through `CLASS_PERK_GRANTS`**, or it is
   never recorded and can never be revoked.
7. **Read ids from the data, never from memory.** Spell.dbc and Achievement.dbc
   both need anchoring on a known row to locate the name field first. This has
   caught a wrong Hemorrhage rank and two achievements that do not exist.
8. **Keep-in-step updates should be bidirectional.** `WHERE x < y` strands rows
   above `y` forever; `WHERE x <> y` converges.

---

## Working on this safely

- **Build both images every change set**: `docker compose build ac-worldserver
  ac-db-import`. `ac-db-import` bakes `data/sql/updates/pending_db_*` into the
  image with no bind mount, so rebuilding worldserver alone skips migrations
  silently and permanently.
- **Validate DML in a rolled-back transaction. Validate DDL against a scratch
  database** — `CREATE`/`ALTER`/`DROP`/`RENAME` force an implicit commit and
  the rollback is a no-op. This cost a live table once.
- **Capture a before-number** for any migration whose success is a value rather
  than a row count.
- **Instrument rather than theorise.** When every link reads correct, add one
  log line at the boundary. That has resolved more bugs here than reading has.
