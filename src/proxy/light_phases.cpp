#include "light_phases.h"
#include "light_phase_sites.h"
#include "frame_phases.h"
#include "lean_stub.h"
#include "stamp_install.h"
#include "cpu_state.h"
#include "object_trace.h"
#include "telemetry.h"
#include "log_tiers.h"
#include "capture.h"
static_assert(sizeof(void*) == 4, "R7 requires the qualified x86 ABI");
namespace x3m::light_phases {
std::atomic<bool> active{false};
namespace {
std::atomic<bool> installed{false};
bool initialized = false;
std::uint64_t frequency = 0, dropped = 0;
detail::Gate gate;
detail::Accumulator accumulator;
detail::Window window;
engine_patch::Site patches[sites::Count];
// Owner-only, also protects the interrupted handler from asynchronous reentry.
volatile bool dispatching = false;
#ifdef X3M_GAME_PHASE_FIXTURE
std::uint32_t test_cockpit = 0, test_traversal = 0;
#endif
struct ErrorGuard {
    DWORD value = GetLastError();
    ~ErrorGuard() { SetLastError(value); }
};
void* emit(unsigned, void***);
bool install_group(const engine_patch::SiteSpec* specs, const char*& status) {
    return stamp::install_group(patches, specs, &emit, installed, status);
}
void emit_window() {
    detail::Summary s;
    if (!window.close(s)) return;
    LARGE_INTEGER q{};
    const auto now = QueryPerformanceCounter(&q) && q.QuadPart > 0 ? std::uint64_t(q.QuadPart) : 0;
    const auto& e = accumulator.errors;
    log("light_phases qpc=%llu frame=%llu frames=%u valid_frames=%u invalid_frames=%u stamps_p50=%llu stamps_p95=%llu self_p50_us=%llu self_p95_us=%llu dispatch_cost_ns=%llu self_calibrated=%u "
        "cockpit_entries=%llu cockpit_calls=%llu cockpit_ticks=%llu cockpit_calls_p50=%llu cockpit_calls_p95=%llu cockpit_p50_us=%llu cockpit_p95_us=%llu "
        "traversal_entries=%llu traversal_calls=%llu traversal_ticks=%llu traversal_calls_p50=%llu traversal_calls_p95=%llu traversal_p50_us=%llu traversal_p95_us=%llu "
        "unknown_entries=%llu unknown_calls=%llu unknown_ticks=%llu unknown_calls_p50=%llu unknown_calls_p95=%llu unknown_p50_us=%llu unknown_p95_us=%llu "
        "nested=%llu overflow=%llu mismatch=%llu unmatched=%llu clock_failures=%llu clock_reversal=%llu reentry=%llu mode_refused=%llu dropped=%llu early=%u foreign=%u",
        now, s.frame, s.frames, s.valid_frames, s.invalid_frames, s.stamps_p50, s.stamps_p95, s.self_p50, s.self_p95,
        detail::dispatch_cost_ns, unsigned(detail::dispatch_cost_ns != 0), s.entries[0], s.calls[0], s.ticks[0],
        s.calls_p50[0], s.calls_p95[0], s.us_p50[0], s.us_p95[0], s.entries[1], s.calls[1], s.ticks[1], s.calls_p50[1],
        s.calls_p95[1], s.us_p50[1], s.us_p95[1], s.entries[2], s.calls[2], s.ticks[2], s.calls_p50[2], s.calls_p95[2],
        s.us_p50[2], s.us_p95[2], e.nested, e.overflow, e.mismatch, e.unmatched, e.clock_failures, e.clock_reversal,
        e.reentry, e.mode_refused, dropped, gate.early.exchange(0, std::memory_order_relaxed),
        gate.foreign.exchange(0, std::memory_order_relaxed));
    accumulator.errors = {};
    dropped = 0;
}
}
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_light_phase_enter(
    unsigned index, const std::uint32_t* saved) noexcept {
    using namespace x3m::light_phases;
    if (!active.load(std::memory_order_relaxed)) return;
    // Read-only mode observation comes before any FP control write. FEX
    // shares host rounding between x87/SSE; even restoring unchanged MXCSR
    // can change later native x87 arithmetic when the two RC fields differ.
    std::uint16_t cw = 0;
    std::uint32_t mxcsr = 0;
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(cw), "=m"(mxcsr)::"memory");
    ErrorGuard error;
    if (!gate.owned(GetCurrentThreadId())) return;
    if (((cw >> 10) & 3) != ((mxcsr >> 13) & 3)) {
        accumulator.refuse_mode();
        return;
    }
    x3m::LightCallBoundary cpu;
    if (dispatching) {
        accumulator.interrupted();
        return;
    }
    dispatching = true;
    const auto token = saved[index == sites::Enter ? x3m::lean_stub::SavedEsp : x3m::lean_stub::SavedEbp];
    // Entry ESP_saved=E-4. Only read the live return word at E, never node.
    std::uint32_t caller = index == sites::Enter ? *reinterpret_cast<const std::uint32_t*>(token + 4) : 0;
#ifdef X3M_GAME_PHASE_FIXTURE
    if (caller == test_cockpit)
        caller = detail::cockpit_return;
    else if (caller == test_traversal)
        caller = detail::traversal_return;
#endif
    LARGE_INTEGER q{};
    const auto now = QueryPerformanceCounter(&q) && q.QuadPart > 0 ? std::uint64_t(q.QuadPart) : 0;
    // An interrupted dispatch poisons the frame; stamp can only count, never
    // publish elapsed from that frame until the boundary resets its state.
    accumulator.stamp(index, token, caller, now);
    dispatching = false;
}
namespace x3m::light_phases {
namespace {
void* emit(unsigned index, void*** next) {
    return lean_stub::emit_context(reinterpret_cast<const void*>(&x3m_light_phase_enter), index, next);
}
}
bool initialize() {
    ErrorGuard error;
    if (initialized) return active.load(std::memory_order_acquire);
    initialized = true;
    if (!log_tier::draw_trace_flag(L"X3M_LIGHT_PHASES"))
        return false; // X3M_LIGHT_PHASES=1 or X3M_DRAW_TRACE=1 (log_tiers.h)
    const char* status = "telemetry_off";
    if (telemetry::enabled()) {
        frequency = telemetry::frequency();
        if (!frequency)
            status = "clock_unavailable";
        else if (!frame_phases::active.load(std::memory_order_acquire))
            status = "frame_phases_off";
        else if (!object_trace::executable_verified())
            status = "executable_unverified";
        else
            install_group(sites::kSites, status);
    }
    active.store(installed.load(std::memory_order_acquire), std::memory_order_release);
    log("light_phase_mode requested=1 enabled=%u status=%s sites=%u window=%u dispatch_cost_ns=%llu qpc_frequency=%llu",
        unsigned(active.load()), status, sites::Count, detail::window_frames, detail::dispatch_cost_ns, frequency);
    for (unsigned i = 0; i < sites::Count; ++i)
        log("light_phase_site index=%u address=%08lx length=%u patched=%u status=%s", i,
            static_cast<unsigned long>(sites::kSites[i].address), sites::kSites[i].length,
            unsigned(patches[i].patched_in), patches[i].status);
    return active.load(std::memory_order_acquire);
}
namespace detail {
void frame_impl(std::uint64_t frame, bool sampled) noexcept {
    ErrorGuard error;
    if (!gate.admit(GetCurrentThreadId())) return;
    if (!sampled) {
        accumulator.discard();
        ++dropped;
        return;
    }
    Sample sample;
    accumulator.take(frame, frequency, sample);
    window.add(sample);
    if (window.full()) emit_window();
}
}
#ifdef X3M_GAME_PHASE_FIXTURE
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status) {
    LARGE_INTEGER f{};
    frequency = QueryPerformanceFrequency(&f) ? std::uint64_t(f.QuadPart) : 0;
    const char* text = "clock_unavailable";
    const bool okay = frequency && install_group(specs, text);
    active.store(installed.load());
    gate.reset();
    accumulator = {};
    window.reset();
    dropped = 0;
    dispatching = false;
    if (status) *status = text;
    return okay;
}
bool fixture_uninstall() {
    active.store(false);
    installed.store(false);
    return stamp::uninstall_group(patches);
}
const detail::Accumulator* fixture_accumulator() {
    return &accumulator;
}
const detail::Gate* fixture_gate() {
    return &gate;
}
void fixture_returns(std::uint32_t cockpit, std::uint32_t traversal) {
    test_cockpit = cockpit;
    test_traversal = traversal;
}
#endif
}
