/* tools/shot/shot.c -- Shot, a screenshot tool for Windows that works like the Mac's (founder, 2026-11-14: "can we
 * build a screenshot tool i want to stop having to open snipping tool ... it should work like the mac screenshot
 * utility"). A tray app: no window until you press a shortcut.
 *
 *   Ctrl+Shift+3        the screen under the mouse, saved
 *   Ctrl+Shift+4        drag a box; Space switches to picking a window (click one); Esc cancels
 *   Ctrl+Shift+5        the toolbar: Entire Screen / Window / Selection, Options (save to, timer, thumbnail,
 *                       remember the last selection), Capture
 *   ...+Alt             Ctrl+Shift+Alt+3 / +4 copy to the clipboard instead of saving (the Mac uses Ctrl)
 *
 *   shot.exe --screen [FILE.png]   no tray: the screen under the mouse, saved (to FILE, or the usual folder and
 *                                  name), then exit; exit code 0 on success. For scripts.
 *
 * After a save, a floating thumbnail sits in the corner for a few seconds: click it to open the picture, right-click
 * for Copy / Show in Folder / Delete. The tray icon has the same captures, the options, the folder, Start with
 * Windows and Quit. Settings live in %APPDATA%\Shot\settings.ini. Files are named like the Mac's: "Screenshot
 * 2026-11-14 at 09.31.05.png", on the Desktop by default.
 *
 * The PNG is written by PARENA (stdlib/image/png_encode.prn + stdlib/compress/deflate.prn, compiled to
 * shot_png_gen.c by `make shot-gen`; never hand-edit that file), straight from the screen's own BGRA pixels, on a
 * worker thread so the thumbnail appears at once. Everything else here is plain Win32 (user32/gdi32/shell32/dwmapi),
 * per-monitor DPI aware, so a capture is the screen's real pixels on every monitor.
 *
 * Not done yet (named, not hidden): dragging the thumbnail into another app, editing/markup, the shutter sound,
 * capturing a window that is covered by another (a window shot is what was on screen), and capturing every display
 * at once (Ctrl+Shift+3 takes the display under the mouse).
 *
 * Build (MSYS2 MINGW64/UCRT64):  make shot-exe      or see that target's command line. */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef PARENA_NO_GRAPHICS
#define PARENA_NO_GRAPHICS
#endif
#include "parena_runtime.h"
#include "shot_png_gen.c"

/* ---- settings -------------------------------------------------------------------------------------------------- */
enum { SAVE_DESKTOP = 0, SAVE_DOCUMENTS, SAVE_PICTURES, SAVE_CLIPBOARD, SAVE_CUSTOM };
enum { MODE_SCREEN = 0, MODE_WINDOW, MODE_SELECTION };

typedef struct {
    int save_to, timer, thumbnail, remember, mode;
    wchar_t custom[MAX_PATH];
    RECT last;                      /* the last selection, screen coordinates (remember) */
} Settings;

static Settings g_set = { SAVE_DESKTOP, 0, 1, 1, MODE_SELECTION, L"", { 0, 0, 0, 0 } };
static wchar_t g_ini[MAX_PATH];

static void settings_path(void) {
    PWSTR appdata = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdata))) {
        swprintf(g_ini, MAX_PATH, L"%ls\\Shot", appdata);
        CreateDirectoryW(g_ini, NULL);
        swprintf(g_ini, MAX_PATH, L"%ls\\Shot\\settings.ini", appdata);
        CoTaskMemFree(appdata);
    } else {
        wcscpy(g_ini, L"shot-settings.ini");
    }
}

static void settings_load(void) {
    g_set.save_to = GetPrivateProfileIntW(L"shot", L"save_to", SAVE_DESKTOP, g_ini);
    g_set.timer = GetPrivateProfileIntW(L"shot", L"timer", 0, g_ini);
    g_set.thumbnail = GetPrivateProfileIntW(L"shot", L"thumbnail", 1, g_ini);
    g_set.remember = GetPrivateProfileIntW(L"shot", L"remember", 1, g_ini);
    g_set.mode = GetPrivateProfileIntW(L"shot", L"mode", MODE_SELECTION, g_ini);
    GetPrivateProfileStringW(L"shot", L"custom", L"", g_set.custom, MAX_PATH, g_ini);
    g_set.last.left = GetPrivateProfileIntW(L"shot", L"last_x", 0, g_ini);
    g_set.last.top = GetPrivateProfileIntW(L"shot", L"last_y", 0, g_ini);
    g_set.last.right = g_set.last.left + GetPrivateProfileIntW(L"shot", L"last_w", 0, g_ini);
    g_set.last.bottom = g_set.last.top + GetPrivateProfileIntW(L"shot", L"last_h", 0, g_ini);
    if (g_set.save_to < 0 || g_set.save_to > SAVE_CUSTOM) g_set.save_to = SAVE_DESKTOP;
    if (g_set.save_to == SAVE_CUSTOM && !g_set.custom[0]) g_set.save_to = SAVE_DESKTOP;
    if (g_set.timer != 0 && g_set.timer != 5 && g_set.timer != 10) g_set.timer = 0;
    if (g_set.mode < 0 || g_set.mode > MODE_SELECTION) g_set.mode = MODE_SELECTION;
}

static void put_int(const wchar_t *key, int v) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", v);
    WritePrivateProfileStringW(L"shot", key, buf, g_ini);
}

static void settings_save(void) {
    put_int(L"save_to", g_set.save_to);
    put_int(L"timer", g_set.timer);
    put_int(L"thumbnail", g_set.thumbnail);
    put_int(L"remember", g_set.remember);
    put_int(L"mode", g_set.mode);
    WritePrivateProfileStringW(L"shot", L"custom", g_set.custom, g_ini);
    put_int(L"last_x", g_set.last.left);
    put_int(L"last_y", g_set.last.top);
    put_int(L"last_w", g_set.last.right - g_set.last.left);
    put_int(L"last_h", g_set.last.bottom - g_set.last.top);
}

