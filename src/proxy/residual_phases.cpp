#include "residual_phases.h"
#include "residual_phase_sites.h"
#include "frame_phases.h"
#include "pass_phases.h"
#include "lean_stub.h"
#include "stamp_install.h"
#include "cpu_state.h"
#include "object_trace.h"
#include "telemetry.h"
#include "log_tiers.h"
#include "capture.h"
#include <atomic>

static_assert(sizeof(void*) == 4, "Reviewed x86 game ABI only");
namespace x3m::residual_phases {
std::atomic<bool> active{false};
namespace {
std::atomic<bool> installed{false};
bool initialized = false;
std::uint64_t frequency = 0;
std::uint64_t dropped = 0; // frames closed without a frame-phase sample (owner thread only)
detail::Gate gate;
detail::Accumulator accumulator;
detail::Window window;
detail::Sample last_sample;
engine_patch::Site patches[sites::Count];
// The sibling clocks this group pairs with, both written on the owner thread.
pass_phases::detail::Accumulator* pass_link = nullptr;
const frame_phases::detail::Tracker* frame_link = nullptr;
struct ErrorGuard {
    DWORD value = GetLastError();
    ~ErrorGuard() { SetLastError(value); }
};
void* emit(unsigned index, void*** next_out);
// The shared transaction (stamp_install.h): preflight every span, claim in
// order, roll back every patched site on the first failure, activate last.
bool install_group(const engine_patch::SiteSpec* specs, const char*& status) {
    return stamp::install_group(patches, specs, &emit, installed, status);
}
void emit_window() {
    detail::Summary s;
    if (!window.close(s)) return;
    // One clock read per window; see frame_phases::emit_window for qpc=.
    LARGE_INTEGER v{};
    const std::uint64_t emitted = QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0;
    log("residual_phases qpc=%llu frame=%llu frames=%u materials_p50=%llu particle_views_p50=%llu passes_p50=%llu views_p50=%llu prepare_p50_us=%llu prepare_p95_us=%llu setup_p50_us=%llu setup_p95_us=%llu prepare_per_pass_p50_ns=%llu setup_per_pass_p50_ns=%llu particles_p50_us=%llu particles_p95_us=%llu other_p50_us=%llu other_p95_us=%llu self_p50_us=%llu dispatch_cost_ns=%llu prepare_skipped=%llu setup_skipped=%llu view_skipped=%llu other_underflow=%llu clock_errors=%llu clock_failures=%llu unmatched=%llu dropped=%llu early=%u foreign=%u",
        emitted, s.frame, s.frames, s.materials_p50, s.particle_views_p50, s.passes_p50, s.views_p50, s.interval_p50[0],
        s.interval_p95[0], s.interval_p50[1], s.interval_p95[1], s.prepare_per_pass_p50, s.setup_per_pass_p50,
        s.interval_p50[2], s.interval_p95[2], s.interval_p50[3], s.interval_p95[3], s.self_p50,
        detail::dispatch_cost_ns, accumulator.prepare_skipped, accumulator.setup_skipped, accumulator.view_skipped,
        accumulator.other_underflow, accumulator.clock_errors, accumulator.clock_failures, accumulator.unmatched,
        dropped, gate.early.exchange(0, std::memory_order_relaxed),
        gate.foreign.exchange(0, std::memory_order_relaxed));
    log("residual_attribution frame=%llu frames=%u between_prepare_p50_us=%llu between_prepare_p95_us=%llu outside_setup_p50_us=%llu outside_setup_p95_us=%llu outside_materials=%llu scope_errors=%llu",
        s.frame, s.frames, s.interval_p50[4], s.interval_p95[4], s.interval_p50[5], s.interval_p95[5],
        accumulator.outside_materials, accumulator.scope_errors);
    accumulator.outside_materials = accumulator.scope_errors = 0;
    accumulator.prepare_skipped = accumulator.setup_skipped = accumulator.view_skipped = accumulator
                                                                                             .other_underflow = 0;
    accumulator.clock_errors = accumulator.clock_failures = accumulator.unmatched = 0;
    dropped = 0;
}
}
}

// The per-dispatch handler: called by the lean stub with flags, EAX/ECX/EDX
// and XMM0-7 already saved; it must preserve everything else, execute no x87
// opcode, never log and never allocate. The owner check is one relaxed load.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_residual_phase_enter(unsigned index) {
    using namespace x3m::residual_phases;
    if (!active.load(std::memory_order_relaxed)) return;
    x3m::LightCallBoundary cpu; // MXCSR + LastError: QueryPerformanceCounter may set the last error
    if (!gate.owned(GetCurrentThreadId())) return;
    LARGE_INTEGER v{};
    const std::uint64_t now = QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0;
    if (index == sites::MaterialSetup)
        accumulator.material(now, pass_link->end_clock, pass_link->begin_clock, pass_link->begin_armed,
                             frame_link->submission_ticks_at(now), pass_link->end_submission,
                             pass_link->begin_submission, frame_link->live && frame_link->submit_begin);
    else if (index == sites::ViewParticles)
        accumulator.view(now, frame_link->submit_end);
    else
        ++accumulator.unmatched;
}

