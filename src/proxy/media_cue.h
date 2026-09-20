#pragma once
#include <atomic>
#include <cstdint>
#include "engine_patch.h"
#include "media_cue_core.h"
#include "../ownership/surface_lock_observation.h"

// Default-off gate on the media-record allocator 0x00498140
// (X3M_MEDIA_CUE_TRACE=1, launcher --media-cue-trace, requires --telemetry;
// X3M_MEDIA_CUE_CACHE=1, launcher --media-cue-cache on; X3M_MEDIA_CUE_RETRY_S).
// One byte-verified entry site (media_cue_sites.h) carries a two-arm gate stub
// (media_cue.cpp emit_gate): flags, EAX/ECX/EDX and XMM0-7 are saved around a
// handler that reads the media id, the return address and the two dwords
// behind the play helper's frame from the game's own stack. PASS restores
// everything and continues into the claim tail (the displaced `push ebx;
// mov ebx,[esp+8]` at the game's exact ESP); REFUSE restores everything, sets
// EAX = 0 and returns to the caller with a plain `ret` (cdecl, the caller pops
// the argument), the state a real failed build leaves except the two lifetime
// allocation counters (docs/reverse-engineering/media-cue-playback.md, 6).
// A proceeded call's outcome is captured by return-address substitution: the
// handler stores the game's return address in a per-owner pending stack and
// replaces it with the stub's return trampoline, which records EAX (the record
// or 0) and the clock, restores the original return address into its slot and
// `ret`s to it with every register and the flags intact. No second patched
// site. Both handlers run under LightCallBoundary (MXCSR + LastError, x87-free;
// verification/probe/check_no_x87.py walks x3m_media_cue_enter and
// x3m_media_cue_return) and never log; the frame boundary (capture.cpp Present
// path, the owner thread) drains the trace ring into `media_cue` lines (32 per
// second) and one `media_cue_window` line per 300 frames. Off, nothing is
// installed and the per-frame cost is one relaxed load.
// Video blit witness (media-cue-playback.md, 8): with the trace on, the
// ownership layer's Surface::LockRect/UnlockRect shell reports each call with
// its caller's return address; calls from the consumer 0x004d0c40..0x004d14e0
// write `media_video_blit` lines through the same direct handle write as
// `media_cue_enter` (enter and result, since the process may not return from
// either call) and count into the window line. Needs --ownership: without the
// wrapper no shell sees the game's surfaces.
namespace x3m::media_cue {
// Compose with the sole allocator-site owner. Predicate is bounded CPU-only,
// x87-free, noexcept, and checks the complete owned admission state. It receives
// normalized effective flags (ID2 input0 maps to8); null restores cache policy.
using OwnedEligibility=bool(*)(std::uint32_t source,std::uint32_t flags) noexcept;
void set_owned_eligibility(OwnedEligibility) noexcept;
bool initialize(); // after loop_phases::initialize, while the install window is open
// The witness to publish through ownership::set_surface_lock_observer, or
// nullptr when the trace is off (nothing is registered, the shell pays one
// relaxed load). Valid after initialize().
ownership::SurfaceLockObserver video_lock_observer() noexcept;
extern std::atomic<bool> active;
namespace detail {
void frame_impl(std::uint64_t frame) noexcept;
}
// Frame boundary on the Present thread: admits the owner, closes the frame's
// attempt count into the window and drains the trace ring.
inline void frame(std::uint64_t frame_index) noexcept {
    if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index);
}
// The game's stack as the gate stub's handler sees it (media_cue.cpp emit_gate).
struct EnterFrame {
    unsigned char xmm[8][16];
    std::uint32_t edx, ecx, eax, eflags;
    std::uint32_t ret;      // [esp]: the caller's return address (rewritten on PASS)
    std::uint32_t id;       // [esp+4]: the media id
    std::uint32_t slot_8;   // [esp+8]
    std::uint32_t slot_c;   // [esp+0xc]: behind the play helper, the selector's return address
    std::uint32_t slot_10;  // [esp+0x10]: behind the play helper, the cue kind
};
struct ReturnFrame {
    unsigned char xmm[8][16];
    std::uint32_t edx, ecx, eax, eflags;
    std::uint32_t slot;     // the reserved slot the original return address goes back into
    std::uint32_t id;       // the caller's still-pushed argument
};
#ifdef X3M_GAME_PHASE_FIXTURE
// The production install transaction on a fixture span (the fixture supplies
// its own address and discriminators; opcodes and length are real).
bool fixture_install(const engine_patch::SiteSpec* specs, const detail::Addresses& addresses, bool trace, bool cache,
                     std::uint64_t retry_ticks, const char** status);
bool fixture_uninstall();
void* fixture_emit(void*** next); // the production gate stub
const detail::NegativeCache* fixture_cache();
const detail::PendingStack* fixture_pending();
const detail::Gate* fixture_gate();
bool fixture_pop_trace(detail::Entry* out);  // owner thread only
std::uint32_t fixture_attempts_frame();
std::uint64_t fixture_refused();
const char* fixture_site_status();
void fixture_drop_pending();
const detail::RateLimit* fixture_enter_limit();  // the media_cue_enter line limiter
void fixture_reset_enter_limit();
const detail::VideoBlit* fixture_video();        // the blit witness counters
std::uint64_t fixture_video_dropped(bool foreign); // in-range enters dropped: foreign thread / before admission
void fixture_reset_video();
#endif
}
