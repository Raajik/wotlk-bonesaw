# Living Gear backlog

Ordered by importance, not by size.

---

## 1. Audit every class spec, one at a time — DONE (2026-08-23), fixes unshipped

**Results in `CLASS-PERK-AUDIT.md`.** All 30 specs covered, ten checks each plus
an eleventh added during the work ("does the spec grant the prerequisites its
own implementation reaches for?").

Four perk halves were confirmed completely dead, and bug #59 turned out to have
been fixed in 6 places and left in 19. All fixed in code and compiling; none
shipped. What remains is the live in-game spot checks listed at the end of that
file — reading proved the mechanisms, only playing proves the fixes.

The audit also produced the grant rule now written at the top of
`CLASS_PERK_GRANTS`: **picking a spec grants every ability its description names
and every ability its implementation reaches for**, so a spec is playable from
level 1 instead of assembling itself over 50 levels.

Original brief, kept because the ten checks are still the right ten:

<details>
<summary>Original item 1</summary>


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

</details>

**Lesson worth keeping:** all four audit tools reported clean while four perk
halves were completely dead. Mechanical audits answer "is this id mentioned
somewhere?", and every one of these failures was "mentioned, in a place that
never executes." Static checking finds candidates; only reading the execution
path or playing the game confirms behaviour.

---

## 1. LFG "Unknown role: UNKNOWN" -- OPEN, three wrong fixes so far

The dungeon-ready pop-up throws `LFGFrame.lua:337: Unknown role: UNKNOWN` on
every pop. Fixed three times and still live; each fix assumed the role VALUE was
wrong and each was disproved:

  0.1.72 narrow a multi-bit mask to one bit   -- still errored
  0.1.73 also strip PLAYER_ROLE_LEADER        -- still errored
  0.1.74 send an index (0/1/2) not a mask     -- WORSE, reverted in 0.1.76
         (a rogue drew the tank icon, proving the client reads a mask)

**ROOT CAUSE PROVEN 2026-08-24, client-side measurement.** With the ready
window up, `GetLFGProposalMember(1..5)` returned:

    1  TANK        <- real member
    2  DAMAGER     <- real member
    3  UNKNOWN     <- empty
    4  UNKNOWN     <- empty
    5  UNKNOWN     <- empty
    numMembers = 5

The packet contained TWO members and was provably well-formed: LFGBYTES showed
size 33, expected 33, valid roles (4 = healer, 8 = damage) and correct self
flags. **The client reports numMembers = 5 regardless**, and
`LFDDungeonReadyPopup_Update` loops `for i=1, numMembers`, so it asks for five
members and `GetTexCoordsForRole` throws on every empty one.

So the 3.3.5 client requires a 5-man proposal to CONTAIN FIVE MEMBERS. The role
value was never the problem, which is why four different encodings failed
identically and why forcing tank/healer into slots 1-2 changed nothing.

**The fix is therefore to stop sending incomplete proposals for 5-man
dungeons** -- either by requiring a full group before a proposal forms, or by
padding the proposal with bots at creation. That is also what the playerbot fill
was meant to achieve; it currently runs in `LFGMgr::MakeNewGroup`, i.e. AFTER
acceptance, so the ready window never contains the bots.

**Pending config decision (agreed 2026-08-24, apply WITH the fix, not before):**
`AiPlayerbot.LfgDungeonBotFillSec` -> **10**. The live conf says 5, the code
default is 15, and the intent was 15; 10 is the agreed middle. Left alone for
now so it does not muddy the diagnostic.

---

## 1a. Kill XP, Wayfarer and bot roles — DONE (2026-08-23), unshipped

Four things, all built and compiling, none shipped.

**The zone-scaling XP audit.** Asked for as "the monster/world scaling system
feels like a facade — I don't get XP from half the mobs I kill in lower level
zones". It was. Every mob in a low-level zone is grey (`GetGrayLevel(60)` is
51), `Acore::XP::Gain` hard-zeroes a grey kill, and `KillRewarder` then skips
the `OnPlayerGiveXP` hook entirely — so the 1% floor, the elite bonus and
`Rate.XP.Kill` never reached a single one of those kills. A level 60 in a
starter zone was paid 131 XP against a 290,000 bar, 0.045%, for a mob the
module had already scaled to 61 in display and in damage. Grouped it was
worse: `KillRewarder` zeroes a grey kill for the whole party, and the module
granted only to the killing blow, so with playerbots in the group every kill a
bot finished paid the player nothing. Now one funnel, `KillXpFor`, and one
group-aware grant. See ARCHITECTURE.md, "Kill XP, in one place".

**The kill floor is now 2% of the level bar, 4% for elites**, and the quest
floor moved 4% → 10% so a quest is worth five mobs rather than two. Battleground
honourable kills get the same 2% through `SupportKillXp`. Roughly 50 kills a
level at every level, and the numbers behind that choice are in the same commit
message.

