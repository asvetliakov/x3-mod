#pragma once
#include <cstdint>

// Engine-side lens-flare cull (X3M_LENS_FLARE_GAIN=0; lens_flare_cull_core.h;
// docs/reverse-engineering/lod-selection.md, "Lens-flare cull on the
// small-parts site"; docs/verification/sun-occlusion.md). A second stub on the
// small-parts claim of the per-node cull/LOD pass 0x0047cfe0 at 0x0047d2a2
// (cull_small_parts.h chain_stub: one engine_patch claim, both stubs, restored
// once by cull_small_parts::shutdown()): a node whose model id (+0x140) is in
// the flare body bitmap takes the engine's own size-cull instruction at
// 0x0047d2c3, so the lens block's cull walk (0x00472471 -> 0x0047e780 ->
// 0x0047cfe0 per root) clears its renderable bit and the traversal 0x0047e6e0
// never issues its draw. The bitmap is filled on the render thread from the
// engine's body table by name (the 42 names of core::body_names), first at
// the claim and then at begin_frame whenever the table grows or moves (a
// `v\010xx` sprite is a dynamic registration, made when a scene first
// references it; a game load re-binds every dynamic id). Fail closed: with the
// window, the lens block, the walker call or the model read not the verified
// bytes, or no name mapped, the flag stays 0 and the proxy-side skip
// (lens_flare_gain.h, Law::skip) stands alone. The stub is straight-line
// integer code emitted from the core header: no handler runs inside the pass.
namespace x3m::lens_flare_cull {
// Backend-load path only, after cull_small_parts::initialize(); gain_zero = X3M_LENS_FLARE_GAIN parsed as exactly 0.
// Logs one `lens_flare_cull status=patched|off|refused reason= bodies=N` row.
bool initialize(bool gain_zero);
bool shutdown(); // disarms the flag; the shared site's bytes go back in cull_small_parts::shutdown()
// Chains the stub on the shared claim of the given site (the fixture passes a synthetic copy of the engine window);
// cull_target is the address of the engine's `and [edi+0x12c],~2` (window offset 47). The set is resolved by
// begin_frame(); the flag is published only while gain_zero and at least one name is mapped.
bool install_at(std::uintptr_t site, std::uintptr_t cull_target, bool gain_zero);
const char* state();
bool installed();
std::uintptr_t stub_address();
// Per frame in the Present hook (render thread, the one that runs the pass): reads the body table header (two
// bounded reads) and the names of the mapped dynamic slots, restarts the resolution when the table moved, shrank
// or re-bound a mapped name, resolves the names not yet found when it grew, publishes the flag.
void begin_frame(unsigned long long frame);
// Every 300 frames under --perf/--debug: `lens_flare_cull culled=N total=M ...` and the window count restarts.
void report(unsigned long long frame);
struct Stats {
    std::uint32_t culled, total, bodies, mapped, scanned, dynamic, restarts;
    bool enabled;
};
Stats stats();
#ifdef X3M_CULL_CENSUS_FIXTURE
void set_body_table_global(std::uintptr_t va); // the fixture points the reader at a synthetic manager
#endif
}
// The words the stub reads and writes: the flag (1 = test the bitmap), the bitmap of flare body ids (bit = id) and
// the running count of nodes sent down the cull path (render thread only; report() differences it).
extern "C" volatile std::uint32_t x3m_lens_flare_cull_enabled;
extern "C" volatile std::uint32_t x3m_lens_flare_cull_culled;
extern "C" std::uint32_t x3m_lens_flare_cull_bitmap[0x8000 / 32];
