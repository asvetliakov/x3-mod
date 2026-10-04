#pragma once
#include <cstdint>
#include "engine_effects_core.h"
#include "engine_plumes_core.h"

// Engine effects, phases 1a and 2 (docs/architecture/engine-effects-modern.md sections 1-6): the process-wide part of
// the glow-jet suppression. The option X3M_ENGINE_EFFECTS=native|off|plumes (ini engine_effects, default plumes) is
// read once at load and never toggled (a switch mid-flight would freeze native sprites and trails); plumes suppresses
// like off and arms the proxy's plume stage (motion_output_engine_plumes_inc.h) with the preset read here. The per-draw recogniser, the record ring and the census rows live in the
// motion route (motion_output_engine_effects_inc.h), which reads the shadow it already keeps. This module owns:
// - the executable identity gate (object_trace::executable_verified(), evaluated at initialize): without it nothing
//   is suppressed (fail closed);
// - the body table <EXE directory>\x3m\engine_bodies.json (X3M_ENGINE_BODIES overrides the path, 0 or none disables),
//   loaded once on the render thread at the first begin_frame, like the fog family table; a malformed file leaves
//   count 0 (every record unknown_body) and one engine_effects_bodies row;
// - the names -> ids resolution over the engine's body table (the lens_flare_cull recipe) at begin_frame: the table
//   header every frame, at most 32 mapped dynamic names re-read per frame round-robin, a scan when the table moves,
//   shrinks, grows or re-binds a name. Render thread only (the one that draws), so no synchronisation.
namespace x3m::engine_effects {
// Load path (initialize_log, after the config resolver): parses the option, checks the identity, logs one
// engine_effects_mode row (and, for plumes, one engine_effects_plumes row with the preset). LastError preserved.
void initialize();
core::Mode mode();
// X3M_ENGINE_EFFECTS_PRESET=restrained|default|strong (ini engine_effects_preset), read once at initialize (only with
// plumes); unset, refused or another mode = default. The plume stage's starting preset (Ctrl+Alt+F6 cycles it per
// device).
engine_plumes::Preset preset();
// X3M_ENGINE_PLUME_NOZZLE (ini engine_plume_nozzle): the plume's nozzle width in value, one plain decimal in 0.1..1.0,
// read once at initialize (only with plumes); unset or refused = 0.5 (engine_plumes::parse_nozzle). The look's
// proportions: L = z value is 2 / nozzle nozzle widths at full throttle.
float plume_nozzle();
// X3M_ENGINE_PLUME_FLOOR (ini engine_plume_floor): the plume floor's scale of the k(R) curve in 0..3 (0: the floor
// off), one plain decimal read once at initialize (only with plumes); unset or refused = default_look.floor_scale (0.5;
// engine_plumes::parse_floor). A main jet draws at least scale x k(R) x its ship's root-node radius R (k 0.35 at R <= 150
// record units, 0.25 at 500, 0.10 at >= 5,000, log-linear between), at most 4 x its own value.
float plume_floor();
// The motion route's gates: hook = the per-draw recogniser runs (off|plumes with the identity verified, or native
// under --debug for the census); suppress = a recognised draw is not forwarded.
bool hook_wanted();
bool suppress();
// The arming signal of the suppression: both call redirects of engine_effects_patch live (installed()). The motion
// route reads it once per frame; without it a recognised draw is forwarded (forwarded_patch_missing), so the glow never
// disappears while the native sprites, lens flares and trails stay.
bool redirects_live();
const char* status();
// Present path, render thread, only while hook_wanted(): the table load on the first call, then the resolution.
void begin_frame(unsigned long long frame);
// Per draw: the table entry of a model id (-1 unknown) and the entry itself (null for -1).
int body_for_model(std::uint32_t model);
const core::Body* body(int index);
struct Stats {
    unsigned bodies, refused, resolved, mapped, scanned, restarts;
    bool table_loaded;
};
Stats stats();
// The plume stage's travel look (gap 7 of docs/architecture/engine-exhaust-gap-analysis.md; the read of
// docs/reverse-engineering/engine-effects.md section 8): this frame's SETA dwords, warp = cfg+0xcc and mult = cfg+0xd0
// (16.16) with cfg = *0x00606f34, through engine_memory::read: the pointer, then one 8-byte read. Refused without the
// executable identity, when the 12-byte compare of the tick's reads at 0x004d1ef0 (latched at the first readable call)
// differs, on a null pointer or a failed read (of the site too: retried at the next call); the caller then uses 1.0
// (engine_plumes_core.h seta_decode validates the
// values). Render thread only (the Present path; the writers are the game's main thread, the same one). LastError kept.
enum class SetaStatus : std::uint8_t { ok = 0, identity = 1, site = 2, pointer = 3, read = 4 };
const char* seta_status_name(SetaStatus);
SetaStatus seta_read(std::uint32_t* warp, std::uint32_t* mult);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Fixture seams (the motion seam DLL): the identity treated as verified before initialize, and a synthetic body
// manager's global in the fixture's own memory.
void fixture_identity(bool verified);
void fixture_redirects(bool live); // the redirects treated as live (the fixture EXE has no engine sites)
void fixture_body_global(std::uintptr_t va);
// The SETA read's site and pointer slot moved to the fixture's own memory (a synthetic tick and configuration block).
void fixture_seta(std::uintptr_t site_va, std::uintptr_t slot_va);
#endif
}
