//! The auto-login setup dialog: a small modal opened from the button on the
//! launcher window. Writes the same DPAPI blob tools/client-update/
//! save_login.ps1 writes, so players never see PowerShell. Password is
//! masked, Enter saves, Esc cancels, and a REMOVE button appears when a
//! login is already saved. A blank password field means "keep the saved one".

use std::path::{Path, PathBuf};
use windows_sys::Win32::Foundation::{
    COLORREF, HMODULE, HWND, LPARAM, LRESULT, RECT, WPARAM,
};
use windows_sys::Win32::Graphics::Dwm::DwmSetWindowAttribute;
use windows_sys::Win32::Graphics::Gdi::*;
use windows_sys::Win32::Security::Cryptography::{
    CryptProtectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
};
use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
use windows_sys::Win32::UI::Input::KeyboardAndMouse::{EnableWindow, SetFocus};
use windows_sys::Win32::UI::Controls::{DRAWITEMSTRUCT, ODS_SELECTED};
use windows_sys::Win32::UI::WindowsAndMessaging::*;

use crate::util::write_atomic;

pub enum Outcome {
    Saved,
    Removed,
    None,
}

const W: i32 = 384;
const H: i32 = 244;

const ID_SAVE: usize = 1; // IDOK
const ID_CANCEL: usize = 2; // IDCANCEL
const ID_REMOVE: usize = 9;
const ID_ACCOUNT: usize = 100;
const ID_PASSWORD: usize = 101;

// One dialog at a time, on the UI thread -- plain statics, same as ui.rs.
static mut RESULT: u8 = 0;
static mut CLIENT: Option<PathBuf> = None;
static mut NOTE_TEXT: String = String::new();
static mut F_BOLD: HFONT = std::ptr::null_mut();
static mut F_BODY: HFONT = std::ptr::null_mut();
static mut F_NOTE: HFONT = std::ptr::null_mut();
static mut BRUSH_BG: HBRUSH = std::ptr::null_mut();

// The dialog is light so nothing can hide the text: medium grey background,
// stock white input fields, and hand-painted white labels with a black
// outline (the launcher's dark edit controls were unreadable on some setups).
const GREY_BG: COLORREF = 0x00D6D6D6;
const GREY_PLATE: COLORREF = 0x00C9C9C9;
const GREY_PLATE_HOT: COLORREF = 0x00B0B0B0;
const GREY_EDGE: COLORREF = 0x008A8A8A;
const WHITE: COLORREF = 0x00FFFFFF;
const BLACK: COLORREF = 0x00000000;
const BLOOD_FILL: COLORREF = 0x001C169E;

unsafe fn client_dir() -> Option<PathBuf> {
    std::ptr::addr_of!(CLIENT).as_ref().and_then(|c| c.clone())
}

