//! Replacing a running exe on Windows: you cannot delete it, but you can
//! rename it. So the running Bonesaw.exe steps aside to Bonesaw.exe.old, the
//! freshly downloaded copy takes its place, and the new process cleans up the
//! .old on its next start.

use crate::manifest::Manifest;
use crate::ui::Ui;
use crate::util::{sha256_file, R};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

fn old_path(exe: &Path) -> PathBuf {
    let mut n = exe.file_name().unwrap_or_default().to_os_string();
    n.push(".old");
    exe.with_file_name(n)
}

fn new_path(exe: &Path) -> PathBuf {
    let mut n = exe.file_name().unwrap_or_default().to_os_string();
    n.push(".new");
    exe.with_file_name(n)
}

/// Best effort: the previous exe is unlocked once its process is gone, so this
/// normally succeeds on the very next launch.
pub fn cleanup(exe: &Path) {
    let _ = std::fs::remove_file(old_path(exe));
    let _ = std::fs::remove_file(new_path(exe));
}

/// Downloads, verifies, swaps, and relaunches. Returns Ok(true) when the new
/// process has been started and this one should exit.
pub fn apply(ui: &Ui, agent: &ureq::Agent, man: &Manifest, exe: &Path) -> R<bool> {
    let new = new_path(exe);
    let _ = std::fs::remove_file(&new);

    download(ui, agent, man, &new)?;

    let size = std::fs::metadata(&new).map(|m| m.len()).unwrap_or(0);
    if size != man.size {
        let _ = std::fs::remove_file(&new);
        return Err(format!("download is {size} bytes, manifest says {}", man.size));
    }
    let got = sha256_file(&new)?;
    if got != man.sha256 {
        let _ = std::fs::remove_file(&new);
        return Err(format!("download hash {got} does not match the manifest"));
    }

    let old = old_path(exe);
    let _ = std::fs::remove_file(&old);
    std::fs::rename(exe, &old).map_err(|e| format!("cannot move the running launcher aside: {e}"))?;
    if let Err(e) = std::fs::rename(&new, exe) {
        // Put the old exe back so the player still has a working launcher.
        let _ = std::fs::rename(&old, exe);
        return Err(format!("cannot install the new launcher: {e}"));
    }

    let dir = exe.parent().unwrap_or_else(|| Path::new("."));
    std::process::Command::new(exe)
        .arg("--updated")
        .current_dir(dir)
        .spawn()
        .map_err(|e| format!("cannot start the updated launcher: {e}"))?;
    Ok(true)
}

fn download(ui: &Ui, agent: &ureq::Agent, man: &Manifest, dest: &Path) -> R<()> {
    let resp = agent
        .get(&man.url)
        .call()
        .map_err(|e| format!("download failed: {e}"))?;
    let total = resp
        .header("Content-Length")
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(man.size);

    let mut reader = resp.into_reader();
    let mut file =
        std::fs::File::create(dest).map_err(|e| format!("create {}: {e}", dest.display()))?;
    let mut buf = vec![0u8; 64 * 1024];
    let mut done: u64 = 0;
    loop {
        let n = reader
            .read(&mut buf)
            .map_err(|e| format!("download read failed: {e}"))?;
        if n == 0 {
            break;
        }
        file.write_all(&buf[..n])
            .map_err(|e| format!("write {}: {e}", dest.display()))?;
        done += n as u64;
        if done > man.size {
            return Err("download is larger than the manifest says".into());
        }
        ui.progress(crate::manifest::ASSET_NAME, done, total);
    }
    file.flush().map_err(|e| format!("flush download: {e}"))?;
    Ok(())
}
