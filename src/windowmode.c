/* Borderless - NBA 2K14: Video Settings > Window Mode = Windowed / Fullscreen / Borderless.
 *
 * The game's row (record 010BD280 / 010BD460, label 90035F44) has a value object (01619F40 / 01619B38):
 *   +4 text (005376D0: "Off"/"On" from 016199D8), +8 / +C next / prev (005376A0: toggles 016199D8, 1 = windowed).
 * Here: +4/+8/+C are ours. Fullscreen = the game's Off; Windowed and Borderless = the game's On, Borderless also
 * turns the game's window styles / position (its imports SetWindowLongA, SetWindowPos, CreateWindowExA) into a
 * borderless window covering the monitor. The choice is kept in Borderless.ini ([WindowMode] Mode=).
 * Game window object: +2C fullscreen, +850 hwnd, +854 windowed now; 00E0C420 applies the styles.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "slam.h"
#include <d3d9.h>

extern const SLAM_API *g_slam; extern SlamMod *g_slamMe; extern void *g_slamInst;
void glue_migrate(const char *file);
typedef unsigned int u32; typedef unsigned char u8; typedef unsigned short u16;

static char g_log[MAX_PATH]; static const char *g_ini;
static CRITICAL_SECTION g_lk;
static volatile int g_bl;          /* borderless applied (acts while the game's applied mode is windowed) */
static volatile int g_pend;        /* borderless chosen in the menu (applied when the game applies its video settings) */
#define GAME_WINDOWED (*(volatile int *)0x016199D8)      /* the menu's working copy */
#define APPLIED_WINDOWED (*(volatile int *)0x01995BDC)   /* the game's setting 6 (0x01995B7C + 6*16) */
#define APPLIED_VSYNC (*(volatile int *)0x01995BBC)      /* setting 4 */
static int g_logN;

static void rlog(const char *fmt, ...)
{
    char b[1400]; int n; va_list ap; HANDLE f; DWORD w; SYSTEMTIME t;
    GetLocalTime(&t);
    n = wsprintfA(b, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt); n += wvsprintfA(b + n, fmt, ap); va_end(ap);
    b[n++] = '\r'; b[n++] = '\n';
    EnterCriticalSection(&g_lk);
    f = CreateFileA(g_log, FILE_APPEND_DATA, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, b, n, &w, 0); CloseHandle(f); }
    LeaveCriticalSection(&g_lk);
}

/* ---------------- the window ---------------- */
typedef LONG (WINAPI *SWL_t)(HWND, int, LONG);
typedef BOOL (WINAPI *SWP_t)(HWND, HWND, int, int, int, int, UINT);
typedef HWND (WINAPI *CWE_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static SWL_t g_swl; static SWP_t g_swp; static CWE_t g_cwe;
static HWND g_game;
static LONG g_wantStyle, g_wantEx; static int g_haveStyle, g_haveEx;   /* what the game last asked for */
static RECT g_wantRect; static HWND g_wantAfter; static UINT g_wantFlags; static int g_haveRect;
#define BAD_STYLE (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME)
#define BAD_EX    (WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE)

static int ours(HWND h) { if (h && !g_game && !GetParent(h) && IsWindowVisible(h)) g_game = h; return h && (g_game ? h == g_game : GetParent(h) == 0); }
static int win_style(HWND h)   /* the game's window is a windowed one (caption), not its fullscreen WS_POPUP one */
{
    LONG st = g_haveStyle ? g_wantStyle : GetWindowLongA(h, GWL_STYLE);
    return (st & WS_CAPTION) == WS_CAPTION || !(st & WS_POPUP);
}
static int acting_h(HWND h) { return g_bl && win_style(h); }
static void mon_rect(HWND h, RECT *r)
{
    MONITORINFO mi; HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY);
    mi.cbSize = sizeof mi;
    if (m && GetMonitorInfoA(m, &mi)) *r = mi.rcMonitor;
    else { r->left = r->top = 0; r->right = GetSystemMetrics(SM_CXSCREEN); r->bottom = GetSystemMetrics(SM_CYSCREEN); }
}
/* 1.0.1: another mod that also changes the window (2K14Hook, crash report: stack overflow 3 s in) answered our style
 * change with its own, which came back through these hooks again and again. Inside our own call nothing is changed,
 * and if the frame keeps coming back (30 times in 2 s) Borderless stops changing the window for this session. */
