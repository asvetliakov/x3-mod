#pragma once
#include <cstdint>

// Projected-size cull of small parts (X3M_CULL_SMALL_PARTS_PX=<px>; unset or 0
// = vanilla, nothing patched): one trampoline on the engine's per-node cull
// and LOD pass 0x0047cfe0 at 0x0047d2a2, the instruction before the pass
// computes its effective size limit (docs/reverse-engineering/lod-selection.md,
// "Cull small parts site"; docs/architecture/engine-frame-time.md 2.3). Per
// frame the proxy converts the pixel setting into the pass's own `s = r*640/D`
// units from the live projection (P[0], camera_state) and the back-buffer
// width, exactly as the census summariser buckets nodes; a node whose `s`
// is below that threshold takes the engine's own size-cull instruction at
// 0x0047d2c3 (renderable bit cleared, nothing queued), every other node runs
// the vanilla compare, and so does a node below it that carries the engine's
// class-0 projectile marker (+0x130 & 0x20000000: bolts and beams;
// X3M_CULL_SMALL_PARTS_PROJECTILES=on|off, default on; cull_small_parts_core.h
// "Projectile exemption"). With the threshold at 0 (option off, no valid camera
// yet, a Reset) the stub is one compare and a branch. No handler call: the
// stub is straight-line integer code, LastError and the x87 stack untouched.
// Installed on the backend-load path inside the engine_patch install window
// after the exact-executable and window-byte checks, with this module pinned.
// The claim is disjoint from the census's 0x0047d258/0x0047d528 and the
// lod_scale's 0x0047d44b; all three coexist.
namespace x3m::cull_small_parts {
bool initialize();  // backend-load path only; logs one cull_small_parts line when the variable is set
bool shutdown();    // restores the site (dynamic-unload detach only); true when nothing is installed
// Verifies the window around the given site (the fixture passes a synthetic
// copy of the engine bytes), claims it, emits the stub and chains it in; the
// production initialize() passes the engine site. cull_target is the address
// of the engine's `and [edi+0x12c],~2` (window offset 47).
// bodies_only: the stub culls only parentless nodes (`[node+0x18] == 0`;
// X3M_CULL_SMALL_PARTS_SCOPE=bodies); false = every node (`all`, the default).
// exempt_projectiles: a node carrying the engine's class-0 marker
// (+0x130 & 0x20000000: bolts, beams) runs the vanilla compare
// (X3M_CULL_SMALL_PARTS_PROJECTILES=on, the default; initialize() turns it off
// when the marker's two engine instructions are not the verified bytes).
bool install_at(std::uintptr_t site, std::uintptr_t cull_target, bool bodies_only, bool exempt_projectiles);
const char* state();
const char* scope();                // "bodies" or "all": the installed stub's scope (the default before an install)
bool projectiles_exempt();          // the installed stub exempts marked projectile nodes
std::uintptr_t stub_address();
double requested_px();
bool set_px(double px);             // the setting without a relaunch (fixture and diagnostics); false outside the band
// Per frame, on the thread that runs the pass: publishes the threshold for
// this frame from the given projection scale, width and the engine's base FOV
// (binary angle, 0x4000 = the game's default; the engine's s shrinks with it)
// (0 = vanilla frame) and tells the census the value so its rows can name the
// verdict. The production begin_frame() reads camera_state (P[0] and P[5]),
// the recorded back-buffer width, derives the view's focus from the
// projection (core::focus_from_projection: the camera's +0x298, zoom
// included, no engine read) and calls publish(); only when P[5] is unusable
// while P[0] is valid does it fall back to fov::current_focus()
// (registry+0x24, else the --fov value).
std::int32_t publish(float m00, unsigned width, std::uint32_t focus = 0x4000);
void begin_frame();
void set_backbuffer_width(unsigned width);
void after_reset(unsigned width);   // disarms until the next begin_frame
// Emits the cull_small_parts_frame row for a captured frame and clears the count.
void present(unsigned long long device, unsigned long long frame, bool captured);
struct Stats { std::int32_t threshold; std::uint32_t culled, exempt; float m00; unsigned width; std::uint32_t focus; };
Stats stats();
}
// The words the stub reads and writes: the frame's threshold in `s` units
// (0 = off) and the per-frame count of nodes it sent down the cull path
// (every node below the threshold, including those the engine's own limit or
// degenerate-size test would have culled; the census names the difference),
// and the per-frame count of nodes below the threshold it let through as
// projectiles (same inclusive rule).
extern "C" volatile std::int32_t x3m_cull_small_parts_threshold;
extern "C" volatile std::uint32_t x3m_cull_small_parts_culled;
extern "C" volatile std::uint32_t x3m_cull_small_parts_exempt;
