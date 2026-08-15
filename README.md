# wotlk-bonesaw

Bonesaw overlay for AzerothCore 3.3.5a (Playerbots). This is not a full core fork.

Drop `modules/mod-living-gear` into an AzerothCore `modules/` tree, apply the SQL under `data/sql/updates/pending_db_*`, and ship the client MPQs from [Releases](https://github.com/Raajik/wotlk-bonesaw/releases).

## Client auto-update

The Bonesaw client folder already has `Bonesaw.bat`. It checks this repo's latest GitHub release, verifies SHA256, and only writes:

- `Data/patch-Y.MPQ` (*Windblown* Spell.dbc)
- `Data/enUS/patch-enUS-4.MPQ` (Living Gear FrameXML)
- `Bonesaw.version`

It will not replace files while Wow is running. It will not download `Wow.exe`.

First-time setup still needs a locally patched `Wow.exe` (interface-edit + signature bypass). Stock `Wow.exe` plus `patch-enUS-4.MPQ` shows the corrupt FrameXML error. Keep the stock binary as `Wow.exe.stock`.

## Living Gear

Equipped gear gains XP and levels. Grown stats apply on the character sheet. Tooltips show level / XP / extras. `*Windblown` opens the attune window.

Do not run this alongside `mod-attunement-plus`.
