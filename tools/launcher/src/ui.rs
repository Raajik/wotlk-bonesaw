//! A small native Win32 progress window. No GUI framework: one window class,
//! one double-buffered WM_PAINT, and a shared state struct the worker thread
//! updates. Everything the player sees during a launch is drawn here.
//!
//! The look is lifted from assets/bonesaw.ico: a charcoal disc, a white steel
//! sawblade and deep blood. The blade in the header is drawn here rather than
//! blitted from the icon so it can turn -- slowly while the launcher idles,
//! and visibly faster whenever something is actually downloading.

use std::path::Path;
use std::sync::{Arc, Mutex};
use std::time::Instant;
use windows_sys::core::PCWSTR;
use windows_sys::Win32::Foundation::{COLORREF, HWND, LPARAM, LRESULT, POINT, RECT, WPARAM};
use windows_sys::Win32::Graphics::Gdi::*;
use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
use windows_sys::Win32::UI::WindowsAndMessaging::*;

const W: i32 = 512;
const H: i32 = 352;

// COLORREF is 0x00BBGGRR.
pub(crate) const BG: COLORREF = 0x00141113; // near-black with a plum hint
pub(crate) const BLADE_BG: COLORREF = 0x001A1518; // inside the blade ring
pub(crate) const BLOOD: COLORREF = 0x001C169E; // arterial red, the accent
pub(crate) const BLOOD_DEEP: COLORREF = 0x0001016E; // the icon's own blood, top edge
pub(crate) const BLOOD_HOT: COLORREF = 0x002C40D8; // bright red: hover, percent, bar edge
pub(crate) const STEEL: COLORREF = 0x00F0F2F2; // blade teeth, straight from the icon
pub(crate) const BONE: COLORREF = 0x00D2DEE4; // primary text, warm bone white
pub(crate) const BONE_DIM: COLORREF = 0x007E888F; // secondary text
pub(crate) const TRACK: COLORREF = 0x0019171E; // progress track
pub(crate) const RULE: COLORREF = 0x00242127;
pub(crate) const FRAME: COLORREF = 0x0027242A;

const WM_UI_REFRESH: u32 = WM_APP + 1;

struct State {
    status: String,
    file: String,
    percent: Option<f64>,
    hover_close: bool,
    hover_login: bool,
    spin: f64, // blade angle, degrees
    version: String,
    client: Option<std::path::PathBuf>,
    auto_login: bool,
    dialog_open: bool,
    launch_in: Option<u8>,
    created: Instant,
}

