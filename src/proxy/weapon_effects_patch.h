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
// caller pops its two arguments; EAX/ECX/EDX/EFLAGS are dead after the call). In shields mode a shield_hit while
// Present latched owned_next (the stage armed in the previous frame's resolve and the device holds the pass) is the
// stage's (flag_stage: it draws the hex patch from the record) and, with core::suppress_native_shield_hit
// (HookState::suppress_native, true by default since the coordinator decision of 2026-11-06), its native spawn is
// skipped: the native sprite, its light and its PROPAGATE children are not created. Every other record (observe, hull
// hits, a dying hit object, a frame the stage did not own) forwards and is census-only.
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
// weapon_event rows (the first 64 events of the run, then up to 8 per 300-frame period, and every event of a capture
// frame; each with pos_r = |pos| / radius, the radial region, the travel dir and its region), the forwarded records
// leaving the ring once logged, expired records (game time) leaving it, weapon_pos_check (observe) and
// weapon_effects_frame every 300 frames with the stage's period figures (patches, decals, retired, stage_us, and the
// drops: dropped, cells_dropped, hits_dropped, with spills; and dir_fallback, the records without a travel).
struct StageFrame {
    unsigned patches = 0, decals = 0, retired = 0;
    // stage::Counts' drops: regions or decals the budget refused, lit cells no draw carried (the oldest clocks), live
    // hits past an owner's owner_hits newest; and the regions' spill draws.
    unsigned dropped = 0, cells_dropped = 0, hits_dropped = 0, spills = 0;
    unsigned faces_culled = 0; // face draws the stage culled (their box faces away from the eye)
    unsigned area_dropped = 0; // face draws past the frame's screen-area budget
    unsigned bubbles = 0, bubble_fallback = 0; // owners drawn on their ship bubble, of them on the fallback sphere
    // the hull walks (stage::BubbleStats): run, failed, retries held back, deferred past the frame's budget, bubbles cut
    // to the object radius
    unsigned bubble_walks = 0, bubble_failures = 0, bubble_held = 0, bubble_deferred = 0, bubble_clamped = 0;
    float stage_us = 0.f;
    bool drew = false;
};
// `scope` (core::Scope: the cockpit's sector key, the load epoch, loading) clears the stage-owned records on a load, a
// sector change or a new load epoch (note 3.1), before the frame's upkeep.
void present(unsigned long long frame, bool capture, bool owned, const StageFrame& stage, const core::Scope& scope = {});
// The resolve's cut verdict, a Reset, a load or a sector change: every stage-owned record leaves the ring.
void clear_owned();
// Counts the ring's scope changes (a load, a sector change, a new load epoch; shields mode): the stage clears its depth
// cache when the count moved since its last frame.
std::uint32_t scope_changes();
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
