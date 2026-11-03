// FL edition of x3m::media_cue (src/proxy/media_cue.h; the ap target keeps src/proxy/media_cue.cpp): the default ID2
// video skip and the optional trace / negative cache / video witness on X3FL.exe's allocator
// (docs/reverse-engineering/fl/media-cue.md, "Port (2026-11-03)"; sites, register frame and gate encoding:
// media_cue_sites_fl.h). The value-only policy (media_cue_core.h: refuse_id2_video, NegativeCache, PendingStack,
// TraceRing, RateLimit, Window, VideoBlit) is AP's, unchanged.
//
//   base+0x0b4080  the allocator entry `push ebx; push esi; push edi; mov edi,edx` claimed (5 bytes, one lock
//                  cmpxchg8b) with the two-arm gate in front: REFUSE answers `xor eax,eax; ret` (ID2 with EDX 0 or 8,
//                  from any caller, before any API or state), PASS replays the displaced instructions at the game's
//                  ESP; with the trace or the cache on, a proceeded call's result is observed by return-address
//                  substitution (the allocator never reads its stack: media-cue.md section 4).
//
// Install (backend-load path, inside the engine_patch install window): X3FL.exe identity (fl::module_base() != 0),
// every media window of music_windows_fl_inc.h and the six wrapper call sites before anything is emitted or written;
// one claim, one chained gate, read back; a failure puts the bytes back (rollback_failed keeps the site registered).
// The FL loader has no unload path for this module (cull_small_parts_fl.cpp): no shutdown wiring. Per frame:
// frame() is one relaxed load unless the trace or the cache is on.
#include "../../proxy/media_cue.h"
#include "../../proxy/capture.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/log_tiers.h"
#include "../../proxy/telemetry.h"
#include "executable_identity_fl.h"
#include "media_cue_sites_fl.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace F = x3m::fl::media_cue_fl;
namespace W = x3m::fl::music_windows;
namespace x3m::media_cue {
std::atomic<bool> active{false};
namespace {
namespace engine_patch = x3m::engine_patch;
std::atomic<bool> installed{false};
bool initialized = false, trace_on = false, cache_on = false;
std::uint64_t frequency = 0, retry_ticks = 0;
constexpr unsigned default_retry_s = 30, max_retry_s = 3600;
std::uintptr_t base_ = 0;
detail::Addresses addresses{}; // only blit_begin / blit_end (the witness range) are used on FL
detail::Gate gate;
detail::NegativeCache cache;
detail::PendingStack pending;
detail::TraceRing ring;
detail::RateLimit limit, enter_limit;
detail::Window window;
detail::VideoBlit video;
std::atomic<std::uint64_t> video_foreign{0}, video_early{0};
std::uint32_t attempts_frame = 0;
std::uint64_t attempts_total = 0, refused_total = 0;
std::atomic<std::uint64_t> current_frame{0};
void* return_trampoline = nullptr;
bool lost_reported = false;
engine_patch::Site site_{};
const char* status_ = "unset";
const char* window_ = "-";
#ifdef X3M_MEDIA_CUE_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
#endif
struct ErrorGuard {
    DWORD value = GetLastError();
    ~ErrorGuard() { SetLastError(value); }
};
std::uint64_t qpc() noexcept {
    LARGE_INTEGER v{};
    return QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0;
}
std::uint64_t to_us(std::uint64_t ticks) noexcept {
    return frequency ? ticks * 1000000ull / frequency : 0;
}
void reset_state() {
    gate.reset();
    cache.clear();
    pending = detail::PendingStack{};
    ring = detail::TraceRing{};
    limit = {};
    enter_limit = {};
    window.reset();
    video = {};
    video_foreign.store(0, std::memory_order_relaxed);
    video_early.store(0, std::memory_order_relaxed);
    attempts_frame = 0;
    attempts_total = refused_total = 0;
    current_frame.store(0);
    lost_reported = false;
}
const char* result_text(const detail::Entry& e, char (&buffer)[16]) {
    if (e.outcome == detail::unobserved) return "unobserved";
    if (!e.result) return "0";
    std::snprintf(buffer, sizeof buffer, "0x%08lx", static_cast<unsigned long>(e.result));
    return buffer;
}
const char* kind_text(const detail::Entry& e, char (&buffer)[16]) {
    if (e.caller == detail::selector)
        std::snprintf(buffer, sizeof buffer, "0x%lx", static_cast<unsigned long>(e.kind));
    else
        std::snprintf(buffer, sizeof buffer, "none"); // FL: no stack word carries the kind (media-cue.md section 2)
    return buffer;
}
void write_enter_line(const detail::Entry& e) {
    const HANDLE handle = log_handle();
    if (handle == INVALID_HANDLE_VALUE || !handle) {
        ++enter_limit.suppressed;
        return;
    }
    x3m::call_preserved([&] {
        char kind[16];
        log("media_cue_enter frame=%llu qpc=%llu id=%lu kind=%s caller=%s flags=0x%lx attempt=%lu edition=fl", e.frame,
            e.qpc, static_cast<unsigned long>(e.id), kind_text(e, kind), detail::caller_names[e.caller],
            static_cast<unsigned long>(e.flags), static_cast<unsigned long>(e.attempt));
    });
}
void write_video_line(const ownership::SurfaceLockEvent& e, const char* stage) {
    const HANDLE handle = log_handle();
    if (handle == INVALID_HANDLE_VALUE || !handle) {
        ++video.suppressed;
        return;
    }
    x3m::call_preserved([&] {
        D3DSURFACE_DESC desc{};
        if (!e.native || FAILED(e.native->GetDesc(&desc))) desc = D3DSURFACE_DESC{};
        char result[16];
        if (e.phase == ownership::SurfaceLockPhase::LockEnter || e.phase == ownership::SurfaceLockPhase::UnlockEnter)
            std::snprintf(result, sizeof result, "pending");
        else
            std::snprintf(result, sizeof result, "0x%08lx", static_cast<unsigned long>(e.result));
        log("media_video_blit frame=%llu qpc=%llu texture=%p width=%u height=%u format=%u flags=0x%lx result=%s stage=%s blits=%llu unlocks=%llu edition=fl",
            current_frame.load(std::memory_order_relaxed), qpc(), static_cast<void*>(e.surface), unsigned(desc.Width),
            unsigned(desc.Height), unsigned(desc.Format), static_cast<unsigned long>(e.flags), result, stage,
            video.locks_total, video.unlocks);
    });
}
void video_lock_observe(const ownership::SurfaceLockEvent& e) {
    if (!active.load(std::memory_order_relaxed)) return;
    if (!detail::video_blit_caller(std::uint32_t(reinterpret_cast<std::uintptr_t>(e.return_address)), addresses))
        return;
    const DWORD owner = gate.owner.load(std::memory_order_relaxed);
    if (owner != GetCurrentThreadId()) {
        if (e.phase == ownership::SurfaceLockPhase::LockEnter || e.phase == ownership::SurfaceLockPhase::UnlockEnter)
            (owner ? video_foreign : video_early).fetch_add(1, std::memory_order_relaxed);
        return;
    }
    switch (e.phase) {
    case ownership::SurfaceLockPhase::LockEnter:
        if (video.lock_enter()) write_video_line(e, "lock_enter");
        break;
    case ownership::SurfaceLockPhase::LockResult:
        if (video.lock_result(FAILED(e.result))) write_video_line(e, "lock");
        break;
    case ownership::SurfaceLockPhase::UnlockEnter:
        if (video.unlock_enter()) write_video_line(e, "unlock_enter");
        break;
    case ownership::SurfaceLockPhase::UnlockResult:
        if (video.unlock_result(FAILED(e.result))) write_video_line(e, "unlock");
        break;
    }
}
void emit_entry(const detail::Entry& e) {
    char result[16];
    char kind[16];
    kind_text(e, kind);
    log("media_cue frame=%llu qpc=%llu id=%lu kind=%s caller=%s flags=0x%lx result=%s us=%llu attempts_frame=%lu cached=%u edition=fl",
        e.frame, e.qpc, static_cast<unsigned long>(e.id), kind, detail::caller_names[e.caller],
        static_cast<unsigned long>(e.flags), result_text(e, result),
        e.outcome == detail::observed ? to_us(e.duration_ticks) : 0ull, static_cast<unsigned long>(e.attempt),
        unsigned(e.outcome == detail::refused));
}
void emit_window() {
    detail::Summary s;
    if (!window.close(s)) return;
    char ids[8 * 24 + 8];
    unsigned used = 0;
    ids[0] = 0;
    for (unsigned i = 0; i < s.id_count; ++i) {
        const int n = std::snprintf(ids + used, sizeof ids - used, "%s%lu:%lu", i ? "," : "",
                                    static_cast<unsigned long>(s.ids[i].id),
                                    static_cast<unsigned long>(s.ids[i].count));
        if (n <= 0 || unsigned(n) >= sizeof ids - used) break;
        used += unsigned(n);
    }
    if (!log_tier::perf_rows() && !s.failures && !video.failures) {
        gate.early.exchange(0, std::memory_order_relaxed);
        gate.foreign.exchange(0, std::memory_order_relaxed);
        video_foreign.exchange(0, std::memory_order_relaxed);
        video_early.exchange(0, std::memory_order_relaxed);
    } else
        log("media_cue_window qpc=%llu frame=%llu frames=%u attempts=%llu failures=%llu successes=%llu refused=%llu unobserved=%llu attempts_frame_p50=%llu attempts_frame_max=%lu ids=%s id_overflow=%llu cache_used=%u evictions=%llu depth_max=%u overflow=%llu stale=%llu mismatched=%llu lost=%llu suppressed=%llu enter_suppressed=%llu dropped=%llu early=%u foreign=%u video_blits=%llu video_unlocks=%llu video_failures=%llu video_suppressed=%llu video_reentries=%llu video_foreign=%llu video_early=%llu edition=fl",
            qpc(), s.frame, s.frames, s.attempts, s.failures, s.successes, s.refused, s.unobserved,
            s.attempts_frame_p50, static_cast<unsigned long>(s.attempts_frame_max), s.id_count ? ids : "none",
            s.id_overflow, cache.used, cache.evictions, pending.max_depth, pending.overflow, pending.stale,
            pending.mismatched, pending.lost, limit.suppressed, enter_limit.suppressed, ring.dropped,
            gate.early.exchange(0, std::memory_order_relaxed), gate.foreign.exchange(0, std::memory_order_relaxed),
            video.locks, video.unlocks, video.failures, video.suppressed, video.reentries,
            video_foreign.exchange(0, std::memory_order_relaxed), video_early.exchange(0, std::memory_order_relaxed));
    limit.suppressed = 0;
    enter_limit.suppressed = 0;
    ring.dropped = 0;
    video.close();
    if (pending.lost && !lost_reported) {
        lost_reported = true;
        log("media_cue_lost count=%llu last_return=%08lx note=return_matched_no_pending_entry_fail_safe_used edition=fl",
            pending.lost, static_cast<unsigned long>(pending.last_return));
    }
}
}
}

