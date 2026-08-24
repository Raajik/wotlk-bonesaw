# Class-spec perk audit

Backlog item 1, worked 2026-08-23 against ship 0.1.71.

## Status: findings below are FIXED in code, NOT yet shipped

Compiles clean (`docker compose build ac-worldserver ac-db-import`, both images).
Held local per rule 1 — no deploy, no tag, nothing on the realm.

Fixed:

- Rogue Combat's unreachable energy tick (duplicate `else if` merged)
- Warrior Fury's bleed multiplier moved to `ModifyPeriodicDamageAurasTick`
- Hunter Survival's Explosive Shot now also matches damage spell 53352
- Priest Shadow's Mind Flay now also matches damage spell 58381
- **18 of the 19 bug-#59 cooldown sites** converted to `ClearCooldownAfterCast`
- `CLASS_PERK_GRANTS` widened 3 → 8 slots and extended to the new grant rule
  (below); Warlock Destruction and Warrior Fury given entries for the first time
- Dead `GrantMageFrostBlizzard` / `BlizzardRankForLevel` deleted
- `IceLanceForLevel`'s hardcoded rank table replaced with `BestOwnedOrFirst`

Deliberately NOT changed:

- `TryRogueCombatKillingSpree`'s synchronous `RemoveSpellCooldown`
  (`:1682`). It clears the *player's existing* cooldown so the proc gives the
  button back, and the triggered cast that follows sets no new cooldown —
  `Spell::SendSpellCooldown` returns early for
  `TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD`. Verified, not a bug.
- `LivingGear_Next.cpp:895` (Hand of Freedom) — still UNCHECKED for which hook
  it runs under. The one remaining #59 candidate.

**Everything here is verified by reading, not by playing.** The live spot checks
at the bottom are still outstanding and are the only thing that proves the
fixes work.

**Method note, and why this found things the tools did not.** All four existing
audits (`perk_audit`, `perk_spell_audit`, `perk_promise_audit`,
`perk_grant_audit`) were run first. The three static ones report **clean** —
139/139 perks advertised, wired, named and given a `spell_dbc` row. Every
failure below sits underneath that clean bill of health, because the tools ask
"is this id mentioned somewhere?" and the failures are all "it is mentioned, in
a place that never executes."

Where a claim below says CONFIRMED it was checked against the core source, the
DBC, or the live database, and the evidence is quoted. Where it says UNVERIFIED
it needs a live in-game spot check and has not had one.

---

## The four confirmed-dead perk halves

### 1. Rogue Combat — "Energy regeneration increased by 50%" never runs

`LivingGear_ClassPerks.cpp:3109` and `:3117` both test
`selected == SPELL_ROGUE_COMBAT` in the same `else if` chain. The second is
unreachable, so `TickRogueCombat` — which is the entire energy perk — is never
called.

```cpp
else if (selected == SPELL_ROGUE_COMBAT)   // 3109
{ TickRogueCombatAdrenalineRush(player); }
...
else if (selected == SPELL_ROGUE_COMBAT)   // 3117  <-- dead branch
{ TickRogueCombat(player, g_rogue[...], diff); }
```

CONFIRMED by reading the dispatch. This is check 10's exact failure mode:
the code exists, nothing calls it.

### 2. Warrior Fury — "Rend and Deep Wounds deal +300% damage" never applies

`ApplyFuryBleedDamage` is registered only on `ModifySpellDamageTaken`. That hook
is called from exactly one place, `Unit::CalculateSpellDamageTaken`
(`src/server/game/Entities/Unit/Unit.cpp:1507`), which handles **direct** spell
damage. Periodic ticks go to `ModifyPeriodicDamageAurasTick` instead
(`SpellAuraEffects.cpp:6336`).

Rend (772) and Deep Wounds (12721) are pure periodic auras — read out of
Spell.dbc, anchored on field offsets taken from `build_patch.py`'s own live code
(EffectAura_1 = field 95, verified independently against Rejuvenation and Shadow
Word: Pain):

