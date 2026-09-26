#include "session_log.h"
#include "config.h"
#include "capture.h"
#include "cpu_state.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

// x87 audit roots (verification/probe/check_no_x87.py): _x3m_session_log_open,
// _x3m_exception_witness@4, _x3m_session_end, and vlog through x3m::log's existing
// roots. The rows written without the formatter (exception, session_end, log_dropped)
// are assembled by Line; the formatter runs only behind call_preserved.
namespace {
using x3m::session_log::Opened;
constexpr unsigned kHalf = 2u << 20;         // 2 MiB per half, 4 MiB in all; never grown
constexpr unsigned kScratch = 8192;          // one row formatted on the caller's stack
constexpr unsigned kRowCap = 256u << 10;     // a longer row is cut and marked " truncated=1"
constexpr unsigned kChunk = 64u << 10;       // one WriteFile; the detach drain resumes after the last whole chunk
constexpr DWORD kPeriodMs = 200;             // writer drain period
constexpr DWORD kSummaryMs = 10000;          // log_writer row period (telemetry on)
constexpr ULONGLONG kExceptionDrainMs = 500; // the crash row waits this long at most (GetTickCount64) for the writer's
                                             // drain
constexpr DWORD kJoinMs = 1000;              // detach, park and re-arm wait this long at most for the writer

char buffer_[2][kHalf];
SRWLOCK lock_ = SRWLOCK_INIT; // guards active_, used_, handed_, pending_drops_, append_
unsigned active_ = 0;         // the half game threads append to
unsigned used_[2] = {};
bool handed_[2] = {};           // a half given to the writer (full, or taken on the timer)
volatile LONG written_[2] = {}; // bytes of a handed half already written (chunk granular)
// Byte counters (wrap-safe differences): appended_ under lock_, flushed_ by the writer after each chunk. The exception
// row waits until flushed_ reaches the appended_ it read, i.e. for the rows logged before the fault only.
volatile LONG appended_ = 0, flushed_ = 0;
unsigned pending_drops_ = 0; // rows dropped since the last log_dropped row
volatile LONG dropped_total_ = 0;
HANDLE file_ = INVALID_HANDLE_VALUE;
HANDLE wake_ = nullptr, thread_ = nullptr;
HMODULE module_ref_ = nullptr;
DWORD writer_id_ = 0;
volatile LONG stop_ = 0, closing_ = 0;
bool timing_ = false, started_ = false;
std::uint64_t frequency_ = 0;
// Game-side cost with telemetry on: format + append per row, QPC ticks (under lock_).
struct AppendStats {
    std::uint64_t rows = 0, bytes = 0, ticks = 0, max_ticks = 0;
};
AppendStats append_{};
// Writer-side cost (updated by the writer under lock_ after each WriteFile): calls, bytes, ticks, and a
// histogram with the telemetry edges 10 us, 100 us, 1 ms, 10 ms, 100 ms.
struct WriteStats {
    std::uint64_t writes = 0, bytes = 0, ticks = 0, max_ticks = 0, failures = 0, buckets[6] = {};
};
WriteStats write_{};
LONG summary_frames_ = 0; // frames_ at the last log_writer row (under lock_)
volatile LONG frames_ = 0, devices_ = 0, resets_ = 0, exception_latch_ = 0;
// The crash filter (SetUnhandledExceptionFilter): the filter it replaced, called after ours.
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter_ = nullptr;
bool filter_armed_ = false;
const char* filter_state_ = "none"; // session_end filter=: ours | replaced (another filter took over) | none
struct Named {
    std::uintptr_t base;
    char name[40];
};
Named named_[4] = {};
volatile LONG named_count_ = 0;

struct Line {
    char text[384];
    unsigned used = 0;
    void put(char c) noexcept {
        if (used < sizeof text - 1) text[used++] = c;
    }
    void str(const char* s) noexcept {
        while (*s) put(*s++);
    }
    void hex8(std::uint32_t v) noexcept {
        for (int i = 7; i >= 0; --i) put("0123456789abcdef"[(v >> (i * 4)) & 15u]);
    }
    void dec(std::uint32_t v) noexcept {
        char d[10];
        int n = 0;
        do {
            d[n++] = char('0' + v % 10u);
            v /= 10u;
        } while (v);
        while (n) put(d[--n]);
    }
    void dec64(std::uint64_t v) noexcept {
        char d[20];
        int n = 0;
        do {
            d[n++] = char('0' + unsigned(v % 10u));
            v /= 10u;
        } while (v);
        while (n) put(d[--n]);
    }
    unsigned end_row() noexcept {
        text[used] = '\n';
        return used + 1;
    } // put() stops one short, so the newline fits
};
void write_direct(const char* text, DWORD bytes) noexcept {
    DWORD written = 0;
    if (file_ != INVALID_HANDLE_VALUE && bytes) WriteFile(file_, text, bytes, &written, nullptr);
}
// Under lock_: room for n bytes in the active half. A full active half is handed to the writer
// when the other half is free; false when both are in use (the caller drops the row).
bool reserve(unsigned n, bool& wake) noexcept {
    if (n > kHalf) return false;
    if (used_[active_] + n <= kHalf) return true;
    const unsigned other = active_ ^ 1u;
    if (handed_[other] || used_[other]) return false;
    handed_[active_] = true;
    active_ = other;
    wake = true;
    return true;
}
void put_locked(const char* text, unsigned n) noexcept {
    std::memcpy(buffer_[active_] + used_[active_], text, n);
    used_[active_] += n;
    appended_ += LONG(n);
}
bool drops_row_locked(bool& wake) noexcept {
    if (!pending_drops_) return true;
    Line line;
    line.str("log_dropped n=");
    line.dec(pending_drops_);
    const unsigned n = line.end_row();
    if (!reserve(n, wake)) return false;
    put_locked(line.text, n);
    pending_drops_ = 0;
    return true;
}
void drop_locked() noexcept {
    ++pending_drops_;
    InterlockedIncrement(&dropped_total_);
}
std::uint64_t now_ticks() noexcept {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return std::uint64_t(t.QuadPart);
}
double as_double(std::uint64_t v) noexcept {
    return double(std::uint32_t(v >> 32)) * 4294967296.0 + double(std::uint32_t(v));
} // no x87 u64 conversion
// Writer thread: writes a handed half from its written_ mark on, in chunks.
void write_half(unsigned h) noexcept {
    const unsigned size = used_[h]; // stable: game threads do not touch a handed half
    unsigned offset = unsigned(written_[h]);
    while (offset < size) {
        const DWORD part = size - offset < kChunk ? size - offset : kChunk;
        const std::uint64_t begin = timing_ ? now_ticks() : 0;
        DWORD done = 0;
        const BOOL ok = WriteFile(file_, buffer_[h] + offset, part, &done, nullptr);
        const std::uint64_t ticks = timing_ ? now_ticks() - begin : 0;
        AcquireSRWLockExclusive(&lock_); // the stats only; never held across the WriteFile above
        if (timing_) {
            ++write_.writes;
            write_.bytes += part;
            write_.ticks += ticks;
            if (ticks > write_.max_ticks) write_.max_ticks = ticks;
            unsigned bucket = 0;
            for (std::uint64_t edge = frequency_ / 100000u; bucket < 5 && ticks > edge; edge *= 10u)
                ++bucket; // 10 us, 100 us, ... 100 ms
            ++write_.buckets[bucket];
        }
        if (!ok) ++write_.failures; // not retried: the rows of that chunk are lost, the file stays well formed
        ReleaseSRWLockExclusive(&lock_);
        offset += part;
        InterlockedExchange(&written_[h], LONG(offset));
        InterlockedExchangeAdd(&flushed_, LONG(part));
    }
}
void drain_all() noexcept {
    for (;;) {
        unsigned h = 2;
        AcquireSRWLockExclusive(&lock_);
        const unsigned other = active_ ^ 1u;
        if (handed_[other])
            h = other; // the older rows first
        else if (used_[active_]) {
            h = active_;
            handed_[h] = true;
            active_ = other;
        } // the other half is empty here
        ReleaseSRWLockExclusive(&lock_);
        if (h == 2) return;
        write_half(h);
        AcquireSRWLockExclusive(&lock_);
        used_[h] = 0;
        handed_[h] = false;
        InterlockedExchange(&written_[h], 0);
        ReleaseSRWLockExclusive(&lock_);
    }
}
// One log_writer row: the window since the previous one (the writer every 10 s; the last device's release).
void summary(const char* reason) noexcept {
    AcquireSRWLockExclusive(&lock_);
    const AppendStats a = append_;
    append_ = {};
    const WriteStats w = write_;
    write_ = {};
    const LONG frames = frames_;
    const LONG window_frames = frames - summary_frames_;
    summary_frames_ = frames;
    ReleaseSRWLockExclusive(&lock_);
    const double us = frequency_ ? 1e6 / as_double(frequency_) : 0.;
    x3m::log(
        "log_writer reason=%s frames=%ld rows=%llu bytes=%llu dropped_total=%ld format_us=%.1f format_max_us=%.2f format_us_per_frame=%.2f"
        " writes=%llu write_bytes=%llu write_us=%.1f write_max_us=%.1f write_failures=%llu write_hist=%llu,%llu,%llu,%llu,%llu,%llu"
        " buffer_bytes=%u period_ms=%lu",
        reason, long(window_frames), static_cast<unsigned long long>(a.rows), static_cast<unsigned long long>(a.bytes),
        long(dropped_total_), as_double(a.ticks) * us, as_double(a.max_ticks) * us,
        window_frames > 0 ? as_double(a.ticks) * us / double(window_frames) : 0.,
        static_cast<unsigned long long>(w.writes), static_cast<unsigned long long>(w.bytes), as_double(w.ticks) * us,
        as_double(w.max_ticks) * us, static_cast<unsigned long long>(w.failures),
        static_cast<unsigned long long>(w.buckets[0]), static_cast<unsigned long long>(w.buckets[1]),
        static_cast<unsigned long long>(w.buckets[2]), static_cast<unsigned long long>(w.buckets[3]),
        static_cast<unsigned long long>(w.buckets[4]), static_cast<unsigned long long>(w.buckets[5]), 2u * kHalf,
        static_cast<unsigned long>(kPeriodMs));
}
DWORD WINAPI writer(void*) {
    DWORD next_summary = GetTickCount() + kSummaryMs;
    for (;;) {
        WaitForSingleObject(wake_, kPeriodMs);
        const bool stopping = stop_ != 0;
        drain_all();
        if (timing_ && LONG(GetTickCount() - next_summary) >= 0) {
            summary("period");
            next_summary += kSummaryMs;
            drain_all();
        }
        if (stopping) break;
    }
    // The reference start_writer took: the code stays mapped until here. When the application already called
    // FreeLibrary, this is the call that unloads the proxy (DLL_PROCESS_DETACH then runs on this thread).
    FreeLibraryAndExitThread(module_ref_, 0);
}
bool writer_running() noexcept {
    return thread_ && WaitForSingleObject(thread_, 0) == WAIT_TIMEOUT;
}
// The rows not yet written, straight through the OS handle on the calling thread: the handed half from its last
// whole chunk, then the active half. Only while no writer runs (parked, never started, or ended by the OS at
// process exit) and with the buffer lock held or no other thread left.
void drain_direct() noexcept {
    const unsigned other = active_ ^ 1u;
    if (handed_[other] && used_[other] > unsigned(written_[other]))
        write_direct(buffer_[other] + written_[other], used_[other] - unsigned(written_[other]));
    if (used_[active_] > unsigned(written_[active_]))
        write_direct(buffer_[active_] + written_[active_], used_[active_] - unsigned(written_[active_]));
    used_[0] = used_[1] = 0;
    handed_[0] = handed_[1] = false;
    written_[0] = 0;
    written_[1] = 0;
    flushed_ = appended_;
}
void wide_digits(std::wstring& out, unsigned value, unsigned width) {
    wchar_t d[10];
    unsigned n = 0;
    do {
        d[n++] = wchar_t(L'0' + value % 10u);
        value /= 10u;
    } while (value && n < 10);
    while (n < width && n < 10) d[n++] = L'0';
    while (n) out += d[--n];
}
void narrow_digits(char*& at, const char* end, unsigned value, unsigned width) {
    char d[10];
    unsigned n = 0;
    do {
        d[n++] = char('0' + value % 10u);
        value /= 10u;
    } while (value && n < 10);
    while (n < width && n < 10) d[n++] = '0';
    while (n && at < end) *at++ = d[--n];
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::string out(std::size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), &out[0], size, nullptr, nullptr);
    return out;
}
std::wstring environment(const wchar_t* name) {
    const DWORD size = x3m::config::get(name, nullptr, 0); // X3M_LOG_FILE may come from x3m.ini; other names are the
                                                           // environment's
    if (size < 2) return std::wstring();
    std::wstring value(size, L'\0');
    const DWORD length = x3m::config::get(name, &value[0], size);
    if (!length || length >= size) return std::wstring(); // gone or grown in between
    value.resize(length);
    return value;
}
char fold(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}
bool separator(char c) noexcept {
    return c == '\\' || c == '/';
}
// Case-insensitive (ASCII) match of `needle` at `at`, ending at a path boundary.
bool match_at(const std::string& text, std::size_t at, const std::string& needle) noexcept {
    if (needle.empty() || at + needle.size() > text.size()) return false;
    for (std::size_t i = 0; i < needle.size(); ++i)
        if (fold(text[at + i]) != fold(needle[i])) return false;
    const std::size_t end = at + needle.size();
    return end == text.size() || separator(text[end]) || text[end] == ':' || text[end] == ';' || text[end] == ' ' ||
           text[end] == '"';
}
std::string replace_all(const std::string& text, const std::string& needle, const char* token) {
    if (needle.size() < 2) return text; // "/" or a drive root would redact everything
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (match_at(text, i, needle)) {
            out += token;
            i += needle.size();
        } else
            out += text[i++];
    }
    return out;
}
HANDLE create(const std::wstring& path) {
    // _wfopen(L"w") semantics: create or truncate, shared for reading and writing (a player can copy
    // the log while the game runs); not shared for delete, so a second instance's rename is refused.
    return CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}
