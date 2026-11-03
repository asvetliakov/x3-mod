// FL edition of the fast resource reader and the DAT handle pool: the AP policy (src/proxy/resource_reader.cpp) on
// X3FL.exe, over the shared core (src/proxy/resource_reader_core.cpp, unchanged: decode, the entry handler, the pool).
// docs/reverse-engineering/fl/resource-reader.md, "Port (2026-11-03)"; sites in resource_reader_sites_fl.h.
//
// Reader: the entry base+0x105f40 is claimed through engine_patch (six displaced bytes, no relative operand) and the
// FL stub chained in front. FL passes the file object in ECX (AP: EAX); the stub copies ECX into the saved-EAX slot the
// core's x3m_resource_read_entry reads (regs[7]) and puts the caller's EAX back before any fallback to the original.
// Verify mode calls the original through a bridge `mov ecx,eax; jmp [next]` (the core's call_original passes the
// object in EAX). The stub saves EFLAGS and every GPR, clears DF for the C side and restores both; the core keeps
// LastError (fast: the caller's; verify: the original's) and touches no XMM/x87 state (no-SSE build).
// Pool: seventeen of the eighteen direct fclose calls are redirected to the core's pool_fclose (unknown handles pass
// through to the CRT's fclose), then the DAT open's fopen call to pool_fopen: close coverage is live before a pooled
// handle can exist. The eighteenth, the CRT's exit-time fcloseall loop, stays native (resource_reader_sites_fl.h).
// All or none, each write read back, inside engine_patch's install window, after the music keep and gz_buffer (whose
// close_wrapper window holds the fclose call at 0x10696c). The FL loader has no unload path: no shutdown wiring.
#include "resource_reader_fl.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_patch.h"
#include "executable_identity_fl.h"
#include "resource_reader_sites_fl.h"
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
namespace x3m::resource_reader {
namespace {
namespace F = x3m::fl::resource_reader_fl;
namespace W = x3m::fl::loading_windows;
Mode mode_ = Mode::Native;
bool initialized_ = false, installed_ = false, pool_requested_ = false, pool_installed_ = false;
const char* status_ = "disabled";
const char* pool_status_ = "disabled";
const char* window_ = "-";
std::uintptr_t base_ = 0;
#ifdef X3M_RESOURCE_READER_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif
engine_patch::Site site_;
engine_patch::CallSite open_site_, close_sites_[F::close_site_count];
unsigned close_live_ = 0;
void** next_slot_ = nullptr; // the stub's continuation word (arena): the original entry's tail
void* bridge_word_ = nullptr; // the verify bridge; the core's continuation (it reads *continuation)
Environment env_;
uint64_t frequency = 1, last_report_calls = 0, last_pool_opens = 0;
unsigned mismatch_lines = 0;
constexpr unsigned mismatch_line_limit = 64;
const char* mode_name(Mode m) {
    return m == Mode::Fast ? "fast" : m == Mode::Verify ? "verify" : "native";
}
bool fault() {
#ifdef X3M_RESOURCE_READER_FL_FIXTURE
    return F::fixture_fail_step && !--F::fixture_fail_step;
#else
    return false;
#endif
}
bool read_self(std::uintptr_t at, void* out, std::size_t n) {
    return engine_patch::read_code(at, static_cast<unsigned char*>(out), unsigned(n));
}
void verify_sink(const VerifyEvent& e) {
    PreserveCpuState cpu;
    if (mismatch_lines++ >= mismatch_line_limit) return;
    log("resource_reader verify equal=%u size=%lu original_size=%lu mismatches=%lu first=%lu original_null=%u globals_ok=%u counters_ok=%u cursor_ok=%u position_ok=%u cursor=%ld expected_cursor=%ld position=%ld expected_position=%ld catalogue=%u scrambled=%u our_us=%.3f original_us=%.3f edition=fl",
        unsigned(e.equal), static_cast<unsigned long>(e.size), static_cast<unsigned long>(e.original_size),
        static_cast<unsigned long>(e.mismatches), static_cast<unsigned long>(e.first_mismatch),
        unsigned(e.original_null), unsigned(e.globals_ok), unsigned(e.counters_ok), unsigned(e.cursor_ok),
        unsigned(e.position_ok), long(e.cursor), long(e.expected_cursor), e.position, e.expected_position,
        unsigned(e.catalogue), unsigned(e.scrambled), double(e.our_ticks) * 1e6 / double(frequency),
        double(e.original_ticks) * 1e6 / double(frequency));
}
// FL reader entry stub (the object in ECX):
//   pushfd ; cld ; pushad
//   mov eax,[esp+0x18] ; mov edx,[esp+0x1c] ; mov [esp+0x1c],eax   regs[7] = object, EDX = the caller's EAX
//   push edx ; lea eax,[esp+4] ; push eax ; call x3m_resource_read_entry ; add esp,4 ; pop edx
//   test eax,eax ; jz continue ; popad ; popfd ; ret                     handled: EAX = regs[7] = the buffer
//   continue: mov [esp+0x1c],edx ; popad ; popfd ; jmp [next]           the caller's EAX and ECX for the original
void* emit_stub(void*** next_out) {
    engine_patch::Emitter e(64);
    if (!e.ok()) return nullptr;
    void* start = e.here();
    static const unsigned char head[] = {0x9c, 0xfc, 0x60, 0x8b, 0x44, 0x24, 0x18, 0x8b, 0x54, 0x24, 0x1c,
                                         0x89, 0x44, 0x24, 0x1c, 0x52, 0x8d, 0x44, 0x24, 0x04, 0x50, 0xe8};
    e.bytes(head, sizeof head);
    e.rel32(reinterpret_cast<const void*>(&x3m_resource_read_entry));
    static const unsigned char tail[] = {0x83, 0xc4, 0x04, 0x5a, 0x85, 0xc0, 0x74, 0x03, 0x61, 0x9d, 0xc3,
                                         0x89, 0x54, 0x24, 0x1c, 0x61, 0x9d, 0xff, 0x25};
    e.bytes(tail, sizeof tail);
    std::uintptr_t next = reinterpret_cast<std::uintptr_t>(e.here()) + 4;
    next = (next + 3) & ~std::uintptr_t(3);
    e.dword(std::uint32_t(next));
    while (reinterpret_cast<std::uintptr_t>(e.here()) < next) e.byte(0xcc);
    *next_out = reinterpret_cast<void**>(next);
    e.dword(0);
    return e.finish() ? start : nullptr;
}
// Verify bridge: mov ecx,eax ; jmp [next] (the original takes the object in ECX).
void* emit_bridge(void** next) {
    engine_patch::Emitter e(16);
    if (!e.ok()) return nullptr;
    void* start = e.here();
    e.byte(0x89);
    e.byte(0xc1);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(next)));
    return e.finish() ? start : nullptr;
}
template <unsigned N> bool windows_match(const W::CodeWindow (&windows)[N], const W::Fixup* fixups) {
    if (const char* bad = W::first_mismatch(read_self, base_, windows, fixups)) {
        window_ = bad;
        return false;
    }
    return true;
}
bool patched_bytes(std::uintptr_t at, const unsigned char* expected) {
    unsigned char now[5]{};
    return engine_patch::read_code(at, now, 5) && !std::memcmp(now, expected, 5);
}
template <class T> T* at(std::uint32_t rva) {
    return reinterpret_cast<T*>(base_ + rva);
}
bool install_reader() {
    if (!windows_match(W::reader_windows, W::reader_fixups)) {
        status_ = "bytes_mismatch";
        return false;
    }
    Environment env;
    env.fread = reinterpret_cast<decltype(env.fread)>(base_ + F::fread_rva);
    env.fseek = reinterpret_cast<decltype(env.fseek)>(base_ + F::fseek_rva);
    env.ftell = reinterpret_cast<decltype(env.ftell)>(base_ + F::ftell_rva);
    env.malloc = reinterpret_cast<decltype(env.malloc)>(base_ + F::malloc_rva);
    env.free = reinterpret_cast<decltype(env.free)>(base_ + F::free_rva);
    for (unsigned i = 0; i < 4; ++i) env.counters[i] = at<uint32_t>(F::counter_rva[i]);
    for (unsigned i = 0; i < 2; ++i) env.size_globals[i] = at<uint32_t>(F::size_global_rva[i]);
    // zlib1.dll's exports by name (no DLL hash); the reader's own import slots must hold the same functions.
    if (HMODULE zlib = GetModuleHandleW(L"zlib1.dll")) {
        env.inflateInit2_ = reinterpret_cast<decltype(env.inflateInit2_)>(GetProcAddress(zlib, "inflateInit2_"));
        env.inflate = reinterpret_cast<decltype(env.inflate)>(GetProcAddress(zlib, "inflate"));
        env.inflateEnd = reinterpret_cast<decltype(env.inflateEnd)>(GetProcAddress(zlib, "inflateEnd"));
    }
    if (!env.inflateInit2_ || !env.inflate || !env.inflateEnd) {
        status_ = "zlib_exports_missing";
        return false;
    }
    std::uint32_t slots[3]{};
    if (!read_self(base_ + F::inflate_init_slot_rva, &slots[0], 4) ||
        !read_self(base_ + F::inflate_slot_rva, &slots[1], 4) ||
        !read_self(base_ + F::inflate_end_slot_rva, &slots[2], 4) ||
        slots[0] != std::uint32_t(reinterpret_cast<std::uintptr_t>(env.inflateInit2_)) ||
        slots[1] != std::uint32_t(reinterpret_cast<std::uintptr_t>(env.inflate)) ||
        slots[2] != std::uint32_t(reinterpret_cast<std::uintptr_t>(env.inflateEnd))) {
        status_ = "import_mismatch";
        return false;
    }
    engine_patch::SiteSpec spec{"resource_read", base_ + F::reader_rva, {}, F::reader_length, 0, 0};
    std::memcpy(spec.expected, F::reader_bytes, F::reader_length);
    if (!engine_patch::claim(site_, spec)) {
        status_ = site_.status;
        return false;
    }
    auto undo = [](const char* reason) {
        core_bind(Environment{}, Mode::Native, nullptr, nullptr);
        const bool back = engine_patch::restore(site_) && engine_patch::verify_bytes(base_ + F::reader_rva,
                                                                                      F::reader_bytes, F::reader_length);
        status_ = back ? reason : "rollback_failed";
        return false;
    };
    if (fault() || !patched_bytes(base_ + F::reader_rva, site_.patched)) return undo("readback_failed");
    void** next = nullptr;
    void* stub = emit_stub(&next);
    void* bridge = stub && next ? emit_bridge(next) : nullptr;
    if (!bridge) return undo("stub_failed");
    if (!engine_patch::store_pointer(next, *site_.entry)) return undo("chain_write_failed");
    bridge_word_ = bridge;
    env_ = env;
    core_bind(env_, mode_, &bridge_word_, &verify_sink);
    if (fault() || !engine_patch::push_front(site_, stub)) return undo("chain_failed");
    next_slot_ = next;
    status_ = "active";
    return true;
}
bool install_pool() {
    if (!windows_match(W::pool_windows, W::pool_fixups)) {
        pool_status_ = "bytes_mismatch";
        return false;
    }
    char rb[3]{};
    if (!read_self(base_ + F::rb_string_rva, rb, 3) || rb[0] != 'r' || rb[1] != 'b' || rb[2] != 0) {
        pool_status_ = "mode_string_mismatch";
        return false;
    }
    PoolEnvironment pool;
    pool.fopen = reinterpret_cast<decltype(pool.fopen)>(base_ + F::fopen_rva);
    pool.fclose = reinterpret_cast<decltype(pool.fclose)>(base_ + F::fclose_rva);
    pool.flag_offset = F::file_flag_offset;
    pool.error_flag = F::file_error_flag;
    pool_bind(pool);
    // Close coverage first (every direct fclose), the pooled open last.
    unsigned done = 0;
    bool open_ok = false;
    const char* failed = "-";
    for (; done < F::close_site_count; ++done) {
        engine_patch::CallSite& s = close_sites_[done];
        const bool claimed = engine_patch::claim_call(s, base_ + F::close_site_rva[done], base_ + F::fclose_rva,
                                                      reinterpret_cast<void*>(&x3m_pool_fclose));
        if (!claimed || fault() || !patched_bytes(s.address, s.patched)) {
            failed = claimed ? "readback_failed" : s.status;
            window_ = W::pool_windows[5 + done].name;
            ++done; // this site may hold a write: roll it back too
            break;
        }
    }
    if (done == F::close_site_count && !std::strcmp(failed, "-")) {
        const bool claimed = engine_patch::claim_call(open_site_, base_ + F::fopen_site_rva, base_ + F::fopen_rva,
                                                      reinterpret_cast<void*>(&x3m_pool_fopen));
        open_ok = claimed && !fault() && patched_bytes(open_site_.address, open_site_.patched);
        if (!open_ok) {
            failed = claimed ? "readback_failed" : open_site_.status;
            window_ = "dat_open";
        }
    }
    if (open_ok) {
        close_live_ = F::close_site_count;
        pool_status_ = "ok";
        return true;
    }
    // Every write this transaction holds goes back, newest first, each read back as the original call.
    auto put_back = [](engine_patch::CallSite& s) {
        return !s.patched_in || (engine_patch::restore_call(s) && patched_bytes(s.address, s.original));
    };
    bool rolled_back = put_back(open_site_);
    for (unsigned k = done; k-- > 0;) rolled_back = put_back(close_sites_[k]) && rolled_back;
    close_live_ = 0;
    for (const engine_patch::CallSite& s : close_sites_) close_live_ += s.patched_in ? 1u : 0u;
    // Unbind only when nothing points at the pool any more: a site left redirected (rollback_failed) must still reach
    // the CRT through the bound pool (unknown handles pass through to fclose; an unbound pool_fclose closes nothing).
    if (rolled_back) pool_bind(PoolEnvironment{});
    pool_status_ = rolled_back ? failed : "rollback_failed";
    return false;
}
}
Mode mode() {
    return mode_;
}
bool installed() {
    return installed_;
}
const char* status() {
    return status_;
}
bool pool_requested() {
    return pool_requested_;
}
bool pool_installed() {
    return pool_installed_;
}
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) {
        SetLastError(error);
        return installed_ || pool_installed_;
    }
    initialized_ = true;
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart > 0 ? uint64_t(f.QuadPart) : 1;
    wchar_t setting[16]{};
    const DWORD length = x3m::config::get(L"X3M_RESOURCE_READ", setting, 16);
    if (length && length < 16) {
        if (!lstrcmpiW(setting, L"fast"))
            mode_ = Mode::Fast;
        else if (!lstrcmpiW(setting, L"verify"))
            mode_ = Mode::Verify;
    }
    wchar_t pool_setting[8]{};
    pool_requested_ = x3m::config::get(L"X3M_DAT_HANDLES", pool_setting, 8) == 1 && pool_setting[0] == L'1';
    if (mode_ == Mode::Native && !pool_requested_) {
        status_ = "disabled";
        SetLastError(error);
        return false;
    }
