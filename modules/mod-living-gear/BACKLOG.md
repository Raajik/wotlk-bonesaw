# Living Gear backlog

Ordered by importance, not by size.

---

## 1. Audit every class spec, one at a time — HIGH

**Why this is top of the list.** Class perks have failed one spec at a time for
weeks, and every single failure was a different link in the same chain being
missing. Reports #25, #33, #36, #37, #39, #40, #52, #53 and #54 are all the
same shape: the perk *looks* wired, and one step of it is not. Four separate
audit tools now exist and none of them catch this, because the failures are
per-spec facts that only a human reading the spec's own description can check.

Thirty specs. Go one at a time. For each, verify **all ten**:

| # | Check | How it failed before |
|---|---|---|
| 1 | The perk id has a `spell_dbc` row | 40 perks had none (#22) |
| 2 | Something in the module reads the perk id | `perk_audit.py` |
| 3 | Every ability the description names is referenced in code | `perk_promise_audit.py` |
| 4 | Spells the description says "Learn X" are in `CLASS_PERK_GRANTS` | #33 #36 #40 — 3 of 30 specs granted anything |
| 5 | Those grants are **recorded**, so switching away revokes them | #54 — Frost granted Blizzard by a legacy path, unrecorded, unrevokable |
| 6 | No ad-hoc grant path running *beside* the table | Same bug. Two sources of truth is the disease |
| 7 | Every cast uses `BestOwnedOrFirst`, never a hardcoded rank | #38 — Living Bomb pinned to rank 1 in three places |
| 8 | Damage multipliers **compose**, they do not stack | 11x Garrote under 21x Subtlety would have been 231x |
| 9 | Anything that casts is guarded (`LivingGear_SafeToCastOn` / `SafeAuraTarget`) | Four `_AddAura` crashes |
| 10 | The spec's tick is actually dispatched in `OnPlayerUpdate` | Code exists, nothing calls it |

**Do not batch this.** The reason these keep escaping is that they get checked
in bulk, at which point "looks fine" wins. One spec, ten checks, write down the
result, move on. Thirty specs is a couple of hours and it ends a months-long
class of bug.

Suggested output: a table in this file, one row per spec, ten columns, so the
next person can see what was actually verified rather than trusting that it was.

---

## 2. Attunement and item leveling redesign — HIGH

Spec is complete in `ATTUNEMENT-REDESIGN.md`. Not started.

Blocked on two things, both small:
- **Armory redesign** — bulk attune wants to live on the first page, which was
  not laid out with a primary action button in mind.
- **22 achievement IDs** for the milestone list, read out of the DBC rather
  than recalled.

---

## 3. Professions account-wide, and cross-faction — HIGH

Reports #50 and #56, the same request twice. Rule 6 territory, and the answer
the user gave is specific: **everything truly account-wide, and hide recipes
that are invalid for your faction** rather than refusing to share them.

That split matters. Sharing the *skill* and the *recipe book* across the
account is the feature; the faction-locked recipes are a display problem, not a
sharing problem. Alliance-only patterns should sit in a Horde character's book
greyed out or hidden, not cause the whole profession to be per-character.

Shape, following the pattern `lg_account_recipe` already establishes:
- skill values keyed on `account_id`, taking the highest any character reached
- learned recipes likewise, unioned across the account
- a display filter at the client for anything whose `RequiredRace`/faction does
  not match the current character

Unknowns worth checking before building: whether a recipe learned by a Horde
character can even be *cast* by an Alliance one (some have hard faction
requirements in the spell data), and what happens to a profession the character
has not "learned" but the account has.

---

## 4. More than 10 characters per realm — RESEARCHED, tractable

Asked whether this is a big ask. It is not, and it is two separate changes.

**Server side.** `WorldConfig.cpp:232` clamps it:

    SetConfigValue<uint32>(CONFIG_CHARACTERS_PER_REALM, "CharactersPerRealm", 10,
        ..., [](uint32 const& value) { return value > 0 && value <= 10; }, "> 0 && <= 10");

The validator rejects anything above 10, so raising the config alone does
nothing. It needs a one-line core patch to widen that bound. The limit is
enforced in exactly one place, `CharacterHandler.cpp:422`, so nothing else
needs to change.

**Client side.** The stock config comments say why the cap exists:
`Default: 10 - (Client limitation)`. The character select screen has ten slots
and no scrollbar. This is the half Synastria solved by shrinking the login
font — it is a GlueXML change (`GlueXML/CharacterSelect.lua` / `.xml`), and we
already ship FrameXML overrides through `patch-Y.MPQ`, so the delivery
mechanism exists. Shrinking the font and row height to fit ~20, or adding a
scroll, are both plausible.

**Order matters:** raise the server bound first and verify with 11 characters
before touching the client, so a broken character list is never confused with
a broken server limit.

---

## 5. Open bug reports

Current as of 2026-08-23. See `tools/bug-reports/bug_resolve.py` for status.

- **#45** achievements should be account-wide — feature, and squarely rule 6
- **#46** fishing skill-ups only +1 at a time; fishing autoloot poor
- **#47** cooking skill-ups only +1; reagent bank still says "missing reagent"
  — this is #16 again, which 0.1.60 was supposed to fix via the
  `GetTradeSkillInfo` hook. It did not.
- **#48** quest auto-accept only fires when a giver has exactly one quest
- **#50** professions not shared with alts — rule 6 again
- **#51** stray non-functional mailbox in the blood elf starting area, probably
  a leftover `*Mailbox` summon that never despawned
- **#52** Living Bomb not spreading in combat
- **#53** Blizzard does no damage — instrumented, awaiting a fresh attempt

Fixed and awaiting ship: **#49** (fel iron chests), **#54** (Blizzard revoke).

---

## 4. Known-but-unfixed

- **#30 craft speed on Blacksmithing/Leatherworking.** Not a mystery any more:
  the log shows `spell 53042 cast time 0 -> 0`. The perk applies correctly and
  the cast time is already zero at `OnSpellPrepare`, so there is nothing to
  multiply. Needs a different hook, or the reduction has to be expressed some
  other way.
- **×21 Subtlety bleeds and ×4 mage frost/arcane.** Live, deliberate, and the
  two most likely numbers to need pulling back once someone plays with them.