// Rotation and open in one directory: x3m.log -> x3m.prev.log, then x3m.log truncated; any rename
// failure other than "no previous log" keeps the old file and opens x3m-<pid>.log instead.
// Stale x3m-<pid>.log files of earlier busy rotations: those last written before the current x3m.prev.log are
// deleted (this directory only, only names matching the pattern), so the busy fallback cannot accumulate files.
unsigned remove_stale(const std::wstring& base) {
    WIN32_FILE_ATTRIBUTE_DATA prev{};
    if (!GetFileAttributesExW((base + L"\\x3m.prev.log").c_str(), GetFileExInfoStandard, &prev)) return 0;
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((base + L"\\x3m-*.log").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    unsigned removed = 0;
    do {
        const wchar_t* name = found.cFileName;
        std::size_t i = 4, digits = 0; // after "x3m-"
        while (name[i] >= L'0' && name[i] <= L'9') {
            ++i;
            ++digits;
        }
        const bool pattern = !_wcsnicmp(name, L"x3m-", 4) && digits > 0 && !_wcsicmp(name + i, L".log") &&
                             !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        if (pattern && CompareFileTime(&found.ftLastWriteTime, &prev.ftLastWriteTime) < 0 &&
            DeleteFileW((base + L"\\" + name).c_str()))
            ++removed;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return removed;
}
HANDLE open_in(const std::wstring& base, const char*& previous, std::wstring& opened, unsigned& stale) {
    const std::wstring log = base + L"\\x3m.log", prev = base + L"\\x3m.prev.log";
    std::wstring pid = base + L"\\x3m-";
    wide_digits(pid, unsigned(GetCurrentProcessId()), 1);
    pid += L".log";
    const std::wstring* target = &log;
    if (MoveFileExW(log.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING))
        previous = "renamed";
    else {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            previous = "absent";
        else {
            previous = "busy";
            target = &pid;
        }
    }
    HANDLE file = create(*target);
    if (file == INVALID_HANDLE_VALUE && target == &log) {
        previous = "busy";
        target = &pid;
        file = create(pid);
    }
    if (file != INVALID_HANDLE_VALUE) {
        opened = *target;
        stale = remove_stale(base);
    }
    return file;
}
// The path the OS resolved for the open handle (documented since Vista; resolved at run time so an
// older kernel32 keeps the opened name): with UAC file virtualisation it names the VirtualStore copy.
std::wstring final_path(HANDLE handle, const std::wstring& fallback) {
    using FinalPathFn = DWORD(WINAPI*)(HANDLE, LPWSTR, DWORD, DWORD);
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const FARPROC proc = kernel ? GetProcAddress(kernel, "GetFinalPathNameByHandleW") : nullptr;
    if (!proc || handle == INVALID_HANDLE_VALUE) return fallback;
    const FinalPathFn fn = reinterpret_cast<FinalPathFn>(reinterpret_cast<void*>(proc)); // via void*:
                                                                                         // -Wcast-function-type
    std::wstring path(1024, L'\0');
    DWORD length = fn(handle, &path[0], DWORD(path.size()), 0 /* FILE_NAME_NORMALIZED | VOLUME_NAME_DOS */);
    if (length >= path.size()) {
        path.assign(length + 1, L'\0');
        length = fn(handle, &path[0], DWORD(path.size()), 0);
    }
    if (!length || length >= path.size()) return fallback;
    path.resize(length);
    if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
        path = L"\\\\" + path.substr(8);
    else if (path.compare(0, 4, L"\\\\?\\") == 0)
        path = path.substr(4);
    return path;
}
bool buffer_empty() noexcept {
    return !handed_[0] && !handed_[1] && !used_[active_];
} // racy read: a wait condition only
bool flushed_past(LONG target) noexcept {
    return LONG(flushed_ - target) >= 0;
} // wrap-safe; racy read of an aligned word
}

extern "C" void x3m_session_log_open(HMODULE module, Opened* out) {
    Opened& o = *out;
    { // Session name: local time and process id, as the former session-*.log name carried.
        SYSTEMTIME now{};
        GetLocalTime(&now);
        char* at = o.session;
        const char* end = o.session + sizeof o.session - 1;
        narrow_digits(at, end, now.wYear, 4);
        narrow_digits(at, end, now.wMonth, 2);
        narrow_digits(at, end, now.wDay, 2);
        if (at < end) *at++ = '-';
        narrow_digits(at, end, now.wHour, 2);
        narrow_digits(at, end, now.wMinute, 2);
        narrow_digits(at, end, now.wSecond, 2);
        if (at < end) *at++ = '-';
        narrow_digits(at, end, unsigned(GetCurrentProcessId()), 1);
        *at = 0;
    }
    std::wstring game(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, &game[0], DWORD(game.size()));
    game.resize(length && length < game.size() ? length : 0);
    const std::size_t cut = game.find_last_of(L"\\/");
    game.resize(cut == std::wstring::npos ? 0 : cut);
    // An unknown module path never becomes "\\x3m.log" at the drive root: the %LOCALAPPDATA% fallback below takes it.
    const bool game_known = game.size() >= 2;
    if (game_known) {
        o.captures = game + L"\\x3-modern-captures";
        CreateDirectoryW(o.captures.c_str(), nullptr);
    }
    std::wstring opened;
    const std::wstring override_path = environment(L"X3M_LOG_FILE");
    if (!override_path.empty()) {
        o.file = create(override_path);
        if (o.file != INVALID_HANDLE_VALUE) {
            o.source = "override";
            o.previous = "none";
            opened = override_path;
        } else
            o.override_failed = true;
    }
    if (o.file == INVALID_HANDLE_VALUE && game_known) {
        o.file = open_in(game, o.previous, opened, o.stale_removed);
        if (o.file != INVALID_HANDLE_VALUE) o.source = "game";
    }
    {
        // W3 of the native-Windows audit: the game directory may be read-only (Program Files without
        // Steam's ACL grant, a virtualised install). %LOCALAPPDATA% (else %USERPROFILE%\AppData\Local)
        // \x3-modern-renderer holds the log; the captures move to its captures subdirectory.
        std::wstring base = environment(L"LOCALAPPDATA");
        if (base.empty()) {
            base = environment(L"USERPROFILE");
            if (!base.empty()) base += L"\\AppData\\Local";
        }
        if (!base.empty() && (o.file == INVALID_HANDLE_VALUE || o.captures.empty())) {
            base += L"\\x3-modern-renderer";
            CreateDirectoryW(base.c_str(), nullptr);
            const std::wstring captures = base + L"\\captures";
            CreateDirectoryW(captures.c_str(), nullptr);
            if (o.file == INVALID_HANDLE_VALUE) {
                o.file = open_in(base, o.previous, opened, o.stale_removed);
                if (o.file != INVALID_HANDLE_VALUE) {
                    o.source = "localappdata";
                    o.captures_source = "localappdata";
                    o.captures = captures;
                }
            }
            if (o.captures.empty()) {
                o.captures = captures;
                o.captures_source = "localappdata";
            } // an override with no known game directory
        }
    }
    if (o.file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER f{};
    frequency_ = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    o.path = x3m::session_log::redact_path(utf8(final_path(o.file, opened)));
    file_ = o.file; // log() appends from here on (the writer starts after telemetry::initialize)
}

// The crash filter: SetUnhandledExceptionFilter, so it runs only for an exception nobody handled (a probe read or
// a driver's own try/except never reaches it), chained to the filter it replaced.
extern "C" LONG WINAPI x3m_exception_witness(EXCEPTION_POINTERS* info) {
    const LPTOP_LEVEL_EXCEPTION_FILTER previous = previous_filter_;
    const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
    if (!record || InterlockedCompareExchange(&exception_latch_, 1, 0) != 0) // the first one only
        return previous ? previous(info) : EXCEPTION_CONTINUE_SEARCH;
    const DWORD error = GetLastError();
    // Integers only, a stack buffer, no allocation, no formatter. The rows logged before the fault go first:
    // the writer is woken and waited for (at most kExceptionDrainMs by the clock) until it has written them;
    // without a writer (parked, or this is the writer) they are written here under a try-lock of the buffer
    // (a thread that faulted inside log() holding it just loses them). Then this row through the OS handle.
    if (record->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
        const LONG target = appended_;
        const bool other_writer = writer_running() && GetCurrentThreadId() != writer_id_;
        if (other_writer && wake_) {
            SetEvent(wake_);
            const ULONGLONG deadline = GetTickCount64() + kExceptionDrainMs;
            while (!flushed_past(target) && GetTickCount64() < deadline) Sleep(1);
        }
        if (!flushed_past(target) && !other_writer && TryAcquireSRWLockExclusive(&lock_)) {
            drain_direct();
            ReleaseSRWLockExclusive(&lock_);
        }
    }
    const auto address = std::uint32_t(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress));
    const CONTEXT* c = info->ContextRecord;
    std::uintptr_t base = 0;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(record->ExceptionAddress, &region, sizeof region) == sizeof region)
        base = reinterpret_cast<std::uintptr_t>(region.AllocationBase);
    const char* name = base ? "other" : "none";
    const LONG named = named_count_;
    for (LONG i = 0; i < named && i < 4; ++i)
        if (base && named_[i].base == base) name = named_[i].name;
    Line line;
    line.str("exception code=");
    line.hex8(std::uint32_t(record->ExceptionCode));
    line.str(" address=");
    line.hex8(address);
    line.str(" eip=");
    line.hex8(c ? std::uint32_t(c->Eip) : 0u);
    line.str(" esp=");
    line.hex8(c ? std::uint32_t(c->Esp) : 0u);
    line.str(" thread=");
    line.dec(std::uint32_t(GetCurrentThreadId()));
    line.str(" frame=");
    line.dec(std::uint32_t(frames_));
    line.str(" module=");
    line.str(name);
    line.str(" base=");
    line.hex8(std::uint32_t(base));
    line.str(" offset=");
    line.hex8(address - std::uint32_t(base));
    line.str(" access_kind=");
    line.dec(record->NumberParameters >= 1 ? std::uint32_t(record->ExceptionInformation[0]) : 0u);
    line.str(" access=");
    line.hex8(record->NumberParameters >= 2 ? std::uint32_t(record->ExceptionInformation[1]) : 0u);
    line.str(" unhandled=1");
    write_direct(line.text, line.end_row());
    SetLastError(error);
    return previous ? previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

extern "C" void x3m_session_end() {
    const DWORD error = GetLastError();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const std::uint64_t ticks = std::uint64_t(now.QuadPart) > x3m::dll_load_qpc
                                    ? std::uint64_t(now.QuadPart) - x3m::dll_load_qpc
                                    : 0;
    const std::uint64_t elapsed_ms = frequency_ ? ticks / frequency_ * 1000u + ticks % frequency_ * 1000u / frequency_
                                                : 0;
    Line line;
    line.str("session_end frames=");
    line.dec(std::uint32_t(frames_));
    line.str(" elapsed_ms=");
    line.dec64(elapsed_ms);
    line.str(" devices=");
    line.dec(std::uint32_t(devices_));
    line.str(" resets=");
    line.dec(std::uint32_t(resets_));
    line.str(" exception=");
    line.dec(exception_latch_ ? 1u : 0u);
    line.str(" dropped=");
    line.dec(std::uint32_t(dropped_total_));
    line.str(" filter=");
    line.str(filter_state_);
    write_direct(line.text, line.end_row());
    SetLastError(error);
}

namespace x3m::session_log {
Opened open(HMODULE module) {
    Opened opened;
    x3m_session_log_open(module, &opened);
    return opened;
}
void start_writer(bool timing) noexcept {
    if (file_ == INVALID_HANDLE_VALUE) return;
    if (thread_) {
        if (writer_running()) {
            if (!stop_) return; // running
            if (WaitForSingleObject(thread_, kJoinMs) != WAIT_OBJECT_0)
                return; // a parked writer still draining: never two
        }
        CloseHandle(thread_);
        thread_ = nullptr;
        writer_id_ = 0;
    }
    if (!started_) {
        timing_ = timing;
        started_ = true;
    }
    if (!wake_) wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    InterlockedExchange(&stop_, 0);
    // A module reference for the thread's lifetime (released by FreeLibraryAndExitThread): held only while a
    // device exists (park_writer drops it with the last device), so the application's FreeLibrary can unload.
    if (!wake_ ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&writer), &module_ref_))
        return;
    thread_ = CreateThread(nullptr, 64u << 10, &writer, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &writer_id_);
    if (!thread_) {
        FreeLibrary(module_ref_);
        module_ref_ = nullptr;
    }
}
void park_writer(const char* reason) noexcept {
    if (!thread_ || GetCurrentThreadId() == writer_id_) return;
    InterlockedExchange(&stop_, 1);
    if (wake_) SetEvent(wake_);
    // Bounded: the final drain is milliseconds; a writer that has not ended is left to finish on its own.
    const bool exited = WaitForSingleObject(thread_, kJoinMs) == WAIT_OBJECT_0;
    if (exited) {
        CloseHandle(thread_);
        thread_ = nullptr;
        writer_id_ = 0;
    }
    x3m::log("log_writer_parked reason=%s exited=%u", reason, unsigned(exited)); // buffered: the next writer or detach
                                                                                 // writes it
}
void vlog(const char* format, va_list args) noexcept {
    if (file_ == INVALID_HANDLE_VALUE) return;
    const bool timed = timing_;
    const std::uint64_t begin = timed ? now_ticks() : 0;
    char scratch[kScratch];
    int n = -1;
    va_list copy;
    va_copy(copy, args);
    call_preserved([&] { n = std::vsnprintf(scratch, kScratch - 1, format, args); });
    bool wake = false;
    unsigned bytes = 0;
    if (!closing_)
        AcquireSRWLockExclusive(&lock_);
    else if (!TryAcquireSRWLockExclusive(&lock_)) {
        InterlockedIncrement(&dropped_total_);
        va_end(copy);
        return;
    } // DllMain: never wait
    if (n > 0 && unsigned(n) < kScratch - 1 && scratch[n - 1] == '\n')
        --n; // a caller's own line end (former direct writers)
    if (n >= 0 && unsigned(n) < kScratch - 1) {
        scratch[n] = '\n';
        bytes = unsigned(n) + 1;
        if (drops_row_locked(wake) && reserve(bytes, wake))
            put_locked(scratch, bytes);
        else {
            drop_locked();
            bytes = 0;
        }
    } else if (n >= 0) {
        // Longer than the scratch (proxy_options with many variables): formatted straight into the
        // buffer once, under the lock, capped at kRowCap with a marker.
        const unsigned cap = unsigned(n) < kRowCap ? unsigned(n) : kRowCap;
        const unsigned want = cap + 13; // the row, " truncated=1" when cut, the newline
        if (drops_row_locked(wake) && reserve(want, wake)) {
            char* at = buffer_[active_] + used_[active_];
            int m = -1;
            call_preserved([&] { m = std::vsnprintf(at, cap + 1, format, copy); });
            unsigned length = m < 0 ? 0u : unsigned(m) < cap ? unsigned(m) : cap;
            if (m > 0 && unsigned(m) > cap) {
                std::memcpy(at + length, " truncated=1", 12);
                length += 12;
            }
            at[length++] = '\n';
            used_[active_] += length;
            appended_ += LONG(length);
            bytes = length;
        } else
            drop_locked();
    }
    if (timed) {
        const std::uint64_t ticks = now_ticks() - begin;
        ++append_.rows;
        append_.bytes += bytes;
        append_.ticks += ticks;
        if (ticks > append_.max_ticks) append_.max_ticks = ticks;
    }
    ReleaseSRWLockExclusive(&lock_);
    va_end(copy);
    if (wake && wake_) SetEvent(wake_);
}
void request_drain() noexcept {
    if (wake_) SetEvent(wake_);
}
void report(const char* reason) noexcept {
    if (timing_ && file_ != INVALID_HANDLE_VALUE) summary(reason);
}
void closing() noexcept {
    InterlockedExchange(&closing_, 1);
}
void drain_now(DWORD max_ms) noexcept {
    if (!thread_ || !wake_ || GetCurrentThreadId() == writer_id_) return;
    SetEvent(wake_);
    for (DWORD waited = 0; waited < max_ms && !buffer_empty(); waited += 5) Sleep(5);
}
HANDLE handle() noexcept {
    return file_;
}
unsigned dropped_total() noexcept {
    return unsigned(dropped_total_);
}
std::string redact_wide_path(const wchar_t* path) {
    return path ? redact_path(utf8(std::wstring(path))) : std::string();
}
std::string redact_path(const std::string& text) {
    const std::string profile = utf8(environment(L"USERPROFILE"));
    if (profile.size() >= 3 && match_at(text, 0, profile)) return "%USERPROFILE%" + text.substr(profile.size());
    // <drive>:\Users\<name> or <drive>:\home\<name> (the host home under Wine is Z:\Users\<name>).
    if (text.size() > 3 && text[1] == ':' && separator(text[2])) {
        static const char* const roots[] = {"users", "home"};
        for (const char* root : roots) {
            const std::size_t r = std::strlen(root);
            if (text.size() <= 3 + r || !separator(text[3 + r])) continue;
            bool same = true;
            for (std::size_t i = 0; i < r; ++i) same = same && fold(text[3 + i]) == root[i];
            if (!same) continue;
            const std::size_t name = 4 + r, end = text.find_first_of("\\/", name);
            return text.substr(0, name) + "~" + (end == std::string::npos ? std::string() : text.substr(end));
        }
    }
    return text;
}
std::string redact_value(const std::string& text) {
    std::string home = utf8(environment(L"HOME"));
    if (home.empty() || home[0] != '/') home = utf8(environment(L"WINE_HOST_HOME"));
    std::string out = text;
    if (home.size() >= 2 && home[0] == '/') {
        out = replace_all(out, home, "~");
        std::string back = home;
        for (char& ch : back)
            if (ch == '/') ch = '\\';
        out = replace_all(out, back, "~");
    }
    const std::string profile = utf8(environment(L"USERPROFILE"));
    if (profile.size() >= 3) out = replace_all(out, profile, "%USERPROFILE%");
    return out;
}
void note_frame() noexcept {
    InterlockedIncrement(&frames_);
}
void note_device() noexcept {
    InterlockedIncrement(&devices_);
}
void note_reset() noexcept {
    InterlockedIncrement(&resets_);
}
void name_module(HMODULE module, const char* name) noexcept {
    if (!module || !name) return;
    const LONG slot = named_count_;
    if (slot >= 4) return;
    Named& n = named_[slot];
    n.base = reinterpret_cast<std::uintptr_t>(module);
    unsigned i = 0;
    for (; name[i] && i < sizeof n.name - 1; ++i) {
        const char ch = name[i];
        n.name[i] = ch > 0x20 && ch < 0x7f ? ch : '_';
    }
    n.name[i] = 0;
    InterlockedExchange(&named_count_, slot + 1); // published after the entry is complete
}
void arm_exception_witness(HMODULE proxy) noexcept {
    if (filter_armed_ || file_ == INVALID_HANDLE_VALUE) return;
    // The game executable by its file name, the proxy as "proxy".
    const HMODULE exe = GetModuleHandleW(nullptr);
    wchar_t path[MAX_PATH]{};
    const DWORD length = exe ? GetModuleFileNameW(exe, path, MAX_PATH) : 0;
    char name[40] = "exe";
    if (length && length < MAX_PATH) {
        unsigned start = length;
        while (start && path[start - 1] != L'\\' && path[start - 1] != L'/') --start;
        unsigned n = 0;
        for (unsigned i = start; i < length && n < sizeof name - 1; ++i)
            name[n++] = path[i] > 0x20 && path[i] < 0x7f ? char(path[i]) : '_';
        name[n] = 0;
    }
    name_module(exe, name);
    name_module(proxy, "proxy");
    if (filter_armed_) return;
    previous_filter_ = SetUnhandledExceptionFilter(&x3m_exception_witness); // chained: ours first, then the one it
                                                                            // replaced
    filter_armed_ = true;
    filter_state_ = "ours";
}
void detach(bool process_exit) noexcept {
    // The crash filter goes back to whoever preceded it, unless a later SetUnhandledExceptionFilter replaced ours
    // (the game's own, or a runtime's): that one stays, and session_end says filter=replaced.
    if (filter_armed_) {
        const LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(previous_filter_);
        if (current != &x3m_exception_witness) {
            SetUnhandledExceptionFilter(current);
            filter_state_ = "replaced";
        }
        filter_armed_ = false;
    }
    // At process exit Windows has already ended every other thread, the writer included, so the wait returns at once
    // and nothing else can touch the buffer. A dynamic FreeLibrary reaches here only once the writer dropped its module
    // reference: parked (it has ended), or on the writer's own FreeLibraryAndExitThread (this is the writer: no wait).
    // A wait that times out leaves the thread and its rows alone rather than racing it on the file.
    bool writer_gone = true;
    if (thread_ && GetCurrentThreadId() != writer_id_) {
        InterlockedExchange(&stop_, 1);
        if (wake_) SetEvent(wake_);
        writer_gone = WaitForSingleObject(thread_, kJoinMs) == WAIT_OBJECT_0;
    }
    if (!writer_gone || file_ == INVALID_HANDLE_VALUE) return;
    // The rows still buffered, then session_end: lock-free at process exit (the lock may have died with a terminated
    // thread), under a try-lock on a dynamic unload (a live thread may still be inside log()).
    if (process_exit)
        drain_direct();
    else if (TryAcquireSRWLockExclusive(&lock_)) {
        drain_direct();
        ReleaseSRWLockExclusive(&lock_);
    } else
        return;
    x3m_session_end();
}
}
