//! Bringing Wow to the front, and optionally typing the player in.
//!
//! A freshly spawned Wow.exe often loses the race for the foreground: the
//! launcher window was frontmost when the game started, Windows does not hand
//! foreground rights to a background process, and the game opens behind
//! whatever the player was doing. So the launcher waits for the game window,
//! raises it, and -- only if the player opted in by creating an encrypted
//! Bonesaw.login -- types the account and password into the login screen.
//!
//! Bonesaw.login is a single DPAPI blob (CryptProtectData, CurrentUser
//! scope): decryptable only by the same Windows user on the same machine,
//! never logged, never shipped, never synced. It is opt-in per machine;
//! delete the file to go back to manual login.

use std::path::Path;
use std::thread;
use std::time::{Duration, Instant};

/// How long to wait for the game's window before giving up on focus/login.
pub const WINDOW_TIMEOUT: Duration = Duration::from_secs(12);

/// Polls until the spawned process owns a visible, titled top-level window.
/// Wow's window appears well before the login screen is interactive, which is
/// why the settle delay in `type_login` exists.
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
/// the login screen (the launcher's typed login updates it too). Used only
/// to pre-fill the auto-login dialog; the value is never logged.
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

/// DPAPI decrypt. The blob was written by save_login.ps1 on this machine.
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

/// Types the saved login into the Wow login screen. `hwnd` is the game
/// window; typing only begins once it verifiably holds the keyboard focus.
///
/// The client pre-fills the account field from WTF/Config.wtf when the
/// player ticked "remember account name", so the account keystrokes are
/// skipped entirely in that case (`account_prefilled`) -- and with them, the
/// fragile Tab hop from the account field: the login screen focuses the
/// account field by default, one TAB lands in the password field, and the
/// password is the only thing we type. When the account must be typed, both
/// fields are cleared first (the client pre-fills the account name, and the
/// typed account would otherwise append to what is already there).
pub fn type_login(hwnd: isize, account: &str, password: &str, account_prefilled: bool) {
    if !wait_foreground(hwnd, Duration::from_secs(3)) {
        return; // not frontmost: type nothing rather than type somewhere else
    }
    // Login screen still finishing load after the window exists.
    thread::sleep(Duration::from_millis(if account_prefilled { 1200 } else { 1500 }));

    if !account_prefilled {
        clear_field();
        for c in account.chars() {
            type_char(c);
            thread::sleep(Duration::from_millis(10));
        }
    }
    press(VK_TAB);
    thread::sleep(Duration::from_millis(150));
    for c in password.chars() {
        type_char(c);
        thread::sleep(Duration::from_millis(10));
    }
    thread::sleep(Duration::from_millis(50));
    press(VK_RETURN);
}

/// True once the game window really owns the keyboard. Typing into a window
/// that lost the foreground race would put a password somewhere else.
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

fn clear_field() {
    // Backspace on an empty field is a no-op, so this is safe to overdo.
    // 40 covers every legal account/password length with margin.
    for _ in 0..40 {
        press(VK_BACK);
        thread::sleep(Duration::from_millis(3));
    }
}

fn press(vk: u16) {
    unsafe {
        send(KEYBDINPUT {
            wVk: vk,
            wScan: 0,
            dwFlags: 0,
            time: 0,
            dwExtraInfo: 0,
        });
        send(KEYBDINPUT {
            wVk: vk,
            wScan: 0,
            dwFlags: KEYEVENTF_KEYUP,
            time: 0,
            dwExtraInfo: 0,
        });
    }
}

/// Unicode scan codes sidestep keyboard layout issues entirely: no VK mapping
/// to guess, and shift states for symbols in passwords come out right.
fn type_char(c: char) {
    let scan = c as u32 as u16;
    unsafe {
        send(KEYBDINPUT {
            wVk: 0,
            wScan: scan,
            dwFlags: KEYEVENTF_UNICODE,
            time: 0,
            dwExtraInfo: 0,
        });
        send(KEYBDINPUT {
            wVk: 0,
            wScan: scan,
            dwFlags: KEYEVENTF_UNICODE | KEYEVENTF_KEYUP,
            time: 0,
            dwExtraInfo: 0,
        });
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

use windows_sys::Win32::UI::Input::KeyboardAndMouse::{
    KEYBDINPUT, KEYEVENTF_KEYUP, KEYEVENTF_UNICODE, VK_BACK, VK_RETURN, VK_TAB,
};