namespace x3m::residual_phases {
namespace {
// The shared lean stub (lean_stub.cpp) entering x3m_residual_phase_enter.
void* emit(unsigned index, void*** next_out) {
    return lean_stub::emit(reinterpret_cast<const void*>(&x3m_residual_phase_enter), index, next_out);
}
}
bool initialize() {
    ErrorGuard error;
    if (initialized) return active.load(std::memory_order_acquire);
    initialized = true;
    const bool wanted = log_tier::draw_trace_flag(L"X3M_RESIDUAL_PHASES"); // X3M_RESIDUAL_PHASES=1 or X3M_DRAW_TRACE=1
                                                                           // (log_tiers.h)
    if (!wanted) return false;
    const char* status = "telemetry_off";
    if (telemetry::enabled()) {
        frequency = telemetry::frequency();
        pass_link = pass_phases::shared_accumulator();
        frame_link = frame_phases::shared_tracker();
        if (!frequency)
            status = "clock_unavailable";
        else if (!frame_phases::active.load(std::memory_order_acquire))
            status = "frame_phases_off"; // the frame boundary, the views phase and submit_end come from the frame group
        else if (!pass_phases::active.load(std::memory_order_acquire))
            status = "pass_phases_off"; // the pass count and the pass_end/pass_begin clocks come from the pass group
        else if (!object_trace::executable_verified())
            status = "executable_unverified";
        else
            install_group(sites::kSites, status);
    }
    active.store(installed.load(std::memory_order_acquire), std::memory_order_release);
    log("residual_phase_mode requested=1 enabled=%u status=%s sites=%u window=%u owner=present_thread dispatch_cost_ns=%llu qpc_frequency=%llu",
        unsigned(active.load()), status, sites::Count, detail::window_frames, detail::dispatch_cost_ns, frequency);
    for (unsigned i = 0; i < sites::Count; ++i)
        log("residual_phase_site index=%u address=%08lx length=%u patched=%u status=%s", i,
            static_cast<unsigned long>(sites::kSites[i].address), sites::kSites[i].length,
            unsigned(patches[i].patched_in), patches[i].status);
    return active.load(std::memory_order_acquire);
}
namespace detail {
void frame_impl(std::uint64_t frame, bool sampled, std::uint64_t views_us, std::uint64_t view_setup_us,
                std::uint64_t view_submit_us, std::uint32_t views) noexcept {
    ErrorGuard error;
    if (!gate.admit(GetCurrentThreadId())) return;
    if (!sampled) {
        accumulator.discard(pass_link->begin_armed);
        ++dropped;
        return;
    }
    accumulator.take(frame, frequency, views_us, view_setup_us, view_submit_us, views, pass_link->passes,
                     pass_link->begin_clock, pass_link->begin_armed, last_sample, pass_link->begin_submission);
    window.add(last_sample);
    if (window.full()) emit_window();
}
}
#ifdef X3M_GAME_PHASE_FIXTURE
bool fixture_install(const engine_patch::SiteSpec* specs, const char** status) {
    const char* text = "unset";
    LARGE_INTEGER f{};
    frequency = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
    pass_link = pass_phases::shared_accumulator();
    frame_link = frame_phases::shared_tracker();
    const bool okay = frequency && install_group(specs, text);
    if (!frequency) text = "clock_unavailable";
    active.store(installed.load(std::memory_order_acquire));
    gate.reset();
    accumulator = {};
    window.reset();
    last_sample = {};
    dropped = 0;
    if (status) *status = text;
    return okay;
}
bool fixture_uninstall() {
    active.store(false);
    installed.store(false, std::memory_order_release);
    return stamp::uninstall_group(patches);
}
void* fixture_emit(unsigned index, void*** next) {
    return emit(index, next);
}
bool fixture_last_sample(detail::Sample* out) {
    if (!out || gate.owner.load() != GetCurrentThreadId()) return false;
    *out = last_sample;
    return true;
}
const detail::Accumulator* fixture_accumulator() {
    return &accumulator;
}
const detail::Gate* fixture_gate() {
    return &gate;
}
std::uint64_t fixture_dropped() {
    return dropped;
}
#endif
}