/* ---- globals ---------------------------------------------------------------------------------------------------- */
#define WM_TRAY      (WM_APP + 1)
#define WM_SAVED     (WM_APP + 2)
#define HK_SCREEN    1
#define HK_SELECT    2
#define HK_TOOLBAR   3
#define HK_SCREEN_CB 4
#define HK_SELECT_CB 5
#define TRAY_ID      1

static HINSTANCE g_inst;
static HWND g_main, g_overlay, g_toolbar, g_thumb, g_count;
static NOTIFYICONDATAW g_nid;

/* ---- pixels ----------------------------------------------------------------------------------------------------- */
typedef struct { int w, h; unsigned char *bgra; } Pixels;   /* top-down rows, 4 bytes per pixel, B G R A */

static void pixels_free(Pixels *p) { free(p->bgra); p->bgra = NULL; p->w = p->h = 0; }

/* A rectangle of the live screen (virtual-screen coordinates). */
static Pixels grab(RECT r) {
    Pixels p = { r.right - r.left, r.bottom - r.top, NULL };
    if (p.w <= 0 || p.h <= 0) { p.w = p.h = 0; return p; }
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = p.w; bi.bmiHeader.biHeight = -p.h;      /* negative: top-down rows */
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, p.w, p.h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    p.bgra = (unsigned char *)malloc((size_t)p.w * p.h * 4);
    if (p.bgra) {
        memcpy(p.bgra, bits, (size_t)p.w * p.h * 4);
        for (size_t i = 3; i < (size_t)p.w * p.h * 4; i += 4) p.bgra[i] = 255;
    }
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(NULL, screen);
    return p;
}

/* A piece of an earlier grab: `r` in the same coordinates the grab used, `origin` its top-left. */
static Pixels crop(const Pixels *src, RECT r, POINT origin) {
    Pixels p = { 0, 0, NULL };
    RECT inside = { origin.x, origin.y, origin.x + src->w, origin.y + src->h };
    if (!IntersectRect(&r, &r, &inside)) return p;
    p.w = r.right - r.left; p.h = r.bottom - r.top;
    p.bgra = (unsigned char *)malloc((size_t)p.w * p.h * 4);
    if (!p.bgra) { p.w = p.h = 0; return p; }
    for (int y = 0; y < p.h; y++)
        memcpy(p.bgra + (size_t)y * p.w * 4,
               src->bgra + ((size_t)(r.top - origin.y + y) * src->w + (r.left - origin.x)) * 4, (size_t)p.w * 4);
    return p;
}

static HBITMAP to_bitmap(const Pixels *p, int dim) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = p->w; bi.bmiHeader.biHeight = -p->h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) return NULL;
    unsigned char *d = (unsigned char *)bits;
    size_t n = (size_t)p->w * p->h * 4;
    if (!dim) memcpy(d, p->bgra, n);
    else for (size_t i = 0; i < n; i++) d[i] = (unsigned char)(p->bgra[i] * 5 / 10);   /* the dimmed backdrop */
    return bmp;
}

static RECT virtual_screen(void) {
    RECT r;
    r.left = GetSystemMetrics(SM_XVIRTUALSCREEN); r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN); r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

static RECT monitor_under_mouse(void) {
    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi; mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcMonitor;
}

/* ---- saving and the clipboard ---------------------------------------------------------------------------------- */
static void folder_for(int save_to, wchar_t *out) {
    const KNOWNFOLDERID *id = save_to == SAVE_DOCUMENTS ? &FOLDERID_Documents :
                              save_to == SAVE_PICTURES ? &FOLDERID_Pictures : &FOLDERID_Desktop;
    PWSTR path = NULL;
    out[0] = 0;
    if (save_to == SAVE_CUSTOM && g_set.custom[0]) { wcsncpy(out, g_set.custom, MAX_PATH - 1); out[MAX_PATH - 1] = 0; return; }
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &path))) {
        if (save_to == SAVE_PICTURES) { swprintf(out, MAX_PATH, L"%ls\\Screenshots", path); CreateDirectoryW(out, NULL); }
        else wcsncpy(out, path, MAX_PATH - 1);
        CoTaskMemFree(path);
    }
}

/* "Screenshot 2026-11-14 at 09.31.05.png", then " (2)" and so on if that exists. */
static void next_name(const wchar_t *folder, wchar_t *out) {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t base[MAX_PATH];
    swprintf(base, MAX_PATH, L"%ls\\Screenshot %04d-%02d-%02d at %02d.%02d.%02d", folder,
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    swprintf(out, MAX_PATH, L"%ls.png", base);
    for (int n = 2; GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES && n < 1000; n++)
        swprintf(out, MAX_PATH, L"%ls (%d).png", base, n);
}

/* The PNG, by PARENA. Returns malloc'd bytes (caller frees) and their length. */
static unsigned char *encode_png(const Pixels *p, int *len) {
    Arena a; arena_init(&a);
    Bytes px; px.data = p->bgra; px.len = p->w * p->h * 4;
    Bytes png = png_encode(px, p->w, p->h, 4, 1, 0, &a);
    unsigned char *out = (unsigned char *)malloc((size_t)png.len);
    if (out) memcpy(out, png.data, (size_t)png.len);
    *len = out ? png.len : 0;
    arena_free_all(&a);
    return out;
}

/* Both a DIB (every app) and a PNG (browsers, Discord, Office keep it lossless) on the clipboard. */
static int to_clipboard(const Pixels *p) {
    if (!OpenClipboard(g_main)) return 0;
    EmptyClipboard();
    size_t n = (size_t)p->w * p->h * 4;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + n);
    if (dib) {
        BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)GlobalLock(dib);
        memset(h, 0, sizeof *h);
        h->biSize = sizeof *h; h->biWidth = p->w; h->biHeight = p->h;    /* positive: bottom-up, as CF_DIB expects */
        h->biPlanes = 1; h->biBitCount = 32; h->biCompression = BI_RGB; h->biSizeImage = (DWORD)n;
        unsigned char *d = (unsigned char *)(h + 1);
        for (int y = 0; y < p->h; y++) memcpy(d + (size_t)y * p->w * 4, p->bgra + (size_t)(p->h - 1 - y) * p->w * 4, (size_t)p->w * 4);
        GlobalUnlock(dib);
        if (!SetClipboardData(CF_DIB, dib)) GlobalFree(dib);
    }
    int len = 0;
    unsigned char *png = encode_png(p, &len);
    if (png) {
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (size_t)len);
        if (g) {
            memcpy(GlobalLock(g), png, (size_t)len);
            GlobalUnlock(g);
            if (!SetClipboardData(RegisterClipboardFormatW(L"PNG"), g)) GlobalFree(g);
        }
        free(png);
    }
    CloseClipboard();
    return 1;
}

