//! Bonesaw.exe - one file a player drops into their 3.3.5a folder and pins to
//! the taskbar. It updates itself from GitHub, writes the client patches it
//! carries, patches Wow.exe, and starts the game.

#![windows_subsystem = "windows"]

mod manifest;
mod payload;
mod realmlist;
mod selfupdate;
mod ui;
mod util;
mod wowpatch;

use std::path::{Path, PathBuf};
use ui::Ui;
use util::R;

const VERSION: &str = env!("BONESAW_VERSION");

fn main() {
    let ui = Ui::create(VERSION);
    let worker = ui.clone();
    std::thread::spawn(move || {
        if let Err(e) = run(&worker) {
            log(&format!("ERROR {e}"));
            worker.error(&e);
        }
        worker.quit();
    });
    ui.run();
}

fn run(ui: &Ui) -> R<()> {
    let exe = std::env::current_exe().map_err(|e| format!("cannot locate myself: {e}"))?;
    let client = exe
        .parent()
        .ok_or("cannot locate the client folder")?
        .to_path_buf();
    validate(&client)?;
    log("");
    log(&format!("Bonesaw launcher {VERSION} in {}", client.display()));

    selfupdate::cleanup(&exe);
    let just_updated = std::env::args().any(|a| a == "--updated");
    if just_updated {
        // The exe just changed under a path Windows' icon cache considers
        // settled; without this, shortcuts and pins keep the old icon.
        util::refresh_shell_icons(&exe);
        log("refreshed shell icon cache after update");
    }

    let agent = manifest::agent();
    let mut realm: Option<String> = None;

    // The manifest is fetched even right after an update, because it also
    // carries the realmlist. Only the upgrade itself is skipped, which is what
    // keeps --updated from being able to loop.
    ui.status(if just_updated {
        "Finishing update..."
    } else {
        "Checking for updates..."
    });
    match manifest::fetch(&agent) {
        Err(e) => {
            // Never keep someone from playing because GitHub is unreachable.
            log(&format!("update check skipped: {e}"));
            ui.status("Update check skipped.");
        }
        Ok(man) => {
            realm = man.realmlist.clone();
            if just_updated {
                ui.status(&format!("Updated to {VERSION}."));
                log(&format!("updated to {VERSION}"));
            } else if !util::is_newer(&man.version, VERSION) {
                log(&format!("up to date at {VERSION}"));
            } else if util::wow_running() {
                ui.status(&format!(
                    "Close World of Warcraft to update to {}.",
                    man.version
                ));
                log("update deferred: Wow is running");
            } else {
                ui.status("Updating your Bonesaw client...");
                log(&format!("updating {VERSION} -> {}", man.version));
                match selfupdate::apply(ui, &agent, &man, &exe) {
                    Ok(true) => {
                        log("restarting into the new launcher");
                        std::process::exit(0);
                    }
                    Ok(false) => {}
                    Err(e) => {
                        // A failed update is not fatal: play on the old version.
                        log(&format!("update failed: {e}"));
                        ui.status("Update failed, starting the installed version...");
                    }
                }
            }
        }
    }

    ui.status("Preparing Bonesaw...");
    let written = payload::extract(ui, &client)?;
    for w in &written {
        log(&format!("wrote {w}"));
    }

    // Cache/WDB holds the client's copy of SERVER-sent data (items, creatures,
    // gameobjects, quests) as well as DBC-derived caches, and WoW trusts its
    // cached copy over the server. Most cache-relevant changes ship as
    // server-side SQL that never touches the launcher payload, so keying the
    // clear off "payload wrote something" left the cache stale across almost
    // every update -- the bug behind recurring "client still shows the old
    // name/icon" reports. The client re-queries what it needs the moment it
    // needs it, so clearing every launch costs a few packets.
    for f in clear_wdb_cache(&client) {
        log(&format!("cleared cache {f}"));
    }

    match wowpatch::ensure(&client)? {
        wowpatch::Outcome::Patched => log("patched Wow.exe (backup at Wow.exe.stock)"),
        wowpatch::Outcome::AlreadyPatched => log("Wow.exe already patched"),
        wowpatch::Outcome::Skipped(why) => {
            log(&format!("Wow.exe not patched: {why}"));
            ui.status(&why);
        }
    }

    // A local Bonesaw.realmlist beats the manifest, and applies even when the
    // update check could not reach GitHub.
    match realmlist::read_override(&client) {
        Some(Ok(host)) => {
            log(&format!("realmlist override: {host}"));
            realm = Some(host);
        }
        Some(Err(why)) => {
            log(&format!("ignoring realmlist override: {why}"));
            ui.status(&why);
        }
        None => {}
    }

    if let Some(host) = realm.as_deref() {
        for changed in realmlist::write(&client, host)? {
            log(&format!("set realmlist {host} in {changed}"));
        }
    }

    ui.status("Ready, starting Bonesaw...");
    launch(&client)?;
    log("started Wow.exe");
    Ok(())
}