#ifdef X3M_RESOURCE_READER_FL_FIXTURE
    base_ = fixture_base_;
#else
    base_ = x3m::fl::module_base();
#endif
    const char* gate = !base_ ? "executable_mismatch" : !engine_patch::install_window_open() ? "late_claim" : nullptr;
    if (mode_ != Mode::Native) {
        window_ = "-";
        installed_ = gate ? (status_ = gate, false) : install_reader();
        log("resource_reader mode=%s installed=%u status=%s site=0x%08lx chained_after_probe=0 input=ecx scope=whole_extent_fread,word_xor,single_inflate,game_malloc,no_memset fallback=original window=%s arena_used=%u edition=fl",
            mode_name(mode_), unsigned(installed_), status_, static_cast<unsigned long>(base_ + F::reader_rva), window_,
            engine_patch::arena_used());
    }
    if (pool_requested_) {
        window_ = "-";
        pool_installed_ = gate ? (pool_status_ = gate, false) : install_pool();
        unsigned live = close_live_ + (open_site_.patched_in ? 1u : 0u);
        log("dat_handle_pool requested=1 installed=%u status=%s open_site=0x%08lx open_status=%s close_sites=%u live=%u window=%s entries=32 error_flag=0x%02x scope=catalogue_fopen_reuse edition=fl",
            unsigned(pool_installed_), pool_status_, static_cast<unsigned long>(base_ + F::fopen_site_rva),
            open_site_.status, F::close_site_count, live, window_, F::file_error_flag);
    }
    SetLastError(error);
    return installed_ || pool_installed_;
}
void report() {
    if (!installed_ && !pool_installed_) return;
    const DWORD error = GetLastError();
    const PreserveCpuState cpu;
    if (installed_) {
        const Statistics s = statistics();
        if (s.calls != last_report_calls) {
            last_report_calls = s.calls;
            log("resource_reader_metric cumulative=1 mode=%s calls=%llu handled=%llu fallbacks=%llu bytes_in=%llu bytes_out=%llu fast_us=%.3f fast_max_us=%.3f original_us=%.3f verify_files=%llu verify_equal=%llu verify_mismatched=%llu verify_original_null=%llu catalogue=%llu scrambled=%llu cursor_short=%llu "
                "fast_read_us=%.3f fast_scan_us=%.3f fast_alloc_us=%.3f fast_inflate_us=%.3f "
                "fallback_unopened=%llu fallback_gz_handle=%llu fallback_progress=%llu fallback_tell=%llu fallback_state=%llu fallback_length=%llu fallback_scratch=%llu fallback_short_read=%llu fallback_not_gzip=%llu fallback_method=%llu fallback_reserved=%llu fallback_header=%llu fallback_empty=%llu fallback_alloc=%llu fallback_init=%llu fallback_inflate=%llu fallback_size=%llu edition=fl",
                mode_name(mode_), s.calls, s.handled, s.fallbacks, s.bytes_in, s.bytes_out,
                double(s.ticks) * 1e6 / double(frequency), double(s.max_ticks) * 1e6 / double(frequency),
                double(s.original_ticks) * 1e6 / double(frequency), s.verify_files, s.verify_equal, s.verify_mismatched,
                s.verify_original_null, s.catalogue, s.scrambled, s.cursor_short,
                double(s.read_ticks) * 1e6 / double(frequency), double(s.scan_ticks) * 1e6 / double(frequency),
                double(s.alloc_ticks) * 1e6 / double(frequency), double(s.inflate_ticks) * 1e6 / double(frequency),
                s.reasons[1], s.reasons[2], s.reasons[3], s.reasons[4], s.reasons[5], s.reasons[6], s.reasons[7],
                s.reasons[8], s.reasons[9], s.reasons[10], s.reasons[11], s.reasons[12], s.reasons[13], s.reasons[14],
                s.reasons[15], s.reasons[16], s.reasons[17]);
        }
    }
    if (pool_installed_) {
        const PoolStatistics p = pool_statistics();
        if (p.opens != last_pool_opens) {
            last_pool_opens = p.opens;
            log("dat_handle_pool_metric cumulative=1 opens=%llu reused=%llu real_opens=%llu closes=%llu kept=%llu real_closes=%llu errors=%llu full=%llu held=%u edition=fl",
                p.opens, p.reused, p.real_opens, p.closes, p.kept, p.real_closes, p.errors, p.full, p.held);
        }
    }
    SetLastError(error);
}
}