/* ---- the floating thumbnail ------------------------------------------------------------------------------------ */
typedef struct {
    Pixels pix;                 /* the shot itself (for Copy and the picture) */
    HBITMAP bmp;
    wchar_t path[MAX_PATH];
    volatile LONG saved;        /* 1 once the worker has written the file, -1 if it failed */
} Thumb;
static Thumb *g_thumb_data;

typedef struct { Pixels pix; wchar_t path[MAX_PATH]; HWND notify; } SaveJob;

static DWORD WINAPI save_worker(LPVOID arg) {
    SaveJob *job = (SaveJob *)arg;
    int len = 0, ok = 0;
    unsigned char *png = encode_png(&job->pix, &len);
    if (png) {
        HANDLE f = CreateFileW(job->path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            ok = WriteFile(f, png, (DWORD)len, &wrote, NULL) && wrote == (DWORD)len;
            CloseHandle(f);
            if (!ok) DeleteFileW(job->path);
        }
        free(png);
    }
    PostMessageW(job->notify, WM_SAVED, (WPARAM)ok, (LPARAM)job);
    return 0;
}

static void thumb_close(void) {
    if (g_thumb) { DestroyWindow(g_thumb); g_thumb = NULL; }
}

static void show_in_folder(const wchar_t *path) {
    wchar_t args[MAX_PATH + 16];
    swprintf(args, MAX_PATH + 16, L"/select,\"%ls\"", path);
    ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
}

static void tray_balloon(const wchar_t *title, const wchar_t *text);

static LRESULT CALLBACK thumb_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    Thumb *t = g_thumb_data;
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps);
        RECT rc; GetClientRect(w, &rc);
        if (t && t->bmp) {
            HDC mem = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mem, t->bmp);
            SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, 3, 3, rc.right - 6, rc.bottom - 6, mem, 0, 0, t->pix.w, t->pix.h, SRCCOPY);
            SelectObject(mem, old); DeleteDC(mem);
        }
        HBRUSH dark = CreateSolidBrush(RGB(90, 90, 90)), frame = CreateSolidBrush(RGB(255, 255, 255));
        RECT in = rc;
        FrameRect(dc, &in, dark); InflateRect(&in, -1, -1);          /* a dark edge, so it shows on white too */
        for (int i = 0; i < 2; i++) { FrameRect(dc, &in, frame); InflateRect(&in, -1, -1); }
        DeleteObject(dark); DeleteObject(frame);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:                           /* while the mouse is on it, it stays */
        SetTimer(w, 1, 5000, NULL);
        return 0;
    case WM_TIMER:
        thumb_close();
        return 0;
    case WM_LBUTTONUP:
        if (t && t->saved == 1) ShellExecuteW(NULL, L"open", t->path, NULL, NULL, SW_SHOWNORMAL);
        thumb_close();
        return 0;
    case WM_RBUTTONUP: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (t && t->saved == 1 ? 0 : MF_GRAYED), 1, L"Open");
        AppendMenuW(menu, MF_STRING, 2, L"Copy");
        AppendMenuW(menu, MF_STRING | (t && t->saved == 1 ? 0 : MF_GRAYED), 3, L"Show in Folder");
        AppendMenuW(menu, MF_STRING | (t && t->saved == 1 ? 0 : MF_GRAYED), 4, L"Delete");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, 5, L"Close");
        POINT pt; GetCursorPos(&pt);
        KillTimer(w, 1);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, w, NULL);
        DestroyMenu(menu);
        if (!t) return 0;
        if (cmd == 1) ShellExecuteW(NULL, L"open", t->path, NULL, NULL, SW_SHOWNORMAL);
        else if (cmd == 2) to_clipboard(&t->pix);
        else if (cmd == 3) show_in_folder(t->path);
        else if (cmd == 4) DeleteFileW(t->path);
        if (cmd == 0 || cmd == 2) { SetTimer(w, 1, 3000, NULL); return 0; }
        thumb_close();
        return 0;
    }
    case WM_DESTROY:
        if (t) { if (t->bmp) DeleteObject(t->bmp); pixels_free(&t->pix); free(t); g_thumb_data = NULL; }
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

