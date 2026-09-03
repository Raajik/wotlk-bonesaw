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

Dependencies: `wine` to play, plus `ydotool` and `libsecret` for auto-login.
The installer names the package for your distro if anything is missing.

Auto-login also needs the `ydotoold` daemon, since `/dev/uinput` is root-only:

```bash
sudo install -m644 ydotoold.service /etc/systemd/system/
sudo sed -i "s/1000:1000/$(id -u):$(id -g)/" /etc/systemd/system/ydotoold.service
sudo systemctl daemon-reload && sudo systemctl enable --now ydotoold
```

It runs as root but with `--mouse-off` and a `0600` control socket owned by your
user, so only you can inject input. That is deliberately *not* the usual "add
yourself to the `input` group" advice, which would grant read access to every
input device on the machine — a keylogging surface. Without the daemon the
launcher still starts the game, it just skips auto-login.

## Use

```bash
bonesaw                 # launch, logging in if credentials are stored
bonesaw --set-login     # store account + password
bonesaw --forget-login  # remove them
bonesaw --no-login      # skip auto-login for one launch
bonesaw --set-dir DIR   # point at a different client
bonesaw --sessions      # show recorded session/shutdown timings
```

## How auto-login works

It mirrors `src/login.rs` from the Windows launcher rather than inventing a
second design:

- The **account name** is written to `WTF/Config.wtf` as `SET accountName`, so
  the *client* prefills the field. The launcher never types it. This happens
  before the game starts, because WoW rewrites `Config.wtf` on a clean exit.
- The **password** is read from the Secret Service keyring and piped to
  `ydotool type --file -` on **stdin**. Passing it as an argument would expose
  it in `ps` to every process on the machine.
- With the account prefilled the client focuses the password field itself, so
  the password is typed straight in and `Return` pressed 250ms later. **No Tab**
  — a Tab moves focus off the password field and the password ends up somewhere
  visible.

Timing follows `paste_login()` on Windows: the game must hold focus for 6
seconds, then 250ms between the password and `Return`.

### Why ydotool and not wtype

`wtype` looks like the obvious tool here and it does type correctly, but it
**reliably wedges the Wine client**. It creates a Wayland virtual keyboard with
its own keymap and tears it down when it exits, and Wine does not survive that
keymap churn — the client keeps rendering but never receives input again.

This was confirmed by suppressing `Return` entirely: the client still froze with
no Enter ever sent, which ruled out every key-timing explanation. `ydotool`
injects through `/dev/uinput` as a real kernel input device, so there is no
virtual keyboard, no keymap swap, and nothing torn down afterwards.

If you ever need to type the password but press Enter yourself:

```bash
BONESAW_SEND_ENTER=0 bonesaw
```

The password is never written to disk. Only the account name is, in
`~/.config/bonesaw/account`.

On Linux this is a little safer than the Windows path, which pastes via the
clipboard and briefly exposes the password to any clipboard watcher. `ydotool`
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

## Session tracing

Every launch appends a row to `~/.local/state/bonesaw/sessions.csv`:

```bash
bonesaw --sessions
```

```
started                    session_s  quiet_s  wineserver_s  peak_rss_mb  threads  samples
2026-09-02T22:53:05-06:00  38         5        4             416          50       38
```

| column | meaning |
|---|---|
| `session_s` | how long the client ran |
| `quiet_s` | seconds it sat with no CPU before the process exited |
| `wineserver_s` | extra seconds `wineserver` lingered after that |
| `peak_rss_mb` | peak resident memory |

`quiet_s` is the "closing takes forever" number. The launcher cannot see the
keypress that closed the window, so it infers when shutdown began from the
render loop: the client burns CPU continuously while running, and that stops the
moment it starts shutting down. Collecting this across real sessions shows
whether teardown scales with session length or peak memory, which a short test
session never reveals.

Sampling is one row per second from `/proc` — a few file reads, no subprocesses.
Set `BONESAW_TRACE=0` to disable.

Caveat: on the login and character-select screens the client uses little CPU, so
`quiet_s` can read high for sessions that never entered the world. In-world
sessions give a clean signal.

## Environment

| variable | default | meaning |
|---|---|---|
| `BONESAW_DIR` | `~/.config/bonesaw/gamedir` | client directory |
| `BONESAW_SETTLE` | `6` | seconds the game must hold focus before typing |
| `BONESAW_WINDOW_TIMEOUT` | `90` | overall budget to start, focus and settle |
| `BONESAW_KEY_DELAY` | `20` | milliseconds between keystrokes |
| `YDOTOOL_SOCKET` | `/run/ydotoold.socket` | ydotoold control socket |
| `BONESAW_TRACE` | `1` | `0` disables session tracing |
| `BONESAW_SEND_ENTER` | `1` | `0` types the password but leaves Enter to you |
| `WINEPREFIX` | `~/.local/share/wineprefixes/bonesaw` | Wine prefix |
