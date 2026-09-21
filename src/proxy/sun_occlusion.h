#pragma once
#include "sun_occlusion_core.h"
#include <cstdint>

// Partial sun occlusion, step 1 (X3M_SUN_OCCLUSION=1; X3M_SUN_OCCLUSION_LOG=1 adds the diagnostic
// and installs the same two redirects observe-only when the feature itself is off; both unset =
// vanilla, nothing patched, no cost). docs/architecture/sun-partial-occlusion.md; sites and ABI:
// docs/reverse-engineering/lens-flare-visibility.md sections 12-16.
//
//   0x00471630  call 0x00488720   the flare probe, __cdecl (record, view), caller cleans 8, EAX = 1
//                                 "hide". x3m_sun_probe_thunk asks x3m_sun_probe_decide: 0 / 1 are
//                                 returned as the probe's answer without running it, 2 jumps to the
//                                 original with the caller's frame untouched.
//   0x00472491  call 0x0047e6e0   the lens-scene traversal, __cdecl (view), caller cleans 4, EAX dead.
//                                 x3m_sun_lens_thunk calls begin, the original with a copy of the
//                                 argument, then end; the caller's argument stays where it is.
//
// Both thunks save EFLAGS and clear DF around the C handlers and are otherwise plain cdecl frames: EBX / EBP / ESI / EDI come back as the C ABI and the original
// return them (EBP is a value register at both sites and is never used as a frame pointer by the
// thunks); EAX / ECX / EDX and the flags are dead at both sites. The probe handler is integer only
// (no x87, no SSE arithmetic, MXCSR untouched) inside a GetLastError / SetLastError envelope (the
// frame's first probe reaches engine_memory's VirtualQuery); with the log on it writes through log(), which preserves the full CPU state itself.
// begin / end run once per frame under PreserveCpuState (FNSAVE / FRSTOR, MXCSR, LastError): they
// reach D3D, float math and the formatter.
//
// Install: exact-executable identity, 21 whole-instruction bytes around each call, the probe's
// prologue with its three early tests (148 bytes, FNV-1a: the override replicates exactly those)
// and the traversal's entry, then engine_patch::claim_call on both sites inside the install
// window; a failure of the second claim restores the first. Refused with
// X3M_SUBMIT_PHASES=1, whose sort_return_b stamp claims 0x00472490..0x00472495. Any refusal logs
// one line and leaves both sites vanilla.
//
// Threads: all state belongs to the first thread through the probe thunk (the game's render
// thread, also the Present thread); any other thread gets the original and no bracket.
namespace x3m::sun_occlusion {
struct Addresses {
    std::uintptr_t probe_site = 0, probe_target = 0, lens_site = 0, lens_target = 0;
    std::uintptr_t config_global = 0;       // address of the pointer whose +0xfc word holds VideoD3DFlags
    const std::uintptr_t* main_view = nullptr; // fixtures: the main view is *main_view; production: null, the cockpit registry walk
};
struct Counters {
    std::uint32_t probes = 0, own = 0, answered_visible = 0, answered_hidden = 0, original = 0, foreign_thread = 0;
    std::uint32_t brackets = 0, multi_record_frames = 0, blocked = 0;
};
bool initialize();   // backend-load path only; one sun_occlusion line when either variable is set
bool shutdown();     // restores both calls; true when nothing is installed
bool install_at(const Addresses&, bool override_enabled, bool log_enabled);
const char* state();
bool installed();
bool override_enabled();
bool logging();
// The bracket's listener (capture.cpp): begin runs the visibility pass, end closes the draws' window.
void set_listener(void (*begin)(), void (*end)());
void present(unsigned long long next_device_frame = 0); // frame boundary (the Present hook; the number labels the log lines): also closes a bracket an unwind left open
void device_reset();     // readiness restarts: vanilla until a pass has run again
namespace detail { extern volatile bool bracket_open; }
inline bool bracket_open() noexcept { return detail::bracket_open; } // the draw hooks' one flag test
// For begin (owner thread, inside the bracket): what the probe latched this frame.
struct FrameInputs { core::Latch latch; bool single = false, answered = false; std::uint32_t frame = 0; };
FrameInputs frame_inputs();
void report_pass(bool ok);       // the visibility pass ran for the latched record in this frame
void block(const char* reason);  // a lens draw that can never carry the fraction: vanilla from the next frame on, for the process (a Reset does not lift it)
Counters counters();
}
extern "C" {
void x3m_sun_probe_thunk();
void x3m_sun_lens_thunk();
int __cdecl x3m_sun_probe_decide(const std::uint32_t* record, const std::uint32_t* view);
void __cdecl x3m_sun_lens_begin();
void __cdecl x3m_sun_lens_end();
extern std::uint32_t x3m_sun_probe_target, x3m_sun_lens_target;
}
