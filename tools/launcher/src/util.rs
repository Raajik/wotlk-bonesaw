use sha2::{Digest, Sha256};
use std::path::Path;

pub type R<T> = Result<T, String>;

pub fn sha256(bytes: &[u8]) -> String {
    let mut h = Sha256::new();
    h.update(bytes);
    hex(&h.finalize())
}

pub fn sha256_file(path: &Path) -> R<String> {
    let mut f = std::fs::File::open(path).map_err(|e| format!("open {}: {e}", path.display()))?;
    let mut h = Sha256::new();
    std::io::copy(&mut f, &mut h).map_err(|e| format!("read {}: {e}", path.display()))?;
    Ok(hex(&h.finalize()))
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// Dotted numeric compare, missing components treated as 0. Any component that
/// is not purely numeric makes the whole comparison refuse to say "newer".
pub fn is_newer(candidate: &str, current: &str) -> bool {
    let (Some(a), Some(b)) = (parse_version(candidate), parse_version(current)) else {
        return false;
    };
    a > b
}

fn parse_version(s: &str) -> Option<[u32; 4]> {
    let mut out = [0u32; 4];
    let mut parts = s.trim().split('.');
    for slot in out.iter_mut() {
        match parts.next() {
            None => break,
            Some(p) => *slot = p.trim().parse::<u32>().ok()?,
        }
    }
    if parts.next().is_some() {
        return None;
    }
    Some(out)
}

/// Replace a file the safe way: write a sibling .tmp, then rename over dest.
pub fn write_atomic(dest: &Path, bytes: &[u8]) -> R<()> {
    let mut tmp_name = dest
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    tmp_name.push_str(".tmp");
    let tmp = dest.with_file_name(tmp_name);
    if let Some(parent) = dest.parent() {
        std::fs::create_dir_all(parent).map_err(|e| format!("mkdir {}: {e}", parent.display()))?;
    }
    std::fs::write(&tmp, bytes).map_err(|e| format!("write {}: {e}", tmp.display()))?;
    // rename() on Windows fails when the destination exists, so clear it first.
    let _ = std::fs::remove_file(dest);
    std::fs::rename(&tmp, dest).map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        format!("replace {}: {e}", dest.display())
    })
}

/// Windows keys its icon cache to the exe's path, so an in-place update leaves
/// every shortcut and taskbar pin showing the OLD icon until the shell is told
/// the file changed -- the launcher can look updated while the taskbar still
/// wears the last version's face. Two broadcasts fix it without killing
/// Explorer and without admin:
/// - SHCNE_ASSOCCHANGED is the installer-standard "icon resources changed"
///   refresh (what ie4uinit -show runs under the hood).
/// - SHCNE_UPDATEITEM asks views showing this exact exe to re-resolve it.
pub fn refresh_shell_icons(exe: &Path) {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::UI::Shell::{
        SHChangeNotify, SHCNE_ASSOCCHANGED, SHCNE_UPDATEITEM, SHCNF_IDLIST, SHCNF_PATHW,
    };

    unsafe {
        SHChangeNotify(
            SHCNE_ASSOCCHANGED as i32,
            SHCNF_IDLIST,
            std::ptr::null(),
            std::ptr::null(),
        );
        let mut wide: Vec<u16> = exe.as_os_str().encode_wide().collect();
        wide.push(0);
        SHChangeNotify(
            SHCNE_UPDATEITEM as i32,
            SHCNF_PATHW,
            wide.as_ptr() as *const std::ffi::c_void,
            std::ptr::null(),
        );
    }
}

pub fn wow_running() -> bool {
    use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, Process32First, Process32Next, PROCESSENTRY32,
        TH32CS_SNAPPROCESS,
    };

    const NAMES: [&str; 2] = ["wow.exe", "wow-bonesaw.exe"];
    unsafe {
        let snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if snap == INVALID_HANDLE_VALUE {
            return false;
        }
        let mut entry: PROCESSENTRY32 = std::mem::zeroed();
        entry.dwSize = std::mem::size_of::<PROCESSENTRY32>() as u32;
        let mut found = false;
        let mut ok = Process32First(snap, &mut entry);
        while ok != 0 {
            let end = entry
                .szExeFile
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(entry.szExeFile.len());
            let name = entry.szExeFile[..end]
                .iter()
                .map(|&c| c as u8 as char)
                .collect::<String>()
                .to_ascii_lowercase();
            if NAMES.contains(&name.as_str()) {
                found = true;
                break;
            }
            ok = Process32Next(snap, &mut entry);
        }
        CloseHandle(snap);
        found
    }
}