/* Show `pix` (taken over) in the corner of the monitor it came from, for `path`. */
static void thumb_show(Pixels pix, const wchar_t *path, RECT where) {
    thumb_close();
    Thumb *t = (Thumb *)calloc(1, sizeof *t);
    if (!t) { pixels_free(&pix); return; }
    t->pix = pix;
    t->bmp = to_bitmap(&pix, 0);
    wcsncpy(t->path, path, MAX_PATH - 1);
    g_thumb_data = t;
    MONITORINFO mi; mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromRect(&where, MONITOR_DEFAULTTONEAREST), &mi);
    int maxw = 260, maxh = 170, tw = maxw, th = pix.h * maxw / (pix.w ? pix.w : 1);
    if (th > maxh) { th = maxh; tw = pix.w * maxh / (pix.h ? pix.h : 1); }
    if (tw < 40) tw = 40;
    if (th < 30) th = 30;
    g_thumb = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"ShotThumb", L"", WS_POPUP,
                              mi.rcWork.right - tw - 24, mi.rcWork.bottom - th - 24, tw, th, NULL, NULL, g_inst, NULL);
    ShowWindow(g_thumb, SW_SHOWNOACTIVATE);
    SetTimer(g_thumb, 1, 5000, NULL);
}

/* ---- delivering a shot: the clipboard, or a file (with the thumbnail) ----------------------------------------- */
static void deliver(Pixels pix, int clipboard, RECT where) {
    if (!pix.bgra || pix.w <= 0 || pix.h <= 0) { pixels_free(&pix); return; }
    if (clipboard || g_set.save_to == SAVE_CLIPBOARD) {
        to_clipboard(&pix);
        pixels_free(&pix);
        return;
    }
    wchar_t folder[MAX_PATH];
    folder_for(g_set.save_to, folder);
    if (!folder[0]) { tray_balloon(L"Shot", L"Could not find the folder to save in."); pixels_free(&pix); return; }
    SaveJob *job = (SaveJob *)calloc(1, sizeof *job);
    if (!job) { pixels_free(&pix); return; }
    next_name(folder, job->path);
    job->notify = g_main;
    job->pix.w = pix.w; job->pix.h = pix.h;
    job->pix.bgra = (unsigned char *)malloc((size_t)pix.w * pix.h * 4);
    if (!job->pix.bgra) { free(job); pixels_free(&pix); return; }
    memcpy(job->pix.bgra, pix.bgra, (size_t)pix.w * pix.h * 4);
    if (g_set.thumbnail) thumb_show(pix, job->path, where); else pixels_free(&pix);
    HANDLE th = CreateThread(NULL, 0, save_worker, job, 0, NULL);
    if (th) CloseHandle(th); else { pixels_free(&job->pix); free(job); }
}

/* ---- the selection overlay (Ctrl+Shift+4, and Window/Selection from the toolbar) ------------------------------- */
typedef struct { RECT r; } WinRect;
static struct {
    Pixels frozen;                  /* the whole virtual screen at the moment the shortcut was pressed */
    POINT origin;                   /* its top-left, in screen coordinates */
    HDC bright, dim;
    HBITMAP bright_bmp, dim_bmp;
    int window_mode, clipboard, dragging, has_sel, hover;
    POINT a, b;                     /* the drag, in screen coordinates */
    WinRect wins[512];
    int nwins;
} O;

