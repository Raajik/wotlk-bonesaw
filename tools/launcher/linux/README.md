# Bonesaw on Linux

The Windows launcher (`Bonesaw.exe`) is a native Win32 app: it self-updates,
writes the client patches it carries, patches `Wow.exe` and starts the game.
None of that is portable, so Linux gets a shell launcher that does the one job
players actually need — start the game under Wine, and log in for you.

Everything else (patching `Wow.exe`, writing `patch-Y.MPQ`) still comes from a
Windows launcher run or a manual copy of `tools/client-patch/dist/`.

## Install

```bash
tools/launcher/linux/install.sh "/path/to/World of Warcraft/3.3.5/Bonesaw"
```

Installs `bonesaw` to `~/.local/bin`, a desktop entry with the Bonesaw mark,
and remembers the client directory. No root, nothing outside `~/.local`.

Dependencies: `wine` to play, plus `wtype` and `libsecret` for auto-login.
The installer names the package for your distro if anything is missing.

## Use

```bash
bonesaw                 # launch, logging in if credentials are stored
bonesaw --set-login     # store account + password
bonesaw --forget-login  # remove them
bonesaw --no-login      # skip auto-login for one launch
bonesaw --set-dir DIR   # point at a different client
```

## How auto-login works

It mirrors `src/login.rs` from the Windows launcher rather than inventing a
second design:

- The **account name** is written to `WTF/Config.wtf` as `SET accountName`, so
  the *client* prefills the field. The launcher never types it. This happens
  before the game starts, because WoW rewrites `Config.wtf` on a clean exit.
- The **password** is read from the Secret Service keyring and piped to `wtype`
  on **stdin**. Passing it as an argument would expose it in `ps` to every
  process on the machine.
- With the account prefilled the client focuses the password field itself, so
  the password is typed straight in and `Return` pressed 250ms later. **No Tab**
  — a Tab moves focus off the password field and the password ends up somewhere
  visible.

Timing matches `paste_login()` on Windows: a 12s window timeout, a 6s settle
once the game has focus, then 250ms before `Return`. Keystrokes are spaced 12ms
apart (`wtype` defaults to 0, which fires the whole password at XWayland in one
burst and can wedge the client).

`Return` is pressed and released as explicit ops with a trailing sleep, not as a
single `-k`. From `wtype(1)`: *"modifiers get released automatically once the
program terminates"* — `wtype` drops its virtual keyboard the moment it exits,
so a release racing that teardown leaves the client seeing `Return` held down,
which wedges it on the login screen.

If the client still locks up on the synthetic `Return`, type the password but
press Enter yourself:

```bash
BONESAW_SEND_ENTER=0 bonesaw
```

The password is never written to disk. Only the account name is, in
`~/.config/bonesaw/account`.

On Linux this is a little safer than the Windows path, which pastes via the
clipboard and briefly exposes the password to any clipboard watcher. `wtype`
injects keystrokes directly, so nothing reaches the clipboard.

### The focus guard

Synthetic keystrokes go to whatever window has focus. The launcher checks that
the game is focused **twice** — once waiting for it to come up, and again
immediately before typing — so that alt-tabbing during the wait cannot send
your password to another window. On a miss it gives up and you log in by hand.

Known limitation: the guard checks the *window*, not which *field* has focus.
If the client has not focused the account field when the `Tab` lands, the
password can end up somewhere visible. The Windows launcher has the same
caveat and handles it the same way, with a generous settle. Raise it if the
login screen is slow to appear:

```bash
BONESAW_SETTLE=12 bonesaw
```

## Hyprland / Omarchy

WoW asks the WM to maximize, which Omarchy suppresses for every window, so the
game is left at whatever size it started with. A window rule fixes it:

```lua
o.window("^wow.*\\.exe$", {
  fullscreen = true,
  size = { 3440, 1440 },   -- your monitor
  center = true,
  tag = "-default-opacity", -- a translucent game window is not what you want
  opacity = "1 1",
  idle_inhibit = "fullscreen",
})
```

Giving the game its own workspace keeps alt-tab from knocking it out of
fullscreen, since Hyprland drops fullscreen when focus moves to another window
on the same workspace:

```lua
o.window("^wow.*\\.exe$", { workspace = "10" })
hl.workspace_rule({ workspace = "10", monitor = "DP-3" })
```

Pin the workspace to the right monitor if any of your displays are rotated — a
`transform` makes a 1920x1080 panel report as 1080x1920, and the game will
happily open sideways on it.

## Environment

| variable | default | meaning |
|---|---|---|
| `BONESAW_DIR` | `~/.config/bonesaw/gamedir` | client directory |
| `BONESAW_SETTLE` | `6` | seconds after focus before typing |
| `BONESAW_WINDOW_TIMEOUT` | `12` | seconds to wait for the game window |
| `BONESAW_KEY_DELAY` | `12` | milliseconds between keystrokes |
| `BONESAW_SEND_ENTER` | `1` | `0` types the password but leaves Enter to you |
| `WINEPREFIX` | `~/.local/share/wineprefixes/bonesaw` | Wine prefix |
