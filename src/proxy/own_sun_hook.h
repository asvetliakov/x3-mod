#pragma once
#include "own_sun_hook_core.h"
#include <cstdint>

// The own sun's flare-probe hook and the lens bracket (docs/architecture/own-sun-pass.md section 5; sites and ABI:
// docs/reverse-engineering/lens-flare-visibility.md sections 12-16). Installed when the own sun is requested
// (X3M_OWN_SUN, request_own_sun()) or the lens bracket is wanted for the lens-flare gain or the fog's transmittance
// (initialize(bracket_wanted)); otherwise nothing is patched and nothing costs. X3M_OWN_SUN_LOG=1 adds the sun_probe
// diagnostic row of an installed hook.
//
//   0x00471630  call 0x00488720   the flare probe, __cdecl (record, view), caller cleans 8, EAX = 1
//                                 "hide". x3m_sun_probe_thunk asks x3m_sun_probe_decide: 1 is returned as the
//                                 probe's answer without running it, 2 jumps to the original with the caller's frame
//                                 untouched. The own sun answers 1 for the eligible record (core::eligible, run223:
//                                 the main view's re-probe of a record owned by a background-regime view, owner+0x270
//                                 & 0x400000, one validated engine_memory read of the owner's flags) in a frame its
//                                 Present-time latch made active; every other probe is the original's.
//   0x00472491  call 0x0047e6e0   the lens-scene traversal, __cdecl (view), caller cleans 4, EAX dead.
//                                 x3m_sun_lens_thunk calls begin, the original with a copy of the
//                                 argument, then end; the caller's argument stays where it is. The bracket carries
//                                 only the lens-flare gain (lens_flare_gain.h) and its fog transmittance.
//
// Both thunks save EFLAGS and clear DF around the C handlers and are otherwise plain cdecl frames: EBX / EBP / ESI /
// EDI come back as the C ABI and the original return them (EBP is a value register at both sites and is never used as a
// frame pointer by the thunks); EAX / ECX / EDX and the flags are dead at both sites. The probe handler is integer only
// (no x87, no SSE arithmetic, MXCSR untouched) inside a GetLastError / SetLastError envelope (the
// frame's first probe reaches engine_memory's VirtualQuery); with the log on it writes through log(), which preserves
// the full CPU state itself. begin / end run once per frame under PreserveCpuState (FNSAVE / FRSTOR, MXCSR, LastError):
// they reach D3D, float math and the formatter.
//
// Install: exact-executable identity, 21 whole-instruction bytes around each call, the probe's
// prologue with its three early tests (148 bytes, FNV-1a: the probe's identity) and the traversal's entry, then
// engine_patch::claim_call on both sites inside the install window; a failure of the second claim restores the
// first. Refused with X3M_SUBMIT_PHASES=1, whose sort_return_b stamp claims 0x00472490..0x00472495. Any refusal logs
// one line and leaves both sites vanilla.
//
// Threads: all state belongs to the first thread through the probe thunk (the game's render
// thread, also the Present thread); any other thread gets the original and no bracket.
namespace x3m::own_sun_hook {
struct Addresses {
    std::uintptr_t probe_site = 0, probe_target = 0, lens_site = 0, lens_target = 0;
    const std::uintptr_t* main_view = nullptr; // fixtures: the main view is *main_view; production: null, the cockpit
                                               // registry walk
};
struct Counters {
    std::uint32_t probes = 0, own = 0, original = 0, foreign_thread = 0, brackets = 0;
    std::uint32_t own_sun_hidden = 0; // eligible probes the own sun answered "hidden" (own_sun_core.h answer)
};
bool initialize(bool bracket_wanted = false); // backend-load path only; one own_sun_hook row (AP; FL: fl.sun_occlusion)
                                              // when the own sun or the bracket is wanted (bracket_wanted: the
                                              // lens-flare gain below 1 or the volumetric fog)
bool shutdown();                              // restores both calls; true when nothing is installed
bool install_at(const Addresses&, bool log_enabled);
const char* state();
bool installed();
bool logging();
// The bracket's listener (capture.cpp): begin sets the bracket's lens-flare gain, end (may be null) closes it.
void set_listener(void (*begin)(), void (*end)());
void present(unsigned long long next_device_frame = 0); // frame boundary (the Present hook; the number labels the log
                                                        // lines): also closes a bracket an unwind left open
#if !X3M_EDITION_FL
// Own sun (docs/architecture/own-sun-pass.md; X3M_OWN_SUN, AP edition): request_own_sun() before initialize() makes it
// install both redirects; set_own_sun_frame() is the Present-time latch for the next frame (own_sun::core::frame_active):
// while it is set, the probe answers "hidden" for the sun's eligible record (core::eligible), every other probe is the
// original's. own_sun_hidden(): this frame's main-view eligible probe was answered "hidden" (owner thread; the main view
// is probed before the scene-end hook, lens-flare-visibility.md section 14): the stage draws only then, so the engine's
// chain and the own sun never show together and a sun the probe did not reach (another view, the flare option off) is
// not drawn.
void request_own_sun();
void set_own_sun_frame(bool active);
bool own_sun_hidden();
#endif
namespace detail {
extern volatile bool bracket_open;
}
inline bool bracket_open() noexcept {
    return detail::bracket_open;
} // the draw hooks' one flag test
Counters counters();
#ifdef X3M_SUN_OCCLUSION_FL_FIXTURE
// FL edition fixture seams (src/engine/fl/sun_occlusion_fl.cpp, verification/probe/fl_sun_occlusion.cpp).
const char* fixture_gate(std::uintptr_t base); // the 17 anchors at base: nullptr or the first refused anchor
void fixture_slots(std::uintptr_t registry_slot, std::uintptr_t mode_slot); // before install_at
std::uintptr_t fixture_main_view();                                         // the registry walk now
unsigned fixture_arm_sample();
void fixture_force_sample();
void fixture_log_status(); // the fl.sun_occlusion row of the current install
#endif
}
extern "C" {
void x3m_sun_probe_thunk();
void x3m_sun_lens_thunk();
int __cdecl x3m_sun_probe_decide(const std::uint32_t* record, const std::uint32_t* view);
void __cdecl x3m_sun_lens_begin();
void __cdecl x3m_sun_lens_end();
extern std::uint32_t x3m_sun_probe_target, x3m_sun_lens_target;
}