static BOOL CALLBACK collect_window(HWND w, LPARAM lp) {
    (void)lp;
    if (O.nwins >= 512 || !IsWindowVisible(w) || IsIconic(w) || w == g_overlay || w == g_thumb || w == g_toolbar) return TRUE;
    int cloaked = 0;
    DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    if (cloaked) return TRUE;
    if (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    RECT r;
    if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(w, &r);
    if (r.right - r.left < 8 || r.bottom - r.top < 8) return TRUE;
    O.wins[O.nwins++].r = r;                     /* EnumWindows walks top to bottom: the first hit is the visible one */
    return TRUE;
}

static RECT sel_rect(void) {
    RECT r;
    r.left = min(O.a.x, O.b.x); r.top = min(O.a.y, O.b.y);
    r.right = max(O.a.x, O.b.x) + 1; r.bottom = max(O.a.y, O.b.y) + 1;
    return r;
}

static int window_at(POINT pt) {
    for (int i = 0; i < O.nwins; i++) if (PtInRect(&O.wins[i].r, pt)) return i;
    return -1;
}

static void overlay_close(void) {
    if (g_overlay) { DestroyWindow(g_overlay); g_overlay = NULL; }
    if (O.bright) { DeleteDC(O.bright); DeleteObject(O.bright_bmp); O.bright = NULL; }
    if (O.dim) { DeleteDC(O.dim); DeleteObject(O.dim_bmp); O.dim = NULL; }
    pixels_free(&O.frozen);
}

static void start_timer_then(int mode, RECT r, int clipboard);

/* The overlay's choice is made: take it from the frozen screen (or, with a timer, from the live one later). */
static void overlay_finish(RECT r) {
    int clipboard = O.clipboard;
    if (!O.window_mode && g_set.remember) { g_set.last = r; settings_save(); }
    if (g_set.timer > 0) {
        overlay_close();
        start_timer_then(MODE_SELECTION, r, clipboard);
        return;
    }
    Pixels pix = crop(&O.frozen, r, O.origin);
    overlay_close();
    deliver(pix, clipboard, r);
}

static void draw_label(HDC dc, int x, int y, const wchar_t *text) {
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz; GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    RECT box = { x, y, x + sz.cx + 12, y + sz.cy + 6 };
    HBRUSH bg = CreateSolidBrush(RGB(20, 20, 20));
    FillRect(dc, &box, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, x + 6, y + 3, text, (int)wcslen(text));
    SelectObject(dc, old);
}

static LRESULT CALLBACK overlay_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, O.window_mode ? IDC_HAND : IDC_CROSS));
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps);
        int W = O.frozen.w, H = O.frozen.h;
        HDC back = CreateCompatibleDC(dc);
        HBITMAP bb = CreateCompatibleBitmap(dc, W, H);
        HGDIOBJ old = SelectObject(back, bb);
        BitBlt(back, 0, 0, W, H, O.dim, 0, 0, SRCCOPY);
        RECT hi = { 0, 0, 0, 0 };
        int show = 0;
        if (O.window_mode && O.hover >= 0) { hi = O.wins[O.hover].r; show = 1; }
        else if (!O.window_mode && O.has_sel) { hi = sel_rect(); show = 1; }
        if (show) {
            RECT l = hi; OffsetRect(&l, -O.origin.x, -O.origin.y);
            BitBlt(back, l.left, l.top, l.right - l.left, l.bottom - l.top, O.bright, l.left, l.top, SRCCOPY);
            HBRUSH edge = CreateSolidBrush(O.window_mode ? RGB(80, 160, 255) : RGB(255, 255, 255));
            RECT f = l;
            for (int i = 0; i < (O.window_mode ? 3 : 1); i++) { FrameRect(back, &f, edge); InflateRect(&f, -1, -1); }
            DeleteObject(edge);
            if (!O.window_mode) {
                wchar_t label[64];
                swprintf(label, 64, L"%d \u00d7 %d", (int)(hi.right - hi.left), (int)(hi.bottom - hi.top));
                draw_label(back, l.right + 8, l.bottom + 8 > H - 30 ? l.bottom - 30 : l.bottom + 8, label);
            }
        }
        if (!show) {
            POINT pt; GetCursorPos(&pt);
            draw_label(back, pt.x - O.origin.x + 16, pt.y - O.origin.y + 16,
                       O.window_mode ? L"Click a window  \u00b7  Space: drag a box  \u00b7  Esc: cancel"
                                     : L"Drag a box  \u00b7  Space: pick a window  \u00b7  Esc: cancel");
        }
        BitBlt(dc, 0, 0, W, H, back, 0, 0, SRCCOPY);
        SelectObject(back, old); DeleteObject(bb); DeleteDC(back);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp) + O.origin.x, GET_Y_LPARAM(lp) + O.origin.y };
        if (O.window_mode) {
            int h = window_at(pt);
            if (h != O.hover) { O.hover = h; InvalidateRect(w, NULL, FALSE); }
            else if (h < 0) InvalidateRect(w, NULL, FALSE);
        } else {
            if (O.dragging) { O.b = pt; O.has_sel = 1; }
            InvalidateRect(w, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (!O.window_mode) {
            O.a.x = O.b.x = GET_X_LPARAM(lp) + O.origin.x; O.a.y = O.b.y = GET_Y_LPARAM(lp) + O.origin.y;
            O.dragging = 1; O.has_sel = 0;
            SetCapture(w);
        }
        return 0;
    case WM_LBUTTONUP:
        if (O.window_mode) {
            if (O.hover >= 0) overlay_finish(O.wins[O.hover].r);
        } else if (O.dragging) {
            O.dragging = 0;
            ReleaseCapture();
            RECT r = sel_rect();
            if (r.right - r.left > 2 && r.bottom - r.top > 2) overlay_finish(r);
            else { O.has_sel = 0; InvalidateRect(w, NULL, FALSE); }
        }
        return 0;
    case WM_RBUTTONUP:
        overlay_close();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { overlay_close(); return 0; }
        if (wp == VK_SPACE && !O.dragging) {
            O.window_mode = !O.window_mode; O.has_sel = 0;
            POINT pt; GetCursorPos(&pt); O.hover = O.window_mode ? window_at(pt) : -1;
            InvalidateRect(w, NULL, FALSE);
        }
        if (wp == VK_RETURN && !O.window_mode && g_set.remember && g_set.last.right > g_set.last.left) {
            overlay_finish(g_set.last);                      /* Enter: the last selection again */
        }
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void overlay_open(int window_mode, int clipboard) {
    if (g_overlay) return;
    thumb_close();
    RECT vs = virtual_screen();
    memset(&O, 0, sizeof O);
    O.frozen = grab(vs);
    if (!O.frozen.bgra) return;
    O.origin.x = vs.left; O.origin.y = vs.top;
    O.window_mode = window_mode; O.clipboard = clipboard; O.hover = -1;
    HDC screen = GetDC(NULL);
    O.bright = CreateCompatibleDC(screen); O.bright_bmp = to_bitmap(&O.frozen, 0); SelectObject(O.bright, O.bright_bmp);
    O.dim = CreateCompatibleDC(screen); O.dim_bmp = to_bitmap(&O.frozen, 1); SelectObject(O.dim, O.dim_bmp);
    ReleaseDC(NULL, screen);
    EnumWindows(collect_window, 0);
    g_overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"ShotOverlay", L"Shot", WS_POPUP,
                                vs.left, vs.top, vs.right - vs.left, vs.bottom - vs.top, NULL, NULL, g_inst, NULL);
    ShowWindow(g_overlay, SW_SHOW);
    SetForegroundWindow(g_overlay);
    SetFocus(g_overlay);
    if (window_mode) { POINT pt; GetCursorPos(&pt); O.hover = window_at(pt); }
}

/* ---- the timer: a big countdown, then the capture --------------------------------------------------------------- */
static struct { int left, mode, clipboard; RECT r; } T;

static LRESULT CALLBACK count_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps);
        RECT rc; GetClientRect(w, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(25, 25, 25)); FillRect(dc, &rc, bg); DeleteObject(bg);
        HFONT f = CreateFontW(-(rc.bottom * 2 / 3), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ old = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        wchar_t s[8]; swprintf(s, 8, L"%d", T.left);
        DrawTextW(dc, s, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old); DeleteObject(f);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (--T.left > 0) { InvalidateRect(w, NULL, TRUE); return 0; }
            KillTimer(w, 1);
            ShowWindow(w, SW_HIDE);
            SetTimer(w, 2, 150, NULL);                      /* let the countdown leave the screen before the grab */
            return 0;
        }
        if (wp == 2) {
            KillTimer(w, 2);
            DestroyWindow(w); g_count = NULL;
            RECT r = T.mode == MODE_SCREEN ? monitor_under_mouse() : T.r;
            deliver(grab(r), T.clipboard, r);
            return 0;
        }
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void start_timer_then(int mode, RECT r, int clipboard) {
    if (g_count) DestroyWindow(g_count);
    T.left = g_set.timer; T.mode = mode; T.r = r; T.clipboard = clipboard;
    RECT mon = monitor_under_mouse();
    int s = 140;
    g_count = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, L"ShotCount", L"", WS_POPUP,
                              (mon.left + mon.right - s) / 2, (mon.top + mon.bottom - s) / 2, s, s, NULL, NULL, g_inst, NULL);
    SetLayeredWindowAttributes(g_count, 0, 210, LWA_ALPHA);
    ShowWindow(g_count, SW_SHOWNOACTIVATE);
    SetTimer(g_count, 1, 1000, NULL);
}