**Kill Combo (910089) is gone.** Its XP half is what the flat floor now does
better and its speed half is what Wayfarer now does better. State, table
round-trip and per-second recast all removed; existing `lg_account_perk` rows
are left alone.

**Wayfarer (910038) is now a slider**, not a flat +40% speed for 100 quests.
One dial trading movement speed against damage, +/-50 at tier 1, widening to
+/-100 through exploration; mounted speed gets half; swapping takes 30 seconds
and cannot be done in combat. Unlocked by any home-zone exploration achievement
or by 964 "Going Down?" — early, but a deed rather than time served.

**One definition of a bot's dungeon role.** `PlayerbotAI::GetDungeonRole`.
Reported as "healers marked as dps or dps marked as healers and everyone dies";
the cause was that the queue role read the talent tab and the fill read the
active AI strategy. Core-patch 0023.

Still to do, and only playing proves them:
- Confirm the LFG "Unknown role: UNKNOWN" Lua error is actually gone. The
  leader bit is the last untested suspect (core-patch 0013) and stripping it is
  a hypothesis, not a proven fix.
- Watch the levelling curve at 2%/4% for a few real levels before calling the
  numbers right.

---

## 1c. Audit the 111 non-class perks — DONE (2026-08-24)

**Results in `NON-CLASS-PERK-AUDIT.md`.** The companion to the class audit: the
30 class specs are done, these are the other 111 -- every world toggle, action
and progression track in the Account Perks panel.

Why it earns a slot this high: the 0.1.73 work touched about six of these
incidentally and two had a silently dead path (the Leveling +50%-per-alt XP
bonus and the dungeon pace bonus, both read through a hook `Player::GiveXP`
never fires, so both were absent from every kill in a low-level zone). All four
mechanical audits passed both, because the ids *are* read -- in a place that
does not execute.

All 111 examined. **One dead perk found: 910104 Mounted Opener could never
fire** -- its only trigger was a cast branch for a spell that is not castable
and is never learned. Scrapped on the user's call: off the panel, no longer
granted, implementation parked rather than deleted so it can return as a real
mounted button if that is ever judged worthwhile.

Otherwise: Craft 1-5 PARTIAL, confirmed as bug #30 with the reason (the cast
time is already 0 where it is applied); Trade 75-450 UNPROVEN, bug #20 -- the
code reads correctly and the hook has never once fired for a real profession
craft, so there is nothing to act on yet. Its instrumentation was drowning in 71
lines of bots casting class spells and is now silent for the non-trade case, so
the next real craft will be findable.

Four polish items, no defects: Solo Queue's comment claims it half-works when it
fully works; faction rep perks unlock silently; `CheckReputationPerks` is
login-only; the gather scan uses the max reach across professions while the text
is per profession.

Also produced `tools/perk_hook_audit.py`, which closes one of the ten checks for
all 111 at once: it reads the core's own dispatchers for every hook/method pair
and reports overrides absent from their script's enabled-hook list -- dead code
the core never calls. Living Gear has none; mod-playerbots has 16.

The pattern worth carrying forward: shared helpers are safe (gathering is 30
perks and two functions), and every failure was a perk with its own **bespoke
trigger** -- Mounted Opener's unreachable cast branch, the Leveling perks' wrong
hook, Craft's already-zero cast time. Look at one-off triggers first.

---

## 1b. Attunement interface redesign — HIGH, requested 2026-08-23

Bulk attune *works* ("Attuned 9 item(s). 21 already known.") but the interface
around it is disliked and wants redesigning. The mechanic is fine; this is
purely the UI.

Not started, and not specced yet — the shape of the redesign has not been
discussed, so that conversation comes first. Related: item 2 below already
notes that bulk attune wants to live on the first page and that the Armory page
was never laid out with a primary action button in mind, which is probably the
same problem seen from the other side.

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

Fixed and awaiting ship: **#49** (fel iron chests), **#54** (Blizzard revoke),
plus the two reported on 2026-08-23: the LFG dungeon-ready Lua error (leader bit
stripped from the proposal role, core-patch 0013 — unconfirmed, see item 1a) and
bots queueing with a role their spec does not play (core-patch 0023).

---

## 4. Known-but-unfixed

- **#30 craft speed on Blacksmithing/Leatherworking.** Not a mystery any more:
  the log shows `spell 53042 cast time 0 -> 0`. The perk applies correctly and
  the cast time is already zero at `OnSpellPrepare`, so there is nothing to
  multiply. Needs a different hook, or the reduction has to be expressed some
  other way.
- **×21 Subtlety bleeds and ×4 mage frost/arcane.** Live, deliberate, and the
  two most likely numbers to need pulling back once someone plays with them.
