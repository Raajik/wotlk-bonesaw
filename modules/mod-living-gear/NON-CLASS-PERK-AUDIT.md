# Non-class perk audit

The companion to `CLASS-PERK-AUDIT.md`. That one covered the 30 class specs.
This one covers the other **111 perks** — every world toggle, action, and
progression track in the Account Perks panel.

Started 2026-08-24, after the 0.1.73 ship.

## Why this is worth doing, in one number

The 0.1.73 work touched roughly six non-class perks incidentally, and **two of
them had a silently dead path**:

- **Leveling 1-10 (910053-910062)**, the +50%-per-max-level-alt XP bonus. Read
  through `OnPlayerGiveXP` — which `Player::GiveXP` never fires. So the bonus
  was absent from every grey kill, meaning every mob in a low-level zone, which
  is exactly where an XP bonus is supposed to matter.
- **Dungeon pace (910100)**, same shape, same hook, same silence.

Both were advertised, wired, and read by real code. All four mechanical audits
passed them, because the ids *are* referenced — in a place that does not
execute for the case the perk claims. Two hits in a sample of six is not a
statistic, but it is a reason to read the other 105.

## The ten checks

Adapted from the class audit's ten, but re-derived from failures that happened
to **non-class** perks specifically.

| # | Check | How it failed before |
|---|---|---|
| 1 | The perk id has a `spell_dbc` row | 40 perks had none (#22). `UnlockPerk` refuses and logs |
| 2 | Something reads the perk id | `perk_audit.py`. Solo Queue was read by nothing |
| 3 | **The read actually EXECUTES for the case the perk claims** | Leveling perks: read in a hook `Player::GiveXP` never fires |
| 4 | **State the perk writes is read by something** | Solo Queue wrote an account bool that nothing consumed |
| 5 | The unlock condition is reachable | Attune tier 1 keyed on "Realm First! Level 80" — one account on the realm, ever (#60) |
| 6 | Account-owned, but learned per character | One account owned 70 perks whose 5 characters knew 20, 14, 11, 14 and 2 (#33 #36 #37 #39 #40) |
| 7 | The advertised number equals the implemented number | Kill Combo said 5% speed per stack; effect 1 was fed the raw stack count, so on-foot speed rose 1% |
| 8 | Its hook is in the script's registered hook list | `CALL_ENABLED_HOOKS` never calls a hook absent from the constructor's list |
| 9 | Anything that casts is guarded | `LivingGear_SafeToCastOn` / `SafeAuraTarget`; four `_AddAura` crashes |
| 10 | A client command has a server handler | Every "this button does nothing" report in this module |

Checks 3 and 4 are the ones that find things. 1, 2 and 7 are already covered
mechanically by the four tools in `tools/`; they are listed for completeness and
because a tool reporting clean is not the same as the perk working.

**Do not batch this.** One perk, ten checks, write the row, move on.

---

## Check 8 is done for all 111, mechanically

`tools/perk_hook_audit.py` (written 2026-08-24) reads the core's own dispatchers
to learn every `hook enum <-> script method` pair, then reports any module
override whose hook is absent from its constructor's enabled-hook list. Such an
override compiles, reads correctly and is never called.

**Living Gear: 0 dead overrides.** Two harmless stale registrations
(`PLAYERHOOK_ON_MAP_CHANGED` in `PerksPlayer` and `NextPlayer`, registered with
no override). mod-playerbots has 16 genuinely dead overrides, all upstream code
and none of them perks.

That tool's first run confidently reported the Living Gear addon dispatcher
(`LivingGearPlayer::OnPlayerCanUseChat`) as dead code. It is not -- the addon
works, visibly. `OnPlayerCanUseChat` is overloaded five ways (plain, private,
group, guild, channel), each paired with a different hook, and keying on the
method NAME alone matched the wrong one. It now keys on method + arity + last
parameter type. Recorded because an audit that cries wolf is worse than no
audit, and this one nearly filed a phantom bug on its first outing.

---

## Results

Legend: OK = verified by reading the execution path. BROKEN = confirmed defect.
PARTIAL = works, but not for everything it claims. `?` = not yet checked.

### Group 1 — World toggles (5) — DONE, all pass

State-writing switches. Check 4 is the whole game here: a toggle whose bool
nobody reads is the Solo Queue bug, and it shipped that way for weeks.

| Perk | Verdict | Notes |
|---|---|---|
| 910092 Solo Queue | OK | See below — the perk works and its own comment says it does not |
| 910105 Auto-Mount | OK | `g_autoMountOn` read in `TryAutoMount`, called from `OnPlayerLeaveCombat`, hook registered |
| 910168 Pull Radius | OK | Read in `OnPlayerUpdate`, recast every 10s inside the aura's own duration |
| 910170 Track Ore | OK | Same tick, casts native Find Minerals |
| 910171 Track Herbs | OK | Same tick, casts native Find Herbs |

**910092 Solo Queue — the code comment is stale and wrong.** It says: *"A lone
player still cannot cause a proposal to form, so the queue would never pop for
them at all."* That has since stopped being true. `LFGQueue::CheckCompatibility`
computes `allowIncomplete = hasSoloQueue() || raidQueue || allowBotFill`, so
`hasSoloQueue()` **alone** lets an incomplete proposal form. Verified end to end:
Deserter waived (`LFGMgr.cpp:737`), random-dungeon cooldown waived (`:784`),
incomplete proposals allowed (`LFGQueue.cpp:339`), raid fill skips bots while
dungeons still get them (`LFGMgr.cpp:1839`). The perk delivers what it
advertises. Fix the comment, not the code — a comment claiming a live feature is
half-dead will cost the next reader an hour.

Minor, not defects: the tracking perks recast native tracking every 10 seconds,
which overrides whatever the player chose to track; and the three tick-driven
casts are unguarded by `LivingGear_SafeToCastOn` (check 9). `OnPlayerUpdate` is
a safe context, unlike the leave-combat hook that produced the crash history, so
this is a latent risk rather than a bug.

### Group 2 — World passives and actions (15) — DONE, all pass

| Perk | Verdict | Notes |
|---|---|---|
| 910106 Class Buffs | OK | Spot check above |
| 910107 Riding | OK, badge | Spot check above |
| 910172 CC Reduction | OK | Spot check above |
| 910108 Auto-Accept | OK | Implemented CLIENT side (`LivingGear.lua`, gated on `PerkKnown(910108)`) — the legitimate client-side case `perk_audit.py` documents |
| 910091 Armory | OK | Castable; `OnPlayerSpellCast` -> `SendArmory` |
| 910090 Quests - Finish | OK | Castable; -> `AutoQuestFinish` |
| 910088 Quests - Find | OK | Castable; -> `FindQuests` |
| 910008 Autoloot | OK | No unlock condition by design ("on by default"); the rule engine in `LivingGear_Vault.cpp` owns it |
| 910002/3/4/5/6/7/9 amenities | OK | All seven handled in the `OnPlayerSpellCast` switch in `LivingGear_Amenities.cpp` (mailbox, auction, trainer, bank, stable, bind, flight) |

**A useful contrast for the next reader.** `OnPlayerSpellCast` also carries cast
branches for `SPELL_SOLO_QUEUE` and `SPELL_AUTO_MOUNT`, and both of those spells
are deliberately absent from `CASTABLE_SPELLS` — so those two branches are dead
code too. They are *harmless* dead code: each perk is a toggle the Account Perks
checkbox already drives through a live handler, and `PerkIsCastable`'s comment
records that decision on purpose.

That is exactly what makes 910104 different, and it is the distinction to apply
when reading the rest of these: a dead branch beside a live path is debris; a
dead branch that is the ONLY path is a broken perk.

### Group 4 — Gathering tracks (30) — DONE, all pass

Two shared implementations cover all 30, so this group verifies as a pair.

| Perks | Verdict | Notes |
|---|---|---|
| Yield x3 per profession (15) | OK | `YieldMult` = `1u << ranks`, i.e. 2x/4x/8x, matching the advertised numbers. Reached via `YieldForStore` from `OnAfterLootTemplateProcess`, `MISCHOOK_ON_AFTER_LOOT_TEMPLATE_PROCESS` registered |
| Reach x3 per profession (15) | OK | `ExtraReach` = 3 yards per rank on a 10 yard base, i.e. +3/+6/+9 as advertised |

One check-7 note, not a defect: the auto-gather scan takes the **maximum** reach
across all five professions rather than the reach for the node being gathered,
so Mining Reach 375 also extends herb and fishing pickup. The descriptions are
written per profession ("Auto-gather ore from +9 yards"). One shared scan radius
is a reasonable implementation and it is strictly generous, but the text and the
code do not say the same thing.

### Group 3 — Multiplier tracks (26) — partly done

| Perk | Verdict | Notes |
|---|---|---|
| 910053-910062 Leveling 1-10 | FIXED in 0.1.73 | Was dead on every grey kill. `ApplyOffHookXpMultipliers` now applies it on the direct-grant path |
| 910010-910012 Honor | OK | `HonorPerkMultiplier` consumed by `OnPlayerRewardHonor`, registered, fired by the core at `Player.cpp:6366` |
| 910013-910015 Reputation | OK | `RepPerkMultiplier` consumed by `OnPlayerGiveReputation`, registered |
| 910016-910025 Factions (10) | OK | Same multiplier; unlocked from `CheckReputationPerks` against account-wide exalted |

One note on the faction ten: earning one was **silent** (`UnlockPerk` called
with no message, unlike every other rep tier). Fixed 2026-08-24 -- they now
announce, with the faction name read from `sFactionStore` rather than a
hand-written table of ten strings.

(A second note here claimed `CheckReputationPerks` was login-only. That was
wrong -- `OnPlayerReputationRankChange` already calls it. See the correction in
the polish list below.)

### Random spot checks (7, chosen by seeded shuffle)

Done at the user's request rather than working the groups in order — the point
being that the mechanical tools pass things a human would not.

| Perk | Verdict | Notes |
|---|---|---|
| 910172 CC Reduction | OK | `ReduceCrowdControl` from `OnAuraApply`, `UNITHOOK_ON_AURA_APPLY` registered, `CC_REDUCTION_PCT` 0.95 matches the advertised 95% |
| 910107 Riding | OK, badge | Nothing gates on the perk; `ApplyAccountRiding` really does grant the account's best riding skill. The badge is a record, by design |
| 910106 Class Buffs | OK | Read in `PlayerHasClassBuffUnlock` -> `ShouldHaveClassBuff` -> `ApplyClassBuffs` |
| 910010 Honor Defeat | OK | As above |
| 910093 Craft 1 | PARTIAL, known | Bug #30. Applied in `OnSpellPrepare`, where the cast time is already 0 for some skills, so there is nothing to multiply |
| 910048 First Aid Cleanse | OK | `TickFirstAidCleanse` from `OnPlayerUpdate` |
| **910104 Mounted Opener** | **BROKEN** | **Cannot ever fire. See below** |

## Finding 1 — 910104 Mounted Opener is dead

Advertised: *"Reach level 40. While mounted: jump while moving forward for a
boosted leap (+50% forward momentum). Jump again midair to slam down, pull
enemies within 20 yards, and Thunder Clap."* Unlocked at account level 40
(`LivingGear_Perks.cpp`), shows green in the panel, and does nothing.

Its only implementation is one branch in `OnPlayerSpellCast`:

    if (info->Id == SPELL_MOUNTED_OPENER && player->IsMounted())

which requires the player to CAST 910104. They cannot:

- it is not in `CASTABLE_SPELLS`, so `build_patch.py` writes no
  `SkillLineAbility` row and there is no spellbook button;
- `PerkIsCastable(910104)` is false, so `UnlockPerk` never calls `learnSpell`
  and no character ever knows the spell;
- nothing else in the module or the addon casts it. The addon's `HookJump` only
  replays movement keys after a knockback landing; it sends the server nothing;
- its `spell_dbc` row is `Effect_1 = 3` (dummy), no aura, `Attributes` 16 (not
  passive), so the engine applies nothing on its own either.

Both mechanical audits pass it: the id IS read (check 2) and the module does
reference Thunder Clap (perk_promise_audit). Only following the execution path
finds it. Same shape as the four dead class-perk halves.

It also carries a name mismatch — server `*Leveling: Mounted Opener`, client
`*Movement: Mounted Opener` — evidence it was moved between tracks and half
re-pointed.

**The fix needs a decision, not an improvisation.** The advertised behaviour is
jump-driven, and jump-driven movement is disabled on purpose: AGENTS.md says
"extra jump is disabled; do not advertise it", and the wiki records
`ApplyLgMoveSpeed` + `KnockbackFrom` crashing on the first tick with
"do not re-enable". So this perk advertises a feature that is deliberately off.
Three honest options:

1. Make it a real mounted ability: add 910104 to `CASTABLE_SPELLS` and
   `PerkIsCastable`, and reword the description to a button press ("while
   mounted, slam down: pull enemies within 20 yards and Thunder Clap"). The
   slam/pull/Thunder Clap code already exists and works; only the trigger is
   missing.
2. Drive it from the addon's existing jump hook by sending a command on a
   mounted midair jump, letting the server run the same branch. Closer to the
   advertised text, but it is the disabled-jump area the wiki warns about.
3. Remove the perk from the panel, which is what the Double/Triple Jump entries
   got for exactly this reason.

Option 1 is the recommendation: it keeps a real reward, needs no new mechanics,
and stops advertising a disabled one.

### Group 4 — Gathering tracks (30)

Mining, Herbalism, Skinning, Fishing, Engineering: yield multipliers and
auto-gather reach.

### Group 5 — Crafting and consumables (20) — DONE

| Perks | Verdict | Notes |
|---|---|---|
| 910063-910068 Cooking 75-450 | OK | Tier gate at the `COOK_BREAKS` match; `TickCooking` runs from `OnPlayerUpdate` |
| 910046 First Aid Instant | OK | `OnSpellPrepare` sets bandage cast time to 0 for `MECHANIC_BANDAGE` |
| 910047 First Aid Restore | OK | Scales bandage healing by the account's First Aid tier through `ModifyHealReceived`, and `UNITHOOK_MODIFY_HEAL_RECEIVED` is registered |
| 910048 First Aid Cleanse | OK | Spot-checked earlier |
| 910093-910097 Craft 1-5 | **PARTIAL** | Bug #30, already on the backlog. Applied in `OnSpellPrepare`, where the cast time is already 0 for Blacksmithing and Leatherworking, so there is nothing to multiply |
| 910026-910031 Trade 75-450 | **UNPROVEN** | Bug #20. See below |

**910026-910031 Trade — cannot be settled by reading, and the log now says why.**
The implementation is correct as far as reading can establish: `TRADE_SKILLS`
holds the right eight professions (core `SKILL_*` enums, so no wrong-constant
risk), the multiplier is applied in `OnPlayerUpdateCraftingSkill`, and that hook
is registered and demonstrably fires.

What the log shows is that it has **never once fired for a real profession
craft**. 71 lines over a full uptime, every one of them "is not a trade skill",
every one a bot casting a class spell — skill lines 237 (Arcane) and 354
(Demonology) reach this hook through `Player::UpdateCraftSkill`, which
`Spell::EffectCreateItem` and two sibling effects call. Nobody crafted.

So #20 is still open, and the instrumentation added for it was drowning itself:
71 useless lines would have buried the one line that matters. The non-trade case
is silent now, so the next real craft will be findable. That is the whole change
here — no behaviour was touched, because there is no evidence yet to act on.

### Group 6 — Travel, movement, attune (15) — DONE

| Perks | Verdict | Notes |
|---|---|---|
| 910073-910077 Travel 1-5 | OK | Hearthstone: cast time zeroed in `OnSpellPrepare`, cooldown cleared after cast. "Teleportation stone" once unlocked at all, deliberately not a per-rank ramp |
| 910098 Travel Swim | OK | Cast at login; swim-speed aura (58) at +500% |
| 910038 / 910176 / 910177 Wayfarer | OK | Rebuilt in 0.1.73 and verified there |
| 910101 Curator | OK | `TickCurator` from `OnPlayerUpdate`; feeds item XP to the five least-levelled items. Note the hardcoded `level < 25` must track `LivingGear.Attune.CapLevel`, which is 25 in the live conf — the coupling is commented at the call site |
| ~~910104 Mounted Opener~~ | **SCRAPPED** | Was broken (finding 1). Removed from the panel and no longer granted, 2026-08-24, on the user's call. Implementation parked, not deleted |

---

## Progress — all 111 examined

| Group | Perks | Result |
|---|---|---|
| 1 World toggles | 5 | all pass |
| 2 World passives and actions | 15 | all pass |
| 3 Multiplier tracks | 26 | all pass (Leveling was broken; fixed in 0.1.73) |
| 4 Gathering | 30 | all pass |
| 5 Crafting and consumables | 20 | 18 pass, 1 PARTIAL (Craft, #30), 1 UNPROVEN (Trade, #20) |
| 6 Travel, movement, attune | 15 | 14 pass, 1 scrapped (Mounted Opener) |

**Outcome: 1 dead perk found and scrapped, 1 known PARTIAL confirmed with the
reason, 1 UNPROVEN with the instrumentation now usable, 0 further defects.**

Four smaller things worth a line of polish, none of them defects:

- Solo Queue's code comment says the perk half-works. It fully works; the
  comment predates `allowIncomplete` in `LFGQueue::CheckCompatibility`.
- Earning one of the ten faction rep perks announces nothing (`UnlockPerk` with
  no message), unlike every other rep tier.
- ~~`CheckReputationPerks` runs at login only~~ -- WRONG, and worth recording as
  a miss. `OnPlayerReputationRankChange` already calls it, so exalted grants
  immediately. I claimed otherwise from grepping call sites and only noticing the
  login one. The script that was about to "fix" it checked the method body first
  and found the call already there. Grep found two call sites; I read one.
- The auto-gather scan uses the **maximum** reach across all five professions,
  while the descriptions are written per profession. Generous, and harmless, but
  the text and the code do not say the same thing.

## What this says about where the bugs actually are

The non-class perks held up far better than the class specs did, and the reason
is visible in the code: most of them are small, and most read through one shared
helper, so a helper that works works for fifteen perks at once. Gathering is 30
perks and two functions.

The failures cluster where a perk has a **bespoke trigger**. Mounted Opener had
its own cast branch and nothing to fire it. The Leveling perks had their own
hook and it was the wrong one. Craft has its own cast-time mechanism and it hits
a value that is already zero. That is the pattern worth carrying into the next
audit: shared paths are safe, one-off triggers are where to look.
