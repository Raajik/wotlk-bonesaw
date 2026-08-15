# Bonesaw client updates

Repo: https://github.com/Raajik/wotlk-bonesaw

The launcher only replaces allowlisted files, and only after a SHA256 match:

- `Data/patch-Y.MPQ`
- `Data/enUS/patch-enUS-4.MPQ`
- `Bonesaw.version`
- `Wow.exe` (optional, private releases only)

It will not update while `Wow.exe` is running.

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

## GitHub release

Do not attach `Wow.exe` to a public release. Players already have a client; they patch it once locally with `tools/client-patch/patch_wow_exe.py`.

1. Bump `Bonesaw.version`.
2. Run `python tools/client-update/make_manifest.py`.
3. Create a GitHub release tagged `v0.1.0` (same as the version).
4. Attach `Bonesaw.manifest.json`, `patch-Y.MPQ`, `patch-enUS-4.MPQ`.
