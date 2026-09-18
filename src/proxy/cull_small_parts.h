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
// the vanilla compare. With the threshold at 0 (option off, no valid camera
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
// X3M_CULL_SMALL_PARTS_SCOPE=bodies, the default); false = every node (`all`).
bool install_at(std::uintptr_t site, std::uintptr_t cull_target, bool bodies_only);
const char* state();
const char* scope();                // "bodies" or "all": the installed stub's scope (the default before an install)
std::uintptr_t stub_address();
double requested_px();
bool set_px(double px);             // the setting without a relaunch (fixture and diagnostics); false outside the band
// Per frame, on the thread that runs the pass: publishes the threshold for
// this frame from the given projection scale and width (0 = vanilla frame)
// and tells the census the value so its rows can name the verdict. The
// production begin_frame() reads camera_state and the recorded back-buffer
// width and calls publish().
std::int32_t publish(float m00, unsigned width);
void begin_frame();
void set_backbuffer_width(unsigned width);
void after_reset(unsigned width);   // disarms until the next begin_frame
// Emits the cull_small_parts_frame row for a captured frame and clears the count.
void present(unsigned long long device, unsigned long long frame, bool captured);
struct Stats { std::int32_t threshold; std::uint32_t culled; float m00; unsigned width; };
Stats stats();
}
// The words the stub reads and writes: the frame's threshold in `s` units
// (0 = off) and the per-frame count of nodes it sent down the cull path
// (every node below the threshold, including those the engine's own limit or
// degenerate-size test would have culled; the census names the difference).
extern "C" volatile std::int32_t x3m_cull_small_parts_threshold;
extern "C" volatile std::uint32_t x3m_cull_small_parts_culled;