```
772   Rend         Effect_1=6 (APPLY_AURA)  Aura=3 (PERIODIC_DAMAGE)
12721 Deep Wounds  Effect_1=6 (APPLY_AURA)  Aura=3 (PERIODIC_DAMAGE)
```

So the multiplier is on a hook their damage never reaches. CONFIRMED.

Mage Fire is the counter-example that shows the correct shape — it registers
**both** `ApplyMageFireDamage` (direct, for the explosion) and
`ApplyMageFirePeriodic` (periodic, for the DoT).

### 3. Hunter Survival — "Explosive Shot deals double damage" never applies

`ApplyHunterSurvivalExplosiveShotDamage` gates on
`RankOf(info, SPELL_EXPLOSIVE_SHOT_R1)` i.e. 53301. But Explosive Shot's damage
is not dealt by 53301 — its `PERIODIC_DUMMY` aura casts a separate spell:

```
src/server/game/Spells/Auras/SpellAuraEffects.cpp:5953
    caster->CastCustomSpell(53352, SPELLVALUE_BASE_POINT0, m_amount, target, ...)
```

and 53352 is not in 53301's rank chain (live DB, `spell_ranks`: 53301 → 60051,
60052, 60053 only). `RankOf` therefore returns false for every damage event.
CONFIRMED.

### 4. Priest Shadow — "Mind Flay deals quadruple damage" never applies

Same shape. `ApplyPriestShadowMindFlayDamage` gates on
`RankOf(info, 15407)`. Mind Flay's damage comes from its
`PERIODIC_TRIGGER_SPELL_WITH_VALUE` effect, whose trigger spell (Spell.dbc field
118) is **58381**, and 58381 is not in the rank chain (live DB: 15407 → 17311,
17312, 17313, 17314, 18807, 25387, 48155, 48156). CONFIRMED.

---

## The systematic one: bug #59 was fixed in 6 places and left in ~19

`ClearCooldownAfterCast` was written for report #59 ("Penance has a cooldown when
it shouldn't"). Its own comment says *"Six perks promise 'no cooldown' and all
six had the same bug"*. The mechanism it describes is real and I re-verified it
in the core rather than trusting the comment:

- the hook fires at `Spell.cpp:3869` (`sScriptMgr->OnPlayerSpellCast`)
- the cooldown is written at `Spell.cpp:3986` (`SendSpellCooldown()`)
- **both are inside `Spell::cast()`, hook first, and the write is unconditional**

So clearing a cooldown synchronously from `OnPlayerSpellCast` clears one that
does not exist yet, and the real one lands ~117 lines later. CONFIRMED.

Six call sites were converted. Nineteen were not, and every one of them is a
spec whose tooltip promises "no cooldown":

| Fixed (uses `ClearCooldownAfterCast`) | Still broken (direct `RemoveSpellCooldown`) |
|---|---|
| 1549 Rogue Combat — Adrenaline Rush | 1033 Mage Arcane — Arcane Power |
| 2341 Hunter Survival — traps | 1622 Rogue Combat — Killing Spree |
| 2577 Warlock Destruction — Chaos Bolt | 1705 Warrior Arms — Bladestorm |
| 2659 Druid Feral — cat/bear abilities | 1831 Warrior Protection — Shockwave |
| 2674 Druid Restoration — Wild Growth | 1882 Hunter Marksmanship — Chimera Shot |
| 2724 Priest Discipline — Penance | 1917 Shaman Elemental — Thunderstorm |
| | 2019 DK Unholy — Summon Gargoyle |
| | 2026 DK Unholy — Army of the Dead |
| | 2293 Hunter Beast Mastery — Bestial Wrath |
| | 2380 Shaman Enhancement — Feral Spirit |
| | 2387 Shaman Enhancement — Stormstrike |
| | 2437 Shaman Restoration — Riptide |
| | 2536 Warlock Demonology — Metamorphosis |
| | 2609 Druid Balance — Starfall |
| | 2648 Druid Feral — Berserk itself |
| | 2743 Priest Holy — Guardian Spirit |
| | 2777 Priest Shadow — Shadowfiend |
| | 2811 DK Blood — Dancing Rune Weapon |
| | 2841 DK Frost — Hungering Cold |

