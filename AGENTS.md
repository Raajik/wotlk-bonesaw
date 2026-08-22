# AGENTS.md

AzerothCore is a C++ MMORPG server emulator for World of Warcraft 3.3.5a (WotLK), built with CMake, backed by MySQL.

## Agent rules

- **Hold live until explicit ship.** Default is accumulate changes only. Until the user explicitly says to ship, restart, deploy, or push live: do not `docker compose up` / replace / restart `ac-worldserver`; do not run `tools/restart_worldserver.ps1` or saveall-for-reboot (`saveall` without reboot is also unnecessary unless they ask); do not `deploy_client.ps1`; do not `gh release` / push updater MPQs; do not git push unless they asked; do not Discord "this is live" notes (draft pending notes in chat/wiki is OK). Do keep writing C++, pending SQL, and client-patch sources. `Bonesaw.version` may bump in git but is not published. When they later say ship, `/bonesaw-ship` owns the order: build first (nothing live moves, and a compile failure aborts before it can), then 45s warn, `saveall`, container replace, SQL import, client deploy, `ship/X.Y.Z` tag, GitHub latest, numbered Discord.
- **Build to verify; never deploy to verify.** `docker compose build ac-worldserver ac-db-import` compiles into images and does not touch the running container -- it is safe, and it is the last step of every change set. Run it in the background and carry on. A change that has never compiled is not "ready to deploy", it is a guess, and batching a week of guesses means every compile error surfaces on ship day at once. Only `docker compose up` / restart moves anything live, and that still needs an explicit ship.
- **Rebuild `ac-db-import`, not just `ac-worldserver`.** That image bakes `data/sql/updates/pending_db_*` in; there is no bind mount. Rebuilding worldserver alone means new migrations are silently never applied -- `rev_living_gear_account_keys.sql` sat unapplied from 2026-08-21 for exactly this reason, so the account key ring was dead while its code read perfectly.
- **Two commands own the lifecycle.** `/bonesaw-status` answers what is committed, built, imported, published and still pending -- read-only, safe any time, and the right first move when the user asks "did that ship?". `/bonesaw-ship` is the only thing permitted to touch the live realm, and only on an explicit ship request. Both are defined in `.claude/skills/`.
- **Every ship gets a `ship/X.Y.Z` git tag**, created by `/bonesaw-ship`. That tag is the only durable record of what players actually have, and `git log ship/<latest>..HEAD` is the pending list. Without it, work that was merely committed is indistinguishable from work that shipped -- which is how eight commits accumulated behind 0.1.50 with nothing anywhere saying so.
- **"Ready to deploy" means:** compiles clean, committed on `Playerbot` with a message that would read as patch notes, pending SQL written under `data/sql/updates/pending_db_*/`, and `/bonesaw-status` showing nothing unexpected. It does not mean "the code is written".
- **Never edit SQL files outside `data/sql/updates/pending_db_*/` unless explicitly requested. ** `data/sql/base/`, `data/sql/archive/`, and `data/sql/updates/db_*/` are immutable.
- When Bonesaw / Living Gear / playerbots features ship, include a short Discord-ready patch notes bullet list in the reply (player-facing, grouped by theme, ASCII-friendly). Title `Bonesaw X.Y.Z - patch notes` from `tools/client-update/Bonesaw.version` after bumping it for this ship (including server-only; do not skip numbers). No file paths, no spell IDs unless needed. List only what actually shipped. Extra jump is disabled; do not advertise it. Until ship, draft notes as pending; do not post live.
- Append durable learnings (crashes, UI rules, deploy, spell IDs, do-not-repeat mistakes) to `A:\obsidian\jeremy\wiki\Bonesaw.md` as part of shipping, not after. Client-facing strings are ASCII only.
- **When shipping: never restart/replace worldserver without warning players, then saving.** Run `powershell tools/restart_worldserver.ps1` before `docker compose up` / `restart` / `kill` of `ac-worldserver`. (Not before `build` -- building only writes an image and is deliberately done first, so a broken compile never reaches the warn.) That announces in-game, waits **45 seconds**, then `saveall`. Skip only if the container is not running. Do not use AzerothCore `server shutdown` for docker replace. Do not start this sequence unless the user asked to ship or restart.