namespace x3m::fl::resource_reader_fl {
namespace R = x3m::resource_reader;
const char* pool_status() {
    return R::pool_status_;
}
const char* window() {
    return R::window_;
}
#ifdef X3M_RESOURCE_READER_FL_FIXTURE
void fixture_bind(std::uintptr_t base) {
    R::fixture_base_ = base;
}
void fixture_reset() {
    if (R::site_.patched_in) x3m::engine_patch::restore(R::site_);
    x3m::engine_patch::restore_call(R::open_site_);
    for (auto& s : R::close_sites_) x3m::engine_patch::restore_call(s);
    R::pool_drain();
    R::site_ = x3m::engine_patch::Site{};
    R::open_site_ = x3m::engine_patch::CallSite{};
    for (auto& s : R::close_sites_) s = x3m::engine_patch::CallSite{};
    R::core_bind(R::Environment{}, R::Mode::Native, nullptr, nullptr);
    R::pool_bind(R::PoolEnvironment{});
    R::initialized_ = R::installed_ = R::pool_requested_ = R::pool_installed_ = false;
    R::mode_ = R::Mode::Native;
    R::status_ = R::pool_status_ = "disabled";
    R::window_ = "-";
    R::close_live_ = 0;
    R::next_slot_ = nullptr;
    R::bridge_word_ = nullptr;
    R::mismatch_lines = 0;
    fixture_fail_step = 0;
}
void fixture_mode(R::Mode mode) {
    R::mode_ = mode;
    R::core_bind(R::env_, mode, &R::bridge_word_, &R::verify_sink);
}
void** fixture_next_slot() {
    return R::next_slot_;
}
#endif
}