static void capture(int mode, int clipboard) {
    if (g_overlay || g_count) return;
    if (mode == MODE_SCREEN) {
        RECT r = monitor_under_mouse();
        if (g_set.timer > 0) { start_timer_then(MODE_SCREEN, r, clipboard); return; }
        thumb_close();
        deliver(grab(r), clipboard, r);
        return;
    }
    overlay_open(mode == MODE_WINDOW, clipboard);
}

/* ---- the options menu (tray and toolbar share it) -------------------------------------------------------------- */
#define CMD_SCREEN 100
#define CMD_WINDOW 101
#define CMD_SELECT 102
#define CMD_TOOLBAR 103
#define CMD_SAVE_DESKTOP 110
#define CMD_SAVE_DOCUMENTS 111
#define CMD_SAVE_PICTURES 112
#define CMD_SAVE_CLIPBOARD 113
#define CMD_SAVE_OTHER 114
#define CMD_TIMER_0 120
#define CMD_TIMER_5 121
#define CMD_TIMER_10 122
#define CMD_THUMB 130
#define CMD_REMEMBER 131
#define CMD_OPEN_FOLDER 140
#define CMD_AUTOSTART 141
#define CMD_QUIT 150

static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static int autostart_on(void) {
    HKEY k; int on = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, L"Shot", NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}

static void autostart_set(int on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH], quoted[MAX_PATH + 4];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        swprintf(quoted, MAX_PATH + 4, L"\"%ls\"", exe);
        RegSetValueExW(k, L"Shot", 0, REG_SZ, (const BYTE *)quoted, (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"Shot");
    }
    RegCloseKey(k);
}

static HMENU options_menu(void) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Save to");
    AppendMenuW(m, MF_STRING | (g_set.save_to == SAVE_DESKTOP ? MF_CHECKED : 0), CMD_SAVE_DESKTOP, L"    Desktop");
    AppendMenuW(m, MF_STRING | (g_set.save_to == SAVE_DOCUMENTS ? MF_CHECKED : 0), CMD_SAVE_DOCUMENTS, L"    Documents");
    AppendMenuW(m, MF_STRING | (g_set.save_to == SAVE_PICTURES ? MF_CHECKED : 0), CMD_SAVE_PICTURES, L"    Pictures\\Screenshots");
    AppendMenuW(m, MF_STRING | (g_set.save_to == SAVE_CLIPBOARD ? MF_CHECKED : 0), CMD_SAVE_CLIPBOARD, L"    Clipboard");
    wchar_t other[MAX_PATH + 32];
    if (g_set.save_to == SAVE_CUSTOM) swprintf(other, MAX_PATH + 32, L"    %ls", g_set.custom);
    else wcscpy(other, L"    Other Location\u2026");
    AppendMenuW(m, MF_STRING | (g_set.save_to == SAVE_CUSTOM ? MF_CHECKED : 0), CMD_SAVE_OTHER, other);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Timer");
    AppendMenuW(m, MF_STRING | (g_set.timer == 0 ? MF_CHECKED : 0), CMD_TIMER_0, L"    None");
    AppendMenuW(m, MF_STRING | (g_set.timer == 5 ? MF_CHECKED : 0), CMD_TIMER_5, L"    5 Seconds");
    AppendMenuW(m, MF_STRING | (g_set.timer == 10 ? MF_CHECKED : 0), CMD_TIMER_10, L"    10 Seconds");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Options");
    AppendMenuW(m, MF_STRING | (g_set.thumbnail ? MF_CHECKED : 0), CMD_THUMB, L"    Show Floating Thumbnail");
    AppendMenuW(m, MF_STRING | (g_set.remember ? MF_CHECKED : 0), CMD_REMEMBER, L"    Remember Last Selection (Enter)");
    return m;
}

static void pick_other_folder(HWND owner) {
    BROWSEINFOW bi; memset(&bi, 0, sizeof bi);
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Save screenshots in";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(id, path)) { wcscpy(g_set.custom, path); g_set.save_to = SAVE_CUSTOM; settings_save(); }
    CoTaskMemFree(id);
}

static int options_command(int cmd, HWND owner) {
    switch (cmd) {
    case CMD_SAVE_DESKTOP: g_set.save_to = SAVE_DESKTOP; break;
    case CMD_SAVE_DOCUMENTS: g_set.save_to = SAVE_DOCUMENTS; break;
    case CMD_SAVE_PICTURES: g_set.save_to = SAVE_PICTURES; break;
    case CMD_SAVE_CLIPBOARD: g_set.save_to = SAVE_CLIPBOARD; break;
    case CMD_SAVE_OTHER: pick_other_folder(owner); return 1;
    case CMD_TIMER_0: g_set.timer = 0; break;
    case CMD_TIMER_5: g_set.timer = 5; break;
    case CMD_TIMER_10: g_set.timer = 10; break;
    case CMD_THUMB: g_set.thumbnail = !g_set.thumbnail; break;
    case CMD_REMEMBER: g_set.remember = !g_set.remember; break;
    default: return 0;
    }
    settings_save();
    return 1;
}

