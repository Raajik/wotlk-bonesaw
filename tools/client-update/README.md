# Bonesaw client updates

Repo: https://github.com/Raajik/wotlk-bonesaw

The launcher only replaces allowlisted files, and only after a SHA256 match:

- `Data/patch-Y.MPQ`
- `Data/enUS/patch-enUS-4.MPQ`
- `Data/enGB/patch-enGB-4.MPQ`
- `Bonesaw.version`
- `Wow.exe` (optional, private releases only)

It will not update while `Wow.exe` is running.

Players get files from **GitHub Releases/latest**, not from a local copy on one machine. `Bonesaw.bat` checks `https://github.com/Raajik/wotlk-bonesaw/releases/latest`. A deploy that never creates that release leaves everyone else on the old MPQs.

## Local install

Copy into the Bonesaw client folder:

- `Bonesaw.bat`
- `BonesawLauncher.ps1`
- `Bonesaw.version`
- `Bonesaw.update.json`

Launch with `Bonesaw.bat` so the updater can run. `Wow.exe` still starts the game with no check.

`Bonesaw.update.json` is set to `Raajik/wotlk-bonesaw`.

To push a newly built MPQ / patched exe onto this machine after closing Wow:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/client-update/deploy_client.ps1
```

## Ship to all players

Do this on every Bonesaw / Living Gear ship that changes client files (MPQs, addon, launcher). Server-only ships still bump the version and Discord notes.

Push **only** to origin `Raajik/wotlk-bonesaw`. Never push to remote `playerbots` / `mod-playerbots/azerothcore-wotlk`. Do not force-push `main`. The `Playerbot` branch does not share history with `origin/main`; put updater files on a branch based on `origin/main`, or create the GitHub release from the built assets.

1. Close Wow (MPQs are locked while it is open).
2. Bump `Bonesaw.version` (every ship, including server-only; Discord notes use this number). Do not skip versions.
3. `python tools/client-patch/build_patch.py`
4. `powershell -NoProfile -ExecutionPolicy Bypass -File tools/client-update/deploy_client.ps1`
5. `python tools/client-update/make_manifest.py` (hashes the **client folder** after deploy)
6. Commit and push updater files (`Bonesaw.version`, `Bonesaw.manifest.json`, launcher, README) to origin. Prefer a branch from `origin/main`.
7. Create a GitHub release tagged `vX.Y.Z` (same as the version) and attach:
   - `Bonesaw.manifest.json`
   - `patch-Y.MPQ`
   - `patch-enUS-4.MPQ`
   - `patch-enGB-4.MPQ`
   - `BonesawPatchExe.bat`
   - `BonesawPatchExe.ps1`
   - `Bonesaw.bat`
   - `BonesawLauncher.ps1`
   - `Bonesaw.update.json`
8. Discord: numbered `Bonesaw X.Y.Z - patch notes`. Tell players to close Wow and run `Bonesaw.bat`.

Do not attach `Wow.exe` to a public release. Players already have a client; they patch it once locally with `BonesawPatchExe.bat` (or `tools/client-patch/patch_wow_exe.py`).

If worldserver must reboot for C++: `powershell tools/restart_worldserver.ps1` (45s warn + saveall) before docker replace.

Example:

```
gh release create v0.1.17 --repo Raajik/wotlk-bonesaw --latest --title "Bonesaw client 0.1.17" --notes "Close Wow, then run Bonesaw.bat." ^
  tools/client-update/Bonesaw.manifest.json ^
  tools/client-patch/dist/patch-Y.MPQ ^
  tools/client-patch/dist/patch-enUS-4.MPQ ^
  tools/client-patch/dist/patch-enGB-4.MPQ ^
  tools/client-update/BonesawPatchExe.bat ^
  tools/client-update/BonesawPatchExe.ps1 ^
  tools/client-update/Bonesaw.bat ^
  tools/client-update/BonesawLauncher.ps1 ^
  tools/client-update/Bonesaw.update.json
```
