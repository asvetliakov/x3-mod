#pragma once
// The render-node fields the proxy-side draw culls read through the draw's scope node (cull_small_props_core.h,
// occlusion_cull_core.h): parent, handle and model id. The same offsets in both editions except the model id,
// X3AP.exe +0x140, X3FL.exe +0x144 (docs/reverse-engineering/fl/cull-chain.md, "5. Node layout"). The FL values are
// object_trace_fl_inc.h's node_parent/node_handle/node_model, generated from X3FL.exe's operands; motion_output.cpp
// static_asserts them against `fl`. `edition` is the build's layout (no edition define, as in the host fixtures that
// compile the cores directly, means AP). Pure: no Windows.
namespace x3m::engine_node_layout {
struct Layout {
    unsigned parent, handle, model;
};
constexpr Layout ap{0x18, 0x28, 0x140};
constexpr Layout fl{0x18, 0x28, 0x144};
static_assert(ap.handle == ap.parent + 0x10 && fl.handle == fl.parent + 0x10,
              "parent .. handle in one 20-byte read (cull_small_props::core::Culler::walk)");
#if X3M_EDITION_FL
constexpr Layout edition = fl;
#else
constexpr Layout edition = ap;
#endif
} // namespace x3m::engine_node_layout
