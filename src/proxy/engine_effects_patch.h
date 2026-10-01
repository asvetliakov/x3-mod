#pragma once
#include <cstdint>

// Engine-effects call redirects (X3M_ENGINE_EFFECTS=native|off|plumes; unset = native; docs/architecture/
// engine-effects-modern.md phase 1b, docs/reverse-engineering/engine-effects.md section 7). off and plumes redirect
// the two calls of the per-ship engine effect routine 0x00414590 (engine_effects_sites.h): site A 0x004147eb
// (call 0x004148a0, the effect instance: emitter sprite and engine lens flare) and site B 0x0041482c (call
// 0x00412d70, the Particles3 engine trail generator). Each stub returns without creating anything when the object's
// class word obj+0x48 is 7 (ship) and jumps to the original callee for every other class (missiles); no Win32 call,
// no x87/SSE, no memory written, LastError/MXCSR/x87/DF untouched; native leaves the engine's bytes.
//
// Installed on the backend-load path inside the engine_patch install window (a later call is refused, late_claim)
// after the structural executable check and a compare of both windows (47 and 50 bytes, whole instructions; fail
// closed before any write). A is claimed first (one lock cmpxchg8b), B only when A is live (plain copy: its rel32
// crosses a qword); each write is read back. Both or none: when B fails, A is restored; installed() is true only
// with both redirects live (the draw-path plumes arm on it). Arm once: instances and trails created before the
// redirect stay alive, so nothing re-arms later. No allocation, nothing per frame beyond the stubs, no device
// state: Reset and recovery are not involved. shutdown() (dynamic unload only) restores B then A, each only while
// its five bytes still hold this module's call (engine_patch::restore_call; foreign bytes: restore_not_owned).
//
// Rows: one per site at initialize, `engine_effects_patch site=A|B va=<site> state=<site state> reason=<overall>
// mode=native|off|plumes|- setting=<value> write=none|atomic|plain`.
namespace x3m::engine_effects_patch {
struct Addresses {
    std::uintptr_t window_a, target_a, window_b, target_b; // the two windows and their calls' expected targets
};
bool initialize(); // backend-load path only; logs one engine_effects_patch row per site
bool shutdown();   // dynamic-unload detach only; true when nothing stays registered
// Verifies both windows (the engine's, or a fixture's copies) and redirects A, then B. Returns whether both are
// live; state() carries the overall reason, site_state() each site's.
bool install_at(const Addresses& a);
bool installed();                      // both redirects live
const char* state();                   // ok | native | the refusal or rollback reason
const char* site_state(unsigned site); // 0 = A, 1 = B: active | rolled_back | not_attempted | restored | <reason>
const char* write_path(unsigned site); // none | atomic | plain: how claim_call wrote the site's five bytes
}
extern "C" {
// The redirected calls' targets (engine_effects_patch.cpp, file-scope asm).
void x3m_engine_effects_stub_a();
void x3m_engine_effects_stub_b();
extern std::uintptr_t x3m_engine_effects_continue_a; // the original callees, set by install_at before the redirects
extern std::uintptr_t x3m_engine_effects_continue_b;
}
