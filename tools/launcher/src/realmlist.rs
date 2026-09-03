//! Keep realmlist.wtf pointed at the Bonesaw realm. The host normally comes
//! from the manifest, so the realm can move without shipping a new exe.
//!
//! A `Bonesaw.realmlist` file next to the exe overrides that, for machines the
//! shared address is wrong for - most obviously whoever is hosting the server
//! and needs 127.0.0.1. The launcher reads that file and never writes it, so a
//! local choice survives every update.

use crate::util::R;
use std::path::Path;

pub const OVERRIDE_FILE: &str = "Bonesaw.realmlist";

/// A bare host or host:port. realmlist.wtf is written verbatim, so anything
/// that could smuggle a second SET line - quotes, spaces, newlines - is out.
pub fn valid_host(host: &str) -> bool {
    !host.is_empty()
        && host.len() <= 253
        && host
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_' | ':'))
}

/// The local override, if there is a usable one. Returns Err with a reason when
/// the file exists but cannot be used, so the caller can say so out loud rather
/// than silently ignoring what someone wrote.
pub fn read_override(client: &Path) -> Option<Result<String, String>> {
    let path = client.join(OVERRIDE_FILE);
    let text = std::fs::read_to_string(&path).ok()?;
    let host = text
        .lines()
        .map(str::trim)
        .find(|l| !l.is_empty() && !l.starts_with('#'));
    Some(match host {
        None => Err(format!("{OVERRIDE_FILE} is empty")),
        Some(h) if !valid_host(h) => Err(format!("{OVERRIDE_FILE} is not a host: {h}")),
        Some(h) => Ok(h.to_string()),
    })
}

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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_hosts_and_ips() {
        for h in ["127.0.0.1", "100.65.58.17", "logon.example.com", "host:3724"] {
            assert!(valid_host(h), "rejected {h}");
        }
    }

    #[test]
    fn rejects_anything_that_could_inject_a_set_line() {
        for h in ["", "a b", "a\"b", "a\nSET x \"y\"", "a;b"] {
            assert!(!valid_host(h), "accepted {h:?}");
        }
    }
}