(`LivingGear_Perks.cpp:2541`, the Travel perk, defers correctly by 300 ms and
documents exactly this reasoning. `LivingGear_Next.cpp:895`, Hand of Freedom,
clears synchronously and has NOT been checked for which hook it runs under.)

This is one edit repeated nineteen times, not nineteen investigations.

---

## Grants: what a spec hands you, and what it forgets

`CLASS_PERK_GRANTS` has 27 of 30 specs. The three without an entry:

- **Warrior Fury** — deliberate and correct (Titan's Grip is a flag, not a spell).
- **Warlock Affliction** — deliberate and correct (it amplifies DoTs you own).
- **Warlock Destruction** — **NOT correct.** Its whole text is *"Chaos Bolt has
  no cooldown. Conflagrate also casts a free, instant Chaos Bolt."* Both are
  Destruction talent spells. `TryWarlockDestroOnCast` reaches for
  `BestOwned(player, SPELL_CHAOS_BOLT_R1)` and silently does nothing when the
  player has neither. A player who picks Destruction without those talents gets
  a perk that is 100% inert.

### Prerequisites the perk needs but does not grant

This is the gap raised during the audit: a spec can grant its headline ability
and still be unusable because the thing that *triggers* the perk was never
handed over.

| Spec | Granted | Needed to actually fire, not granted |
|---|---|---|
| Mage Fire | Living Bomb | **Fire Blast** — it is the detonator (`TryMageFireDetonate`), and the tooltip never mentions it either |
| Warlock Destruction | *nothing* | **Chaos Bolt**, **Conflagrate** |
| Hunter Marksmanship | Chimera Shot | Serpent Sting, Aimed Shot (both via `BestOwned`, no-op if unowned) |
| Warrior Arms | Bladestorm | Whirlwind, Thunder Clap (autocast silently skips if unowned) |
| Warrior Protection | Shockwave | Thunder Clap (the radius and bleed clauses both key off it) |
| Shaman Elemental | Thunderstorm | Lava Burst, Chain Lightning |

Mage Fire is the clearest case: Fireball is baseline so "fire spells apply Living
Bomb" works, but the detonator half of the perk is unreachable below the level
Fire Blast is trained, and nothing tells the player it exists.

---

## Two grant paths running beside the table (invariant 6)

Report #54's disease — "two sources of truth" — is not fully gone:

- `LivingGear_Next.cpp:1032` grants Crusader Strike (35395) by raw `learnSpell`
  at login, while `CLASS_PERK_GRANTS` also grants the same id for Paladin
  Retribution.
- `LivingGear_Perks.cpp:2247` grants Shadowstep (36554) by raw `learnSpell`,
  while `CLASS_PERK_GRANTS` also grants it for Rogue Subtlety.

Both raw paths skip `NoteGranted`, so nothing they hand over is recorded. In
both cases the table path also runs and *does* record, and both raw paths are
guarded by the matching perk being selected — so this is not currently leaking
an unrevokable spell the way #54 did. It is still the exact structure that
produced #54 and should collapse to one path.

`GrantMageFrostBlizzard` and `BlizzardRankForLevel` are now **dead code** — no
call sites remain (grep-verified). Worth deleting so the next reader does not
wire them back up.

---

## Hardcoded ranks (invariant 3)

`BestOwnedOrFirst` / `BestRankForLevel` are the sanctioned way. Two functions
bypass it with hand-written level tables:

- `IceLanceForLevel` (`:1385`) — hardcodes ranks 30455 / 42913 / 42914
- `BlizzardRankForLevel` (`:1260`) — hardcodes nine ranks and eight breakpoints

`BestRankForLevel` already walks `spell_ranks` correctly and would replace both.
This is the shape that caused #38 (Living Bomb pinned to rank 1).

---

## The 30 specs

Legend: OK verified good · **DEAD** confirmed non-functional · GAP missing
something · `?` not independently verified.

Checks: 1 dbc row · 2 code reads id · 3 abilities referenced · 4 "Learn X"
granted · 5 grants recorded · 6 no side grant path · 7 no hardcoded rank ·
8 multipliers compose · 9 casts guarded · 10 tick dispatched · **P** prerequisites
granted.

| Spec | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | P | Headline problem |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Mage Arcane | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | cooldown #59; x4 arcane dmg undocumented |
| Mage Fire | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | **GAP** | Fire Blast detonator ungranted + undocumented |
| Mage Frost | OK | OK | OK | OK | OK | GAP | **GAP** | OK | OK | OK | OK | hardcoded ranks; dead legacy grant fn |
| Rogue Assassination | OK | OK | OK | OK | OK | OK | ? | ? | ? | n/a | OK | not deep-read (Perks.cpp) |
| Rogue Combat | OK | OK | OK | OK | OK | OK | OK | ? | OK | **DEAD** | OK | **energy tick unreachable**; cooldown #59 |
| Rogue Subtlety | OK | OK | OK | OK | OK | **GAP** | ? | ? | ? | n/a | OK | duplicate raw Shadowstep grant |
| Paladin Holy | OK | OK | OK | OK | OK | OK | ? | ? | ? | ? | OK | not deep-read (Next.cpp) |
| Paladin Protection | OK | OK | OK | OK | OK | OK | ? | ? | OK | n/a | OK | not deep-read |
| Paladin Retribution | OK | OK | OK | OK | OK | **GAP** | ? | ? | ? | ? | OK | duplicate raw Crusader Strike grant |
| Warrior Arms | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | GAP | cooldown #59; uses `m_Events` the header bans |
| Warrior Fury | OK | OK | OK | n/a | n/a | OK | OK | **DEAD** | OK | OK | OK | **bleed multiplier on the wrong hook** |
| Warrior Protection | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | GAP | cooldown #59 |
| Hunter Marksmanship | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | GAP | cooldown #59 |
| Hunter Beast Mastery | OK | OK | OK | OK | OK | OK | ? | ? | OK | n/a | OK | cooldown #59 |
| Hunter Survival | OK | OK | OK | OK | OK | OK | OK | **DEAD** | OK | n/a | OK | **Explosive Shot x2 never matches (53352)** |
| Shaman Elemental | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | GAP | cooldown #59 |
| Shaman Enhancement | OK | OK | OK | OK | OK | OK | ? | OK | OK | OK | OK | cooldown #59 (x2 sites) |
| Shaman Restoration | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | OK | cooldown #59 |
| Warlock Affliction | OK | OK | OK | n/a | n/a | OK | OK | OK | OK | OK | OK | periodic hook used correctly |
| Warlock Demonology | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | OK | cooldown #59 |
| Warlock Destruction | OK | OK | OK | **GAP** | n/a | OK | OK | OK | OK | n/a | **GAP** | **grants nothing; needs 2 talent spells** |
| Druid Balance | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | cooldown #59 |
| Druid Feral | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | OK | cooldown #59 on Berserk itself |
| Druid Restoration | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK | clean |
| Priest Discipline | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | OK | clean (#59 fixed here) |
| Priest Holy | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | OK | cooldown #59 |
| Priest Shadow | OK | OK | OK | OK | OK | OK | OK | **DEAD** | OK | n/a | OK | **Mind Flay x4 never matches (58381)** |
| DK Blood | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | OK | cooldown #59 |
| DK Frost | OK | OK | OK | OK | OK | OK | OK | OK | OK | n/a | OK | cooldown #59 |
| DK Unholy | OK | OK | OK | OK | OK | OK | ? | ? | OK | n/a | OK | cooldown #59 (x2 sites) |

---

## Beyond the class specs

Two findings from starting the wider perk sweep.

### The castable list disagrees three ways

`ARCHITECTURE.md` says castable perks are *"the only ones with a
`SkillLineAbility.dbc` row, which is what makes a learned spell appear at all"*
and that there are 15. The three lists that must agree do not:

```
PerkIsCastable()  (C++)        15 ids
CASTABLE_SPELLS   (build_patch) 12 ids
CASTABLE          (grant audit) 12 ids
```

**910008 (*Autoloot), 910092 (*Solo Queue), 910105 (*Auto-Mount)** are castable
server-side — so they get `learnSpell`ed — but have no `SkillLineAbility` row.
Independently confirmed against the last build's own output, which emitted
exactly 12 SLA rows.

**This is INTENTIONAL — confirmed with the user 2026-08-23. These three are
passive toggles and are not supposed to have spellbook buttons. Do not "fix"
this.** The defect is only in `ARCHITECTURE.md`, which says castable perks are
*"the only ones with a `SkillLineAbility.dbc` row"* and counts 15; the accurate
statement is 15 castable, 12 of them with buttons, 3 deliberately buttonless.

### 11 castable perks are missing from characters that own them

`perk_grant_audit.py` against the live DB:

```
910090  645 / 953 characters      910006  144 / 220
910091  440 / 953                 910002   70 / 123
910009  311 / 483                 910004   67 / 133
910007   67 / 133                 910005    3 / 13
910042    2 / 13                  910088    2 / 13
910003    1 / 7
```

### RESOLVED by live spot check — the repair works, the number was bots

A character was logged in on 2026-08-23 and the log immediately showed the
repair firing for 19 characters (`Leham learned 5 account perk spell(s) it was
missing`, etc). But re-running the audit gave **byte-identical numbers**, which
is the contradiction that mattered.

Cause: 642 of the 645 were `RNDBOT` accounts. Bots never keep these spells —
`ReconcilePerkSpells` learns them at login, the playerbot system owns the bot's
spellbook, and an explicit `saveall` does **not** persist them (checked: Leham
still shows 5 owned / 1 stored afterwards). So the same bots are "repaired"
every login forever and the database never moves.

Only **3 real characters** were behind that 645, all offline, all fixed the
moment they log in. The logged-in character (Muckfuppet) had all 10 of its
account's castable perks already, which is why it produced no repair line.

So: **the repair works correctly.** The tool was the problem — it buried a
3-character issue under a 645-character headline. `perk_grant_audit.py` now
excludes bot accounts and reports single digits.

`LivingGear.ReconcilePerkSpells` being absent from the live config is harmless:
`GetOption<bool>(..., true)` defaults it on.

---

## What needs a live spot check, in priority order

Static reading cannot close these. Each is one character, a few minutes.

1. **Log any character in and grep the log** for `account perk spell(s) it was
   missing`. Settles whether the 645/953 gap drains or is permanent.
2. **Pick Priest Shadow, cast Mind Flay, read the combat log.** If damage is
   unchanged by the x4, finding 4 is confirmed in-game as well as on paper.
3. **Pick any "no cooldown" spec from the broken column and press the button
   twice.** Bladestorm or Starfall is the most visible.
4. **Pick Rogue Combat and watch energy regen** against a spec with none.
5. **Check whether *Autoloot / *Solo Queue / *Auto-Mount have spellbook buttons.**

---

## Questions worth answering before the fixes are written

1. **Warlock Destruction** — grant Chaos Bolt + Conflagrate outright (consistent
   with #36's "usable while levelling"), or leave it talent-gated and reword the
   tooltip to say so?
2. **Mage Fire** — grant Fire Blast, and should the tooltip mention that Fire
   Blast is the detonator? Right now the best half of the perk is undiscoverable.
3. **Mage Arcane** — the x4 arcane damage under Arcane Power is real but appears
   in no tooltip. Intended and just undocumented, or leftover?
4. **The prerequisite rule in general** — should picking a spec always grant
   every ability its description names *and* every ability its implementation
   reaches for? That single rule would close Fire, Destruction, Marksmanship,
   Arms, Protection and Elemental at once.