/// Opens the dialog modally over the launcher. Blocks until it closes.
pub fn show(owner: isize, client: &Path) -> Outcome {
    unsafe {
        let hinst = GetModuleHandleW(std::ptr::null());
        register_class(hinst);

        let mut rc = RECT {
            left: 0,
            top: 0,
            right: W,
            bottom: H,
        };
        AdjustWindowRect(&mut rc, (WS_CAPTION | WS_SYSMENU) as u32, 0);
        let w = rc.right - rc.left;
        let h = rc.bottom - rc.top;
        let mut orc = RECT {
            left: 0,
            top: 0,
            right: 0,
            bottom: 0,
        };
        GetWindowRect(owner as HWND, &mut orc);
        let sx = GetSystemMetrics(SM_CXSCREEN);
        let sy = GetSystemMetrics(SM_CYSCREEN);
        let x = ((orc.left + orc.right) / 2 - w / 2).clamp(0, (sx - w).max(0));
        let y = ((orc.top + orc.bottom) / 2 - h / 2).clamp(0, (sy - h).max(0));

        RESULT = 0;
        CLIENT = Some(client.to_path_buf());
        let class = wide("BonesawLoginDialog");
        let title = wide("Bonesaw - auto-login");
        let hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            class.as_ptr(),
            title.as_ptr(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            x,
            y,
            w,
            h,
            owner as HWND,
            std::ptr::null_mut(),
            hinst,
            std::ptr::null(),
        );
        if hwnd.is_null() {
            return Outcome::None;
        }
        // Dark title bar where supported; best effort, failure is fine.
        let dark: i32 = 1;
        DwmSetWindowAttribute(hwnd, 20, &dark as *const i32 as *const _, 4);

        EnableWindow(owner as HWND, 0);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);

        // Nested pump; IsDialogMessage gives the edits Tab / Enter / Esc.
        let mut msg: MSG = std::mem::zeroed();
        while IsWindow(hwnd) != 0 {
            let r = GetMessageW(&mut msg, std::ptr::null_mut(), 0, 0);
            if r <= 0 {
                break;
            }
            if IsDialogMessageW(hwnd, &mut msg) == 0 {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        EnableWindow(owner as HWND, 1);
        SetForegroundWindow(owner as HWND);
        match RESULT {
            1 => Outcome::Saved,
            2 => Outcome::Removed,
            _ => Outcome::None,
        }
    }
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

unsafe fn register_class(hinst: HMODULE) {
    static ONCE: std::sync::Once = std::sync::Once::new();
    ONCE.call_once(|| {
        let class = wide("BonesawLoginDialog");
        let mut wc: WNDCLASSW = std::mem::zeroed();
        wc.lpfnWndProc = Some(dlgproc);
        wc.hInstance = hinst;
        wc.lpszClassName = class.as_ptr();
        wc.hCursor = LoadCursorW(std::ptr::null_mut(), IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(GREY_BG);
        RegisterClassW(&wc);
    });
}

unsafe fn control(
    parent: HWND,
    class: &str,
    label: &str,
    ex_style: u32,
    style: u32,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    id: usize,
    font: HFONT,
) -> HWND {
    let cls = wide(class);
    let label = wide(label);
    let hwnd = CreateWindowExW(
        ex_style,
        cls.as_ptr(),
        label.as_ptr(),
        style,
        x,
        y,
        w,
        h,
        parent,
        id as HMENU,
        GetModuleHandleW(std::ptr::null()),
        std::ptr::null(),
    );
    SendMessageW(hwnd, WM_SETFONT, font as _, 1);
    hwnd
}

unsafe fn build_controls(parent: HWND) {
    let client = client_dir();
    let old = client.as_deref().and_then(crate::login::load);
    let has_login = client
        .as_ref()
        .map(|c| c.join("Bonesaw.login").is_file())
        .unwrap_or(false);

    BRUSH_BG = CreateSolidBrush(GREY_BG);
    F_BOLD = crate::ui::font(12, true, "Segoe UI");
    F_BODY = crate::ui::font(16, false, "Segoe UI");
    F_NOTE = crate::ui::font(12, false, "Segoe UI");

    let account = control(
        parent,
        "EDIT",
        "",
        WS_EX_CLIENTEDGE,
        WS_TABSTOP | ES_AUTOHSCROLL as u32,
        24,
        40,
        336,
        28,
        ID_ACCOUNT,
        F_BODY,
    );
    if let Some((a, _)) = &old {
        SetWindowTextW(account, wide(a).as_ptr());
    }
    control(
        parent,
        "EDIT",
        "",
        WS_EX_CLIENTEDGE,
        WS_TABSTOP | (ES_PASSWORD | ES_AUTOHSCROLL) as u32,
        24,
        96,
        336,
        28,
        ID_PASSWORD,
        F_BODY,
    );

    // Buttons, owner-drawn: grey plates, blood fill for the primary action.
    button(parent, "CANCEL", W - 262, 186, 106, 32, ID_CANCEL, false);
    button(parent, "SAVE LOGIN", W - 140, 186, 116, 32, ID_SAVE, true);
    if has_login {
        button(parent, "REMOVE", 24, 186, 96, 32, ID_REMOVE, false);
    }
    SetFocus(account);
}

unsafe fn button(parent: HWND, label: &str, x: i32, y: i32, w: i32, h: i32, id: usize, primary: bool) {
    let mut style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW as u32;
    if primary {
        style |= BS_DEFPUSHBUTTON as u32;
    }
    control(parent, "BUTTON", label, 0, style, x, y, w, h, id, F_BOLD);
}

unsafe extern "system" fn dlgproc(hwnd: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match msg {
        WM_CREATE => {
            build_controls(hwnd);
            0
        }
        WM_PAINT => {
            let mut ps: PAINTSTRUCT = std::mem::zeroed();
            let dc = BeginPaint(hwnd, &mut ps);
            outlined_text(dc, "ACCOUNT", 24, 16, 200, F_BOLD);
            outlined_text(dc, "PASSWORD", 24, 74, 200, F_BOLD);
            let note = std::ptr::addr_of!(NOTE_TEXT).read();
            let note = if note.is_empty() {
                "Encrypted to this Windows user, on this machine only.".to_string()
            } else {
                note
            };
            outlined_text(dc, &note, 24, 138, 340, F_NOTE);
            EndPaint(hwnd, &ps);
            0
        }
        WM_COMMAND => match (wp & 0xffff) as usize {
            ID_SAVE => {
                if do_save(hwnd) {
                    DestroyWindow(hwnd);
                }
                0
            }
            ID_CANCEL => {
                DestroyWindow(hwnd);
                0
            }
            ID_REMOVE => {
                do_remove();
                RESULT = 2;
                DestroyWindow(hwnd);
                0
            }
            _ => 0,
        },
        WM_CLOSE => {
            DestroyWindow(hwnd);
            0
        }
        WM_DESTROY => {
            DeleteObject(BRUSH_BG as _);
            DeleteObject(F_BOLD as _);
            DeleteObject(F_BODY as _);
            DeleteObject(F_NOTE as _);
            0
        }
        WM_DRAWITEM => {
            draw_button(lp);
            1
        }
        _ => DefWindowProcW(hwnd, msg, wp, lp),
    }
}

/// Encrypts and writes Bonesaw.login. Returns false (with a note on the
/// dialog) when the input is unusable, so the player can correct it.
unsafe fn do_save(hwnd: HWND) -> bool {
    let Some(client) = client_dir() else {
        return false;
    };
    let account_in = field_text(hwnd, ID_ACCOUNT);
    let password_in = field_text(hwnd, ID_PASSWORD);
    let old = crate::login::load(&client);
    let account = if account_in.is_empty() {
        old.as_ref().map(|(a, _)| a.clone())
    } else {
        Some(account_in)
    };
    let password = if password_in.is_empty() {
        old.as_ref().map(|(_, p)| p.clone())
    } else {
        Some(password_in)
    };
    let (Some(account), Some(password)) = (account, password) else {
        set_note(hwnd, "Both an account and a password are required.");
        return false;
    };
    let plain = format!("{account}\n{password}");
    let Some(blob) = protect(plain.as_bytes()) else {
        set_note(hwnd, "Windows refused to encrypt (DPAPI failure).");
        return false;
    };
    match write_atomic(&client.join("Bonesaw.login"), &blob) {
        Ok(()) => {
            crate::log("auto-login saved via the launcher button (DPAPI)");
            RESULT = 1;
            true
        }
        Err(e) => {
            set_note(hwnd, &e);
            false
        }
    }
}

unsafe fn do_remove() {
    if let Some(client) = client_dir() {
        let _ = std::fs::remove_file(client.join("Bonesaw.login"));
        crate::log("auto-login removed via the launcher button");
    }
}

unsafe fn field_text(hwnd: HWND, id: usize) -> String {
    let mut buf = [0u16; 256];
    let n = GetDlgItemTextW(hwnd, id as i32, buf.as_mut_ptr(), 256);
    String::from_utf16_lossy(&buf[..n as usize])
}

unsafe fn set_note(hwnd: HWND, s: &str) {
    std::ptr::addr_of_mut!(NOTE_TEXT).write(s.to_string());
    InvalidateRect(hwnd, std::ptr::null(), 1);
}

/// DPAPI encrypt (CurrentUser scope): decryptable only by this Windows user.
fn protect(plain: &[u8]) -> Option<Vec<u8>> {
    use windows_sys::Win32::Foundation::LocalFree;

    unsafe {
        let input = CRYPT_INTEGER_BLOB {
            cbData: plain.len() as u32,
            pbData: plain.as_ptr() as *mut u8,
        };
        let mut out: CRYPT_INTEGER_BLOB = std::mem::zeroed();
        let ok = CryptProtectData(
            &input,
            std::ptr::null(),
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

/// Owner-drawn buttons on the grey dialog: white outlined labels, blood fill
/// for the primary action.
unsafe fn draw_button(lp: LPARAM) {
    let ds = &*(lp as *const DRAWITEMSTRUCT);
    let save = ds.CtlID as usize == ID_SAVE;
    let selected = ds.itemState & ODS_SELECTED != 0;
    let (bg, border) = if save {
        (BLOOD_FILL, BLOOD_FILL)
    } else if selected {
        (GREY_PLATE_HOT, GREY_EDGE)
    } else {
        (GREY_PLATE, GREY_EDGE)
    };
    let brush = CreateSolidBrush(bg);
    FillRect(ds.hDC, &ds.rcItem, brush);
    DeleteObject(brush as _);
    let border_brush = CreateSolidBrush(border);
    FrameRect(ds.hDC, &ds.rcItem, border_brush);
    DeleteObject(border_brush as _);

    let mut buf = [0u16; 64];
    let n = GetWindowTextW(ds.hwndItem, buf.as_mut_ptr(), 64);
    let mut rc = ds.rcItem;
    let old_font = SelectObject(ds.hDC, F_BOLD as _);
    SetBkMode(ds.hDC, TRANSPARENT as i32);
    // Black offset passes under a white face: readable on any plate color.
    SetTextColor(ds.hDC, BLACK);
    for (dx, dy) in [(-1i32, 0i32), (1, 0), (0, -1), (0, 1)] {
        let mut o = rc;
        o.left += dx;
        o.right += dx;
        o.top += dy;
        o.bottom += dy;
        DrawTextW(
            ds.hDC,
            buf.as_mut_ptr(),
            n,
            &mut o,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE,
        );
    }
    SetTextColor(ds.hDC, WHITE);
    DrawTextW(
        ds.hDC,
        buf.as_mut_ptr(),
        n,
        &mut rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE,
    );
    SelectObject(ds.hDC, old_font);
    if selected {
        DrawFocusRect(ds.hDC, &rc);
    }
}

/// White label text over four black offset passes.
unsafe fn outlined_text(dc: HDC, s: &str, x: i32, y: i32, w: i32, f: HFONT) {
    let t = wide(s);
    let n = (t.len() - 1) as i32;
    let old_font = SelectObject(dc, f as _);
    SetBkMode(dc, TRANSPARENT as i32);
    SetTextColor(dc, BLACK);
    for (dx, dy) in [(-1i32, 0i32), (1, 0), (0, -1), (0, 1)] {
        let mut rc = RECT {
            left: x + dx,
            top: y + dy,
            right: x + dx + w,
            bottom: y + dy + 20,
        };
        DrawTextW(dc, t.as_ptr(), n, &mut rc, DT_LEFT | DT_SINGLELINE);
    }
    let mut rc = RECT {
        left: x,
        top: y,
        right: x + w,
        bottom: y + 22,
    };
    SetTextColor(dc, WHITE);
    DrawTextW(dc, t.as_ptr(), n, &mut rc, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, old_font);
}
