#pragma once
#include <cstdint>

// Cull/LOD census (X3M_CULL_CENSUS=1; unset = vanilla, nothing patched): two
// read-only trampolines on the engine's per-node cull and LOD pass 0x0047cfe0
// (docs/reverse-engineering/lod-selection.md, "Cull census sites"). The
// measure site 0x0047d258 records, per node visited on a capture frame, the
// node, its model id, the LOD metric s = r*640/D, the small-object measure
// r*W/D, D, the radius, the two per-node thresholds and the effective limit;
// the exit site 0x0047d528 completes the entry with the renderable bit and the
// LOD index the pass selected, and keeps EBX when it is the model pointer the
// pass resolved (core::exit_model_pointer); on a captured frame's Present the
// rows gain the model's LOD count (word model+0x10) and up to eight record
// thresholds (record+0x34) and the body name of the model id (the engine's body
// table, docs/reverse-engineering/body-format-bob1.md 6), read through
// engine_memory::read, never inside the pass. Bounded ring of 8,192 entries, no allocation
// and no logging per node: outside capture frames each site costs the
// trampoline round trip (site jmp, dispatcher jmp, a byte compare and
// branch, jmp back and the displaced tail; measured 0.234 -> 0.244 us per
// 12-node pass), and a compare-and-store through an integer-only cdecl
// handler inside them. The rows are emitted at Present
// (`cull_census_frame` with `overflow=`, then one `cull_census` row per
// entry, ending ` lods=<n|-> thr=<t0,t1,...|-> body=<name|->`; the frame row
// ends ` culled_small_exempt_bullet=<n>`). Installed on the backend-load path inside the engine_patch install
// window after the exact-executable and window-byte checks, with this module
// pinned; a failed second claim rolls the first back.
namespace x3m::cull_census {
bool initialize();  // backend-load path only; logs one cull_census line when the variable is set
bool shutdown();    // restores both sites (dynamic-unload detach only); true when nothing is installed
// Verifies the two windows around the given sites (the fixture passes a
// synthetic copy of the engine bytes), claims them, emits the stubs and
// chains them in; the production initialize() passes the engine sites.
bool install_at(std::uintptr_t measure_site, std::uintptr_t exit_site);
const char* state();
std::uintptr_t measure_stub_address();
std::uintptr_t exit_stub_address();
// Frame telemetry, only while the patch is live. begin_frame(capture) arms the
// stubs for a capture frame (and clears the ring); present() emits the frame
// row and the entry rows when the ended frame was captured, then disarms.
void begin_frame(bool capture);
void present(unsigned long long device, unsigned long long frame, bool captured);
// The Reset path: disarms like begin_frame(false) and clears the LOD-switch table.
void reset();
// LOD-switch log (X3M_LOD_SWITCH_LOG=N, read by initialize() once the census is
// live; launcher --lod-switch-log [N], default 16): cap = rows per frame, 0 = off.
// While on, the stubs are armed on every frame (not only capture frames) and
// present() compares the selected record +0x14c of every kept entry with the same
// (node, view) on the previous frame (core::track_observe; one probe per kept
// entry, table committed once, 16,384 slots), logging
//   lod_switch frame= node= body= from= to= s= D= T_pad= flag31= view=
// per change up to cap, then one lod_switch_overflow frame= dropped= cap= row,
// and lod_switch_frame frame= switches= nodes= on frames with a switch.
// Returns false when cap is out of range or the table cannot be committed.
bool set_lod_switch_log(unsigned cap);
struct Stats { std::uint32_t entries, overflow, unmeasured, exited; bool armed; };
Stats stats();
// The small-parts stub's threshold for the current frame (cull_small_parts,
// 0 = none): rows whose `s` is below it and that the engine's own limit did
// not cull are reported as `culled_small`, except a node the stub exempts as
// a projectile (+0x130 & 0x20000000 with the exemption on), which the frame
// row counts as `culled_small_exempt_bullet=`. One plain store per frame.
void note_small_threshold(std::int32_t threshold, bool exempt_projectiles = false);
#ifdef X3M_CULL_CENSUS_FIXTURE
// Fixture build only: the image global holding the body manager pointer
// (production reads the constant core::body_global_va); the CPU fixture points
// it at a synthetic manager.
void set_body_table_global(std::uintptr_t va);
#endif
}
// The stubs' cdecl targets: integer only, no Win32 call, LastError untouched by
// construction; EBX/ESI/EDI/EBP preserved by the ABI, the stubs save EAX/ECX/EDX.
extern "C" void x3m_cull_census_measure(std::uint32_t node, std::int32_t measure, std::int32_t s, std::int32_t d, std::uint32_t view);
extern "C" void x3m_cull_census_exit(std::uint32_t node, std::uint32_t ebx, std::uint32_t model_slot, std::uint32_t d_slot);
// The byte both stubs test: non-zero only while a capture frame is armed.
extern "C" volatile unsigned char x3m_cull_census_enabled;
