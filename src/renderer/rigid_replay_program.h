#pragma once
#include "rigid_position.h"
#include <array>

namespace x3m::renderer {
// Original, fixed SM3 program. c0..3 are current submitted rows; c4..7 are
// previous submitted rows. Local DEF c8=(1,+0,+0,+0) overrides application c8.
// POSITION0 is converted by the original FLOAT3/FLOAT16_4 declaration, then
// MAD constructs xyz and W from input X. Stored input W is never consumed.
// Outputs are current POSITION0 and previous TEXCOORD0, in XYZW order.
// No matrix multiplication, jitter adjustment, HLSL compilation or allocation.
const std::array<std::uint32_t, 54>& rigid_replay_program() noexcept;

// Exact whole-program lookup first, then the qualified source operation/model
// family. Never accepts a caller-authored profile as proof. This is only shader
// compatibility: finite payload, original declaration conversion, buffer
// stability, lifetime/correspondence and opaque coverage remain caller gates.
// Legacy source models and WXYZ output order deliberately remain unsupported.
const RigidPositionProfile* find_rigid_replay_profile(
    const std::uint32_t* words, std::size_t word_count) noexcept;
} // namespace x3m::renderer
