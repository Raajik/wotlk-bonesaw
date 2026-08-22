//! Patch the player's own stock 3.3.5a Wow.exe so modified FrameXML loads.
//!
//! This is the same table as tools/client-patch/patch_wow_exe.py: six "allow
//! interface edits" edits and five signature-bypass edits, byte patterns from
//! pathetic-lynx/WoW_335a_Patcher (WoWFix335 lineage). We patch the file the
//! player already owns and never ship or download a Blizzard binary.

use crate::util::R;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

pub const EXPECTED_SIZE: u64 = 7_704_216;

/// (file offset, original bytes, replacement bytes)
const PATCHES: &[(u64, &[u8], &[u8])] = &[
    // Allow interface edits
    (0x1F41BF, &[0x74], &[0xEB]),
    (0x415A25, &[0x75], &[0xEB]),
    (0x415A3F, &[0x01], &[0x03]),
    (0x415A95, &[0x01], &[0x03]),
    (0x415B46, &[0x7F], &[0xEB]),
    (
        0x415B5F,
        &[0x83, 0xC0, 0x03, 0x5E, 0x8B, 0xE5, 0x5D],
        &[0xB8, 0x03, 0x00, 0x00, 0x00, 0xEB, 0xED],
    ),
    // Signature bypass
    (
        0x123676,
        &[0x8B, 0xCE, 0xE8, 0xA3, 0x18, 0x1F, 0x00],
        &[0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90],
    ),
    (0x1241C6, &[0x50], &[0x90]),
    (
        0x1241C9,
        &[0xE8, 0x82, 0xC0, 0x1F, 0x00],
        &[0x90, 0x90, 0x90, 0x90, 0x90],
    ),
    (
        0x320273,
        &[0x0F, 0x85, 0xFE, 0x00, 0x00, 0x00],
        &[0x90, 0x90, 0x90, 0x90, 0x90, 0x90],
    ),
    (
        0x320282,
        &[0x0F, 0x85, 0xEF, 0x00, 0x00, 0x00],
        &[0x90, 0x90, 0x90, 0x90, 0x90, 0x90],
    ),
];

pub enum Outcome {
    AlreadyPatched,
    Patched,
    /// Not a 12340 client, or the bytes do not match what we expect. We say so
    /// and start the game anyway rather than refusing to launch.
    Skipped(String),
}

pub fn ensure(client: &Path) -> R<Outcome> {
    let exe = client.join("Wow.exe");
    let meta = std::fs::metadata(&exe).map_err(|_| "Wow.exe not found in this folder.".to_string())?;
    if meta.len() != EXPECTED_SIZE {
        return Ok(Outcome::Skipped(format!(
            "Wow.exe is {} bytes, expected {EXPECTED_SIZE} (3.3.5a build 12340). Left it alone.",
            meta.len()
        )));
    }

    let mut f = std::fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(&exe)
        .map_err(|e| format!("cannot open Wow.exe: {e}"))?;

    // Classify every site first: all patched, all stock, or something we do not
    // recognise. Never write a partial patch.
    let mut needed = Vec::new();
    for (off, orig, repl) in PATCHES {
        let got = read_at(&mut f, *off, repl.len().max(orig.len()))?;
        if got.starts_with(repl) {
            continue;
        }
        if got.starts_with(orig) {
            needed.push((*off, *repl));
            continue;
        }
        return Ok(Outcome::Skipped(format!(
            "Wow.exe has unexpected bytes at {off:#x}; left it alone."
        )));
    }
    if needed.is_empty() {
        return Ok(Outcome::AlreadyPatched);
    }

    let stock = client.join("Wow.exe.stock");
    if !stock.exists() {
        std::fs::copy(&exe, &stock).map_err(|e| format!("cannot back up Wow.exe: {e}"))?;
    }

    for (off, repl) in needed {
        f.seek(SeekFrom::Start(off))
            .map_err(|e| format!("seek {off:#x}: {e}"))?;
        f.write_all(repl)
            .map_err(|e| format!("write {off:#x}: {e}"))?;
    }
    f.flush().map_err(|e| format!("flush Wow.exe: {e}"))?;
    Ok(Outcome::Patched)
}

fn read_at(f: &mut std::fs::File, off: u64, len: usize) -> R<Vec<u8>> {
    let mut buf = vec![0u8; len];
    f.seek(SeekFrom::Start(off))
        .map_err(|e| format!("seek {off:#x}: {e}"))?;
    f.read_exact(&mut buf)
        .map_err(|e| format!("read {off:#x}: {e}"))?;
    Ok(buf)
}
