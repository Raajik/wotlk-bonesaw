//! A small native Win32 progress window. No GUI framework: one window class,
//! one double-buffered WM_PAINT, and a shared state struct the worker thread
//! updates. Everything the player sees during a launch is drawn here.

use std::sync::{Arc, Mutex};
use windows_sys::core::PCWSTR;
use windows_sys::Win32::Foundation::{COLORREF, HWND, LPARAM, LRESULT, POINT, RECT, WPARAM};
use windows_sys::Win32::Graphics::Gdi::*;
use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
use windows_sys::Win32::UI::WindowsAndMessaging::*;

const W: i32 = 512;
const H: i32 = 341;

// COLORREF is 0x00BBGGRR.
const BG: COLORREF = 0x000A0E0A;
const GREEN: COLORREF = 0x004AD93F;
const GREEN_DIM: COLORREF = 0x002E7A28;
const TEXT: COLORREF = 0x00D8E8D8;
const TEXT_DIM: COLORREF = 0x00708070;
const TRACK: COLORREF = 0x0018220E;
const RULE: COLORREF = 0x001E2A1E;

const WM_UI_REFRESH: u32 = WM_APP + 1;

#[derive(Default)]
struct State {
    status: String,
    file: String,
    percent: Option<f64>,
    hover_close: bool,
}

#[derive(Clone)]
pub struct Ui {
    hwnd: isize,
    state: Arc<Mutex<State>>,
}

// The HWND crosses to the worker thread as an isize; every use of it goes back
// through PostMessage, which is thread safe.
unsafe impl Send for Ui {}
unsafe impl Sync for Ui {}

static mut STATE_PTR: *const Mutex<State> = std::ptr::null();

impl Ui {
    pub fn create() -> Ui {
        let state = Arc::new(Mutex::new(State {
            status: "Starting...".into(),
            ..Default::default()
        }));
        unsafe {
            STATE_PTR = Arc::into_raw(state.clone());
        }
        let hwnd = unsafe { create_window() };
        Ui {
            hwnd: hwnd as isize,
            state,
        }
    }

    fn refresh(&self) {
        if self.hwnd != 0 {
            unsafe { PostMessageW(self.hwnd as HWND, WM_UI_REFRESH, 0, 0) };
        }
    }

    pub fn status(&self, text: &str) {
        {
            let mut s = self.state.lock().unwrap();
            s.status = text.to_string();
            s.file.clear();
            s.percent = None;
        }
        self.refresh();
    }

    pub fn progress(&self, file: &str, done: u64, total: u64) {
        {
            let mut s = self.state.lock().unwrap();
            s.file = file.to_string();
            s.percent = if total > 0 {
                Some((done as f64 / total as f64).clamp(0.0, 1.0))
            } else {
                None
            };
        }
        self.refresh();
    }

    pub fn quit(&self) {
        if self.hwnd != 0 {
            unsafe { PostMessageW(self.hwnd as HWND, WM_CLOSE, 0, 0) };
        }
    }

    pub fn error(&self, body: &str) {
        let title = wide("Bonesaw");
        let body = wide(body);
        unsafe {
            MessageBoxW(
                self.hwnd as HWND,
                body.as_ptr(),
                title.as_ptr(),
                MB_OK | MB_ICONERROR,
            )
        };
    }

