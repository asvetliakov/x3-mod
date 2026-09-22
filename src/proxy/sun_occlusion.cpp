#include "sun_occlusion.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include <windows.h>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::sun_occlusion;
namespace engine_patch = x3m::engine_patch;
bool patched_ = false, override_ = false, log_ = false;
const char* state_ = "disabled";
const char* block_reason_ = "";
engine_patch::CallSite probe_site_{}, lens_site_{};
Addresses addresses_{};
void (*listener_begin_)() = nullptr;
void (*listener_end_)() = nullptr;
volatile std::uint32_t frame_ = 0;       // written by present() only
volatile unsigned long long device_frame_ = 0; // the capture layer's number of the frame now being built (log lines only)
volatile LONG owner_thread_ = 0;         // the first thread through the probe thunk
volatile LONG foreign_thread_ = 0;       // counted by the foreign threads themselves
volatile LONG reset_requested_ = 0;      // device Reset: consumed by the owner at its next probe or bracket
// Owner thread only from here on.
core::Ready ready_;
core::Latch latch_;
Counters counters_{};
std::uint32_t seen_frame_ = 0;
std::uintptr_t main_view_ = 0;
bool multi_counted_ = false, frame_started_ = false, foreign_logged_ = false;

bool bytes_match(std::uintptr_t at, const unsigned char* expected, unsigned length) {
    unsigned char actual[32]{};
    return length <= sizeof actual && engine_patch::read_code(at, actual, length) && !std::memcmp(actual, expected, length);
}
bool gates_match() {
    static unsigned char body[core::probe_gates_length];
    return engine_patch::read_code(core::probe_gates_va, body, sizeof body) && !std::memcmp(body, core::probe_entry, sizeof core::probe_entry) &&
           core::fnv1a(body, sizeof body) == core::probe_gates_fnv1a;
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE && module != nullptr;
}
bool owner_thread() {
    const LONG thread = static_cast<LONG>(GetCurrentThreadId()); // no LastError, no x87
    LONG owner = owner_thread_;
    if (owner == 0) owner = InterlockedCompareExchange(&owner_thread_, thread, 0) == 0 ? thread : owner_thread_;
    return owner == thread;
}
// Once per frame, at the frame's first probe: the state machine advances (twice over a gap, so a
// frame without any probe leaves no "previous frame" behind) and the main view is resolved.
void begin_frame(std::uint32_t frame) {
    if (InterlockedExchange(&reset_requested_, 0) != 0) { ready_.reset(); block_reason_ = ""; }
    ready_.begin_frame();
    if (frame_started_ && frame - seen_frame_ > 1u) ready_.begin_frame();
    seen_frame_ = frame; frame_started_ = true; latch_ = core::Latch{}; multi_counted_ = false;
    if (addresses_.main_view) main_view_ = *addresses_.main_view;
    else { auto read = [](std::uintptr_t a, void* out, std::size_t n) { return x3m::engine_memory::read(a, out, n); }; main_view_ = core::main_view(read); }
}
inline std::uint32_t word(const std::uint32_t* base, unsigned offset) { return base[offset / 4]; }
}

namespace x3m::sun_occlusion::detail { volatile bool bracket_open = false; }