/// A launcher that guesses at paths is how you end up shipping someone else's
/// drive letter. Bonesaw.exe works where it stands, or not at all.
fn validate(client: &Path) -> R<()> {
    let has_data = client.join("Data").join("common.MPQ").is_file();
    let has_exe = client.join("Wow.exe").is_file() || client.join("Wow.exe.stock").is_file();
    if has_data && has_exe {
        return Ok(());
    }
    Err(
        "Bonesaw.exe must be placed inside your World of Warcraft 3.3.5a folder \
         - the one containing Wow.exe and the Data folder."
            .into(),
    )
}

fn launch(client: &Path) -> R<()> {
    let wow = client.join("Wow.exe");
    if !wow.is_file() {
        return Err("Wow.exe is missing from this folder.".into());
    }
    std::process::Command::new(&wow)
        .current_dir(client)
        .spawn()
        .map_err(|e| format!("cannot start Wow.exe: {e}"))?;
    Ok(())
}

/// A rolling log next to the exe. Players can paste it when something breaks.
/// Delete the client's cached item/spell/creature data.
///
/// WoW caches what the server tells it about items, spells and creatures in
/// Cache/WDB and then trusts that copy over the server. Shipping new DBC data
/// therefore leaves players looking at the old names, icons and tooltips until
/// those files are gone -- 0.1.82 rewrote 94 spell icons and renamed two spells,
/// and a cached client would have shown none of it.
///
/// Keyed off the payload having actually written something, rather than a flag
/// in the manifest. A flag is a step someone has to remember on every ship, and
/// this is cheap enough not to need one: the client re-queries what it needs
/// from the server the moment it needs it, so the only cost is a few packets.
///
/// Only *.wdb files are removed, never the directory or anything else under
/// Cache -- addons keep saved data there too.
fn clear_wdb_cache(client: &Path) -> Vec<String> {
    let mut removed = Vec::new();
    let wdb = client.join("Cache").join("WDB");
    let Ok(locales) = std::fs::read_dir(&wdb) else {
        return removed; // no cache yet, or no Cache folder at all
    };
    for locale in locales.flatten() {
        let Ok(files) = std::fs::read_dir(locale.path()) else {
            continue;
        };
        for f in files.flatten() {
            let path = f.path();
            let is_wdb = path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("wdb"));
            if !is_wdb {
                continue;
            }
            // A locked file means Wow is open; skip it rather than fail the
            // launch over a cache file.
            if std::fs::remove_file(&path).is_ok() {
                if let Some(name) = path.file_name().and_then(|n| n.to_str()) {
                    removed.push(name.to_string());
                }
            }
        }
    }
    removed
}

fn log(line: &str) {
    use std::io::Write;
    static PATH: std::sync::OnceLock<Option<PathBuf>> = std::sync::OnceLock::new();
    let path = PATH.get_or_init(|| {
        let exe = std::env::current_exe().ok()?;
        let dir = exe.parent()?.to_path_buf();
        let logs = dir.join("Logs");
        let path = if logs.is_dir() {
            logs.join("BonesawLauncher.log")
        } else {
            dir.join("BonesawLauncher.log")
        };
        // Append, so lines written before a self-update survive into the
        // relaunched process. Start over once it gets unreasonably large.
        if std::fs::metadata(&path).map(|m| m.len() > 64 * 1024).unwrap_or(false) {
            let _ = std::fs::remove_file(&path);
        }
        Some(path)
    });
    if let Some(path) = path {
        if let Ok(mut f) = std::fs::OpenOptions::new().append(true).create(true).open(path) {
            let _ = writeln!(f, "{line}");
        }
    }
}
