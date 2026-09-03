# BoneScrape

Passive data recorder for WotLK 3.3.5a clients, plus an offline parser. Point it
at a custom server, play normally, and it writes down everything the server sends
your client so the features can be studied and reimplemented.

## Install

Copy the `BoneScrape` folder into the client's `Interface\AddOns\`:

```bash
cp -r tools/scraper/BoneScrape "B:/Games/WoW 3.3.5/Peloria/Interface/AddOns/"
```

Log in, play. `/bs` prints what has been captured so far. Data is only flushed to
disk on `/reload` or a clean logout — an alt-F4 loses the session.

## What it records

| Bucket | Source | Why it matters |
|---|---|---|
| `items` | every item link that crosses your screen (bags, bank, equipped, vendor, loot, quest rewards, mail, AH, chat) | full `GetItemInfo` fields plus the **complete tooltip text**, including every custom line the server injects |
| `items[id].v` / `vtip` | distinct suffix/enchant/random-property tails | on servers with item upgrade or soulbind systems, the variant tail *is* the feature |
| `spells` | combat log, auras on any unit, spell links in chat | id → name → full tooltip |
| `book` | your spellbook | tooltips for everything you have been granted |
| `npcs` | target and mouseover | entry id, level, max HP/mana, classification, creature type, reaction, and up to 25 sighting coordinates each |
| `vendors` | opening a merchant | full inventory with copper price, stock, and **extended cost** (token/currency prices) |
| `gossip` | opening any gossip NPC | every distinct menu state, body text and all options, deduped by signature |
| `quests` / `questlog` | quest frames and the log | text, objectives, rewards, money, giver entry and coords |
| `trainers` | opening a trainer | service list with cost, level req, skill req |
| `loot` | looting a corpse | per-source-entry drop table with observed quantity ranges |
| `craft` | opening a tradeskill | recipes and reagents |
| `addon` | `CHAT_MSG_ADDON` | **every prefix and up to 200 verbatim payload samples.** This is the protocol behind the server's custom UI panels |
| `chat` | system messages, monster say/yell/whisper, boss emotes | feature announcements, and any item/spell links in them get harvested |
| `currency`, `talents` | on change | custom currencies and talent layouts |

Nothing is sent, clicked, targeted, cast, or moved. It only reads state the
client already has, which makes it behaviourally identical to Auctioneer or
SilverDragon.

## Parse

```bash
python tools/scraper/parse_bonescrape.py "B:/Games/WoW 3.3.5/Peloria/WTF/Account/UNABLE3022/SavedVariables/BoneScrape.lua" -o out/peloria
```

Writes `out/peloria.json` (everything) and `out/peloria.md`, a digest ordered by
usefulness: the addon protocol first, then gossip menus, then **items whose
tooltip lines do not match any stock 3.3.5 shape** — that filter is the fastest
way to spot a custom mechanic — then vendors, spells above id 80000, loot,
quests, and server broadcasts.

## Blind spots

- Only what the server sends *your character*. Gear you never see, spell
  coefficients, proc internals, and every server-side formula stay invisible;
  those get reconstructed from tooltip text plus observed combat log numbers.
- GameObjects have no unit token, so chests/nodes are not captured by entry id.
  They show up as `loot` rows under `unknown`.
- Quests seen in a quest frame are keyed by title (the client has no quest id
  there); quests in your log are keyed by real id from the quest link.

## wdb_dump.py -- the client's own cache