extern "C" {
std::uint32_t x3m_sun_probe_target = 0, x3m_sun_lens_target = 0;

namespace {
int decide_probe(const std::uint32_t* record, const std::uint32_t* view);
}
// The thread's last error comes back exactly as the engine left it on every path: the frame's first
// probe resolves the main view through engine_memory (VirtualQuery, a tick read), and the log mode
// runs the original probe and the formatter.
int __cdecl x3m_sun_probe_decide(const std::uint32_t* record, const std::uint32_t* view) {
    const DWORD error = GetLastError();
    const int decision = decide_probe(record, view);
    SetLastError(error);
    return decision;
}
}
namespace {
int decide_probe(const std::uint32_t* record, const std::uint32_t* view) {
    if (!owner_thread()) { InterlockedIncrement(&foreign_thread_); return core::Original; }
    ++counters_.probes;
    if (!record || !view) { ++counters_.original; return core::Original; }
    const std::uint32_t frame = frame_;
    if (!frame_started_ || frame != seen_frame_) begin_frame(frame);
    core::ProbeInputs in;
    in.record_owner = word(record, core::record_view); in.view = reinterpret_cast<std::uintptr_t>(view); in.main_view = main_view_;
    in.x = std::int32_t(word(record, core::record_x)); in.y = std::int32_t(word(record, core::record_y));
    in.view_flags = word(view, core::view_flags);
    in.top = std::int32_t(word(view, core::view_rect_top)); in.bottom = std::int32_t(word(view, core::view_rect_bottom));
    in.left = std::int32_t(word(view, core::view_rect_left)); in.right = std::int32_t(word(view, core::view_rect_right));
    // The owner's flags decide eligibility (core::eligible): read through engine_memory (validated, cached per
    // region and frame) only where the cheap tests already hold, so main-view-owned records and other views'
    // probes cost no read. The layer is for the log only.
    std::uint32_t owner_flags = 0; std::int32_t owner_layer = -1;
    if (main_view_ != 0 && in.view == main_view_ && in.record_owner != 0 && in.record_owner != in.view && !(in.record_owner & 3)) {
        if (!x3m::engine_memory::read(in.record_owner + core::view_flags, &owner_flags, 4)) owner_flags = 0;
        if (log_ && !x3m::engine_memory::read(in.record_owner + 0x29c, &owner_layer, 4)) owner_layer = -1;
    }
    in.owner_flags = owner_flags;
    const bool own = core::eligible(in.record_owner, in.view, main_view_, owner_flags);
    if (own) {
        ++counters_.own;
        in.ready = ready_.probe(reinterpret_cast<std::uintptr_t>(record), override_);
        if (ready_.records == 1) {
            latch_.record = reinterpret_cast<std::uintptr_t>(record); latch_.x = in.x; latch_.y = in.y;
            latch_.size = std::int32_t(word(record, core::record_size)); latch_.accumulator = std::int32_t(word(record, core::record_accumulator));
            latch_.top = in.top; latch_.bottom = in.bottom; latch_.left = in.left; latch_.right = in.right;
            latch_.fov = word(view, core::view_fov);
            latch_.owner = in.record_owner; latch_.owner_flags = owner_flags; latch_.owner_layer = owner_layer;
            std::int32_t scale = std::int32_t(word(view, core::view_scale_x));
            if (scale <= 0) { // the engine's fallback pair: *0x00606f38 + 0x28
                std::uint32_t mode = 0, value = 0;
                if (!addresses_.main_view && x3m::engine_memory::read(core::mode_global_va, &mode, 4) && mode && !(mode & 3) && x3m::engine_memory::read(mode + 0x28, &value, 4)) scale = std::int32_t(value);
            }
            latch_.scale_x = scale; latch_.valid = true;
        } else {
            latch_.valid = false; // more than one background-owned sun probed by the main view: vanilla
            if (!multi_counted_) { multi_counted_ = true; ++counters_.multi_record_frames; }
        }
        if (in.ready) { // gate 1's word, read as the probe itself reads it
            const std::uint32_t config = addresses_.config_global ? *reinterpret_cast<const std::uint32_t*>(addresses_.config_global) : 0;
            if (config && !(config & 3)) in.video_flags = word(reinterpret_cast<const std::uint32_t*>(config), core::config_video_flags);
            else in.ready = false;
        }
    }
    int decision = core::decide(in);
    if (decision == core::Visible) { ++counters_.answered_visible; ready_.answered = true; }
    else if (decision == core::Hidden) { ++counters_.answered_hidden; ready_.answered = true; }
    else ++counters_.original;
    if (log_) {
        // The diagnostic always runs the original (its side effects are its own: RE note section 13) and
        // returns its answer itself where the decision was "original", so it never runs twice.
        const int vanilla = reinterpret_cast<int(__cdecl*)(const std::uint32_t*, const std::uint32_t*)>(x3m_sun_probe_target)(record, view);
        x3m::log("sun_probe frame=%llu view=%08lx record=%08lx owner=%08lx owner_flags270=%08lx owner_layer=%ld main=%08lx eligible=%u acc=%ld x=%ld y=%ld visible=%lu size=%ld group=%lu flags270=%08lx layer=%ld records=%u ready=%u vanilla=%d answer=%d",
                 static_cast<unsigned long long>(device_frame_), static_cast<unsigned long>(in.view), static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(record)),
                 static_cast<unsigned long>(in.record_owner), static_cast<unsigned long>(owner_flags), static_cast<long>(owner_layer), static_cast<unsigned long>(main_view_), own ? 1u : 0u,
                 static_cast<long>(std::int32_t(word(record, core::record_accumulator))), static_cast<long>(in.x), static_cast<long>(in.y),
                 static_cast<unsigned long>(word(record, core::record_visible)), static_cast<long>(std::int32_t(word(record, core::record_size))),
                 static_cast<unsigned long>(word(record, core::record_group)), static_cast<unsigned long>(in.view_flags), static_cast<long>(std::int32_t(word(view, 0x29c))),
                 ready_.records, in.ready ? 1u : 0u, vanilla, decision == core::Original ? vanilla : decision);
        if (decision == core::Original) decision = vanilla ? core::Hidden : core::Visible;
    }
    return decision;
}
}
extern "C" {
void __cdecl x3m_sun_lens_begin() {
    x3m::PreserveCpuState cpu;
    if (!owner_thread_ || static_cast<LONG>(GetCurrentThreadId()) != owner_thread_) return; // no probe yet, or not the render thread: no bracket
    ++counters_.brackets;
    if (listener_begin_) listener_begin_();
    detail::bracket_open = true;
}
void __cdecl x3m_sun_lens_end() {
    x3m::PreserveCpuState cpu;
    if (!detail::bracket_open) return;
    detail::bracket_open = false;
    if (listener_end_) listener_end_();
}
}
// In: [ESP] = the engine's return address, [ESP+4] = record, [ESP+8] = view. decide() = 0 / 1: that is the probe's
// answer, plain return (the caller pops its two arguments). 2: the original runs on the caller's exact frame.
// In (lens): [ESP+4] = view. begin, the original with its own copy of the argument, end; the caller pops its own.
// Both save EFLAGS and clear DF around the C handlers (the C ABI requires DF = 0; the sites' flags are dead, but
// the original is entered with exactly the flags the site had) and assume only the 4-byte stack contract.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_sun_probe_thunk
_x3m_sun_probe_thunk:
    pushfd
    cld
    push dword ptr [esp+12]
    push dword ptr [esp+12]
    call _x3m_sun_probe_decide
    add esp, 8
    cmp eax, 2
    je 1f
    popfd
    ret