/* ---- the toolbar (Ctrl+Shift+5) --------------------------------------------------------------------------------- */
#define TB_SCREEN 1
#define TB_WINDOW 2
#define TB_SELECT 3
#define TB_OPTIONS 4
#define TB_CAPTURE 5
#define TB_CLOSE 6
static HWND tb_mode[3];

static void toolbar_close(void) { if (g_toolbar) { DestroyWindow(g_toolbar); g_toolbar = NULL; } }

static LRESULT CALLBACK toolbar_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        /* Segoe UI where Windows has it (Vista+), the dialog font otherwise; plain-text labels, so no glyph is missing */
        static HFONT font;
        if (!font) font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        const wchar_t *labels[] = { L"Entire Screen", L"Window", L"Selection" };
        int x = 10;
        for (int i = 0; i < 3; i++) {
            tb_mode[i] = CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_PUSHLIKE | (i == 0 ? WS_GROUP : 0),
                                         x, 10, 104, 30, w, (HMENU)(INT_PTR)(TB_SCREEN + i), g_inst, NULL);
            SendMessageW(tb_mode[i], WM_SETFONT, (WPARAM)font, TRUE);
            x += 108;
        }
        SendMessageW(tb_mode[g_set.mode], BM_SETCHECK, BST_CHECKED, 0);
        const wchar_t *more[] = { L"Options...", L"Capture", L"Cancel" };
        int widths[] = { 96, 90, 64 }, ids[] = { TB_OPTIONS, TB_CAPTURE, TB_CLOSE };
        x += 16;
        for (int i = 0; i < 3; i++) {
            HWND b = CreateWindowExW(0, L"BUTTON", more[i], WS_CHILD | WS_VISIBLE | (ids[i] == TB_CAPTURE ? BS_DEFPUSHBUTTON : 0),
                                     x, 10, widths[i], 30, w, (HMENU)(INT_PTR)ids[i], g_inst, NULL);
            SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);
            x += widths[i] + 4;
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= TB_SCREEN && id <= TB_SELECT) { g_set.mode = id - TB_SCREEN; settings_save(); return 0; }
        if (id == TB_OPTIONS) {
            RECT r; GetWindowRect(GetDlgItem(w, TB_OPTIONS), &r);
            HMENU menu = options_menu();
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_BOTTOMALIGN, r.left, r.top, 0, w, NULL);
            DestroyMenu(menu);
            options_command(cmd, w);
            return 0;
        }
        if (id == TB_CAPTURE || id == IDOK) { int mode = g_set.mode; toolbar_close(); capture(mode, 0); return 0; }
        if (id == TB_CLOSE || id == IDCANCEL) { toolbar_close(); return 0; }
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && (HWND)lp != NULL && GetParent((HWND)lp) != w) toolbar_close();
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void toolbar_open(void) {
    if (g_toolbar) { toolbar_close(); return; }
    thumb_close();
    RECT mon = monitor_under_mouse();
    int W = 10 + 3 * 108 + 16 + 96 + 4 + 90 + 4 + 64 + 10, H = 50;
    g_toolbar = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"ShotToolbar", L"Shot", WS_POPUP | WS_BORDER,
                                (mon.left + mon.right - W) / 2, mon.bottom - H - 90, W, H, NULL, NULL, g_inst, NULL);
    ShowWindow(g_toolbar, SW_SHOW);
    SetForegroundWindow(g_toolbar);
    SetFocus(GetDlgItem(g_toolbar, TB_CAPTURE));
}

/* ---- the tray ---------------------------------------------------------------------------------------------------- */
static void tray_balloon(const wchar_t *title, const wchar_t *text) {
    g_nid.uFlags = NIF_INFO;
    wcsncpy(g_nid.szInfoTitle, title, 63); g_nid.szInfoTitle[63] = 0;
    wcsncpy(g_nid.szInfo, text, 255); g_nid.szInfo[255] = 0;
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_menu(HWND w) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_SCREEN, L"Capture Entire Screen\tCtrl+Shift+3");
    AppendMenuW(m, MF_STRING, CMD_SELECT, L"Capture Selection\tCtrl+Shift+4");
    AppendMenuW(m, MF_STRING, CMD_WINDOW, L"Capture Window\tCtrl+Shift+4, Space");
    AppendMenuW(m, MF_STRING, CMD_TOOLBAR, L"Toolbar\u2026\tCtrl+Shift+5");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    HMENU opts = options_menu();
    AppendMenuW(m, MF_POPUP, (UINT_PTR)opts, L"Options");
    AppendMenuW(m, MF_STRING, CMD_OPEN_FOLDER, L"Open Screenshots Folder");
    AppendMenuW(m, MF_STRING | (autostart_on() ? MF_CHECKED : 0), CMD_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit Shot");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(w);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, w, NULL);
    DestroyMenu(m);                                          /* destroys the Options submenu too */
    if (cmd == CMD_SCREEN) capture(MODE_SCREEN, 0);
    else if (cmd == CMD_SELECT) capture(MODE_SELECTION, 0);
    else if (cmd == CMD_WINDOW) capture(MODE_WINDOW, 0);
    else if (cmd == CMD_TOOLBAR) toolbar_open();
    else if (cmd == CMD_OPEN_FOLDER) {
        wchar_t folder[MAX_PATH];
        folder_for(g_set.save_to == SAVE_CLIPBOARD ? SAVE_DESKTOP : g_set.save_to, folder);
        ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
    } else if (cmd == CMD_AUTOSTART) autostart_set(!autostart_on());
    else if (cmd == CMD_QUIT) DestroyWindow(w);
    else options_command(cmd, w);
}

