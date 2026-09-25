#pragma once
// The session log (docs/architecture/logging-tiers.md, "Log file policy" and "Writer thread").
// File: <game dir>\x3m.log with the previous one kept as x3m.prev.log, x3m-<pid>.log when the
// rename fails (another instance or an editor holding the file), X3M_LOG_FILE=<path> opened
// exactly, %LOCALAPPDATA%\x3-modern-renderer\x3m.log when the game directory is not writable.
// Rows: x3m::log() formats one row into stack scratch and appends it to a 4 MiB in-memory
// buffer (two 2 MiB halves) under a dedicated lock; one writer thread drains it with WriteFile
// every 200 ms or when a half fills. No file I/O on a game thread after log_open; a full
// buffer drops rows and counts them (one log_dropped row when space returns).
// Documented Win32 calls only (CreateFileW, MoveFileExW, CreateThread, WriteFile, events,
// GetFinalPathNameByHandleW, AddVectoredExceptionHandler); nothing Wine-specific.
#include <windows.h>
#include <cstdarg>
#include <string>

namespace x3m::session_log {
struct Opened {
    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring captures;          // capture directory (dumps, readbacks), no trailing separator
    std::string path;               // the log file, UTF-8, redacted (resolved by GetFinalPathNameByHandleW when available)
    const char* source = "game";    // game | localappdata | override
    const char* previous = "absent"; // renamed | absent | busy | none (override: no rotation)
    const char* captures_source = "game"; // game | localappdata
    bool override_failed = false;   // X3M_LOG_FILE was set but could not be opened
    unsigned stale_removed = 0;     // x3m-<pid>.log files older than x3m.prev.log deleted at open
    char session[40] = {};          // YYYYMMDD-HHMMSS-<pid>
};
// Opens the session log per the policy above and makes it the target of log() (rows buffer
// until start_writer); writes nothing itself.
Opened open(HMODULE module);
// The writer thread (after telemetry::initialize: `timing` = telemetry on, which adds the
// per-row QPC cost and the 10-second log_writer rows). Holds a module reference, so a dynamic
// FreeLibrary cannot unmap code it runs.
void start_writer(bool timing) noexcept;
// x3m::log(): format (call_preserved) into stack scratch, append; never blocks on I/O.
void vlog(const char* format, va_list args) noexcept;
// Wakes the writer (no I/O on the caller).
void request_drain() noexcept;
// The last device went: the writer drains, ends and drops its module reference (waiting at most 1 s), so the
// application's FreeLibrary can unload the proxy. Rows logged meanwhile stay buffered; start_writer (the next
// device) or detach writes them.
void park_writer(const char* reason) noexcept;
// A path as UTF-8 with the profile prefix redacted (redact_path): the always-tier path rows.
std::string redact_wide_path(const wchar_t* path);
// Telemetry on: one log_writer row now (the window since the last one), formatted on the caller; no I/O.
void report(const char* reason) noexcept;
// Wakes the writer and waits (at most max_ms) until the buffer is written: for a caller about to end the
// process without DllMain (the seam's TerminateProcess); never used on a game thread in production.
void drain_now(DWORD max_ms) noexcept;
// The log's OS handle (INVALID_HANDLE_VALUE before open): exception-context and teardown rows.
HANDLE handle() noexcept;
// Always-tier privacy (logging-tiers.md section 6): a %USERPROFILE% prefix becomes "%USERPROFILE%",
// a <drive>:\Users\<name> or <drive>:\home\<name> prefix keeps the drive and root with the name as "~".
std::string redact_path(const std::string& utf8);
// proxy_environment values: every occurrence of the host home directory (HOME when it is a Unix path,
// else WINE_HOST_HOME, in both separator forms) and of %USERPROFILE% becomes "~" / "%USERPROFILE%".
std::string redact_value(const std::string& utf8);
// Session counters for session_end and the exception row: one interlocked add each.
void note_frame() noexcept;
void note_device() noexcept;
void note_reset() noexcept;
// After open: the crash filter (SetUnhandledExceptionFilter, chained to the one it replaces) writes one
// `exception` row for the first exception nobody handled.
void arm_exception_witness(HMODULE proxy) noexcept;
// Names a module for the exception row's module= field (the loader's backend).
void name_module(HMODULE module, const char* name) noexcept;
// DllMain DLL_PROCESS_DETACH, first: log() stops waiting for the buffer lock (try once, else the row is dropped).
void closing() noexcept;
// DllMain DLL_PROCESS_DETACH, last: the handler goes; the writer is signalled and waited for at most
// 1 s; at process exit the rows still buffered are written and one session_end row follows,
// through the OS handle.
void detach(bool process_exit) noexcept;
// Rows dropped on a full buffer since open (the motion-output seam's log benchmark reads it).
unsigned dropped_total() noexcept;
}
