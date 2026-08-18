# Bonesaw

WotLK 3.3.5a private realm.

## Play

Close Wow, then run `Bonesaw.bat` from your client folder. It pulls the latest patches from [Releases](https://github.com/Raajik/wotlk-bonesaw/releases/latest) and starts the game. `Wow.exe` alone does not check for updates.

You need a 3.3.5a client (enUS or enGB). The updater never downloads `Wow.exe`.

First time: copy `Bonesaw.bat`, `BonesawLauncher.ps1`, `Bonesaw.update.json`, `BonesawPatchExe.bat`, and `BonesawPatchExe.ps1` into the client folder. Run `BonesawPatchExe.bat` once. Stock Wow.exe will refuse our FrameXML patch with "interface files are corrupt." Then always launch with `Bonesaw.bat`.

## Living Gear

Equipped gear gains XP and levels. Grown stats apply on your character. `*Windblown` opens the attune window.