// The FL gate handler: ECX = id and EDX = constructor flags from the saved frame; 1 = PASS, 0 = REFUSE. The ID2 rule
// precedes every API, owner admission and diagnostic state (foreign and reentrant entries included).
extern "C" __attribute__((force_align_arg_pointer)) unsigned __cdecl x3m_media_cue_fl_enter(
    x3m::fl::media_cue_fl::EnterFrame* f) {
    using namespace x3m::media_cue;
    if (detail::refuse_id2_video(f->ecx, f->edx)) return 0;
    if (!active.load(std::memory_order_relaxed)) return 1;
    x3m::LightCallBoundary cpu;
    if (!gate.owned(GetCurrentThreadId())) return 1;
    const detail::Caller caller = F::classify(f->ret, f->caller, base_);
    const bool scoped = caller == detail::selector; // the proven selector supplies kind 0x5a (media-cue.md section 2)
    const std::uint64_t now = qpc();
    ++attempts_frame;
    ++attempts_total;
    detail::Entry e;
    e.qpc = now;
    e.frame = current_frame.load(std::memory_order_relaxed);
    e.id = f->ecx;
    e.kind = F::kind_of(caller);
    e.flags = f->edx;
    e.attempt = attempts_frame;
    e.caller = caller;
    e.scoped = scoped;
    if (cache_on && scoped && cache.refuses(f->ecx, now, retry_ticks)) {
        ++refused_total;
        e.outcome = detail::refused;
        ring.push(e);
        return 0;
    }
    detail::Pending p;
    p.ret = f->ret;
    p.esp = std::uint32_t(reinterpret_cast<std::uintptr_t>(&f->ret));
    p.id = f->ecx;
    p.kind = e.kind;
    p.flags = f->edx;
    p.attempt = attempts_frame;
    p.qpc = now;
    p.frame = e.frame;
    p.caller = caller;
    p.scoped = scoped;
    if (pending.push(p))
        f->ret = std::uint32_t(reinterpret_cast<std::uintptr_t>(return_trampoline));
    else
        ring.push(e);
    if (trace_on && enter_limit.admit(now, frequency)) write_enter_line(e);
    return 1;
}
// The return handler: EAX is the allocator's result; returns the original return address for the slot. FL keeps no
// stack id to cross-check (the slot address pairs the entry).
extern "C" __attribute__((force_align_arg_pointer)) std::uint32_t __cdecl x3m_media_cue_fl_return(
    x3m::fl::media_cue_fl::ReturnFrame* f) {
    using namespace x3m::media_cue;
    x3m::LightCallBoundary cpu;
    const std::uint64_t now = qpc();
    detail::Pending p;
    if (!pending.pop(std::uint32_t(reinterpret_cast<std::uintptr_t>(&f->slot)), p)) return pending.last_return;
    if (f->eax)
        cache.success(p.id);
    else if (cache_on && p.scoped)
        cache.fail(p.id, now);
    detail::Entry e;
    e.qpc = p.qpc;
    e.frame = p.frame;
    e.duration_ticks = now >= p.qpc ? now - p.qpc : 0;
    e.id = p.id;
    e.kind = p.kind;
    e.flags = p.flags;
    e.result = f->eax;
    e.attempt = p.attempt;
    e.caller = p.caller;
    e.outcome = detail::observed;
    e.scoped = p.scoped;
    ring.push(e);
    return p.ret;
}

