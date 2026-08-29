//! The update manifest: a tiny line-oriented text file served as a GitHub
//! release asset. The manifest lives on a permanent "updater" release so its
//! URL never changes - the launcher fetches the same address forever, and a
//! ship only re-uploads the asset. (It used to be committed to the `main`
//! branch and served over raw.githubusercontent.com, but `main` shares no
//! history with the working branch, so every ship needed a manual
//! cherry-pick dance that was routinely forgotten.)
//!
//!     BONESAW 1
//!     version 0.1.50
//!     realmlist logon.example.com
//!     file <sha256> <size> Bonesaw.exe
//!     url https://github.com/Raajik/wotlk-bonesaw/releases/download/v0.1.50/Bonesaw.exe
//!
//! Unknown keys are ignored so a newer manifest never breaks an older launcher.

use crate::util::R;
use std::time::Duration;

const MANIFEST_URL: &str =
    "https://github.com/Raajik/wotlk-bonesaw/releases/download/updater/Bonesaw.manifest.txt";

/// The launcher only ever downloads from our own release URLs.
const URL_PREFIX: &str = "https://github.com/Raajik/wotlk-bonesaw/releases/download/";

/// Release builds are pinned to the two constants above. Debug builds may be
/// aimed at a local server so the update path can be tested for real; this hook
/// is compiled out of anything a player ever runs.
#[cfg(debug_assertions)]
pub fn manifest_url() -> String {
    std::env::var("BONESAW_MANIFEST_URL").unwrap_or_else(|_| MANIFEST_URL.to_string())
}

#[cfg(not(debug_assertions))]
pub fn manifest_url() -> String {
    MANIFEST_URL.to_string()
}

#[cfg(debug_assertions)]
fn url_allowed(url: &str) -> bool {
    url.starts_with(URL_PREFIX)
        || (std::env::var("BONESAW_MANIFEST_URL").is_ok() && url.starts_with("http://127.0.0.1:"))
}

#[cfg(not(debug_assertions))]
fn url_allowed(url: &str) -> bool {
    url.starts_with(URL_PREFIX)
}

pub const ASSET_NAME: &str = "Bonesaw.exe";

pub struct Manifest {
    pub version: String,
    pub realmlist: Option<String>,
    pub sha256: String,
    pub size: u64,
    pub url: String,
}

pub fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(8))
        .timeout_read(Duration::from_secs(30))
        .user_agent(&format!("BonesawLauncher/{}", env!("BONESAW_VERSION")))
        .build()
}

pub fn fetch(agent: &ureq::Agent) -> R<Manifest> {
    let body = agent
        .get(&manifest_url())
        .call()
        .map_err(|e| format!("manifest request failed: {e}"))?
        .into_string()
        .map_err(|e| format!("manifest read failed: {e}"))?;
    parse(&body)
}

pub fn parse(body: &str) -> R<Manifest> {
    let mut lines = body.lines();
    match lines.next().map(str::trim) {
        Some("BONESAW 1") => {}
        other => return Err(format!("not a Bonesaw manifest: {:?}", other.unwrap_or(""))),
    }

    let mut version = None;
    let mut realmlist = None;
    let mut file = None;
    let mut url = None;

    for line in lines {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (key, rest) = match line.split_once(char::is_whitespace) {
            Some((k, r)) => (k, r.trim()),
            None => continue,
        };
        match key {
            "version" => version = Some(rest.to_string()),
            "realmlist" => realmlist = Some(rest.to_string()),
            "url" => url = Some(rest.to_string()),
            "file" => {
                let parts: Vec<&str> = rest.split_whitespace().collect();
                if parts.len() != 3 {
                    return Err("file line must be: file <sha256> <size> <name>".into());
                }
                if parts[2] != ASSET_NAME {
                    // Nothing but the launcher itself is ever downloaded; the
                    // MPQs ride along inside the exe.
                    return Err(format!("unexpected file in manifest: {}", parts[2]));
                }
                let sha = parts[0].to_ascii_lowercase();
                if sha.len() != 64 || !sha.chars().all(|c| c.is_ascii_hexdigit()) {
                    return Err("file sha256 is not 64 hex characters".into());
                }
                let size: u64 = parts[1]
                    .parse()
                    .map_err(|_| "file size is not a number".to_string())?;
                file = Some((sha, size));
            }
            _ => {}
        }
    }

    let version = version.ok_or("manifest has no version")?;
    if version.is_empty() || !version.split('.').all(|p| p.parse::<u32>().is_ok()) {
        return Err(format!("manifest version is not numeric: {version}"));
    }
    let (sha256, size) = file.ok_or("manifest has no file line")?;
    let url = url.ok_or("manifest has no url")?;
    if !url_allowed(&url) {
        return Err(format!("refusing download url outside {URL_PREFIX}"));
    }
    if let Some(host) = &realmlist {
        if !crate::realmlist::valid_host(host) {
            return Err(format!("manifest realmlist is not a host: {host}"));
        }
    }

    Ok(Manifest {
        version,
        realmlist,
        sha256,
        size,
        url,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const GOOD: &str = "BONESAW 1\nversion 0.1.50\nrealmlist logon.example.com\nfile 0000000000000000000000000000000000000000000000000000000000000000 123 Bonesaw.exe\nurl https://github.com/Raajik/wotlk-bonesaw/releases/download/v0.1.50/Bonesaw.exe\n";

    #[test]
    fn parses_a_good_manifest() {
        let m = parse(GOOD).unwrap();
        assert_eq!(m.version, "0.1.50");
        assert_eq!(m.size, 123);
        assert_eq!(m.realmlist.as_deref(), Some("logon.example.com"));
    }

    #[test]
    fn ignores_unknown_keys() {
        let m = parse(&GOOD.replace("version 0.1.50", "version 0.1.50\nfuture yes")).unwrap();
        assert_eq!(m.version, "0.1.50");
    }

    #[test]
    fn rejects_foreign_download_host() {
        let bad = GOOD.replace(
            "https://github.com/Raajik/wotlk-bonesaw/releases/download/v0.1.50/Bonesaw.exe",
            "https://example.com/Bonesaw.exe",
        );
        assert!(parse(&bad).is_err());
    }

    #[test]
    fn rejects_extra_files() {
        let bad = GOOD.replace("Bonesaw.exe\nurl", "..\\Wow.exe\nurl");
        assert!(parse(&bad).is_err());
    }

    #[test]
    fn accepts_a_bare_ip_realmlist() {
        let m = parse(&GOOD.replace("logon.example.com", "100.65.58.17")).unwrap();
        assert_eq!(m.realmlist.as_deref(), Some("100.65.58.17"));
    }

    /// realmlist.wtf is written verbatim, so the host must not be able to carry
    /// quotes, spaces, or anything else that could smuggle a second SET line in.
    #[test]
    fn rejects_a_realmlist_that_is_not_a_host() {
        for payload in ["evil.example.com\" ; SET foo \"bar", "evil.example.com extra"] {
            let bad = GOOD.replace("logon.example.com", payload);
            assert!(parse(&bad).is_err(), "accepted realmlist {payload:?}");
        }
    }

    #[test]
    fn rejects_bad_header() {
        assert!(parse("PELORIA 1\nversion 300\n").is_err());
    }
}