static void register_hotkeys(HWND w) {
    struct { int id; UINT mods; UINT vk; const wchar_t *name; } keys[] = {
        { HK_SCREEN, MOD_CONTROL | MOD_SHIFT, '3', L"Ctrl+Shift+3" },
        { HK_SELECT, MOD_CONTROL | MOD_SHIFT, '4', L"Ctrl+Shift+4" },
        { HK_TOOLBAR, MOD_CONTROL | MOD_SHIFT, '5', L"Ctrl+Shift+5" },
        { HK_SCREEN_CB, MOD_CONTROL | MOD_SHIFT | MOD_ALT, '3', L"Ctrl+Shift+Alt+3" },
        { HK_SELECT_CB, MOD_CONTROL | MOD_SHIFT | MOD_ALT, '4', L"Ctrl+Shift+Alt+4" },
    };
    wchar_t taken[256] = L"";
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (!RegisterHotKey(w, keys[i].id, keys[i].mods | MOD_NOREPEAT, keys[i].vk)) {
            if (taken[0]) wcscat(taken, L", ");
            wcscat(taken, keys[i].name);
        }
    }
    if (taken[0]) {
        wchar_t msg[300];
        swprintf(msg, 300, L"Another app already uses %ls. The tray menu still works.", taken);
        tray_balloon(L"Shot: shortcut taken", msg);
    } else {
        tray_balloon(L"Shot is running", L"Ctrl+Shift+3 screen \u00b7 4 selection \u00b7 5 toolbar. Add Alt to copy instead of saving.");
    }
}

static LRESULT CALLBACK main_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_HOTKEY:
        if (wp == HK_SCREEN) capture(MODE_SCREEN, 0);
        else if (wp == HK_SELECT) capture(MODE_SELECTION, 0);
        else if (wp == HK_TOOLBAR) toolbar_open();
        else if (wp == HK_SCREEN_CB) capture(MODE_SCREEN, 1);
        else if (wp == HK_SELECT_CB) capture(MODE_SELECTION, 1);
        return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) tray_menu(w);
        else if (LOWORD(lp) == WM_LBUTTONUP) toolbar_open();
        return 0;
    case WM_SAVED: {
        SaveJob *job = (SaveJob *)lp;
        if (g_thumb_data && wcscmp(g_thumb_data->path, job->path) == 0) g_thumb_data->saved = wp ? 1 : -1;
        if (!wp) {
            wchar_t msg[MAX_PATH + 64];
            swprintf(msg, MAX_PATH + 64, L"Could not save %ls", job->path);
            tray_balloon(L"Shot", msg);
        }
        pixels_free(&job->pix);
        free(job);
        return 0;
    }
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void register_class(const wchar_t *name, WNDPROC proc, HBRUSH bg) {
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = proc; wc.hInstance = g_inst; wc.lpszClassName = name;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = bg;
    RegisterClassExW(&wc);
}

/* Per-monitor DPI awareness, so every capture is the real pixels on every monitor (Windows 10 1703+; the older
 * call where that is missing). Looked up at run time so the exe still starts on older Windows. */
static void dpi_aware(void) {
    typedef BOOL (WINAPI *SetCtx)(HANDLE);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetCtx set = u ? (SetCtx)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (set && set((HANDLE)-4)) return;                     /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    SetProcessDPIAware();
}

/* `shot.exe --screen [FILE]`: one capture, written synchronously, no tray. */
static int run_once(const wchar_t *cmd) {
    const wchar_t *arg = wcsstr(cmd, L"--screen") + 8;
    while (*arg == L' ') arg++;
    wchar_t path[MAX_PATH];
    if (*arg) {
        size_t n = 0;
        if (*arg == L'"') { arg++; while (arg[n] && arg[n] != L'"' && n < MAX_PATH - 1) n++; }
        else while (arg[n] && n < MAX_PATH - 1) n++;
        wcsncpy(path, arg, n); path[n] = 0;
    } else {
        wchar_t folder[MAX_PATH];
        folder_for(g_set.save_to == SAVE_CLIPBOARD ? SAVE_DESKTOP : g_set.save_to, folder);
        next_name(folder, path);
    }
    Pixels p = grab(monitor_under_mouse());
    if (!p.bgra) return 1;
    int len = 0;
    unsigned char *png = encode_png(&p, &len);
    pixels_free(&p);
    if (!png) return 1;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote = 0;
    int ok = f != INVALID_HANDLE_VALUE && WriteFile(f, png, (DWORD)len, &wrote, NULL) && wrote == (DWORD)len;
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    free(png);
    return ok ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)show;
    g_inst = inst;
    if (cmd && wcsstr(cmd, L"--screen")) {
        dpi_aware();
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        settings_path();
        settings_load();
        int rc = run_once(cmd);
        CoUninitialize();
        return rc;
    }
    HANDLE one = CreateMutexW(NULL, TRUE, L"Local\\ShotScreenshotTool");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Shot is already running: look for its icon in the tray.", L"Shot", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    dpi_aware();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    settings_path();
    settings_load();
    register_class(L"ShotMain", main_proc, NULL);
    register_class(L"ShotOverlay", overlay_proc, NULL);
    register_class(L"ShotThumb", thumb_proc, NULL);
    register_class(L"ShotCount", count_proc, NULL);
    register_class(L"ShotToolbar", toolbar_proc, (HBRUSH)(COLOR_BTNFACE + 1));
    g_main = CreateWindowExW(0, L"ShotMain", L"Shot", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL);

    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid; g_nid.hWnd = g_main; g_nid.uID = TRAY_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"Shot \u2014 Ctrl+Shift+3 / 4 / 5");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    register_hotkeys(g_main);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_toolbar && IsDialogMessageW(g_toolbar, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    ReleaseMutex(one);
    CloseHandle(one);
    return 0;
}
