#pragma once
#include "../../proxy/engine_far_jets_core.h"

// FL edition of the far-jet hand-off's engine read set (src/proxy/engine_far_jets.cpp; the AP offsets are
// engine_far_jets_core.h). Verified from X3FL.exe's bytes: docs/reverse-engineering/fl/cull-chain.md, "Verified
// 2026-11-03" (producer verification/results/fl-far-jets/read_set.py, read_set.json: 18 fields, 47 whole-instruction
// anchors, RVAs below). Integer offsets only, no Windows dependency (the native fixture and the host tests compile it).
//
// Two fields differ from AP:
//  - the model id moves +0x140 -> +0x144 (selector 0x09621c `mov ecx,[esi+0x144]`, setter 0x0a365d);
//  - the context scale is owned at the site (FL only): the handler copies node +0x1c and, when it equals the view's
//    context (camera +0x1c), that context's +0x2c. The selector itself loads exactly that pair on the same node
//    (0x095fc7 `mov eax,[esi+0x1c]`, 0x095fce `movss xmm2,[eax+0x2c]`), on every path to the site (dominance), and
//    no selector instruction writes node +0x1c; children inherit the context from their parent at reparent
//    (0x0a5824/0x0a5827), and the sector scene passes camera +0x1c as the walker's context (0x08ba48). The plume
//    stage then builds the far record from the owned value instead of dereferencing context +0x2c later.
// Unchanged (same offsets as AP, each anchored in read_set.json): parent +0x18, handle +0x28, scales +0x70/+0x80/+0x88,
// position +0xb0..+0xb8, basis rows +0xc0..+0xc8 and +0xe0..+0xe8, flags +0x12c (bit 26) / +0x130 (JET pair),
// thresholds +0x1d8 (own and parent), camera handle +0x28 and context +0x1c, context scale +0x2c, parent radius +0xa4.
namespace x3m::fl::engine_far_jets_fl {
namespace core = x3m::engine_far_jets::core;
constexpr unsigned model_offset = 0x144;       // AP core::model_offset 0x140
constexpr unsigned node_context_offset = 0x1c; // the node's scene context (FL owned scale)
static_assert(core::parent_offset == 0x18 && core::handle_offset == 0x28 && core::scale70_offset == 0x70 &&
                  core::scale80_offset == 0x80 && core::scale88_offset == 0x88 && core::position_offset == 0xb0 &&
                  core::basis_x_offset == 0xc0 && core::basis_z_offset == 0xe0 && core::flags12c_offset == 0x12c &&
                  core::threshold_1d8_offset == 0x1d8 && core::view_handle_offset == 0x28 &&
                  core::view_context_offset == 0x1c && core::context_scale_offset == 0x2c,
              "every other far-copy field sits at the AP offset on X3FL.exe (read_set.json)");
static_assert(core::model_offset == 0x140 && model_offset != core::model_offset, "the model id is the moved field");
} // namespace x3m::fl::engine_far_jets_fl