Every `.wdb` under `Cache\WDB\<locale>\` is the client's stored copy of the
server's query responses, packet body verbatim. Decoding it recovers almost the
entire server-side template row for every object the client has ever asked about
— no play session required.

```bash
python tools/scraper/wdb_dump.py "B:/Games/WoW 3.3.5/Synastria/Cache/WDB/enUS" -o out/syn --sql
```

Writes `out/syn.json`, `out/syn.md` (a digest highlighting entries outside the
retail 3.3.5a id range), and with `--sql`, `REPLACE INTO` statements for
`item_template`, `creature_template` and `gameobject_template`.

Handles item, creature, gameobject, quest, npctext (gossip), itemname, pagetext
and itemtext caches. Each decoder asserts it consumed the payload exactly; a
non-zero leftover is reported rather than silently emitted, since it would mean
the server widened the struct — a finding in its own right.

Current yield from the two clients on disk:

| | Synastria | Peloria |
|---|---|---|
| items | 1181 | 816 |
| creatures | 420 | 586 |
| gameobjects | 317 | 680 |
| quests | 25 | 3 |
| gossip texts | 2 | 11 |

Zero suspect records on either, and all 997 gameobjects decoded at the stock
24-field data width, so neither server has widened these structs.

What the query response does *not* carry: creature combat stats (level, damage,
armor, faction, AI), loot references, spawn data, and `item_template.BuyCount` /
`spellppmRate`. Those stay server-side and have to come from BoneScrape or
observation.

The caches only hold what that client actually queried, so they grow as you
play. Re-run the dump after a session and diff.

## wowext_dump.py -- Synastria's client extension

`WoWExt.dll` registers a spare packet handler so the server can patch client
DBCs at runtime — inventing spells, items, item sets and extended costs without
shipping a DBC edit — and caches what it receives in `WoWExt*.bin`. The formats
are undocumented.

```bash
python tools/scraper/wowext_dump.py "B:/Games/WoW 3.3.5/Synastria" -o out/wowext
```

### Solved

| Source | Yield |
|---|---|
| `WoWExt.bin` | **fully unpacked** — 63 LZ4 blobs, ~1 MB, of which **~750 KB is their entire custom client-side Lua source** |
| `WoWExt.dll` RTTI | all **58 server-side mod types** and the **90-function `Custom_*` Lua API** |
| `WoWExt2.bin` | **5,700 item names**, one record each |
| `WoWExtObjLoc.bin` | 24-byte header plus a **42,951-name string table** of tracked objects, creatures and quests |
| `WoWExt3.bin` | plaintext Lua config, already readable |

#### WoWExt.bin container

Read out of the writer at `0x10044f70` and the decompressor at `0x100343e0`:

```
u32 magic 0x2838c53a
u32 version
u32 blob count
per blob:
    u32 key
    u8  md5[16]      md5 of the packed body, verified at 0x100460d0
    u32 length
    u8  body[length] LZ4 block, into a 64 KB buffer
```

The body is stock **LZ4 block format**. 51 blobs decompress to pure Lua and are
written to `<out>_lua/blob_NN.lua`; the other 12 are DBC mod streams, written to
`<out>_mods/blob_NN.bin` alongside a `.txt` of their extracted strings.

The DLL patches these client DBCs in memory: `SpellRec`, `ItemRec`,
`ItemSetRec`, `ItemExtendedCostRec`, `SpellRuneCostRec`, `SkillLineAbilityRec`,
`SkillRaceClassInfoRec`, `CharBaseInfoRec`, `CharStartOutfitRec`, and four
`SpellVisual*` tables.

The `Custom_*` API groups into: loot filter (13 functions), item
collection/attunement (23), quest state (9), tracked object locations (5),
transmog (4), zone helpers (4), spell introspection (5).

### What the Lua blobs contain

Their whole custom UI, as source. Grep it — this is the feature list:

| Blob | Feature |
|---|---|
| 06 | Perk manager (`PerkMgrFrame`) |
| 08, 09, 16, 20, 21 | Item attunement — skip lists, affix manager, attuned-item highlighting |
| 27 | Loot filter UI (`LFilterFrame`) |
| 17 | Reagent bank (`RBankFrame`) |
| 18 | Leaderboards (`LBoardFrame`) |
| 34 | Transmog, incl. per-race model height offsets |
| 11 | Mythic dungeon difficulty injected into `UnitPopupButtons` |
| 38 | Teleport/warp UI (`TPortFrame`, `TPortData`) |
| 42 | Bounty Hunter's Guild |
| 46, 47, 48 | Item hunt / drop-chance UI |
| 37, 43 | Multi-class system (`CMCGetClassCount`, `CMCIsDualSingleClass`) |
| 10, 24, 25 | Extra talent specializations (5th and 6th) |
| 52 | Synergy text database |
| 30, 53 | Hunter pet aspects, druid spec picker |
| 35 | Changelog data |
| 59, 60, 61 | Auto-equip addon |

### Partly solved

- **The 12 mod-stream blobs.** Strings come out cleanly — they are u16
  length-prefixed with no terminator, followed by f32 parameter tuples, which is
  how the custom perk and spell names and tooltips are recovered. The record
  framing around them is not fully identified, so the tool emits raw bytes plus
  extracted strings rather than typed records.

### Not solved

- **`WoWExtObjLoc.bin` record area.** 1.6 MB after the string table, ~6.4 bytes
  per record, timestamped (unix seconds, little-endian) and delta-coded. Names
  are recovered; the coordinates they attach to are not.
- **`WoWExtItemLoc.bin`.** 14 MB high-entropy bitset. Almost certainly the
  client-side "items you have acquired" cache behind `Custom_IsHaveItem` /
  `Custom_CacheHaveItems`. No feature data in it.