    /// Runs on the main thread until the window closes.
    pub fn run(&self) {
        if self.hwnd == 0 {
            return;
        }
        unsafe {
            let mut msg: MSG = std::mem::zeroed();
            while GetMessageW(&mut msg, std::ptr::null_mut(), 0, 0) > 0 {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

unsafe fn create_window() -> HWND {
    let hinst = GetModuleHandleW(std::ptr::null());
    let class = wide("BonesawLauncher");
    let mut wc: WNDCLASSW = std::mem::zeroed();
    wc.lpfnWndProc = Some(wndproc);
    wc.hInstance = hinst;
    wc.lpszClassName = class.as_ptr();
    wc.hCursor = LoadCursorW(std::ptr::null_mut(), IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(BG);
    wc.hIcon = LoadIconW(hinst, 1 as PCWSTR);
    if RegisterClassW(&wc) == 0 {
        return std::ptr::null_mut();
    }

    let sw = GetSystemMetrics(SM_CXSCREEN);
    let sh = GetSystemMetrics(SM_CYSCREEN);
    let title = wide("Bonesaw");
    let hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        class.as_ptr(),
        title.as_ptr(),
        WS_POPUP,
        (sw - W) / 2,
        (sh - H) / 2,
        W,
        H,
        std::ptr::null_mut(),
        std::ptr::null_mut(),
        hinst,
        std::ptr::null(),
    );
    if !hwnd.is_null() {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    hwnd
}

unsafe extern "system" fn wndproc(hwnd: HWND, msg: u32, wp: WPARAM, lp: LPARAM) -> LRESULT {
    match msg {
        WM_UI_REFRESH => {
            InvalidateRect(hwnd, std::ptr::null(), 0);
            0
        }
        WM_PAINT => {
            paint(hwnd);
            0
        }
        WM_ERASEBKGND => 1,
        WM_NCHITTEST => {
            // Drag anywhere in the header strip, except over the close box.
            let mut pt = POINT {
                x: (lp & 0xFFFF) as i16 as i32,
                y: ((lp >> 16) & 0xFFFF) as i16 as i32,
            };
            ScreenToClient(hwnd, &mut pt);
            if in_close_box(pt.x, pt.y) {
                HTCLIENT as LRESULT
            } else if pt.y < 96 {
                HTCAPTION as LRESULT
            } else {
                HTCLIENT as LRESULT
            }
        }
        WM_MOUSEMOVE => {
            let x = (lp & 0xFFFF) as i16 as i32;
            let y = ((lp >> 16) & 0xFFFF) as i16 as i32;
            let hot = in_close_box(x, y);
            if let Some(st) = state() {
                let mut s = st.lock().unwrap();
                if s.hover_close != hot {
                    s.hover_close = hot;
                    drop(s);
                    InvalidateRect(hwnd, std::ptr::null(), 0);
                }
            }
            0
        }
        WM_LBUTTONDOWN => {
            let x = (lp & 0xFFFF) as i16 as i32;
            let y = ((lp >> 16) & 0xFFFF) as i16 as i32;
            if in_close_box(x, y) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            0
        }
        WM_CLOSE => {
            DestroyWindow(hwnd);
            0
        }
        WM_DESTROY => {
            PostQuitMessage(0);
            0
        }
        _ => DefWindowProcW(hwnd, msg, wp, lp),
    }
}

fn in_close_box(x: i32, y: i32) -> bool {
    (W - 44..W - 12).contains(&x) && (10..42).contains(&y)
}

unsafe fn state() -> Option<&'static Mutex<State>> {
    let p = STATE_PTR;
    if p.is_null() {
        None
    } else {
        Some(&*p)
    }
}

unsafe fn font(size: i32, bold: bool, face: &str) -> HFONT {
    let name = wide(face);
    CreateFontW(
        -size,
        0,
        0,
        0,
        if bold { 700 } else { 400 },
        0,
        0,
        0,
        DEFAULT_CHARSET as u32,
        OUT_DEFAULT_PRECIS as u32,
        CLIP_DEFAULT_PRECIS as u32,
        CLEARTYPE_QUALITY as u32,
        (DEFAULT_PITCH | FF_DONTCARE) as u32,
        name.as_ptr(),
    )
}

unsafe fn fill(dc: HDC, x: i32, y: i32, w: i32, h: i32, color: COLORREF) {
    let r = RECT {
        left: x,
        top: y,
        right: x + w,
        bottom: y + h,
    };
    let b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b as _);
}

unsafe fn text(dc: HDC, s: &str, x: i32, y: i32, w: i32, color: COLORREF, f: HFONT, flags: u32) {
    let old = SelectObject(dc, f as _);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT as i32);
    let mut r = RECT {
        left: x,
        top: y,
        right: x + w,
        bottom: y + 60,
    };
    let mut buf: Vec<u16> = s.encode_utf16().collect();
    let len = buf.len() as i32;
    DrawTextW(dc, buf.as_mut_ptr(), len, &mut r, flags);
    SelectObject(dc, old);
}

unsafe fn paint(hwnd: HWND) {
    let mut ps: PAINTSTRUCT = std::mem::zeroed();
    let dc = BeginPaint(hwnd, &mut ps);

    // Double buffered so the progress bar does not flicker.
    let mem = CreateCompatibleDC(dc);
    let bmp = CreateCompatibleBitmap(dc, W, H);
    let old_bmp = SelectObject(mem, bmp as _);

    let (status, file, percent, hover) = match state() {
        Some(st) => {
            let s = st.lock().unwrap();
            (s.status.clone(), s.file.clone(), s.percent, s.hover_close)
        }
        None => (String::new(), String::new(), None, false),
    };

    fill(mem, 0, 0, W, H, BG);
    fill(mem, 0, 0, W, 1, GREEN_DIM);
    fill(mem, 0, H - 1, W, 1, GREEN_DIM);
    fill(mem, 0, 0, 1, H, GREEN_DIM);
    fill(mem, W - 1, 0, 1, H, GREEN_DIM);

    let pen = CreatePen(PS_SOLID, 2, GREEN);
    let brush = CreateSolidBrush(TRACK);
    let op = SelectObject(mem, pen as _);
    let ob = SelectObject(mem, brush as _);
    Ellipse(mem, 24, 22, 68, 66);
    SelectObject(mem, op);
    SelectObject(mem, ob);
    DeleteObject(pen as _);
    DeleteObject(brush as _);

    let f_mark = font(24, true, "Segoe UI");
    let f_title = font(28, true, "Segoe UI");
    let f_sub = font(11, false, "Segoe UI");
    let f_body = font(15, false, "Segoe UI");
    let f_small = font(13, false, "Segoe UI");
    let f_foot = font(12, false, "Segoe UI");
    let f_close = font(18, false, "Segoe UI");

    text(mem, "B", 24, 31, 44, GREEN, f_mark, DT_CENTER | DT_SINGLELINE);
    text(
        mem,
        "BONESAW",
        82,
        22,
        320,
        GREEN,
        f_title,
        DT_LEFT | DT_SINGLELINE,
    );
    text(
        mem,
        "L A U N C H E R",
        84,
        58,
        320,
        TEXT_DIM,
        f_sub,
        DT_LEFT | DT_SINGLELINE,
    );
    text(
        mem,
        "\u{00D7}",
        W - 44,
        12,
        32,
        if hover { GREEN } else { TEXT_DIM },
        f_close,
        DT_CENTER | DT_SINGLELINE,
    );

    fill(mem, 24, 88, W - 48, 1, RULE);

    text(
        mem,
        &status,
        24,
        108,
        W - 48,
        TEXT,
        f_body,
        DT_LEFT | DT_SINGLELINE,
    );

    fill(mem, 24, 140, W - 48, 14, TRACK);
    if let Some(p) = percent {
        let w = (((W - 48) as f64) * p).round() as i32;
        if w > 0 {
            fill(mem, 24, 140, w, 14, GREEN);
        }
    }

    if !file.is_empty() {
        text(
            mem,
            &file,
            24,
            166,
            W - 140,
            TEXT_DIM,
            f_small,
            DT_LEFT | DT_SINGLELINE,
        );
        if let Some(p) = percent {
            let pct = format!("{}%", (p * 100.0).round() as i32);
            text(
                mem,
                &pct,
                W - 120,
                166,
                96,
                GREEN,
                f_small,
                DT_RIGHT | DT_SINGLELINE,
            );
        }
    }

    text(
        mem,
        "Bonesaw  \u{00B7}  github.com/Raajik/wotlk-bonesaw",
        0,
        H - 34,
        W,
        TEXT_DIM,
        f_foot,
        DT_CENTER | DT_SINGLELINE,
    );

    for f in [f_mark, f_title, f_sub, f_body, f_small, f_foot, f_close] {
        DeleteObject(f as _);
    }

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp as _);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}
