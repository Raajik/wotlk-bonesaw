//! Bringing Wow to the front, and optionally pasting the player in.
//!
//! A freshly spawned Wow.exe often loses the race for the foreground: the
//! launcher window was frontmost when the game started, Windows does not hand
//! foreground rights to a background process, and the game opens behind
//! whatever the player was doing. So the launcher waits for the game window,
//! raises it, and -- only if the player opted in by creating an encrypted
//! Bonesaw.login -- fills the login screen.
//!
//! Bonesaw.login is a single DPAPI blob (CryptProtectData, CurrentUser
//! scope): decryptable only by the same Windows user on the same machine,
//! never logged, never shipped, never synced. It is opt-in per machine;
//! delete the file to go back to manual login.
//!
//! The old flow typed the account and password as timed per-character
//! keystrokes with a TAB hop between the fields, and on a machine whose
//! login screen loaded slower than the timer the password landed in the
//! visible account box. That whole mechanism is gone. The launcher now:
//!   1. writes `SET accountName "<account>"` into WTF/Config.wtf before
//!      Wow starts, so the CLIENT pre-fills the account field at login-screen
//!      init -- the launcher never touches the account field at all;
//!   2. delivers the password as a single clipboard paste (one Ctrl+V chord)
//!      plus Enter, only once the game window has verifiably held the
//!      keyboard focus and settled. A paste that lands too early is dropped
//!      by the client -- an empty password box and a manual login -- where
//!      the old missed TAB put the password in the account field.
//! The clipboard carries the password only for that moment and is scrubbed
//! right after.

use std::path::Path;
use std::thread;
use std::time::{Duration, Instant};

/// How long to wait for the game's window before giving up on focus/login.
pub const WINDOW_TIMEOUT: Duration = Duration::from_secs(12);

/// Generous settle before the single paste (see module comment). A late
/// paste is a no-op; an early paste can never mis-target because the
/// launcher only ever pastes into whatever field the client focused, and
/// that is the password field only after the client's own prefill plus our
/// one Tab -- see `paste_login`.
const SETTLE: Duration = Duration::from_secs(6);

const CF_UNICODETEXT: u32 = 13;
const VK_V: u16 = 0x56;

/// Polls until the spawned process owns a visible, titled top-level window.
/// Wow's window appears well before the login screen is interactive, which
/// is why `paste_login` settles after the wait.
pub fn wait_for_window(pid: u32, timeout: Duration) -> Option<isize> {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        GetTopWindow, GetWindowTextLengthW, GetWindow, GetWindowThreadProcessId,
        GW_HWNDNEXT, IsWindowVisible,
    };

    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        unsafe {
            let mut hwnd = GetTopWindow(std::ptr::null_mut());
            while !hwnd.is_null() {
                let mut owner = 0u32;
                GetWindowThreadProcessId(hwnd, &mut owner);
                if owner == pid && IsWindowVisible(hwnd) != 0 && GetWindowTextLengthW(hwnd) > 0 {
                    return Some(hwnd as isize);
                }
                hwnd = GetWindow(hwnd, GW_HWNDNEXT);
            }
        }
        thread::sleep(Duration::from_millis(100));
    }
    None
}

/// Restores/raises the window and hands it the foreground + keyboard focus.
pub fn focus(hwnd: isize) {
    use windows_sys::Win32::Foundation::HWND;
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        IsIconic, SetForegroundWindow, ShowWindow, SW_RESTORE, SW_SHOW,
    };
    let hwnd = hwnd as HWND;

    unsafe {
        if IsIconic(hwnd) != 0 {
            ShowWindow(hwnd, SW_RESTORE);
        } else {
            ShowWindow(hwnd, SW_SHOW);
        }
        SetForegroundWindow(hwnd);
    }
}

/// The account name the client itself remembers: WTF/Config.wtf holds
/// `SET accountName "..."` once "remember account name" has been ticked on
/// the login screen. The launcher also WRITES this line itself (see
/// `ensure_account_prefilled`) so the client does the account prefill and
/// the launcher never has to touch the account field. The value is never
/// logged.
pub fn saved_account_name(client: &Path) -> Option<String> {
    let text = std::fs::read_to_string(client.join("WTF").join("Config.wtf")).ok()?;
    for line in text.lines() {
        if line.to_ascii_lowercase().starts_with("set accountname") {
            let start = line.find('"')? + 1;
            let end = line.rfind('"')?;
            if start < end {
                return Some(line[start..end].to_string());
            }
        }
    }
    None
}