1:  popfd
    jmp dword ptr [_x3m_sun_probe_target]
    .p2align 4
    .globl _x3m_sun_lens_thunk
_x3m_sun_lens_thunk:
    pushfd
    cld
    call _x3m_sun_lens_begin
    popfd
    push dword ptr [esp+4]
    call dword ptr [_x3m_sun_lens_target]
    add esp, 4
    pushfd
    cld
    call _x3m_sun_lens_end
    popfd
    ret
    .att_syntax
)");

namespace x3m::sun_occlusion {
bool install_at(const Addresses& a, bool override_enabled_value, bool log_enabled) {
    const DWORD error = GetLastError();
    const auto done = [&](const char* reason, bool ok) { state_ = reason; SetLastError(error); return ok; };
    if (patched_) return done("already_installed", false);
    if (!a.probe_site || !a.probe_target || !a.lens_site || !a.lens_target || (!a.config_global && override_enabled_value)) return done("invalid_site", false);
    if (!engine_patch::install_window_open()) return done("late_claim", false);
    if (!pin_self()) return done("pin_failed", false);
    addresses_ = a; override_ = override_enabled_value; log_ = log_enabled;
    x3m_sun_probe_target = static_cast<std::uint32_t>(a.probe_target); x3m_sun_lens_target = static_cast<std::uint32_t>(a.lens_target);
    ready_ = core::Ready{}; latch_ = core::Latch{}; counters_ = Counters{}; foreign_logged_ = false; owner_thread_ = 0; foreign_thread_ = 0; reset_requested_ = 0; seen_frame_ = frame_; main_view_ = 0; frame_started_ = false; multi_counted_ = false;
    detail::bracket_open = false; block_reason_ = "";
    probe_site_ = engine_patch::CallSite{}; lens_site_ = engine_patch::CallSite{};
    if (!engine_patch::claim_call(lens_site_, a.lens_site, a.lens_target, reinterpret_cast<void*>(&x3m_sun_lens_thunk))) {
        if (lens_site_.patched_in) { patched_ = true; return done("rollback_failed", false); } // registered: shutdown() tries again
        return done(lens_site_.status, false);
    }
    if (!engine_patch::claim_call(probe_site_, a.probe_site, a.probe_target, reinterpret_cast<void*>(&x3m_sun_probe_thunk))) {
        const char* reason = probe_site_.status;
        const bool lens_back = engine_patch::restore_call(lens_site_);
        if (probe_site_.patched_in || !lens_back) { patched_ = true; return done("rollback_failed", false); }
        return done(reason, false);
    }
    patched_ = true;
    return done("ok", true);
}
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_) { SetLastError(error); return true; }
    wchar_t setting[4]{};
    const bool requested = GetEnvironmentVariableW(L"X3M_SUN_OCCLUSION", setting, 4) == 1 && setting[0] == L'1';
    const bool log_requested = GetEnvironmentVariableW(L"X3M_SUN_OCCLUSION_LOG", setting, 4) == 1 && setting[0] == L'1';
    if (!requested && !log_requested) { state_ = "disabled"; SetLastError(error); return false; }
    bool applied = false;
    if (GetEnvironmentVariableW(L"X3M_SUBMIT_PHASES", setting, 4) == 1 && setting[0] == L'1') state_ = "submit_phases_conflict"; // its stamp claims 0x00472490..0x00472495
    else if (!object_trace::executable_verified()) state_ = "executable_mismatch";
    else if (!bytes_match(core::probe_context_va, core::probe_context, sizeof core::probe_context)) state_ = "probe_site_mismatch";
    else if (!bytes_match(core::lens_context_va, core::lens_context, sizeof core::lens_context)) state_ = "lens_site_mismatch";
    else if (!gates_match()) state_ = "probe_body_mismatch";
    else if (!bytes_match(core::lens_target_va, core::lens_entry, sizeof core::lens_entry)) state_ = "lens_body_mismatch";
    else applied = install_at(Addresses{core::probe_site_va, core::probe_target_va, core::lens_site_va, core::lens_target_va, core::config_global_va, nullptr}, requested, log_requested);
    log("sun_occlusion requested=%u log=%u patched=%u reason=%s probe_site=0x%08lx probe_target=0x%08lx probe_write=%s lens_site=0x%08lx lens_target=0x%08lx lens_write=%s",
        requested ? 1u : 0u, log_requested ? 1u : 0u, patched_ ? 1u : 0u, state_, static_cast<unsigned long>(core::probe_site_va), static_cast<unsigned long>(core::probe_target_va),
        probe_site_.patched_in ? (probe_site_.atomic_write ? "atomic" : "plain") : "none", static_cast<unsigned long>(core::lens_site_va), static_cast<unsigned long>(core::lens_target_va),
        lens_site_.patched_in ? (lens_site_.atomic_write ? "atomic" : "plain") : "none");
    SetLastError(error);
    return applied;
}
bool shutdown() {
    if (!patched_) return true;
    const DWORD error = GetLastError();
    const bool probe_back = engine_patch::restore_call(probe_site_);
    const bool lens_back = engine_patch::restore_call(lens_site_);
    patched_ = probe_site_.patched_in || lens_site_.patched_in; // retain a failed restoration for a later detach retry
    detail::bracket_open = false;
    state_ = probe_back && lens_back ? "restored" : "restore_failed";
    SetLastError(error);
    return probe_back && lens_back;
}
const char* state() { return state_; }
bool installed() { return patched_; }
bool override_enabled() { return patched_ && override_; }
bool logging() { return patched_ && log_; }
void set_listener(void (*begin)(), void (*end)()) { listener_begin_ = begin; listener_end_ = end; }
void present(unsigned long long next_device_frame) {
    if (!patched_) return;
    frame_ = frame_ + 1; device_frame_ = next_device_frame;
    detail::bracket_open = false; // an unwind past the lens thunk must not leave the draws' window open
    // The owner is fixed for the process (the first thread through the probe). A probe from any other thread is the
    // original's; said once, from the Present thread.
    if (!foreign_logged_ && foreign_thread_ != 0) {
        foreign_logged_ = true;
        log("sun_occlusion_foreign_thread frame=%llu owner=%lu calls=%ld note=probes_from_other_threads_run_the_original", next_device_frame, static_cast<unsigned long>(owner_thread_), static_cast<long>(foreign_thread_));
    }
}
void device_reset() { if (patched_) InterlockedExchange(&reset_requested_, 1); }
FrameInputs frame_inputs() {
    FrameInputs out;
    out.frame = frame_;
    if (!patched_) return out;
    if (InterlockedExchange(&reset_requested_, 0) != 0) { ready_.reset(); latch_ = core::Latch{}; block_reason_ = ""; return out; }
    if (!frame_started_ || seen_frame_ != frame_) return out; // no probe in this frame: nothing latched
    out.latch = latch_; out.single = ready_.single() && latch_.valid; out.answered = ready_.answered;
    return out;
}
void report_pass(bool ok) { if (patched_ && frame_started_ && seen_frame_ == frame_) ready_.pass_ok = ok; }
void block(const char* reason) {
    if (!patched_ || ready_.blocked) return;
    ready_.blocked = true; block_reason_ = reason ? reason : ""; ++counters_.blocked;
    log("sun_occlusion_blocked frame=%llu reason=%s", static_cast<unsigned long long>(device_frame_), block_reason_);
}
Counters counters() { Counters c = counters_; c.foreign_thread = static_cast<std::uint32_t>(foreign_thread_); return c; }
}