namespace x3m::media_cue {
namespace {
bool read_code(std::uintptr_t at, unsigned char* out, unsigned n) {
    return engine_patch::read_code(at, out, n);
}
// The six wrapper call sites and the allocator's one call: rel32 targets, read live.
bool callers_match(std::uintptr_t base) {
    unsigned char code[5];
    for (const F::WrapperCaller& c : F::wrapper_callers)
        if (!read_code(base + c.call_rva, code, 5) || c.return_rva != c.call_rva + 5 ||
            !W::calls(code, std::uint32_t(base + c.call_rva), std::uint32_t(base + F::wrapper_rva)))
            return false;
    return read_code(base + F::wrapper_call_rva, code, 5) &&
           W::calls(code, std::uint32_t(base + F::wrapper_call_rva), std::uint32_t(base + F::allocator_rva));
}
// Emits the gate (4-aligned block) and returns its start; *next the continuation slot.
void* emit_gate(void*** next_out) {
    engine_patch::Emitter e(F::gate_length + 4);
    if (!e.ok()) return nullptr;
    while (e.ok() && (reinterpret_cast<std::uintptr_t>(e.here()) & 3)) e.byte(0xcc);
    const std::uint32_t at = std::uint32_t(reinterpret_cast<std::uintptr_t>(e.here()));
    unsigned char code[F::gate_length];
    if (F::encode_gate(at, std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_media_cue_fl_enter)),
                       std::uint32_t(reinterpret_cast<std::uintptr_t>(&x3m_media_cue_fl_return)), code) != F::gate_length)
        return nullptr;
    e.bytes(code, F::gate_length);
    if (!e.finish()) return nullptr;
    *next_out = reinterpret_cast<void**>(std::uintptr_t(at + F::gate_next));
    return_trampoline = reinterpret_cast<void*>(std::uintptr_t(at + F::gate_trampoline));
    return reinterpret_cast<void*>(std::uintptr_t(at));
}
// Windows and callers, then one claim and one chained gate, read back; a failure restores the bytes.
bool install(std::uintptr_t base) {
    installed.store(false, std::memory_order_release);
    return_trampoline = nullptr;
    reset_state();
    if (!engine_patch::install_window_open()) {
        status_ = "late_claim";
        return false;
    }
    if (const char* bad = W::first_mismatch(read_code, base, W::media_windows, W::media_fixups)) {
        status_ = "bytes_mismatch";
        window_ = bad;
        return false;
    }
    if (!callers_match(base)) {
        status_ = "callers_mismatch";
        return false;
    }
    engine_patch::SiteSpec spec{};
    spec.name = "media_create_enter_fl";
    spec.address = base + F::allocator_rva;
    spec.length = F::entry_length;
    std::memcpy(spec.expected, F::entry_bytes, F::entry_length);
    void** next = nullptr;
    void* stub = emit_gate(&next);
    if (!stub || !next) {
        status_ = "arena_full";
        return false;
    }
    site_ = engine_patch::Site{};
    if (!engine_patch::claim(site_, spec)) {
        status_ = site_.status;
        return false;
    }
    unsigned char now[5]{};
    const char* failure = nullptr;
    if (!engine_patch::store_pointer(next, *site_.entry) || !engine_patch::push_front(site_, stub))
        failure = "chain_failed";
    else if (!read_code(spec.address, now, 5) || std::memcmp(now, site_.patched, 5) || *site_.entry != stub)
        failure = "readback_failed";
    if (failure) {
        engine_patch::restore(site_);
        status_ = site_.patched_in ? "rollback_failed" : failure;
        return false;
    }
    status_ = "ok";
    installed.store(true, std::memory_order_release);
    return true;
}
}
ownership::SurfaceLockObserver video_lock_observer() noexcept {
    return trace_on && active.load(std::memory_order_acquire) ? &video_lock_observe : nullptr;
}
bool initialize() {
    ErrorGuard error;
    if (initialized) return installed.load(std::memory_order_acquire);
    initialized = true;
    wchar_t value[16]{};
    const bool trace_wanted = log_tier::debug_flag(L"X3M_MEDIA_CUE_TRACE");
    const bool cache_wanted = x3m::config::get(L"X3M_MEDIA_CUE_CACHE", value, 4) == 1 && value[0] == L'1';
    unsigned retry_s = default_retry_s;
    if (x3m::config::get(L"X3M_MEDIA_CUE_RETRY_S", value, 16) > 0) {
        const unsigned long n = wcstoul(value, nullptr, 10);
        if (n >= 1 && n <= max_retry_s) retry_s = unsigned(n);
    }
    trace_on = trace_wanted && telemetry::enabled();
    cache_on = cache_wanted;
    if (trace_on || cache_on) {
        LARGE_INTEGER f{};
        frequency = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
        retry_ticks = frequency * retry_s;
        if (!frequency) trace_on = cache_on = false;
    }
#ifdef X3M_MEDIA_CUE_FL_FIXTURE
    base_ = fixture_base_ ? fixture_base_ : x3m::fl::module_base();
#else
    base_ = x3m::fl::module_base();
#endif
    addresses = detail::Addresses{};
    if (base_) {
        addresses.blit_begin = std::uint32_t(base_ + F::video_begin_rva);
        addresses.blit_end = std::uint32_t(base_ + F::video_end_rva);
    }
    if (!base_)
        status_ = "executable_mismatch";
    else
        install(base_);
    active.store(installed.load(std::memory_order_acquire) && (trace_on || cache_on), std::memory_order_release);
    log("media_cue_mode trace_requested=%u cache_requested=%u trace=%u cache=%u enabled=%u id2_video_skip=%u status=%s retry_s=%u cache_entries=%u pending_depth=%u ring=%u lines_per_second=%u window=%u owner=present_thread sector_change=interval_only qpc_frequency=%llu return_trampoline=%08lx edition=fl",
        unsigned(trace_wanted), unsigned(cache_wanted), unsigned(trace_on && active.load()),
        unsigned(cache_on && active.load()), unsigned(active.load()), unsigned(installed.load()), status_, retry_s,
        detail::cache_entries, detail::pending_depth, detail::ring_entries, detail::lines_per_second,
        detail::window_frames, frequency, reinterpret_cast<unsigned long>(return_trampoline));
    log("media_cue_site index=0 address=%08lx rva=%06lx length=%u patched=%u write=%s status=%s window=%s edition=fl",
        static_cast<unsigned long>(base_ ? base_ + F::allocator_rva : 0), static_cast<unsigned long>(F::allocator_rva),
        F::entry_length, unsigned(site_.patched_in), site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none",
        site_.patched_in ? site_.status : status_, window_);
    return installed.load(std::memory_order_acquire);
}
namespace detail {
void frame_impl(std::uint64_t frame) noexcept {
    ErrorGuard error;
    if (!gate.admit(GetCurrentThreadId())) return;
    current_frame.store(frame, std::memory_order_relaxed);
    window.frame(frame, attempts_frame);
    attempts_frame = 0;
    Entry e;
    while (ring.pop(e)) {
        window.entry(e);
        if (trace_on && limit.admit(e.qpc, frequency)) emit_entry(e);
    }
    if (window.full()) emit_window();
}
}
#ifdef X3M_MEDIA_CUE_FL_FIXTURE
// The fixture's image base (0: module_base) and a fresh initialize() (the window state is the caller's).
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_reinitialize() {
    initialized = false;
    installed.store(false);
    active.store(false);
    site_ = engine_patch::Site{};
    status_ = "unset";
    window_ = "-";
}
bool fixture_uninstall() {
    active.store(false);
    installed.store(false);
    return engine_patch::restore(site_);
}
const char* fixture_status() {
    return status_;
}
const char* fixture_window() {
    return window_;
}
const engine_patch::Site* fixture_site() {
    return &site_;
}
void* fixture_trampoline() {
    return return_trampoline;
}
const detail::NegativeCache* fixture_cache() {
    return &cache;
}
const detail::PendingStack* fixture_pending() {
    return &pending;
}
bool fixture_pop_trace(detail::Entry* out) {
    if (!out || gate.owner.load() != GetCurrentThreadId()) return false;
    detail::Entry e;
    if (!ring.pop(e)) return false;
    *out = e;
    return true;
}
void fixture_set_modes(bool trace, bool cache_enabled, std::uint64_t ticks) {
    LARGE_INTEGER f{};
    frequency = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 1;
    trace_on = trace;
    cache_on = cache_enabled;
    retry_ticks = ticks;
    active.store(installed.load() && (trace_on || cache_on));
}
#endif
}