static DWORD g_tls = TLS_OUT_OF_INDEXES; static DWORD g_fixT0; static int g_fixN;
static int enter(void) { int d; if (g_tls == TLS_OUT_OF_INDEXES) return 1; d = (int)(INT_PTR)TlsGetValue(g_tls); TlsSetValue(g_tls, (void *)(INT_PTR)(d + 1)); return d == 0; }
static void leave(void) { if (g_tls != TLS_OUT_OF_INDEXES) TlsSetValue(g_tls, (void *)(INT_PTR)((int)(INT_PTR)TlsGetValue(g_tls) - 1)); }
/* 1.0.0b: no longer "gives up for the whole session" - after a crash the game re-applies its window many times at
 * start (that tripped the old 30-in-2-s rule and left the game windowed). Now it only pauses 5 s, and a watchdog
 * (bl_watch) puts the borderless window back whenever the game's frame returns. */
static volatile DWORD g_pauseUntil;
#define g_giveUp ((int)(GetTickCount() - g_pauseUntil) < 0)
static int fight(void)
{
    DWORD t = GetTickCount();
    if (g_giveUp) return 1;
    if (t - g_fixT0 > 2000) { g_fixT0 = t; g_fixN = 0; }
    if (++g_fixN > 30) { g_fixN = 0; g_pauseUntil = t + 5000; { static int lg; if (lg < 5) { lg++; rlog("the window keeps getting its frame back - Borderless pauses 5 s, then makes it borderless again"); } } return 1; }
    return 0;
}
static LONG WINAPI MySWL(HWND h, int i, LONG v)
{
    LONG r;
    if (!enter() || g_giveUp) { r = g_swl(h, i, v); leave(); return r; }
    if (ours(h) && (i == GWL_STYLE || i == GWL_EXSTYLE)) {
        if (g_logN < 24) { g_logN++; rlog("game: %s %08X%s", i == GWL_STYLE ? "style" : "exstyle", v, acting_h(h) ? " -> borderless" : ""); }
        if (i == GWL_STYLE) { g_wantStyle = v; g_haveStyle = 1; if (acting_h(h)) v = (v & ~BAD_STYLE) | WS_POPUP; }
        else { g_wantEx = v; g_haveEx = 1; if (acting_h(h)) v &= ~BAD_EX; }
    }
    r = g_swl(h, i, v);
    leave();
    return r;
}
static BOOL WINAPI MySWP(HWND h, HWND after, int x, int y, int cx, int cy, UINT f)
{
    BOOL r;
    if (!enter() || g_giveUp) { r = g_swp(h, after, x, y, cx, cy, f); leave(); return r; }
    if (ours(h) && !((f & SWP_NOSIZE) && (f & SWP_NOMOVE))) {
        if (g_logN < 24) { g_logN++; rlog("game: window pos %d,%d %dx%d flags %X%s", x, y, cx, cy, f, acting_h(h) ? " -> monitor" : ""); }
        g_wantRect.left = x; g_wantRect.top = y; g_wantRect.right = x + cx; g_wantRect.bottom = y + cy;
        g_wantAfter = after; g_wantFlags = f; g_haveRect = 1;
        if (acting_h(h)) {
            RECT r; mon_rect(h, &r);
            if ((!g_haveStyle || GetWindowLongA(h, GWL_STYLE) & WS_CAPTION) && !fight()) {   /* window made before us (no style call seen): borderless now */
                LONG st = GetWindowLongA(h, GWL_STYLE), ex = GetWindowLongA(h, GWL_EXSTYLE);
                if (!g_haveStyle) { g_wantStyle = st; g_wantEx = ex; g_haveStyle = g_haveEx = 1; }
                g_swl(h, GWL_STYLE, (st & ~BAD_STYLE) | WS_POPUP); g_swl(h, GWL_EXSTYLE, ex & ~BAD_EX);
                if (g_logN < 30) { g_logN++; rlog("   window had a frame - made borderless"); }
            }
            x = r.left; y = r.top; cx = r.right - r.left; cy = r.bottom - r.top;
            f &= ~(SWP_NOMOVE | SWP_NOSIZE); f |= SWP_FRAMECHANGED;
            if (after == HWND_TOPMOST) after = HWND_TOP;
        }
    }
    r = g_swp(h, after, x, y, cx, cy, f);
    leave();
    return r;
}
static HWND WINAPI MyCWE(DWORD ex, LPCSTR cls, LPCSTR name, DWORD st, int x, int y, int w, int hgt, HWND par, HMENU m, HINSTANCE i, LPVOID p)
{
    HWND h = g_cwe(ex, cls, name, st, x, y, w, hgt, par, m, i, p);
    if (!par && !g_game && h) {
        g_game = h; g_wantStyle = (LONG)st; g_wantEx = (LONG)ex; g_haveStyle = g_haveEx = 1;
        rlog("game window %08X style %08X ex %08X %dx%d (game Window Mode %s)", (u32)h, st, ex, w, hgt, GAME_WINDOWED ? "On" : "Off");
    }
    return h;
}
/* the choice changed while the game stays windowed (Windowed <-> Borderless): restyle now */
static void apply_now(void)
{
    HWND h = g_game;
    if (!h) { DWORD pid = 0; HWND f = GetForegroundWindow(); if (f) GetWindowThreadProcessId(f, &pid);   /* window made before us */
              if (pid == GetCurrentProcessId() && !GetParent(f)) h = g_game = f; }
    if (!h || !APPLIED_WINDOWED) { rlog("restyle skipped (%s)", h ? "game not windowed" : "game window not found"); return; }
    if (g_bl) {
        RECT r; mon_rect(h, &r);
        g_swl(h, GWL_STYLE, ((g_haveStyle ? g_wantStyle : GetWindowLongA(h, GWL_STYLE)) & ~BAD_STYLE) | WS_POPUP | WS_VISIBLE);
        g_swl(h, GWL_EXSTYLE, (g_haveEx ? g_wantEx : GetWindowLongA(h, GWL_EXSTYLE)) & ~BAD_EX);
        g_swp(h, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        if (g_haveStyle) g_swl(h, GWL_STYLE, g_wantStyle | WS_VISIBLE);
        if (g_haveEx) g_swl(h, GWL_EXSTYLE, g_wantEx);
        if (g_haveRect) g_swp(h, g_wantAfter == HWND_TOPMOST ? HWND_TOP : g_wantAfter, g_wantRect.left, g_wantRect.top,
                              g_wantRect.right - g_wantRect.left, g_wantRect.bottom - g_wantRect.top, (g_wantFlags | SWP_FRAMECHANGED | SWP_SHOWWINDOW) & ~(SWP_NOMOVE | SWP_NOSIZE));
        else g_swp(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    rlog("window now %s", g_bl ? "borderless" : "windowed");
}

/* 1.0.0b: borderless chosen, game windowed, but the window has its frame again (game restyled it while paused,
 * after a crash, alt-tab...) -> borderless again. Once a second, outside any of our own window calls. */
static int g_rectMiss, g_rectOff;
static int bl_wrong(HWND h)   /* borderless wanted, game windowed, but the window has a frame or does not cover the monitor */
{
    RECT w, m; LONG st, ex;
    if (!g_bl || !h || !IsWindow(h) || !APPLIED_WINDOWED || IsIconic(h)) return 0;
    st = GetWindowLongA(h, GWL_STYLE); ex = GetWindowLongA(h, GWL_EXSTYLE);
    if ((st & BAD_STYLE) || (ex & BAD_EX)) return 1;
    if (g_rectOff || !GetWindowRect(h, &w)) return 0;
    mon_rect(h, &m);
    if (w.left - m.left > 2 || m.left - w.left > 2 || w.top - m.top > 2 || m.top - w.top > 2 ||
        w.right - m.right > 2 || m.right - w.right > 2 || w.bottom - m.bottom > 2 || m.bottom - w.bottom > 2) {
        if (++g_rectMiss > 3) { g_rectOff = 1; rlog("watchdog: window size can't be matched to the monitor here - only the frame is watched"); return 0; }
        return 2;
    }
    g_rectMiss = 0;
    return 0;
}
/* 1.0.0c: the window procedure is watched too - a frame added by ANYONE (the game, Direct3D itself on Reset, another
 * mod, Windows after alt-tab / a crash) is taken off before it is applied, and the window keeps covering the monitor */
static WNDPROC g_oldProc; static HWND g_subWnd;
static LRESULT CALLBACK BlProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_bl && APPLIED_WINDOWED && !g_giveUp) {
        if (msg == WM_STYLECHANGING && lp) {
            STYLESTRUCT *ss = (STYLESTRUCT *)lp;
            if (wp == (WPARAM)GWL_STYLE && (ss->styleNew & BAD_STYLE)) ss->styleNew = (ss->styleNew & ~BAD_STYLE) | WS_POPUP;
            else if (wp == (WPARAM)GWL_EXSTYLE) ss->styleNew &= ~BAD_EX;
        } else if (msg == WM_WINDOWPOSCHANGING && lp && !IsIconic(h)) {
            WINDOWPOS *p = (WINDOWPOS *)lp; RECT r; mon_rect(h, &r);
            if (!(p->flags & SWP_NOMOVE)) { p->x = r.left; p->y = r.top; }
            if (!(p->flags & SWP_NOSIZE)) { p->cx = r.right - r.left; p->cy = r.bottom - r.top; }
        }
    }
    return CallWindowProcA(g_oldProc, h, msg, wp, lp);
}
static void bl_subclass(HWND h)
{
    if (!h || h == g_subWnd || !IsWindow(h)) return;
    g_oldProc = (WNDPROC)g_swl(h, GWL_WNDPROC, (LONG)(LONG_PTR)BlProc);
    if (g_oldProc) { g_subWnd = h; rlog("window %08X watched (frames added later are taken off)", (u32)h); }
}
static DWORD WINAPI bl_watch(LPVOID u)
{
    (void)u;
    for (;;) {
        Sleep(500);
        if (!g_game) { DWORD pid = 0; HWND f = GetForegroundWindow(); if (f) GetWindowThreadProcessId(f, &pid);
            if (pid == GetCurrentProcessId() && !GetParent(f)) g_game = f; }
        if (g_game && g_game != g_subWnd) bl_subclass(g_game);
        if (!g_giveUp && bl_wrong(g_game)) {
            static int lg; if (lg < 10) { lg++; rlog("watchdog: the game window had a frame / wrong size again - borderless again"); }
            apply_now();
        }
    }
    return 0;
}

/* ---------------- tearing: VSync for every mode ----------------
 * The game's VSync setting only reached exclusive fullscreen: a windowed / borderless device was made with
 * PresentationInterval IMMEDIATE -> tearing (a window covering the monitor gets flipped directly by Windows).
 * Direct3DCreate9 -> IDirect3D9::CreateDevice (slot 16) -> IDirect3DDevice9::Reset (slot 16): the interval is
 * set to ONE while the game's VSync is On. */
typedef IDirect3D9 *(WINAPI *C9_t)(UINT);
typedef HRESULT (WINAPI *CD_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT (WINAPI *RS_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
static CD_t g_cd; static RS_t g_rs; static int cfg_vsync = 1, cfg_copy = 1;
static void fix_pp(D3DPRESENT_PARAMETERS *pp, const char *what)
{
    UINT was;
    if (!pp || !cfg_vsync) return;
    was = pp->PresentationInterval;
    if (APPLIED_VSYNC && was != D3DPRESENT_INTERVAL_ONE) pp->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    {   UINT se = pp->SwapEffect, bc = pp->BackBufferCount;
        /* 0.5: the black / old blocks in the picture: the game redraws only parts of the screen and expects the rest
         * of the last frame still there - with DISCARD a window gets an undefined back buffer. COPY keeps it. */
        if (cfg_copy && pp->Windowed && pp->BackBufferWidth > 64) { pp->SwapEffect = D3DSWAPEFFECT_COPY; pp->BackBufferCount = 1; pp->MultiSampleType = D3DMULTISAMPLE_NONE; }
        rlog("%s: %s %ux%u, VSync %s, interval %08X -> %08X, swap %u -> %u, buffers %u, flags %08X", what, pp->Windowed ? "windowed" : "fullscreen",
             pp->BackBufferWidth, pp->BackBufferHeight, APPLIED_VSYNC ? "On" : "Off", was, pp->PresentationInterval, se, pp->SwapEffect, bc, pp->Flags); }
}
static int vt_set(void **slot, void *fn, void **orig)
{
    DWORD o;
    if (*slot == fn) return 1;
    if (!VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &o)) return 0;
    if (!*orig) *orig = *slot;
    *slot = fn;
    VirtualProtect(slot, 4, o, &o);
    return 1;
}
static HRESULT WINAPI MyReset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp)
{
    fix_pp(pp, "device reset");
    return g_rs(d, pp);
}
static HRESULT WINAPI MyCreateDevice(IDirect3D9 *o, UINT a, D3DDEVTYPE t, HWND w, DWORD f, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr;
    fix_pp(pp, "device made");
    hr = g_cd(o, a, t, w, f, pp, out);
    if (SUCCEEDED(hr) && out && *out) vt_set(&(*(void ***)*out)[16], (void *)MyReset, (void **)&g_rs);
    return hr;
}
/* 1.0.2: no hook on Direct3DCreate9 any more (other mods hook it too - crash reports with 2K14Hook). A Direct3D 9
 * object of our own, made on our own thread (not inside the loader), gives the shared IDirect3D9 vtable: its
 * CreateDevice slot is ours, so every device the game makes gets the fixes, whatever else wraps Direct3D. */
static DWORD WINAPI d3d_setup(LPVOID u)
{
    HMODULE m; C9_t c9; IDirect3D9 *d; int ok = 0;
    (void)u;
    m = LoadLibraryA("d3d9.dll");
    c9 = m ? (C9_t)GetProcAddress(m, "Direct3DCreate9") : 0;
    if (c9 && (d = c9(D3D_SDK_VERSION))) {
        ok = vt_set(&(*(void ***)d)[16], (void *)MyCreateDevice, (void **)&g_cd);
        IDirect3D9_Release(d);
    }
    rlog("VSync / KeepFrame fix: %s", ok ? "ok" : "FAILED (Direct3D 9 not available)");
    return 0;
}
static void hook_d3d(void) { HANDLE t = CreateThread(0, 0, d3d_setup, 0, 0, 0); if (t) CloseHandle(t); }

/* ---------------- the menu row ---------------- */
#define FMT_FN      0x00DC09D0u
#define STR_HANDLER 0x00689720u
#define STR_FMT_KEY 0x159D2468u      /* "{0:STRING}" */
typedef void (__fastcall *FMT_t)(void *sink, u32 key, void *args);
typedef int (__cdecl *Toggle_t)(void);
static const char *const kName[3] = { "Windowed", "Fullscreen", "Borderless" };
static int cur(void) { return !GAME_WINDOWED ? 1 : g_pend ? 2 : 0; }
static volatile int g_userSet;   /* 1.0.0b: the ini is written only when YOU change Window Mode in the menu */
static void save(void) { if (!g_userSet) return; WritePrivateProfileStringA("WindowMode", "Mode", !APPLIED_WINDOWED ? "Fullscreen" : g_bl ? "Borderless" : "Windowed", g_ini); WritePrivateProfileStringA("WindowMode", "ModeSetBy", "menu", g_ini); }
static void __fastcall MyText(void *sink, u32 unused)
{
    static u16 w[24]; struct { const u16 *w; const char *a; } sv; void *vals[1];
    struct { u32 zero, count; void **values; u32 handler; } args; const char *t = kName[cur()]; int i;
    (void)unused;
    for (i = 0; t[i] && i < 22; i++) w[i] = (u8)t[i];
    w[i] = 0;
    sv.w = w; sv.a = 0; vals[0] = &sv;
    args.zero = 0; args.count = 1; args.values = vals; args.handler = STR_HANDLER;
    ((FMT_t)FMT_FN)(sink, STR_FMT_KEY, &args);
}
static void go(int to)
{
    int from = cur();
    if (to == from) return;
    rlog("menu: %s -> %s (applied when the game asks to apply)", kName[from], kName[to]);
    g_pend = to == 2; g_userSet = 1;
    if ((to == 1) != (from == 1)) ((Toggle_t)0x005376A0u)();   /* the game's own toggle (windowed <-> fullscreen) */
}
/* the game's Video Settings flow (back from the menu, 0053A760): 00536E00 "changed?" -> "Video settings have been
 * changed. Apply?" -> Yes: 0053A650 applies. 00536D00 loads the menu's working copy from the applied settings. */
typedef int (__cdecl *Fn0_t)(void);
static int __cdecl MyChanged(void)
{
    int r = ((Fn0_t)0x00536E00u)();
    if (!r && GAME_WINDOWED && g_pend != g_bl) { rlog("menu: only Windowed <-> Borderless changed - asking"); r = 1; }
    return r;
}
static int __cdecl MyApply(void)
{
    int r, was = g_bl;
    g_bl = g_pend && GAME_WINDOWED;
    r = ((Fn0_t)0x0053A650u)();
    rlog("applied: %s", !GAME_WINDOWED ? "Fullscreen" : g_bl ? "Borderless" : "Windowed");
    if (GAME_WINDOWED && APPLIED_WINDOWED && was != g_bl) apply_now();   /* not when going to Fullscreen (the game restyles) */
    save();
    return r;
}
static int __cdecl MyLoadWorking(void)
{
    int r = ((Fn0_t)0x00536D00u)();
    g_pend = g_bl;
    return r;
}
static int __cdecl MyNext(void) { go((cur() + 1) % 3); return 1; }
static int __cdecl MyPrev(void) { go((cur() + 2) % 3); return 1; }

/* the value objects are filled by the game's start-up code (not in the exe file): wait for them */
static DWORD WINAPI patch_rows(LPVOID u)
{
    static const u32 objs[2] = { 0x01619F40, 0x01619B38 };
    int i, n = 0, done[2] = { 0, 0 }; DWORD t0 = GetTickCount();
    (void)u;
    while (n < 2 && GetTickCount() - t0 < 120000) {
        for (i = 0; i < 2; i++) {
            u32 *o = (u32 *)objs[i], f[3];
            if (done[i] || o[1] != 0x005376D0u || o[2] != 0x005376A0u || o[3] != 0x005376A0u) continue;
            f[0] = (u32)&MyText; f[1] = (u32)&MyNext; f[2] = (u32)&MyPrev;
            done[i] = 1; n++;
            rlog("menu value %08X: %s (%u ms)", objs[i], g_slam->patch(g_slamMe, o + 1, f, 12) == SLAM_OK ? "Windowed / Fullscreen / Borderless" : "patch FAILED", GetTickCount() - t0);
        }
        Sleep(n < 2 ? 5 : 0);
    }
    if (n < 2) rlog("Video Settings > Window Mode: only %d/2 rows found in 120 s", n);
    for (;;) {   /* the game sets them up again (e.g. new profile loaded): ours again */
        Sleep(500);
        for (i = 0; i < 2; i++) {
            u32 *o = (u32 *)objs[i]; DWORD old;
            if (o[1] == 0x005376D0u && o[2] == 0x005376A0u && VirtualProtect(o + 1, 12, PAGE_READWRITE, &old)) {
                o[1] = (u32)&MyText; o[2] = (u32)&MyNext; o[3] = (u32)&MyPrev;
                VirtualProtect(o + 1, 12, old, &old);
                rlog("menu value %08X set up again by the game - ours again", objs[i]);
            }
        }
    }
    return 0;
}
static void SLAM_CALL game_ready(void *u)
{
    u8 *b = (u8 *)GetModuleHandleA(0); IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    (void)u;
    if (nt->FileHeader.TimeDateStamp != 0x52202C7Fu) { rlog("not the known NBA 2K14 exe - menu row unchanged"); return; }
    CreateThread(0, 0, patch_rows, 0, 0, 0);
    {   static const struct { u32 at; u8 op; u32 to; void *fn; } cs[4] = {
            { 0x0053A779, 0xE8, 0x00536E00, (void *)MyChanged }, { 0x0053A7D6, 0xE8, 0x0053A650, (void *)MyApply },
            { 0x0053A8A6, 0xE8, 0x00536D00, (void *)MyLoadWorking }, { 0x0053A647, 0xE9, 0x00536D00, (void *)MyLoadWorking } };
        int i, n = 0;
        for (i = 0; i < 4; i++) {
            u8 *c = (u8 *)cs[i].at, b5[5]; int rel;
            if (c[0] != cs[i].op || cs[i].at + 5 + *(int *)(c + 1) != cs[i].to) { rlog("video settings site %08X: unexpected", cs[i].at); continue; }
            rel = (int)((u32)cs[i].fn - (cs[i].at + 5)); b5[0] = cs[i].op; *(int *)(b5 + 1) = rel;
            if (g_slam->patch(g_slamMe, c, b5, 5) == SLAM_OK) n++;
        }
        rlog("video settings confirm / apply: %d/4 sites", n);
    }
}

static void *hook_imp(const char *dll, const char *fn, void *repl)
{
    void *next = 0; int r = g_slam->hook_import(g_slamMe, dll, fn, repl, &next);
    rlog("import hook %s: %s", fn, r == SLAM_OK ? "ok" : "FAILED");
    return r == SLAM_OK ? next : 0;
}

int mod_start(void)
{
    char m[32];
    InitializeCriticalSection(&g_lk);
    g_tls = TlsAlloc();
    lstrcpynA(g_log, g_slam->mod_dir(g_slamMe), MAX_PATH); lstrcatA(g_log, "Borderless.log"); DeleteFileA(g_log);
    rlog("Borderless " MOD_VERSION);
    g_ini = g_slam->mod_ini(g_slamMe);
    GetPrivateProfileStringA("WindowMode", "Mode", "Borderless", m, sizeof m, g_ini);
    g_bl = g_pend = (m[0] | 0x20) == 'b';
    {   char by[16];   /* 1.0.0c: older builds saved "Windowed" on their own (not chosen in the menu) - ignored */
        GetPrivateProfileStringA("WindowMode", "ModeSetBy", "", by, sizeof by, g_ini);
        if (!g_bl && (by[0] | 0x20) != 'm') { rlog("Mode=%s was not chosen in the menu (saved by an older build) - Borderless", m); g_bl = g_pend = 1; lstrcpynA(m, "Borderless", sizeof m); } }
    cfg_vsync = (int)GetPrivateProfileIntA("WindowMode", "VSyncFix", 1, g_ini);
    cfg_copy = (int)GetPrivateProfileIntA("WindowMode", "KeepFrame", 1, g_ini);
    if (cfg_vsync) hook_d3d();
    { HANDLE t = CreateThread(0, 0, bl_watch, 0, 0, 0); if (t) CloseHandle(t); }
    rlog("saved choice: %s (Borderless acts while the game's Window Mode is windowed)", g_bl ? "Borderless" : m);
    g_swl = (SWL_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetWindowLongA");
    g_swp = (SWP_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetWindowPos");
    g_cwe = (CWE_t)GetProcAddress(GetModuleHandleA("user32.dll"), "CreateWindowExA");
    { void *p;
      if ((p = hook_imp("USER32.dll", "SetWindowLongA", (void *)MySWL))) g_swl = (SWL_t)p;
      if ((p = hook_imp("USER32.dll", "SetWindowPos", (void *)MySWP))) g_swp = (SWP_t)p;
      if ((p = hook_imp("USER32.dll", "CreateWindowExA", (void *)MyCWE))) g_cwe = (CWE_t)p; }
    if (!SLAM_HAS(g_slam, on_game_ready) || g_slam->on_game_ready(g_slamMe, game_ready, 0) != SLAM_OK) game_ready(0);
    return 0;
}
#ifdef MOD_HAS_STOP
void mod_stop(void) { }
#endif