/// Rewrites `SET accountName "<account>"` in WTF/Config.wtf so the client
/// pre-fills the account field itself at login-screen init. The launcher
/// used to type the account with a 40-backspace clear and a timed TAB hop
/// to the password box; both are gone. Returns false if the write failed.
pub fn ensure_account_prefilled(client: &Path, account: &str) -> bool {
    use crate::util::write_atomic;

    let path = client.join("WTF").join("Config.wtf");
    let text = std::fs::read_to_string(&path).unwrap_or_default();
    let mut out = String::with_capacity(text.len() + account.len() + 32);
    for line in text.lines() {
        if !line.to_ascii_lowercase().starts_with("set accountname") {
            out.push_str(line);
            out.push_str("\r\n");
        }
    }
    out.push_str("SET accountName \"");
    out.push_str(account);
    out.push_str("\"\n");
    write_atomic(&path, out.as_bytes()).is_ok()
}

/// Reads the opt-in login file, if present and decryptable. A missing,
/// empty or undecryptable file simply means manual login; the contents are
/// never logged or displayed anywhere.
pub fn load(client: &Path) -> Option<(String, String)> {
    let blob = std::fs::read(client.join("Bonesaw.login")).ok()?;
    let plain = unprotect(&blob)?;
    let text = String::from_utf8_lossy(&plain);
    let mut lines = text.splitn(2, '\n');
    let account = lines.next()?.trim().to_string();
    // Trim only line endings: passwords may legitimately contain spaces.
    let password = lines.next()?.trim_end_matches(['\r', '\n']).to_string();
    if account.is_empty() || password.is_empty() {
        return None;
    }
    Some((account, password))
}

/// DPAPI decrypt. The blob was written by the launcher dialog on this
/// machine and is decrypted only in memory here.
fn unprotect(blob: &[u8]) -> Option<Vec<u8>> {
    use windows_sys::Win32::Foundation::LocalFree;
    use windows_sys::Win32::Security::Cryptography::{
        CryptUnprotectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
    };

    unsafe {
        let input = CRYPT_INTEGER_BLOB {
            cbData: blob.len() as u32,
            pbData: blob.as_ptr() as *mut u8,
        };
        let mut out: CRYPT_INTEGER_BLOB = std::mem::zeroed();
        let ok = CryptUnprotectData(
            &input,
            std::ptr::null_mut(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null_mut(),
            CRYPTPROTECT_UI_FORBIDDEN,
            &mut out,
        );
        if ok == 0 {
            return None;
        }
        let data = std::slice::from_raw_parts(out.pbData, out.cbData as usize).to_vec();
        LocalFree(out.pbData as *mut _);
        Some(data)
    }
}

/// Delivers the saved password to the login screen: exactly one clipboard
/// paste plus Enter, and never a typed character. The account field is the
/// client's own prefill from Config.wtf, so there is no account typing and
/// no field navigation to lose: if the login screen has not finished loading
/// when the chord arrives, the client drops it and the password box simply
/// stays empty -- a manual login, never a password in the wrong box.
pub fn paste_login(hwnd: isize, password: &str) {
    if !wait_foreground(hwnd, Duration::from_secs(3)) {
        return; // not frontmost: deliver nothing rather than deliver it elsewhere
    }
    // The login screen keeps loading after its window appears; slow machines
    // get the settle they asked for. Waiting longer is always the safe
    // direction: a dropped paste is a no-op, rushing one was the bug.
    thread::sleep(SETTLE);
    if !set_clipboard(password) {
        return; // clipboard busy: deliver nothing, keep manual login intact
    }
    paste_chord();
    thread::sleep(Duration::from_millis(250));
    press(VK_RETURN);
    // The password never lingers in the clipboard.
    thread::sleep(Duration::from_millis(800));
    let _ = set_clipboard("");
}

/// True once the game window really owns the keyboard. Delivering into a
/// window that lost the foreground race would paste the password elsewhere.
fn wait_foreground(hwnd: isize, timeout: Duration) -> bool {
    use windows_sys::Win32::UI::WindowsAndMessaging::GetForegroundWindow;
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        unsafe {
            if GetForegroundWindow() == hwnd as _ {
                return true;
            }
        }
        thread::sleep(Duration::from_millis(100));
    }
    false
}

