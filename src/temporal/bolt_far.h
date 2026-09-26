#pragma once
// CPU-side ABI of bolt_far.hlsl (docs/architecture/bolts-through-taa.md, B'):
// the post-resolve composite of the write-back programs and the bloom
// extract. This module owns no D3D interfaces or GPU state; hdr_pass.cpp and
// bloom_pass.cpp bind the pre-resolve FP16 scene at s2 and upload c29 before
// their draws. The flag itself is written by the late bullet draw's PS variant
// (linear_emission_sm1.cpp, AdditiveGain with far_flag: oC2 = K * max(rgb))
// and raised by the camera-gate resolve in its output alpha (resolve.hlsl).
#include <cmath>

namespace x3::temporal {
// bolt_far.hlsl binds s2 and c29; the AgX block is c8..c21 (agx.h), RCAS c23
// (sharpen.h), the bloom block c24..c28 (bloom.h).
constexpr unsigned kBoltShowRegister = 29;
constexpr unsigned kBoltSceneSampler = 2;
// K of the lane flag: g = base + K * max(rgb) under the draw's ONE/ONE blend
// (base in [-1, 1] on every lane texel), or K * max(rgb) without MRT
// post-pixel-shader blending; g > 1 is a bolt exactly when max(rgb) > 2 / K.
constexpr float kBoltFlagScale = 32.f;
// The resolve's flag: output alpha = -(1 + held), held in (0, 1]: at most
// kBoltFlagAlpha. A blended scene alpha is never negative; the game's additive
// draws sum alpha past any positive threshold (two overlapping bolts reach 3).
constexpr float kBoltFlagAlpha = -1.f;

struct BoltShowConstants {
    float values[4]{0.f, 0.f, 0.f, 0.f}; // c29: W (0 = off), 0, 0, 0
};

// X3M_BOLT_FAR_SHOW in [0, 1]; 0 turns the composite off (the flag is still
// written and raised: the write-back then copies every texel as before).
inline bool valid_bolt_show(float show) noexcept {
    return std::isfinite(show) && show >= 0.f && show <= 1.f;
}
inline BoltShowConstants prepare_bolt_show(float show) noexcept {
    BoltShowConstants out;
    out.values[0] = valid_bolt_show(show) ? show : 0.f;
    return out;
}
} // namespace x3::temporal
