#pragma once
#include <cstdint>
#include "weapon_effects_core.h"

// Weapon effects, step 1: the shield hit (X3M_WEAPON_EFFECTS=native|observe|shields; unset = native;
// docs/architecture/weapon-effects.md 2.2, 2.6, 2.9, 5.1; the sites: weapon_effects_sites.h).
// observe and shields redirect the impact call 0x00449d85 (-> 0x00413c40, the impact effect) to a stub that hands the
// stack to the non-throwing cdecl handler x3m_weapon_event (weapon_effects_hook.cpp: integer only, no call, no
// allocation, no Win32 or CRT; core::impact), which writes one 64-byte Event into the proxy ring and returns the
// decision. The stub saves EFLAGS, EAX, ECX and EDX around it (CLD inside), so the forward path reaches 0x00413c40
// with every register, the flags and the stack as the call left them; the skip path returns `xor eax,eax; ret` (the
// caller pops its two arguments; EAX/ECX/EDX/EFLAGS are dead after the call). Skip only in shields mode, for a
// shield_hit, while Present latched owned_next (the stage armed in the previous frame's resolve and the device holds
// the pass): the native sprite, its light and its PROPAGATE children are then not created, and the stage draws the
// shell from the record. Everything else forwards unchanged (the record is kept with flag_forwarded for the census).
// observe additionally redirects the two inner instance calls 0x00413da8 / 0x00413e79 (-> 0x00414a70) to a tap that
// compares the handler's contact point with the engine's own (weapon_pos_check) and forwards always.
//
// Install: the backend-load path inside the engine_patch install window (later: late_claim), after the structural
// executable check, a compare of every window and witness (fail closed before any write) and the DLL pin; atomic sites
// first (the taps, lock cmpxchg8b), then the impact (plain copy, its call straddles a qword); each write read back.
// All or none: a failure restores every site claimed before it (rolled_back / rollback_failed). shutdown() (dynamic
// unload only) restores in reverse order, each only while its five bytes still hold this module's call. FL builds
// compile the hook out (edition_unsupported).
//
// Rows: `weapon_effects_patch site=impact|tap1|tap2 va=<site> state=<site> reason=<overall> mode=<mode> setting=<value>
// write=none|atomic|plain` per site at initialize; `weapon_effects_table event= reason= rows= entries= textures=
// overrides= dropped= bytes= path=` once; the frame rows of present() below and `weapon_effects_scope frame= cleared=
// loading= sector_key= load_epoch= clears=` when a scope change clears stage-owned records. The FL edition writes one
// `weapon_effects_patch site=impact ... state=edition_unsupported edition=fl` row at initialize (capture.cpp).
namespace x3m::weapon_effects {
struct Addresses {
    std::uintptr_t impact_window, impact_target, prologue;
    std::uintptr_t tap_window[2], tap_target;
};
bool initialize();
bool shutdown();
bool install_at(const Addresses& a, core::Mode mode);
bool installed();
core::Mode mode();
const char* state();
const char* site_state(unsigned site); // 0 impact, 1 tap1, 2 tap2
const char* write_path(unsigned site);
core::HookState& hook_state(); // the game thread's state (ring, latch, counters): the stage and Present read it
// Present, once per frame on the thread that runs the hooks: the next frame's latch (shields mode: owned), the
// weapon_event rows (the first 64 events of the run and every event of a capture frame), the forwarded records
// leaving the ring once logged, expired records (game time) leaving it, weapon_pos_check (observe) and
// weapon_effects_frame every 300 frames with the stage's period figures.
struct StageFrame {
    unsigned shells = 0, decals = 0, retired = 0;
    float stage_us = 0.f;
    bool drew = false;
};
// `scope` (core::Scope: the cockpit's sector key, the load epoch, loading) clears the stage-owned records on a load, a
// sector change or a new load epoch (note 3.1), before the frame's upkeep.
void present(unsigned long long frame, bool capture, bool owned, const StageFrame& stage, const core::Scope& scope = {});
// The resolve's cut verdict, a Reset, a load or a sector change: every stage-owned record leaves the ring.
void clear_owned();
std::uint32_t game_ms(); // cfg+0x718 through engine_memory (0 when unreadable; hook_state().globals' slot)
}
extern "C" {
void x3m_weapon_effects_stub_impact();
void x3m_weapon_effects_stub_tap();
int __cdecl x3m_weapon_event(const std::uint32_t* entry);
void __cdecl x3m_weapon_tap(const std::uint32_t* entry);
extern std::uintptr_t x3m_weapon_effects_continue_impact;
extern std::uintptr_t x3m_weapon_effects_continue_tap;
}