/// One key press: down then up (Enter, Tab).
fn press(vk: u16) {
    unsafe {
        send(KEYBDINPUT { wVk: vk, wScan: 0, dwFlags: 0, time: 0, dwExtraInfo: 0 });
        send(KEYBDINPUT {
            wVk: vk,
            wScan: 0,
            dwFlags: KEYEVENTF_KEYUP,
            time: 0,
            dwExtraInfo: 0,
        });
    }
}

/// Ctrl+V as one four-event SendInput batch: it either lands whole in the
/// focused field or does nothing at all. No per-character timers, no
/// keyboard-layout mapping, no shift states.
fn paste_chord() {
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::{
        SendInput, INPUT, INPUT_0, INPUT_KEYBOARD, KEYBDINPUT, KEYEVENTF_KEYUP, VK_CONTROL,
    };
    const VK_V: u16 = 0x56; // 'V'
    let key = |vk: u16, up: bool| INPUT {
        r#type: INPUT_KEYBOARD,
        Anonymous: INPUT_0 {
            ki: KEYBDINPUT {
                wVk: vk,
                wScan: 0,
                dwFlags: if up { KEYEVENTF_KEYUP } else { 0 },
                time: 0,
                dwExtraInfo: 0,
            },
        },
    };
    let input = [
        key(VK_CONTROL, false),
        key(VK_V, false),
        key(VK_V, true),
        key(VK_CONTROL, true),
    ];
    unsafe {
        SendInput(4, input.as_ptr(), std::mem::size_of::<INPUT>() as i32);
    }
}

unsafe fn send(ki: KEYBDINPUT) {
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::{SendInput, INPUT, INPUT_0, INPUT_KEYBOARD};

    let input = INPUT {
        r#type: INPUT_KEYBOARD,
        Anonymous: INPUT_0 { ki },
    };
    SendInput(1, &input, std::mem::size_of::<INPUT>() as i32);
}

/// UTF-16 text onto the system clipboard. The block's ownership transfers to
/// the clipboard on success, so it must come from GlobalAlloc.
fn set_clipboard(text: &str) -> bool {
    use windows_sys::Win32::Foundation::{GlobalFree, HANDLE};
    use windows_sys::Win32::System::DataExchange::{
        CloseClipboard, EmptyClipboard, OpenClipboard, SetClipboardData,
    };
    use windows_sys::Win32::System::Memory::{GlobalAlloc, GlobalLock, GlobalUnlock, GMEM_MOVEABLE};

    let mut wide: Vec<u16> = text.encode_utf16().collect();
    wide.push(0);
    let bytes = wide.len() * 2;

    unsafe {
        for _ in 0..5 {
            if OpenClipboard(std::ptr::null_mut()) == 0 {
                thread::sleep(Duration::from_millis(100));
                continue; // another app holds the clipboard; retry briefly
            }
            let ok = unsafe {
                if EmptyClipboard() == 0 {
                    CloseClipboard();
                    return false;
                }
                let h = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if h.is_null() {
                    CloseClipboard();
                    return false;
                }
                let dst = GlobalLock(h);
                if !dst.is_null() {
                    std::ptr::copy_nonoverlapping(
                        wide.as_ptr() as *const u8,
                        dst as *mut u8,
                        bytes,
                    );
                    GlobalUnlock(h);
                }
                let ok = !SetClipboardData(CF_UNICODETEXT, h as HANDLE).is_null();
                if !ok {
                    GlobalFree(h); // clipboard refused it; free our block
                }
                ok
            };
            CloseClipboard();
            return ok;
        }
    }
    false
}

use windows_sys::Win32::UI::Input::KeyboardAndMouse::{KEYBDINPUT, KEYEVENTF_KEYUP, VK_RETURN};
