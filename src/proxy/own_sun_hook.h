#pragma once
#include "own_sun_hook_core.h"
#include <cstdint>

// The own sun's flare-probe hook (docs/architecture/own-sun-pass.md section 5; sites and ABI:
// docs/reverse-engineering/lens-flare-visibility.md sections 12-16). Installed only when the own sun is requested
// (X3M_OWN_SUN, request_own_sun()); otherwise nothing is patched and nothing costs. X3M_OWN_SUN_LOG=1 adds the
// sun_probe diagnostic row of an installed hook. The lens bracket on the engine's lens-scene call 0x00472491 (FL
// base+0x8bcb7) was deleted on 2026-11-02 with the lens-flare gain: that call is never written.
//
//   0x00471630  call 0x00488720   the flare probe, __cdecl (record, view), caller cleans 8, EAX = 1
//                                 "hide". x3m_sun_probe_thunk asks x3m_sun_probe_decide: 1 is returned as the
//                                 probe's answer without running it, 2 jumps to the original with the caller's frame
//                                 untouched. The own sun answers 1 for the eligible record (core::eligible, run223:
//                                 the main view's re-probe of a record owned by a background-regime view, owner+0x270
//                                 & 0x400000, one validated engine_memory read of the owner's flags) in a frame its
//                                 Present-time latch made active; every other probe is the original's.
//
// The thunk saves EFLAGS and clears DF around the C handler and is otherwise a plain cdecl frame: EBX / EBP / ESI /
// EDI come back as the C ABI and the original return them (EBP is a value register at the site and is never used as a
// frame pointer by the thunk); EAX / ECX / EDX and the flags are dead at the site. The probe handler is integer only
// (no x87, no SSE arithmetic, MXCSR untouched) inside a GetLastError / SetLastError envelope (the
// frame's first probe reaches engine_memory's VirtualQuery); with the log on it writes through log(), which preserves
// the full CPU state itself.
//
// Install: exact-executable identity, 21 whole-instruction bytes around the probe call and around the (untouched)
// lens-scene call 0x00472491, the probe's prologue with its three early tests (148 bytes, FNV-1a: the probe's
// identity) and the traversal's entry, then engine_patch::claim_call on the probe site inside the install window. Any
// refusal logs one line and leaves the site vanilla.
//
// Threads: all state belongs to the first thread through the probe thunk (the game's render
// thread, also the Present thread); any other thread gets the original.
namespace x3m::own_sun_hook {
struct Addresses {
    std::uintptr_t probe_site = 0, probe_target = 0;
    const std::uintptr_t* main_view = nullptr; // fixtures: the main view is *main_view; production: null, the cockpit
                                               // registry walk
};
struct Counters {
    std::uint32_t probes = 0, own = 0, original = 0, foreign_thread = 0;
    std::uint32_t own_sun_hidden = 0; // eligible probes the own sun answered "hidden" (own_sun_core.h answer)
    std::uint32_t late = 0;           // eligible probes forwarded because the resolve cleared the latch (diagnosis)
};
bool initialize(); // backend-load path only; installs only after request_own_sun(); one own_sun_hook row (AP; FL:
                   // fl.sun_occlusion) when the own sun is requested
bool shutdown();   // restores the probe call; true when nothing is installed
bool install_at(const Addresses&, bool log_enabled);
const char* state();
bool installed();
bool logging();
void present(unsigned long long next_device_frame = 0); // frame boundary (the Present hook; the number labels the log
                                                        // lines)
// Own sun (docs/architecture/own-sun-pass.md; X3M_OWN_SUN, both editions): request_own_sun() before initialize() makes
// it install the probe redirect; set_own_sun_frame() is the Present-time latch for the next frame (own_sun::core::frame_active): while it
// is set, the probe answers "hidden" for the sun's eligible record (core::eligible), every other probe is the
// original's. own_sun_hidden(): this frame's main-view eligible probe was answered "hidden" (owner thread; the main view
// is probed before the scene-end hook, lens-flare-visibility.md section 14): the stage draws only then, so the engine's
// chain and the own sun never show together and a sun the probe did not reach (another view, the flare option off) is
// not drawn.
void request_own_sun();
void set_own_sun_frame(bool active);
bool own_sun_hidden();
// The resolve saw no hidden answer in a latched frame (own_sun::core::latch_after_resolve): the latch is cleared until
// the next set_own_sun_frame() (Present); an eligible probe after it forwards and counts as Counters::late.
void close_own_sun_frame();
bool main_view_known(); // installed with a main-view source (FL: the admitted registry slot; AP: always)
Counters counters();
#ifdef X3M_SUN_OCCLUSION_FL_FIXTURE
// FL edition fixture seams (src/engine/fl/sun_occlusion_fl.cpp, verification/probe/fl_sun_occlusion.cpp).
const char* fixture_gate(std::uintptr_t base); // the 17 anchors at base: nullptr or the first refused anchor
void fixture_slots(std::uintptr_t registry_slot); // before install_at
std::uintptr_t fixture_main_view();                                         // the registry walk now
unsigned fixture_arm_sample();
void fixture_force_sample();
void fixture_log_status(); // the fl.sun_occlusion row of the current install
#endif
}
extern "C" {
void x3m_sun_probe_thunk();
int __cdecl x3m_sun_probe_decide(const std::uint32_t* record, const std::uint32_t* view);
extern std::uint32_t x3m_sun_probe_target;
}
