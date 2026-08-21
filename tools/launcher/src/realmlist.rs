//! Keep realmlist.wtf pointed at the Bonesaw realm. The host comes from the
//! manifest (already validated as a bare host[:port]) so the realm can move
//! without shipping a new exe.

use crate::util::R;
use std::path::Path;

pub fn write(client: &Path, host: &str) -> R<Vec<String>> {
    let want = format!("set realmlist {host}\n");
    let mut changed = Vec::new();
    for loc in ["enUS", "enGB"] {
        let dir = client.join("Data").join(loc);
        if !dir.is_dir() {
            continue;
        }
        let path = dir.join("realmlist.wtf");
        let same = std::fs::read_to_string(&path)
            .map(|got| got.trim() == want.trim())
            .unwrap_or(false);
        if same {
            continue;
        }
        std::fs::write(&path, &want).map_err(|e| format!("write {}: {e}", path.display()))?;
        changed.push(format!("Data\\{loc}\\realmlist.wtf"));
    }
    Ok(changed)
}