## Build

Out-of-source build is required (in-source is blocked).

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/azeroth-server -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSCRIPTS=static -DMODULES=static
make -j$(nproc) && make install
```

C++20 required (`CMAKE_CXX_STANDARD 20`). Useful flags: `BUILD_TESTING=ON` (Google Test), `NOPCH=1` (disable precompiled headers). Full set in `conf/dist/config.cmake`. `compile_commands.json` is exported automatically.

Tests (Google Test, in `src/test/`): configure `-DBUILD_TESTING=ON`, then `ctest` or `./src/test/unit_tests` from the build dir.

## Repository layout

- `src/common/` — networking (Asio), crypto, config, logging, shared utilities.
- `src/server/game/` — core gameplay; compiled into worldserver.
- `src/server/scripts/` — content scripts grouped by region (`EasternKingdoms/`, `Northrend/`, …), class (`Spells/spell_mage.cpp`, …), and domain (`Commands/`, `Pet/`, `OutdoorPvP/`, `World/`).
- `src/server/database/` — DB abstraction and schema updater.
- `src/server/shared/` — code shared by auth and world servers.
- `src/server/apps/{authserver,worldserver}/` — entry points (ports 3724 and 8085).
- `src/test/` — Google Test unit tests + mocks.
- `data/sql/` — `base/` (historical schema), `updates/db_*/` (merged), `updates/pending_db_*/` (in-flight, **edit here**), `custom/` (gitignored).
- `modules/` — external modules (each a subdir with its own `CMakeLists.txt`). Disable with `-DDISABLED_AC_MODULES="mod1;mod2"`. See `modules/how_to_make_a_module.md`.
- `apps/` — helper scripts; `apps/codestyle/` holds the lint scripts.
- `conf/dist/` — distributed config templates; `conf/*.conf` is gitignored.
- `deps/` — vendored third-party dependencies.

## Adding SQL updates

1. `cd data/sql/updates/pending_db_world/` (or `pending_db_auth` / `pending_db_characters`).
2. `./create_sql.sh` generates an empty `rev_<timestamp>.sql` to write into.
3. Conventions enforced by `apps/codestyle/codestyle-sql.py`: every `INSERT` preceded by a matching `DELETE` (idempotency); no double semicolons; no multiple blank lines; InnoDB engine.

The three databases:

- `acore_auth` — accounts, realm list, IP/account bans, session keys. Shared across all realms.
- `acore_characters` — per-character state: characters, inventory, in-progress quests, mail, guilds, arena teams, achievements. One per realm.
- `acore_world` — static game content: creature/gameobject/item/quest templates, spawn lists, loot tables, SmartAI scripts, gossip, conditions. Read-mostly; rebuilt from SQL.

## Code style

Formatting (charset, indent width, line length, final newline, trailing whitespace) follows `.editorconfig`.

Run the linters before claiming a change is done:

```bash
python apps/codestyle/codestyle-cpp.py     # C++
python apps/codestyle/codestyle-sql.py     # SQL (compares to origin/master)
```

Hard rules (also enforced by CI with `-Werror`, plus `cppcheck`):

- 4-space indent for C++ (tabs forbidden); 2-space for JSON/YAML/sh/ts/js. UTF-8, LF, max 120 cols, trailing newline.
- Allman braces. No braces around single-line statements. `if (x)` — never `if(x)` or `if ( x )`.
- `auto const&` (not `const auto&`); `Type const*` (not `const Type*`).
- Use `{}` format specifiers (`fmt`-style), not `%u`/`%s`.
- Use the typed helpers, not raw flag access:
  - `IsPlayer()`, `IsCreature()`, `IsItem()`, … instead of `GetTypeId() == TYPEID_*`.
  - `GetNpcFlags()`, `HasNpcFlag()`, `SetNpcFlag()`, `RemoveNpcFlag()`, `ReplaceAllNpcFlags()` instead of `*Flag(UNIT_NPC_FLAGS, …)`.
  - `IsRefundable()`, `IsBOPTradable()`, `IsWrapped()` instead of `HasFlag(ITEM_FIELD_FLAGS, …)`.
  - `HasFlag(ItemFlag)` / `HasFlag2(ItemFlag2)` / `HasFlagCu(ItemFlagsCustom)` instead of bitwise `Flags & ITEM_FLAG…`.
  - `ObjectGuid::ToString().c_str()` instead of `ObjectGuid::GetCounter()`.

## Project conventions

- **Logging**: `LOG_INFO("category.sub", "msg with {}", arg)` (also `LOG_WARN`/`ERROR`/`DEBUG`/`TRACE`). Categories are hierarchical, dot-separated (`server.loading`, `entities.player`, `sql.dev`). No `printf`-style, no `sLog->`, no `TC_LOG_*`. Macro in `src/common/Logging/Log.h`.
- **Random**: use helpers in `src/common/Utilities/Random.h` — `urand`, `irand`, `frand`, `rand32`, `rand_chance`, `roll_chance_f`, `roll_chance_i`. Not `std::rand` or `<random>`.
- **Strings**: `Acore::StringFormat(fmt, args...)` (`{}` placeholders) — `src/common/Utilities/StringFormat.h`.
- **Config**: `sConfigMgr->GetOption<T>("Name", default)`.
- **Namespace**: project-wide `Acore::` (no `Trinity::` remnants — rename when porting from upstream forks).
- **Long-lived references**: don't store a raw `Player*` / `Creature*` / `Unit*` past the current call/tick — the object can be removed (logout, despawn, instance unload) and the pointer dangles. Store the `ObjectGuid` and resolve at use time via `ObjectAccessor::FindPlayer(guid)`, `Map::GetCreature(guid)`, etc.
- **DB queries**: use `PreparedStatement` (via `WorldDatabase` / `CharacterDatabase` / `LoginDatabase` and the prepared-statement enums), not raw query strings. Non-blocking reads go async: `_queryProcessor.AddCallback(db.AsyncQuery(stmt).WithPreparedCallback(...))` (or `WithCallback`). Multi-statement writes wrap in `SQLTransaction` + `Execute` / `AppendPreparedStatement`.
- **Timed actions in AI**: use `EventMap` (event id → delay; simple) or `TaskScheduler` (lambdas, repeats, cancellation), both members of `CreatureAI` — don't roll your own tick counters. See any boss script under `src/server/scripts/`.

## Scripting registration

Scripts inherit from a `ScriptObject` subclass (`SpellScript`, `AuraScript`, `CreatureScript`, `InstanceMapScript`, `GameObjectScript`, `CommandScript`, …). Two registration styles coexist:

- **Spell / aura scripts**: `RegisterSpellScript(ClassName)` (or `RegisterSpellAndAuraScriptPair(...)`) inside `AddSC_<name>()`.
- **Creature scripts**: prefer `RegisterCreatureAI(ClassName)` for new code; legacy zones still use `new ClassName();`. Match the surrounding pattern.

Then declare and call `AddSC_<name>()` from the regional loader (`Spells/spells_script_loader.cpp`, `EasternKingdoms/eastern_kingdoms_script_loader.cpp`, …).

**SmartAI** (data-driven creature behaviour) lives in the world DB's `smart_scripts` table, not C++ (engine: `src/server/game/AI/SmartScripts/`). For new creature behaviour prefer SmartAI (via the SQL update workflow); reach for `CreatureScript` only when SmartAI's event/action vocabulary isn't enough.

**Module hooks** (e.g. `OnPlayerLogin`, `OnWorldUpdate`, `OnSpellCast`) are declared in `src/server/game/Scripting/ScriptDefines/*.h`. Implement by inheriting the matching base (`PlayerScript`, `WorldScript`, …) and registering with `new MyClass();` (or its `RegisterXxxScript` macro) inside `AddSC_<name>()`. Full list: https://www.azerothcore.org/wiki/hooks-script.

Custom (non-upstream) scripts go in `src/server/scripts/Custom/` (gitignored).
