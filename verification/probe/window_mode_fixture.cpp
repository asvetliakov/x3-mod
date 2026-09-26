// Wine fixture of --window-monitor-rect (src/proxy/window_mode.cpp with
// window_mode_core.h; docs/architecture/window-mode-and-cursor-fix.md
// section 4). Registers the game's class shape (CS_HREDRAW|CS_VREDRAW, NULL
// class cursor), creates a WS_POPUP|WS_VISIBLE window and places it exactly as
// 0x4dac90 does (monitor-sized at the work-area origin, one SetWindowPos with
// SWP_NOZORDER), then runs the production apply() as create_before would:
// the window must end at the monitor rectangle and stay there (the display
// driver did not constrain it), a second call and a window already at the
// monitor rectangle are untouched, and the refusals (decorated, caption,
// foreign thread, smaller back buffer, fullscreen device, second window,
// option off) leave every rectangle as it was; the thread's last error is kept.
// While the moved window is up the desktop's top 40 rows are read back with
// BitBlt from the screen DC (documented GDI): a soft record of whether rows
// above the work area show the window's fill (menu bar hidden) or not.
// Built by CMake (target window_mode_fixture); run through
// verification/probe/run_window_mode.py under wine_lock.py. Never launches the game.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../../src/proxy/window_mode.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace x3m {
std::string last_line;
unsigned lines = 0;
void log(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    last_line = buffer;
    ++lines;
    std::printf("LOG %s\n", buffer);
    std::fflush(stdout);
}
}
namespace wm = x3m::window_mode;
using wm::core::Rect;

static unsigned checks = 0, failures = 0;
static bool require(const char* label, bool value) {
    ++checks;
    if (!value) ++failures;
    std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL");
    std::fflush(stdout);
    return value;
}
static Rect rect_of(const RECT& r) {
    Rect c;
    c.left = r.left;
    c.top = r.top;
    c.right = r.right;
    c.bottom = r.bottom;
    return c;
}
static Rect window_rect(HWND h) {
    RECT r{};
    GetWindowRect(h, &r);
    return rect_of(r);
}
static void pump(DWORD ms) {
    const DWORD start = GetTickCount();
    do {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(10);
    } while (GetTickCount() - start < ms);
}
static const char* kClass = "X3MWindowModeFixture";
static const COLORREF kFill = RGB(24, 40, 56); // an unusual dark colour: no menu bar pixel equals it
static MONITORINFO monitor_info(HWND h) {
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    return mi;
}
// The game's creation and placement (0x4dae0d, 0x4daf1b..0x4db040): 640x480 at 0,0, then the work-area origin with the
// monitor's size.
static HWND create_game_window(DWORD style, bool at_monitor = false) {
    HWND h = CreateWindowExA(0, kClass, kClass, style, 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleA(nullptr),
                             nullptr);
    if (!h) return nullptr;
    const MONITORINFO mi = monitor_info(h);
    const int w = mi.rcMonitor.right - mi.rcMonitor.left, hgt = mi.rcMonitor.bottom - mi.rcMonitor.top;
    const int x = at_monitor ? mi.rcMonitor.left : mi.rcWork.left, y = at_monitor ? mi.rcMonitor.top : mi.rcWork.top;
    SetWindowPos(h, HWND_TOP, x, y, w, hgt, SWP_NOZORDER);
    return h;
}
struct ForeignWindow {
    HANDLE ready, done;
    HWND hwnd;
};
static DWORD WINAPI foreign_thread(LPVOID p) {
    auto* f = static_cast<ForeignWindow*>(p);
    f->hwnd = create_game_window(WS_POPUP);
    SetEvent(f->ready);
    while (WaitForSingleObject(f->done, 10) == WAIT_TIMEOUT) {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
    }
    DestroyWindow(f->hwnd);
    return 0;
}
static void print_rect(const char* name, const Rect& r) {
    std::printf("%s=%ld,%ld,%ld,%ld", name, long(r.left), long(r.top), long(r.right), long(r.bottom));
}
// Refusal helper: apply must refuse with `reason` and leave the rectangle alone.
static void refused(const char* label, HWND focus, HWND device, bool windowed, UINT bw, UINT bh, const char* reason) {
    HWND target = device ? device : focus;
    const Rect before = window_rect(target);
    const auto r = wm::apply("create_before", focus, device, windowed, bw, bh);
    char text[160];
    std::snprintf(text, sizeof text, "refused_%s", label);
    require(text, r.evaluated && r.decision.action == wm::core::Action::refused &&
                      !std::strcmp(r.decision.reason, reason) && window_rect(target) == before);
}

