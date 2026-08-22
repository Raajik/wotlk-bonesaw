# Core-engine patches

This module is (deliberately) shipped without the AzerothCore engine source
tree -- it's ~600MB and not something this repo tracks or should track.
Almost everything Living Gear needs lives entirely in `modules/mod-living-gear/`
and gets picked up by the normal module build, no core changes required.

The one exception on record is here as a small, self-contained patch file
instead of a tracked copy of the whole engine file it touches. Named
`.core-patch` rather than `.patch`/`.diff` only because this repo's
`.gitignore` excludes those two extensions (unrelated scratch-diff
files elsewhere) -- it's an ordinary unified diff, `git apply` doesn't
care about the extension.

## Applying a patch

From the root of your own AzerothCore checkout:

```
git apply --directory=. path/to/this/repo/modules/mod-living-gear/core-patches/0001-shadow-clone-nameplate.core-patch
```

or by hand -- each patch's header explains exactly what it does and why;
they're short enough to eyeball and apply manually if `git apply` doesn't
like your tree's exact line numbers.

## Patches

- **0001-shadow-clone-nameplate.core-patch** -- `QueryHandler.cpp`. Makes the
  Living Gear Rogue Subtlety "Shadow Clone" pet show its owner's real
  character name instead of the generic "Shadow Clone" name every
  instance would otherwise share (the query is keyed by creature
  *template* entry, not GUID). Purely cosmetic -- skip it if you don't
  care about that nameplate, the rest of the module works fine without it.

- **0002-cap-spell-resist-chance.core-patch** -- `Unit.cpp`. Caps the
  discrete magic-resist roll at 80% and removes the 100%/full-immunity
  bucket entirely. Gameplay balance change, not cosmetic -- recommended
  for anyone running this module.

- **0003-playerbot-lfg-role.core-patch** -- `mod-playerbots`'s
  `RandomPlayerbotMgr.cpp`. Fixes a client Lua error ("Unknown role:
  UNKNOWN") when accepting a dungeon-finder pop that got filled with
  bots. Only relevant if you also run mod-playerbots.

- **0004-playerbot-queue-fill-timers.core-patch** -- `mod-playerbots`'s
  `RandomPlayerbotMgr.cpp`. Shortens LFG/BG bot-fill polling intervals
  (35s/30s/20s -> 10s/10s/8s) so bots react to real-player queue demand
  faster. Gameplay tuning, not a bug fix -- skip it if you're happy with
  the slower default cadence.

- **0005-account-wide-lockpicking.core-patch** -- `Spell.cpp`,
  `Spell::CanOpenLock()`. Makes Lockpicking account-wide, matching Riding.
  Required for the module's chest-autoloot Lockpicking handling to mean
  anything across alts; skip it and Lockpicking stays per-character.

- **0006-stackable-tracking.core-patch** -- `SpellInfo.cpp`,
  `SpellInfo::IsAuraExclusiveBySpecificWith()`. Lets multiple minimap
  tracking types be active simultaneously (Find Minerals + Find Herbs,
  etc.) instead of one cancelling the other. Required for the Track
  Ore/Track Herbs perks to both show at once; skip it and only one
  tracking type can ever be active, matching stock Blizzard behavior.

- **0007-locked-chest-autoloot.core-patch** -- `SpellEffects.cpp`,
  `Spell::EffectOpenLock()`. Chest autoloot only ever fired for chests
  with no lock at all -- any locked chest (Lockpicking, key, or
  otherwise) opens through this completely separate code path, which
  never touches `GameObject::Use()` (where the module's autoloot hook
  lives) at all. Required for autoloot to do anything on the majority of
  real chests, which tend to be locked; skip it and only trivially
  unlocked chests ever autoloot.

- **0009-shadow-dance-stealth-bypass.core-patch** -- `Spell.cpp`,
  `Spell::CheckCast()`. Lets the Shadow Dance perk (Rogue Subtlety) use
  stealth-only openers (Ambush, Garrote, etc.) without being stealthed --
  a permanent "openers anytime" buff rather than a real stealth aura, so
  it has no crouch/transparency/detection-range side effects. Required
  for that half of the Shadow Dance perk; skip it and Subtlety rogues
  keep the normal stealth requirement on those abilities (the party/raid
  attack power buff half is unaffected either way).

- **0010-pickpocket-junkbox-autoloot.core-patch** -- `SpellEffects.cpp`,
  `Spell::EffectPickPocket()`. Gives autoloot a chance at Pickpocket's
  junk-box loot, the one loot source the existing chest/creature-kill
  autoloot hooks could never reach (Pickpocket doesn't kill its target
  and doesn't go through the GameObject chest path). Required for
  pickpocket loot to autoloot at all; skip it and it always opens the
  manual loot window.

- **0011-reagent-vault-craft-from-anywhere.core-patch** -- `Spell.cpp`,
  `Spell::CheckCast()`. Tops the bag up from the account-wide reagent
  vault right before the engine's own reagent check, so auto-banked
  reagents/tools still count as "in your backpack" when crafting instead
  of needing a manual withdraw first. Required for crafting to work at
  all once reagents/tools are auto-banked; skip it and anything vaulted
  becomes invisible to the crafting system (SPELL_FAILED_REAGENTS even
  though the profession window shows it as available).

- **0012-multi-realm-same-worldserver.core-patch** -- `WorldSocket.cpp`,
  `HandleAuthSession()`. Adds `RealmID.Aliases` to worldserver.conf so extra
  `acore_auth.realmlist` rows can describe THIS same worldserver reached at a
  different address, instead of being rejected with "requested connecting with
  realm id N but this realm has id M set in config". Needed because the host
  machine can only reach the server at `127.0.0.1` (Tailscale does not hairpin
  to a node's own address) while everyone else needs the tailnet address, and
  no single value serves both -- AzerothCore's own `localAddress`/
  `localSubnetMask` mechanism cannot help, because Docker NATs every
  connection to the bridge gateway before the authserver sees a client IP.
  Empty by default, so stock behaviour is unchanged without it. Skip it if
  one address reaches your server from everywhere.

Per-viewer mob level scaling (previously patch 0008, `Object.cpp`) no
longer needs a core patch at all -- `Object::BuildValuesUpdate()` turned
out to be dead code for creatures/players (`Unit` has its own override
that actually runs, via virtual dispatch). The real fix lives entirely
in the module now: `PerksUnit` (`LivingGear_Perks.cpp`) hooks the stock
`ShouldTrackValuesUpdatePosByIndex`/`OnPatchValuesUpdate` `UnitScript`
callbacks, which is the same purpose-built per-viewer-field-patch
mechanism the engine itself uses for things like `UNIT_NPC_FLAGS`. No
core changes required for this feature at all anymore.
