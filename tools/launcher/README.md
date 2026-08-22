# Bonesaw.exe

The player-facing launcher. One file, dropped into a 3.3.5a client folder and pinned to the
taskbar: it updates itself, writes the client patches it carries, patches `Wow.exe`, and starts
the game.

The shape is borrowed from the Peloria launcher --- a tiny program whose resources *are* the
payload, and a manifest that lists exactly one file: the launcher itself. Everything else is
derived from what the exe already contains, which is why an update is a single download with a
single hash and nothing can drift out of sync.

## Build

```bash
python tools/launcher/build_launcher.py
```

That copies the MPQs from `tools/client-patch/dist/` into `payload/`, builds the release exe,
copies it to `dist/Bonesaw.exe`, and rewrites `tools/client-update/Bonesaw.manifest.txt`. Run
`tools/client-patch/build_patch.py` first. `cargo build --release` alone works once `payload/` is
populated.

Re-running the script is safe: the payload is only re-copied when the bytes differ, and nothing
else forces a rebuild. That matters because the exe is **not** byte-reproducible - MSVC stamps the
PE - so an unnecessary rebuild would invalidate a hash that may already be published. Upload
`dist/Bonesaw.exe` (the file that was hashed), and run `build_launcher.py --verify` afterwards to
confirm the manifest still matches it.

`payload/`, `dist/`, and `target/` are gitignored; the MPQs and the exe are build output, never
committed.

## Layout

| file | what it does |
| --- | --- |
| `src/main.rs` | the run sequence, and the log |
| `src/manifest.rs` | fetch and strictly parse the manifest |
| `src/selfupdate.rs` | download, verify, rename-self, relaunch |
| `src/payload.rs` | the embedded MPQs and the extract-if-changed check |
| `src/wowpatch.rs` | the 11-byte `Wow.exe` patch table |
| `src/realmlist.rs` | `realmlist.wtf`, the `Bonesaw.realmlist` override, and host validation |
| `src/ui.rs` | the Win32 progress window |
| `build.rs` | bakes `Bonesaw.version` in as `BONESAW_VERSION` |

## Why it is safe to point at a network

- The manifest supplies **no paths**. Every destination is a compile-time constant, so there is
  nothing to traverse.
- The only thing ever downloaded is `Bonesaw.exe`, only from a
  `github.com/Raajik/wotlk-bonesaw/releases/download/` URL, and only after its size and SHA256
  match the manifest. A mismatch leaves the installed exe untouched.
- `Wow.exe` is never fetched. It is patched in place from the player's own copy, after a backup
  to `Wow.exe.stock`, and only when every one of the 11 sites reads exactly as expected.
- The update is skipped entirely while Wow is running.

## Testing the update path

Release builds are pinned to the real manifest URL. Debug builds honour `BONESAW_MANIFEST_URL`
and will accept a `http://127.0.0.1:` download URL, so the whole self-update cycle can be run
against `python -m http.server`. That hook is `#[cfg(debug_assertions)]` and is not present in
anything a player runs.

```bash
cargo test          # manifest parsing, including the rejection cases
```