int main() {
    std::printf("FIXTURE window_mode\n");
    SetEnvironmentVariableW(L"X3M_WINDOW_MONITOR_RECT", L"1");
    SetEnvironmentVariableW(L"X3M_WINDOW_MONITOR_RECT_DEFAULT", L"1");
    const unsigned lines_before = x3m::lines;
    wm::initialize();
    require("enabled_by_variable", wm::enabled());
    require("config_row",
            x3m::lines == lines_before + 1 && x3m::last_line.find("window_mode_config requested=1 default=1") == 0);

    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = nullptr;
    wc.hbrBackground = CreateSolidBrush(kFill);
    wc.lpszClassName = kClass;
    require("register_class", RegisterClassA(&wc) != 0);

    HWND game = create_game_window(WS_POPUP | WS_VISIBLE);
    require("create_window", game != nullptr);
    if (!game) {
        std::printf("RESULT checks=%u failed=%u FAIL\n", checks, failures);
        return 1;
    }
    const MONITORINFO mi = monitor_info(game);
    const Rect monitor = rect_of(mi.rcMonitor), work = rect_of(mi.rcWork);
    const long offset_x = work.left - monitor.left, offset_y = work.top - monitor.top;
    std::printf("GEOMETRY ");
    print_rect("monitor", monitor);
    std::printf(" ");
    print_rect("work", work);
    std::printf(" offset=%ld offset_x=%ld\n", offset_y, offset_x);
    const UINT bw = UINT(monitor.right - monitor.left), bh = UINT(monitor.bottom - monitor.top);
    Rect placement;
    placement.left = work.left;
    placement.top = work.top;
    placement.right = work.left + LONG(bw);
    placement.bottom = work.top + LONG(bh);
    pump(200);
    const Rect placed = window_rect(game);
    std::printf("PLACED ");
    print_rect("rect", placed);
    std::printf("\n");
    require("game_placement_accepted_by_driver", placed == placement);
    const bool menu_bar_offset = !(placement == monitor);

    SetLastError(0x0000e0e0);
    const auto moved = wm::apply("create_before", game, nullptr, true, bw, bh);
    require("last_error_kept", GetLastError() == 0x0000e0e0);
    if (menu_bar_offset) {
        require("moved_to_monitor_rect", moved.evaluated && moved.decision.action == wm::core::Action::move &&
                                             moved.set_result && moved.after == monitor &&
                                             x3m::last_line.find("action=moved ") != std::string::npos);
    } else {
        require("noop_without_offset", moved.evaluated && moved.decision.action == wm::core::Action::noop);
    }
    pump(300);
    require("rect_stays_at_monitor_after_pump", window_rect(game) == monitor);
    RECT client{};
    GetClientRect(game, &client);
    require("client_is_monitor_size", client.right == LONG(bw) && client.bottom == LONG(bh));

    // Soft screen record: rows 0..39 of the monitor through the screen DC while the window is up.
    pump(1200);
    {
        const int rows = 40, width = int(bw);
        HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -rows;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(memory, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = dib ? SelectObject(memory, dib) : nullptr;
        SetLastError(0);
        const BOOL blt = dib && BitBlt(memory, 0, 0, width, rows, screen, monitor.left, monitor.top, SRCCOPY);
        const DWORD blt_error = GetLastError();
        GdiFlush();
        int fill_rows = 0, fill_rows_top = 0, black_rows = 0;
        const DWORD fill = (DWORD(GetRValue(kFill)) << 16) | (DWORD(GetGValue(kFill)) << 8) | GetBValue(kFill);
        if (blt && bits) {
            const auto* pixels = static_cast<const DWORD*>(bits);
            for (int y = 0; y < rows; ++y) {
                int match = 0, dark = 0, samples = 0;
                for (int x = 0; x < width; x += 16) {
                    const DWORD p = pixels[y * width + x] & 0xffffffu;
                    ++samples;
                    match += p == fill;
                    dark += p == 0;
                }
                if (match * 10 >= samples * 9) {
                    ++fill_rows;
                    if (y < offset_y) ++fill_rows_top;
                }
                if (dark * 10 >= samples * 9) ++black_rows;
            }
        }
        std::printf(
            "SCREEN available=%d dib=%d screen_dc=%d error=%lu rows=%d fill_rows=%d fill_rows_top=%d black_rows=%d menu_rows=%ld foreground=%d\n",
            int(blt), int(dib != nullptr), int(screen != nullptr), static_cast<unsigned long>(blt_error), rows,
            fill_rows, fill_rows_top, black_rows, offset_y, int(GetForegroundWindow() == game));
        if (old) SelectObject(memory, old);
        if (dib) DeleteObject(dib);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
    }

    const auto second = wm::apply("reset_before", game, game, true, bw, bh);
    require("second_call_noop", second.decision.action == wm::core::Action::noop && window_rect(game) == monitor);

    HWND at_monitor = create_game_window(WS_POPUP, true);
    const Rect at_before = window_rect(at_monitor);
    const auto untouched = wm::apply("create_before", at_monitor, nullptr, true, bw, bh);
    require("window_at_monitor_untouched", at_before == monitor &&
                                               untouched.decision.action == wm::core::Action::noop &&
                                               window_rect(at_monitor) == monitor);

    HWND spare = create_game_window(WS_POPUP);
    if (menu_bar_offset)
        require("spare_at_work_origin", window_rect(spare) == placement);
    else
        require("spare_at_work_origin", true);
    refused("smaller_backbuffer", spare, nullptr, true, bw / 2, bh, "backbuffer_mismatch");
    refused("fullscreen_device", spare, nullptr, false, bw, bh, "fullscreen");
    refused("second_window", at_monitor, spare, true, bw, bh, "foreign_window");
    HWND decorated = create_game_window(WS_OVERLAPPEDWINDOW);
    refused("decorated_style", decorated, nullptr, true, bw, bh, "not_popup");
    HWND caption = create_game_window(WS_POPUP | WS_CAPTION);
    refused("popup_with_caption", caption, nullptr, true, bw, bh, "decorated");

    ForeignWindow foreign{CreateEventA(nullptr, TRUE, FALSE, nullptr), CreateEventA(nullptr, TRUE, FALSE, nullptr),
                          nullptr};
    HANDLE thread = CreateThread(nullptr, 0, foreign_thread, &foreign, 0, nullptr);
    WaitForSingleObject(foreign.ready, 10000);
    if (foreign.hwnd)
        refused("foreign_thread", foreign.hwnd, nullptr, true, bw, bh, "foreign_thread");
    else
        require("refused_foreign_thread", false);
    SetEvent(foreign.done);
    WaitForSingleObject(thread, 10000);
    CloseHandle(thread);

    SetEnvironmentVariableW(L"X3M_WINDOW_MONITOR_RECT", L"0");
    wm::initialize();
    const unsigned lines_off = x3m::lines;
    const Rect spare_before = window_rect(spare);
    const auto off = wm::apply("create_before", spare, nullptr, true, bw, bh);
    require("option_off_untouched",
            !wm::enabled() && !off.evaluated && x3m::lines == lines_off && window_rect(spare) == spare_before);

    DestroyWindow(caption);
    DestroyWindow(decorated);
    DestroyWindow(spare);
    DestroyWindow(at_monitor);
    DestroyWindow(game);
    std::printf("RESULT checks=%u failed=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