impl Default for State {
    fn default() -> Self {
        Self {
            status: String::new(),
            file: String::new(),
            percent: None,
            hover_close: false,
            hover_login: false,
            spin: 0.0,
            version: String::new(),
            client: None,
            auto_login: false,
            dialog_open: false,
            launch_in: None,
            created: Instant::now(),
        }
    }
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
    pub fn create(version: &str) -> Ui {
        let state = Arc::new(Mutex::new(State {
            status: "Starting...".into(),
            version: version.to_string(),
            created: Instant::now(),
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

    /// Records the client folder (the auto-login dialog writes beside the
    /// launcher) and picks up whether a saved login already exists.
    pub fn set_client_dir(&self, client: &Path) {
        {
            let mut s = self.state.lock().unwrap();
            s.client = Some(client.to_path_buf());
            s.auto_login = client.join("Bonesaw.login").is_file();
        }
        self.refresh();
    }

    /// With Wow focused on top a moment later, a warm local run would flash
    /// this window for a blink. Hold it up long enough to actually be seen,
    /// counting down the last seconds so the hold reads as intentional.
    pub fn linger(&self) {
        const MIN_VISIBLE: std::time::Duration = std::time::Duration::from_secs(4);
        loop {
            let remaining = {
                let mut s = self.state.lock().unwrap();
                match MIN_VISIBLE.checked_sub(s.created.elapsed()) {
                    Some(d) => {
                        s.launch_in = Some(d.as_secs() as u8 + 1);
                        d
                    }
                    None => {
                        s.launch_in = None;
                        std::time::Duration::ZERO
                    }
                }
            };
            self.refresh();
            if remaining.is_zero() {
                return;
            }
            std::thread::sleep(remaining.min(std::time::Duration::from_secs(1)));
        }
    }

    /// Blocks the launch flow while the auto-login dialog is open: the player
    /// is mid-edit and Wow must not spawn out from under them.
    pub fn wait_for_dialog(&self) {
        loop {
            let open = {
                let s = self.state.lock().unwrap();
                s.dialog_open
            };
            if !open {
                return;
            }
            std::thread::sleep(std::time::Duration::from_millis(50));
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
        WM_CREATE => {
            // The saw never rests; the timer drives the sawing stroke at ~30 fps.
            SetTimer(hwnd, 1, 33, None);
            0
        }
        WM_TIMER => {
            if let Some(st) = state() {
                let mut s = st.lock().unwrap();
                // Chewing on a download makes the sawing stroke much faster.
                let speed = if s.percent.is_some() { 11.0 } else { 3.0 };
                s.spin = (s.spin + speed) % 360.0;
                drop(s);
                InvalidateRect(hwnd, std::ptr::null(), 0);
            }
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
            } else if pt.y < 100 {
                HTCAPTION as LRESULT
            } else {
                HTCLIENT as LRESULT
            }
        }
    WM_MOUSEMOVE => {
            let x = (lp & 0xFFFF) as i16 as i32;
            let y = ((lp >> 16) & 0xFFFF) as i16 as i32;
            let hot_close = in_close_box(x, y);
            let hot_login = in_login_button(x, y);
            if let Some(st) = state() {
                let mut s = st.lock().unwrap();
                let mut changed = false;
                if s.hover_close != hot_close {
                    s.hover_close = hot_close;
                    changed = true;
                }
                if s.hover_login != hot_login {
                    s.hover_login = hot_login;
                    changed = true;
                }
                if changed {
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
            } else if in_login_button(x, y) {
                open_login_dialog(hwnd);
            }
            0
        }
        WM_CLOSE => {
            DestroyWindow(hwnd);
            0
        }
        WM_DESTROY => {
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            0
        }
        _ => DefWindowProcW(hwnd, msg, wp, lp),
    }
}

fn in_close_box(x: i32, y: i32) -> bool {
    (W - 44..W - 12).contains(&x) && (10..42).contains(&y)
}

fn in_login_button(x: i32, y: i32) -> bool {
    (W - 192..W - 22).contains(&x) && (H - 40..H - 14).contains(&y)
}

/// Opens the auto-login dialog and reflects the outcome on the button and
/// status line.
unsafe fn open_login_dialog(hwnd: HWND) {
    let dir = state()
        .and_then(|st| st.lock().ok())
        .and_then(|s| s.client.clone());
    let Some(dir) = dir else { return };
    if let Some(st) = state() {
        let mut s = st.lock().unwrap();
        s.dialog_open = true;
        s.status = "Auto-login setup - launch paused...".into();
    }
    InvalidateRect(hwnd, std::ptr::null(), 0);
    match crate::login_dialog::show(hwnd as isize, &dir) {
        crate::login_dialog::Outcome::Saved => {
            if let Some(st) = state() {
                let mut s = st.lock().unwrap();
                s.auto_login = true;
                s.status = "Auto-login saved - encrypted on this machine.".into();
                s.file.clear();
                s.percent = None;
            }
        }
        crate::login_dialog::Outcome::Removed => {
            if let Some(st) = state() {
                let mut s = st.lock().unwrap();
                s.auto_login = false;
                s.status = "Auto-login removed.".into();
                s.file.clear();
                s.percent = None;
            }
        }
        crate::login_dialog::Outcome::None => {}
    }
    if let Some(st) = state() {
        let mut s = st.lock().unwrap();
        s.dialog_open = false;
    }
    InvalidateRect(hwnd, std::ptr::null(), 0);
}

unsafe fn state() -> Option<&'static Mutex<State>> {
    let p = STATE_PTR;
    if p.is_null() {
        None
    } else {
        Some(&*p)
    }
}

pub(crate) unsafe fn font(size: i32, bold: bool, face: &str) -> HFONT {
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

/// Filled shape with no outline: NULL_PEN in, brush in, both restored.
unsafe fn fill_shape(dc: HDC, pts: &[POINT], color: COLORREF) {
    let b = CreateSolidBrush(color);
    let open_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    let open_brush = SelectObject(dc, b as _);
    Polygon(dc, pts.as_ptr(), pts.len() as i32);
    SelectObject(dc, open_pen);
    SelectObject(dc, open_brush);
    DeleteObject(b as _);
}

/// Polyline stroke with a solid pen: the outline mark's brush.
unsafe fn stroke(dc: HDC, pts: &[POINT], color: COLORREF, w: i32) {
    let pen = CreatePen(PS_SOLID, w, color);
    let old = SelectObject(dc, pen as _);
    Polyline(dc, pts.as_ptr(), pts.len() as i32);
    SelectObject(dc, old);
    DeleteObject(pen as _);
}

/// A single thick segment, as two points.
unsafe fn line(dc: HDC, a: POINT, b: POINT, color: COLORREF, w: i32) {
    stroke(dc, &[a, b], color, w);
}

/// The player-picked mark: a bonesaw as a white outline (the icon is the same
/// drawing on the charcoal disc). The saw rocks a few degrees around its
/// middle like a cutting stroke; the phase advances constantly and the swing
/// comes from its sine, so downloads turn sway into vigorous sawing.
unsafe fn bonesaw_x(mem: HDC, cx: i32, cy: i32, phase: f64) {
    let (cxf, cyf) = (cx as f64, cy as f64);
    let k = 0.78f64; // mark units (radius 36) -> header pixels
    let rock = 0.0; // outline strokes below are recomputed per angle below
    let _ = rock;
    let sa = -45.0f64.to_radians() + 7.0f64.to_radians() * phase.to_radians().sin();
    let sp = |lx: f64, ly: f64| -> POINT {
        let (c, s) = (sa.cos(), sa.sin());
        POINT {
            x: (cxf + k * (lx * c - ly * s)).round() as i32,
            y: (cyf + k * (lx * s + ly * c)).round() as i32,
        }
    };

    // Blade: bottom edge out, toothed top edge back, closed.
    let mut p: Vec<POINT> = vec![sp(-24.0, 4.2), sp(14.0, 4.2), sp(14.0, -3.2)];
    let teeth = 8;
    let tw = 34.0 / teeth as f64;
    for i in (0..teeth).rev() {
        let bx = -23.0 + i as f64 * tw;
        p.push(sp(bx + tw * 0.5, -6.4));
        p.push(sp(bx, -3.2));
    }
    p.push(sp(-24.0, 4.2));
    stroke(mem, &p, STEEL, 2);

    // Grip and the ring at its end.
    let grip = [sp(14.0, -3.4), sp(30.0, -3.4), sp(30.0, 3.4), sp(14.0, 3.4), sp(14.0, -3.4)];
    stroke(mem, &grip, STEEL, 2);
    let mut ring_pts: Vec<POINT> = Vec::with_capacity(21);
    for i in 0..=20 {
        let a = std::f64::consts::TAU * i as f64 / 20.0;
        ring_pts.push(sp(33.0 + 4.6 * a.cos(), 4.6 * a.sin()));
    }
    stroke(mem, &ring_pts, STEEL, 2);

    // Drip: a short run off the low edge and a hanging drop, outlined.
    line(mem, sp(2.0, 5.0), sp(2.0, 9.0), STEEL, 2);
    let mut e: Vec<POINT> = Vec::with_capacity(17);
    for i in 0..=16 {
        let a = std::f64::consts::TAU * i as f64 / 16.0;
        e.push(sp(2.0 + 3.0 * a.cos(), 11.5 + 3.4 * a.sin()));
    }
    stroke(mem, &e, STEEL, 2);
}

/// The auto-login button, bottom-right: blood outline while off, bone while
/// on, filled blood on hover.
unsafe fn login_button(mem: HDC, on: bool, hover: bool) {
    let (x, y, w, h) = (W - 192, H - 40, 170, 26);
    let border = if on { BONE_DIM } else { BLOOD };
    let (bg, fg) = if hover {
        (BLOOD, BG)
    } else if on {
        (BLADE_BG, BONE)
    } else {
        (BLADE_BG, BLOOD)
    };
    fill(mem, x, y, w, h, bg);
    fill(mem, x, y, w, 1, border);
    fill(mem, x, y + h - 1, w, 1, border);
    fill(mem, x, y, 1, h, border);
    fill(mem, x + w - 1, y, 1, h, border);
    let label = if on { "AUTO-LOGIN: ON" } else { "SET UP AUTO-LOGIN" };
    let f = font(11, true, "Segoe UI");
    text(mem, label, x, y + 6, w, fg, f, DT_CENTER | DT_SINGLELINE);
    DeleteObject(f as _);
}

/// Sawtooth progress bar: the fill is cut into teeth along its top edge, with
/// a hot leading edge like the side of the blade doing the cutting.
unsafe fn saw_bar(mem: HDC, x0: i32, y0: i32, w: i32, h: i32, p: f64) {
    fill(mem, x0, y0, w, h, TRACK);
    let filled = (w as f64 * p).round() as i32;
    if filled < 2 {
        return;
    }
    let per = 14.0; // tooth pitch
    let valley = 6.0; // tooth depth
    let x0 = x0 as f64;
    let y0 = y0 as f64;
    let x1 = x0 + filled as f64;

    let mut pts: Vec<POINT> = Vec::with_capacity(70);
    pts.push(POINT {
        x: x0 as i32,
        y: (y0 + h as f64) as i32,
    });
    pts.push(POINT {
        x: x0 as i32,
        y: (y0 + valley) as i32,
    });
    // Top edge, left to right: ramp up to a peak, drop back into the valley.
    let mut edge = x0;
    while edge + per <= x1 {
        pts.push(POINT {
            x: (edge + per) as i32,
            y: y0 as i32,
        });
        pts.push(POINT {
            x: (edge + per) as i32,
            y: (y0 + valley) as i32,
        });
        edge += per;
    }
    // The last, partial tooth rises with the same slope.
    let frac = ((x1 - edge) / per).clamp(0.0, 1.0);
    pts.push(POINT {
        x: x1 as i32,
        y: (y0 + valley * (1.0 - frac)) as i32,
    });
    pts.push(POINT {
        x: x1 as i32,
        y: (y0 + h as f64) as i32,
    });
    fill_shape(mem, &pts, BLOOD);
    fill(mem, x1 as i32 - 2, y0 as i32, 2, h as i32, BLOOD_HOT);
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

    // Double buffered so the turning blade does not flicker.
    let mem = CreateCompatibleDC(dc);
    let bmp = CreateCompatibleBitmap(dc, W, H);
    let old_bmp = SelectObject(mem, bmp as _);

    let (status, file, percent, hover, spin, version, auto_login, hover_login, launch_in) =
        match state() {
            Some(st) => {
                let s = st.lock().unwrap();
                (
                    s.status.clone(),
                    s.file.clone(),
                    s.percent,
                    s.hover_close,
                    s.spin,
                    s.version.clone(),
                    s.auto_login,
                    s.hover_login,
                    s.launch_in,
                )
            }
            None => (
                String::new(),
                String::new(),
                None,
                false,
                0.0,
                String::new(),
                false,
                false,
                None,
            ),
        };

    fill(mem, 0, 0, W, H, BG);
    fill(mem, 0, 0, W, 1, FRAME);
    fill(mem, 0, H - 1, W, 1, FRAME);
    fill(mem, 0, 0, 1, H, FRAME);
    fill(mem, W - 1, 0, 1, H, FRAME);
    fill(mem, 0, 0, W, 2, BLOOD_DEEP);

    bonesaw_x(mem, 46, 46, spin);

    let f_title = font(28, true, "Segoe UI");
    let f_sub = font(11, true, "Segoe UI");
    let f_body = font(15, false, "Segoe UI");
    let f_small = font(13, false, "Segoe UI");
    let f_foot = font(12, false, "Segoe UI");
    let f_close = font(18, false, "Segoe UI");

    text(
        mem,
        "BONESAW",
        88,
        20,
        320,
        BONE,
        f_title,
        DT_LEFT | DT_SINGLELINE,
    );
    text(
        mem,
        "CLIENT UPDATER",
        90,
        58,
        320,
        BLOOD,
        f_sub,
        DT_LEFT | DT_SINGLELINE,
    );
    text(
        mem,
        &format!("v{version}"),
        W - 190,
        26,
        130,
        BONE_DIM,
        f_sub,
        DT_RIGHT | DT_SINGLELINE,
    );
    if let Some(n) = launch_in {
        text(
            mem,
            &format!("starting in {n}"),
            W - 190,
            42,
            130,
            BLOOD_HOT,
            f_sub,
            DT_RIGHT | DT_SINGLELINE,
        );
    }
    text(
        mem,
        "\u{00D7}",
        W - 44,
        12,
        32,
        if hover { BLOOD_HOT } else { BONE_DIM },
        f_close,
        DT_CENTER | DT_SINGLELINE,
    );

    fill(mem, 24, 100, W - 48, 1, RULE);

    text(
        mem,
        &status,
        24,
        118,
        W - 48,
        BONE,
        f_body,
        DT_LEFT | DT_SINGLELINE,
    );

    if let Some(p) = percent {
        saw_bar(mem, 24, 148, W - 48, 18, p);
    } else {
        fill(mem, 24, 148, W - 48, 18, TRACK);
    }

    if !file.is_empty() {
        text(
            mem,
            &file,
            24,
            176,
            W - 140,
            BONE_DIM,
            f_small,
            DT_LEFT | DT_SINGLELINE,
        );
        if let Some(p) = percent {
            let pct = format!("{}%", (p * 100.0).round() as i32);
            text(
                mem,
                &pct,
                W - 120,
                176,
                96,
                BLOOD_HOT,
                f_small,
                DT_RIGHT | DT_SINGLELINE,
            );
        }
    }

    text(
        mem,
        "Bonesaw  \u{00B7}  github.com/Raajik/wotlk-bonesaw",
        24,
        H - 30,
        270,
        BONE_DIM,
        f_foot,
        DT_LEFT | DT_SINGLELINE,
    );
    login_button(mem, auto_login, hover_login);

    for f in [f_title, f_sub, f_body, f_small, f_foot, f_close] {
        DeleteObject(f as _);
    }

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp as _);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}
