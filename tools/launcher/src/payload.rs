//! The client patch MPQs ride inside the exe. Writing them out is the
//! "Preparing Bonesaw..." step: each destination is a compile-time constant, so
//! nothing the network says can ever influence where bytes land.

use crate::ui::Ui;
use crate::util::{sha256, sha256_file, write_atomic, R};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

static PATCH_Y: &[u8] = include_bytes!("../payload/patch-Y.MPQ");
// enUS and enGB are byte-identical, so the payload carries one copy.
static PATCH_LOCALE: &[u8] = include_bytes!("../payload/patch-enUS-4.MPQ");

const STATE_FILE: &str = "Bonesaw.state";
const LOCALES: [&str; 2] = ["enUS", "enGB"];

struct Target {
    rel: String,
    bytes: &'static [u8],
}

/// Data/patch-Y.MPQ always, plus one locale patch per locale folder the client
/// actually has. We never create a locale folder that does not already exist:
/// inventing Data/enGB on an enUS-only install would be a lie about what
/// locales are installed.
fn targets(client: &Path) -> R<Vec<Target>> {
    let mut out = vec![Target {
        rel: "Data/patch-Y.MPQ".into(),
        bytes: PATCH_Y,
    }];
    for loc in LOCALES {
        if client.join("Data").join(loc).is_dir() {
            out.push(Target {
                rel: format!("Data/{loc}/patch-{loc}-4.MPQ"),
                bytes: PATCH_LOCALE,
            });
        }
    }
    if out.len() == 1 {
        return Err("No Data\\enUS or Data\\enGB folder found in this client.".into());
    }
    Ok(out)
}

pub fn extract(ui: &Ui, client: &Path) -> R<Vec<String>> {
    let targets = targets(client)?;
    let mut state = read_state(&client.join(STATE_FILE));
    let mut written = Vec::new();

    let total: u64 = targets.iter().map(|t| t.bytes.len() as u64).sum();
    let mut seen: u64 = 0;

    for t in &targets {
        let want = sha256(t.bytes);
        let dest = client.join(t.rel.replace('/', "\\"));
        let name = dest
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default();
        ui.progress(&name, seen, total);

        if up_to_date(&dest, t.bytes.len() as u64, &want, state.get(&t.rel)) {
            seen += t.bytes.len() as u64;
            ui.progress(&name, seen, total);
            state.insert(t.rel.clone(), want);
            continue;
        }

        write_atomic(&dest, t.bytes)?;
        written.push(t.rel.clone());
        state.insert(t.rel.clone(), want);
        seen += t.bytes.len() as u64;
        ui.progress(&name, seen, total);
    }

    write_state(&client.join(STATE_FILE), &state);
    Ok(written)
}

/// The state file lets a normal launch skip hashing 4.7 MB from disk. When it
/// disagrees with reality we fall back to hashing the file, so a hand-edited or
/// half-restored client still heals itself.
fn up_to_date(dest: &Path, size: u64, want: &str, recorded: Option<&String>) -> bool {
    let Ok(meta) = std::fs::metadata(dest) else {
        return false;
    };
    if meta.len() != size {
        return false;
    }
    if recorded.map(|r| r == want).unwrap_or(false) {
        return true;
    }
    matches!(sha256_file(dest), Ok(got) if got == want)
}

fn read_state(path: &Path) -> HashMap<String, String> {
    let mut out = HashMap::new();
    if let Ok(text) = std::fs::read_to_string(path) {
        for line in text.lines() {
            if let Some((sha, rel)) = line.trim().split_once(char::is_whitespace) {
                out.insert(rel.trim().to_string(), sha.to_ascii_lowercase());
            }
        }
    }
    out
}

fn write_state(path: &PathBuf, state: &HashMap<String, String>) {
    let mut lines: Vec<String> = state.iter().map(|(rel, sha)| format!("{sha} {rel}")).collect();
    lines.sort();
    let _ = std::fs::write(path, lines.join("\n") + "\n");
}
