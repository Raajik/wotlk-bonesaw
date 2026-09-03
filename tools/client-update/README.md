# Bonesaw client updates

Repo: https://github.com/Raajik/wotlk-bonesaw

Players run **`Bonesaw.exe`** (source in `tools/launcher/`). It is a single self-contained
file: the client patch MPQs are embedded inside it, so a release is one asset with one hash.

Each launch it:

1. refuses to run unless it is sitting in a real 3.3.5a folder (`Wow.exe` + `Data\common.MPQ`);
2. reads `Bonesaw.manifest.txt` from the permanent `updater` GitHub release and, if a newer
   version exists, downloads the new exe, verifies size + SHA256, swaps itself out and relaunches;
3. writes `Data\patch-Y.MPQ` and the locale patch into whichever of `Data\enUS` / `Data\enGB`
   the client actually has, skipping anything already correct;
4. patches the player's own `Wow.exe` (backup at `Wow.exe.stock`) so custom FrameXML loads;
5. writes `realmlist.wtf` from `Bonesaw.realmlist` if that file exists, otherwise from the
   manifest's `realmlist` line;
6. starts `Wow.exe`.

Every failure short of "you are not in a client folder" is non-fatal: the game still starts.
`BonesawLauncher.log` (in `Logs\` if that folder exists) records what happened.

`Wow.exe` is never downloaded and must never be attached to a public release. It is a Blizzard
binary; we only patch the copy the player already owns.

## The manifest

`Bonesaw.manifest.txt` is published as an asset on the permanent `updater` release
(https://github.com/Raajik/wotlk-bonesaw/releases/download/updater/Bonesaw.manifest.txt) --
a stable URL that never changes, so a ship only re-uploads the asset with
`gh release upload updater --clobber`. It used to be committed to `main`, but `main`
shares no history with the working branch, which forced a fragile cherry-pick dance
on every ship. `build_launcher.py` regenerates it:

```
BONESAW 1
version 0.1.50
realmlist logon.example.com
file <sha256> <size> Bonesaw.exe
url https://github.com/Raajik/wotlk-bonesaw/releases/download/v0.1.50/Bonesaw.exe
```

The launcher refuses any `url` outside `https://github.com/Raajik/wotlk-bonesaw/releases/download/`
and any `file` line naming something other than `Bonesaw.exe`. Unknown keys are ignored, so new
fields do not break older launchers.

The `realmlist` line is hand-written once and carried forward by `build_launcher.py`. Without it
the launcher leaves `realmlist.wtf` alone.

A `Bonesaw.realmlist` file in the client folder overrides the manifest for that machine. It holds
one bare host (blank lines and `#` comments are skipped), and the launcher only ever reads it, so
a local choice survives updates. This is how whoever hosts the server keeps `127.0.0.1` while
everyone else follows the manifest. An unusable override is reported in the log and the status
line, and the manifest value is used instead.

## Ship to all players

Do this on every Bonesaw / Living Gear ship that changes client files (MPQs, addon, launcher).
Server-only ships still bump the version and get Discord notes.

Push **only** to origin `Raajik/wotlk-bonesaw`. Never push to remote `playerbots` /
`mod-playerbots/azerothcore-wotlk`.

1. Close Wow (MPQs are locked while it is open).
2. Bump `Bonesaw.version` (every ship, including server-only; Discord notes use this number).
   Do not skip versions.
3. `python tools/client-patch/build_patch.py`
4. `python tools/launcher/build_launcher.py`  -  copies the new MPQs into the launcher payload,
   builds the exe, copies it to `tools/launcher/dist/Bonesaw.exe`, and rewrites
   `Bonesaw.manifest.txt`. Safe to re-run: it rebuilds only when something actually changed.
5. `powershell -NoProfile -ExecutionPolicy Bypass -File tools/client-update/deploy_client.ps1`
   to put the build on this machine.
6. Create the GitHub release **before** pushing the manifest, because the manifest points at it.
   Upload `dist/Bonesaw.exe`, which is the exact file the manifest hashed:

```
gh release create v0.1.50 --repo Raajik/wotlk-bonesaw --latest --title "Bonesaw client 0.1.50" --notes "Run Bonesaw.exe." tools/launcher/dist/Bonesaw.exe
```

7. `python tools/launcher/build_launcher.py --verify`  -  confirms the manifest still describes the
   uploaded file. The exe is not byte-reproducible, so a rebuild between hashing and uploading
   would leave every player unable to update.
8. Publish the manifest: `gh release upload updater tools/client-update/Bonesaw.manifest.txt
   --repo Raajik/wotlk-bonesaw --clobber`. Players see the update on their next launch.
9. Discord: numbered `Bonesaw X.Y.Z - patch notes`. Tell players to close Wow and run
   `Bonesaw.exe`.

If worldserver must reboot for C++: `powershell tools/restart_worldserver.ps1` (45s warn +
saveall) before docker replace.

## Legacy .bat updater

`Bonesaw.bat` + `BonesawLauncher.ps1` + `Bonesaw.update.json` + `Bonesaw.manifest.json` are the
old path. They still work, but they cannot update themselves, they need `BonesawPatchExe.bat` run
by hand once, and they use the rate-limited GitHub API. Keep attaching them (plus the three MPQs
and `make_manifest.py` output) to releases for a few versions so nobody is stranded, then drop
them. Rebuilding those assets still means running `python tools/client-update/make_manifest.py`
after a deploy.
