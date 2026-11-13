#pragma once
#include <cstddef>
#include <cstdint>
#include "weapon_effects_core.h"
#include "weapon_effects_bolt.h"

// Weapon effects, step 1: the shield hit (X3M_WEAPON_EFFECTS=native|observe|shields; the scratched families' names
// hull, bolts and explosions are accepted beside shields and ignored, a weapon_effects_ignored row each; unset = native;
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
// skipped: the native sprite, its light and its PROPAGATE children are not created. Every other record (observe, in
// shields mode a hull hit, a dying hit object, a frame the stage did not own) forwards and is census-only.
// The family modes (core::Mode; reached through install_at only since the look layer was scratched on 2026-11-08,
// docs/archive/effects-look-2026-11-08/index.md) keep the hook contract the redo builds on: shields,hull arms family_hull
// (a hull hit while Present latched owned_next and hull_ready is the stage's; Present never latches hull_ready now, so
// every hull hit forwards); its record lives core::hull_lifetime_ms, and Present keeps a staged one
// HookState::life_floor_ms longer.
// observe additionally redirects the two inner instance calls 0x00413da8 / 0x00413e79 (-> 0x00414a70) to a tap that
// compares the handler's contact point with the engine's own (weapon_pos_check) and forwards always.
//
// Install: the backend-load path inside the engine_patch install window (later: late_claim), after the structural
// executable check, a compare of every window and witness (fail closed before any write) and the DLL pin; atomic sites
// first (the taps, lock cmpxchg8b), then the impact (plain copy, its call straddles a qword); each write read back.
// All or none: a failure restores every site claimed before it (rolled_back / rollback_failed). shutdown() (dynamic
// unload only) restores in reverse order, each only while its five bytes still hold this module's call.
// FL (X3M_EDITION_FL; src/engine/fl/weapon_effects_sites_fl.h, docs/reverse-engineering/fl/weapon-effect-spawn.md): the
// same families at X3FL.exe's seven calls, module base + RVA (install_base): impact 0x053390, taps 0x014583 / 0x014650,
// object tap 0x0645bd (plain write: it straddles a qword), muzzle 0x04f639, flight 0x014eaa and the one death call
// 0x0543b3 (serving both AP death sites), each window compared with its relocated dword masked and the frame witnesses
// before any write, all or none; FL entry shims rebuild the AP stub contract from ECX/EDX/EBP/ESI and the FL frames
// (weapon_effects_hook.cpp) and the globals are bound at base + RVA (core::fl_globals).
//
// Step 4 (shields,hull,bolts; weapon_effects_bolt.h): also the per-bullet tap 0x0045ac7d (plain write: it straddles a
// qword), the muzzle 0x00446268 and the flight effect 0x004146c0 (atomic), their windows compared with the impact's
// before any write, all or none with the rest. Without an in-place draw nothing latches them: the tap records, the muzzle
// and the flight effect forward.
// Step 3 (shields,hull,explosions and shields,hull,bolts,explosions; weapon_effects_death_sites.h, core::death): the two
// death calls 0x0044ac64 (area; plain write: it straddles a qword) and 0x0044acb2 (other; atomic) -> 0x00413610, their
// windows compared with the rest before any write, all or none. Each death is recorded (core::boom::decide: fighters and
// missiles the stage's while Present latched boom_ready, which it no longer does; the big tiers, asteroids and other
// classes census and forwarded byte-exact; the rule-(b) missile and bullet area deaths forwarded unrecorded); rows
// `weapon_death` (the run's first 64, then 16 per period, every death of a capture frame) and `weapon_boom_frame` every
// 300 frames.
// Reshade (X3M_WEAPON_EFFECTS=reshade; weapon_effects_batch_site.h, docs/reverse-engineering/ap-bullet-batch-site.md):
// the impact (shields) and the bullet batch submit call 0x0046d0a7 -> 0x004bfd40 (plain write: it straddles a qword),
// claimed with the re-shade's scope stub, which forwards every submit with x3m_bullet_scope raised around it; batch
// before the impact, all or none; x3m_bullet_reshade_armed once both are live.
// Rows: `weapon_effects_patch site=impact|tap1|tap2|bolt_tap|muzzle|flight|death_area|death_other|batch va=<site> state=<site> reason=<overall> mode=<mode> setting=<value>
// write=none|atomic|plain` per site at initialize; `weapon_effects_table event= reason= rows= entries= textures=
// overrides= dropped= bytes= path=` once; the frame rows of present() below and `weapon_effects_scope frame= cleared=
// loading= sector_key= load_epoch= clears=` when a scope change clears stage-owned records. The FL edition writes one
// `weapon_effects_patch site=<site> va=<base+rva> rva=<rva> edition=fl state= reason= mode= setting= write=` row per site
// (impact, tap1, tap2, bolt_tap, muzzle, flight, death).
namespace x3m::weapon_effects {
struct Addresses {
    std::uintptr_t impact_window, impact_target, prologue;
    std::uintptr_t tap_window[2], tap_target;
    // Step 4 (shields,hull,bolts; weapon_effects_bolt_sites.h): the per-bullet tap, the muzzle and the flight effect
    // windows and callees (zero outside the bolts mode).
    std::uintptr_t bolt_tap_window = 0, bolt_tap_target = 0, muzzle_window = 0, muzzle_target = 0, flight_window = 0,
                   flight_target = 0;
    // Step 3 (the explosions modes; weapon_effects_death_sites.h): the area and other death windows and their callee.
    std::uintptr_t death_area_window = 0, death_other_window = 0, death_target = 0;
    // The reshade mode (weapon_effects_batch_site.h): the bullet batch submit's window and callee (zero outside it).
    std::uintptr_t batch_window = 0, batch_target = 0;
};
bool initialize();
bool shutdown();
#if X3M_EDITION_FL
// X3FL.exe at its running module base (64 KB aligned): every used window and witness at base + RVA, then the claims.
bool install_base(std::uintptr_t base, core::Mode mode);
#ifdef X3M_WEAPON_EFFECTS_SHIM
void fixture_base(std::uintptr_t base); // initialize()'s base instead of fl::module_base() when nonzero (the fixture)
#endif
#else
bool install_at(const Addresses& a, core::Mode mode);
#endif
bool installed();
core::Mode mode();
// The bullet re-shade's per-texture normaliser (bullet_reshade_core.h; weapon_effects.json tables.bullet_textures,
// loaded at install): the entry the bytes the engine handed D3DXCreateTextureFromFileInMemoryEx are (-1: none, no
// table, or the scope stub is not live), the entries kept, and an entry's p99 x 65535 (0 past the count; c31.w is
// reshade::texture_scale of it, inline at the caller: no float crosses a call, the i686 ABI would return it on x87).
int bullet_texture_key(const void* data, std::size_t size) noexcept;
unsigned bullet_texture_count() noexcept;
std::uint16_t bullet_texture_p99_q(unsigned key) noexcept;
// The geometric size (bullet_reshade_core.h layout_constants): the 16.16 layout words of the tables.bullet_bodies entry
// of an engine body id (the batch's [EDX+0x0c] on FL, [[esp+8]+0x0c] on AP: x3m_bullet_body), nullptr when it has
// none. *final false: the engine's TBullets was not readable yet, so a caller must not cache the miss. One
// bullet_reshade_layout row when the map is built (the first lookup that can read TBullets); then a compare per mapped
// body.
const std::int32_t* bullet_body_layout(std::uint32_t body, bool* final) noexcept;
// The ship profiles of weapon_effects.json (schema 3 ship_profiles; the lathe bubble, weapon_effects_lathe.h), loaded
// with the table into one heap block, read-only afterwards; nullptr when the file carries none (or none was kept).
const core::ShipProfiles* ship_profiles() noexcept;
const char* state();
// 0 impact, 1 tap1, 2 tap2, 3 bolt_tap, 4 muzzle, 5 flight (step 4), 6 death_area, 7 death_other (step 3), 8 batch (the
// bullet batch submit 0x0046d0a7: the re-shade's scope stub, reshade mode); FL: 6 death (the one FL call), 7 batch (the
// bullet batch submit 0x097af8: the re-shade's scope stub, reshade mode)
const char* site_state(unsigned site);
const char* write_path(unsigned site);
core::HookState& hook_state(); // the game thread's state (ring, latch, counters): the stage and Present read it
// Step 4: the bolt lists, the muzzle records and their latch (weapon_effects_bolt.h; the in-place draw reads the lists).
core::BoltHookState& bolt_hook_state();
// Step 4, once per frame after present() (shields,hull,bolts): the generation advance (the next frame's list starts
// empty) and the weapon_bolts_frame row every 300 frames with the hooks' counters; the bolt latches stay 0.
void present_bolts(unsigned long long frame, bool capture);
// Present, once per frame on the thread that runs the hooks: the next frame's latch (shields mode: owned), the
// weapon_event rows (the first 64 events of the run, then up to 8 per 300-frame period, and every event of a capture
// frame; each with pos_r = |pos| / radius, the radial region, the travel dir and its region), the forwarded records
// leaving the ring once logged, expired records (game time) leaving it, weapon_pos_check (observe) and
// weapon_effects_frame every 300 frames with the stage's period figures (patches, decals, retired, stage_us, and the
// drops: dropped, cells_dropped, hits_dropped, with spills; and dir_fallback, the records without a travel).
// The bullet re-shade's lone-path counts (MotionOutput::bullet_reshade_take_frame; bullet-draw-batch.md section 7), one
// array: [0..3] lone draws drawn re-shaded whose state the sequence normalised, per bullet_reshade_route.h lone_states
// bit (Z write, colour mask, sRGB write, separate alpha); additive: those under ONE / ONE; route_state: lone draws the
// additive route refused for a known differing state (a defect); route_unknown: those it refused on an unknown state;
// flagged_native: lone draws flagged outside the route; refused: lone draws that ended without the far flag;
// [refused_why + r - 1]: of them per LoneUnflagged reason r (1..10). The lone classic path (section 7 "Classic bodies",
// standard_lighting DEFAULT): classic: of the re-shaded lone draws those of a classic body (also in lone=), of them
// classic_flagged with the far flag; classic_refused: blended classic draws of a mapped bullet body left vanilla,
// [classic_refused_why + r]: of them per bullet_reshade_core.h ClassicRefused reason r (0..3).
constexpr unsigned lone_count_states = 0, lone_count_additive = 4, lone_count_route_state = 5,
                   lone_count_route_unknown = 6, lone_count_flagged_native = 7, lone_count_refused = 8,
                   lone_count_refused_why = 9, lone_count_classic = 19, lone_count_classic_flagged = 20,
                   lone_count_classic_refused = 21, lone_count_classic_refused_why = 22, lone_count_slots = 26;
struct StageFrame {
    unsigned patches = 0, decals = 0, retired = 0;
    // stage::Counts' drops: regions or decals the budget refused, lit cells no draw carried (the oldest clocks), live
    // hits past an owner's owner_hits newest; and the regions' spill draws.
    unsigned dropped = 0, cells_dropped = 0, hits_dropped = 0, spills = 0;
    unsigned faces_culled = 0; // face draws the stage culled (their box faces away from the eye)
    unsigned area_dropped = 0; // face draws past the frame's screen-area budget
    // The lathe bubble (stage::Counts): owners drawn on it; ship owners the stage could not draw by cause (no profile,
    // class mismatch, unreadable or row past the count, axis scale, degenerate profile, cache full) or skipped while
    // stale (the hook forwards a ship without a usable profile native: core::Counters::ships_native).
    unsigned lathes = 0, lathe_no_profile = 0, lathe_mismatch = 0, lathe_unreadable = 0, lathe_scale = 0;
    unsigned lathe_degenerate = 0, lathe_cache = 0, lathe_stale = 0;
    unsigned lathe_deferred = 0; // ships whose mesh build waits for a later frame (stage::lathe_builds_per_frame)
    unsigned lathe_entry_far = 0; // lathe records whose entry lay past the walk's reach (map_lathe of the contact)
    float stage_us = 0.f;
    bool drew = false;
    // The bullet re-shade (FL reshade mode; MotionOutput::bullet_reshade_take_frame): draws re-shaded, substitutions
    // that failed (the draw kept the game's program), bullet-program draws outside the batch submit, the substitution's
    // CPU time of the substitution sequence alone over the frame's timed draws (us; every 60th frame and capture
    // frames), the re-shaded draws the additive route converted, of them with the route's far flag; the lone path's
    // re-shaded draws (bullet-draw-batch.md section 7: a body drawn through the material submit; in none of the
    // batch's counts or time), of them with the far flag, its failed substitutions, timed draws and their CPU us.
    unsigned reshade_draws = 0, reshade_failures = 0, reshade_outside = 0, reshade_timed = 0;
    unsigned reshade_additive = 0, reshade_flagged = 0, reshade_lone = 0, reshade_lone_flagged = 0;
    unsigned reshade_lone_failures = 0, reshade_lone_timed = 0;
    unsigned reshade_lone_counts[lone_count_slots] = {}; // the lone path's state, flag and refusal counts (lone_count_*)
    float reshade_lone_us = 0.f;
    float reshade_us = 0.f;
    // The muzzle flash re-shade (FL reshade mode; MotionOutput::muzzle_reshade_take_frame): draws re-shaded, muzzle
    // draws matched (body id in the list, ONE / ONE), of the re-shaded those kept off the hull-emission route, failed
    // substitutions, timed draws and the sequence's CPU us over them.
    unsigned muzzle_draws = 0, muzzle_matched = 0, muzzle_hull = 0, muzzle_failures = 0, muzzle_timed = 0;
    float muzzle_us = 0.f;
    // The hull hit re-shade (reshade mode with an edition table; MotionOutput::hull_reshade_take_frame, in the order of
    // hull_reshade_core.h Frame): candidates, matched, effects, classic, failures, zwrite_on, cool_active, shared, timed;
    // the sequence's CPU us over the timed draws; requested: the hull_reshade_frame row is written only then.
    static constexpr unsigned hull_count_slots = 9;
    unsigned hull_counts[hull_count_slots] = {};
    float hull_us = 0.f;
    bool hull_requested = false;
};
// `scope` (core::Scope: the cockpit's sector key, the load epoch, loading) clears the stage-owned records on a load, a
// sector change or a new load epoch (note 3.1), before the frame's upkeep.
void present(unsigned long long frame, bool capture, bool owned, const StageFrame& stage, const core::Scope& scope = {});
// The resolve's cut verdict, a Reset, a load or a sector change: every stage-owned record leaves the ring.
void clear_owned();
// Counts the ring's scope changes (a load, a sector change, a new load epoch; shields mode): the stage clears its depth
// cache when the count moved since its last frame.
std::uint32_t scope_changes();
std::uint32_t game_ms(); // cfg+0x718 (FL +0x708) through engine_memory (0 when unreadable; hook_state().globals' slot)
// The engine's game-time step of the frame, cfg +0x714 (FL +0x704), ms with SETA (0 when unreadable).
std::uint32_t game_step_ms();
}
// The bullet re-shade (mode_reshade), both editions: the batch call's scope stub (FL x3m_weapon_effects_stub_batch_scope_fl,
// AP x3m_weapon_effects_stub_batch_scope), its depth counter (non-zero inside the game's bullet submit) and its call
// count; x3m_bullet_reshade_armed is set once that stub is live.
extern "C" {
extern volatile std::uint32_t x3m_bullet_scope;
extern volatile std::uint32_t x3m_bullet_scope_calls;
extern volatile std::uint32_t x3m_bullet_body; // the submitted batch's body id (FL [EDX+0x0c], AP [[esp+8]+0x0c])
extern volatile std::uint32_t x3m_bullet_reshade_armed;
extern std::uintptr_t x3m_weapon_effects_continue_batch;
}
#if X3M_EDITION_FL
extern "C" {
void x3m_weapon_effects_stub_impact_fl();
void x3m_weapon_effects_stub_batch_scope_fl();
void x3m_weapon_effects_stub_tap_fl();
void x3m_weapon_effects_stub_bolt_tap_fl();
void x3m_weapon_effects_stub_muzzle_fl();
void x3m_weapon_effects_stub_flight_fl();
void x3m_weapon_effects_stub_death_fl();
int __cdecl x3m_weapon_event_fl(const std::uint32_t* entry, std::uint32_t ecx, std::uint32_t edx, std::uint32_t ebp,
                                std::uint32_t esi);
void __cdecl x3m_weapon_tap_fl(const std::uint32_t* entry);
void __cdecl x3m_weapon_bolt_tap_fl(std::uint32_t ecx);
int __cdecl x3m_weapon_muzzle_fl(const std::uint32_t* entry, std::uint32_t edx, std::uint32_t esi);
int __cdecl x3m_weapon_flight_fl(const std::uint32_t* entry);
int __cdecl x3m_weapon_death_fl(const std::uint32_t* entry, std::uint32_t ecx, std::uint32_t edx, std::uint32_t ebp);
extern std::uintptr_t x3m_weapon_effects_continue_impact;
extern std::uintptr_t x3m_weapon_effects_continue_tap;
extern std::uintptr_t x3m_weapon_effects_continue_bolt_tap;
extern std::uintptr_t x3m_weapon_effects_continue_spawner;
extern std::uintptr_t x3m_weapon_effects_continue_death;
}
#else
extern "C" {
void x3m_weapon_effects_stub_impact();
void x3m_weapon_effects_stub_tap();
int __cdecl x3m_weapon_event(const std::uint32_t* entry);
void __cdecl x3m_weapon_tap(const std::uint32_t* entry);
extern std::uintptr_t x3m_weapon_effects_continue_impact;
extern std::uintptr_t x3m_weapon_effects_continue_tap;
// Step 4: the bolt tap, the muzzle and the flight effect.
void x3m_weapon_effects_stub_bolt_tap();
void x3m_weapon_effects_stub_muzzle();
void x3m_weapon_effects_stub_flight();
void __cdecl x3m_weapon_bolt_tap(const std::uint32_t* entry);
int __cdecl x3m_weapon_muzzle(const std::uint32_t* entry, std::uint32_t eax, std::uint32_t edx);
int __cdecl x3m_weapon_flight(const std::uint32_t* entry);
extern std::uintptr_t x3m_weapon_effects_continue_bolt_tap;
extern std::uintptr_t x3m_weapon_effects_continue_muzzle;
extern std::uintptr_t x3m_weapon_effects_continue_flight;
// Step 3: the death sites.
void x3m_weapon_effects_stub_death_area();
void x3m_weapon_effects_stub_death_other();
int __cdecl x3m_weapon_death_area(const std::uint32_t* entry, std::uint32_t ebp);
int __cdecl x3m_weapon_death_other(const std::uint32_t* entry, std::uint32_t ebp);
extern std::uintptr_t x3m_weapon_effects_continue_death;
// The reshade mode: the bullet batch submit's scope stub (no handler).
void x3m_weapon_effects_stub_batch_scope();
}
#endif
