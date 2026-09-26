#pragma once
// CPU-side ABI for rcas.hlsl, the post-resolve sharpen (docs/architecture/
// temporal-integration.md, "Post-resolve sharpen"). This module owns no D3D
// interfaces or GPU state; src/renderer/temporal_pass.cpp (8-bit route) and
// src/renderer/hdr_pass.cpp (HDR write-back) upload the register before the
// sharpened draw. One register, uploaded once per frame.
#include <cmath>

namespace x3::temporal {
// rcas.hlsl binds c23; the resolve owns c0..c7 and c22 (resolve.h), the AgX
// block c8..c21 (agx.h).
constexpr unsigned kSharpenRegister = 23;
// X3M_TAA_SHARPEN maps its (0, 1] onto the published sharpness-in-stops
// parameter: stops = kSharpenMaxStops * (1 - sharpness), gain = exp2(-stops).
// 1 is the strongest published setting (0 stops, gain 1); 0.5 is one stop
// (gain 0.5); 0.25 is one and a half (gain 0.354).
constexpr float kSharpenMaxStops = 2.f;

struct SharpenConstants {
    float values[4]{1.f, 0.f, 0.f, 0.f}; // c23: gain, 1/width, 1/height, 0
};

// sharpness in [0, 1] is the switch's domain; 0 means the pass is off and is
// refused here because nothing must be drawn with it.
inline bool valid_sharpen(float sharpness) noexcept {
    return std::isfinite(sharpness) && sharpness >= 0.f && sharpness <= 1.f;
}

inline bool prepare_sharpen(SharpenConstants& out, float sharpness, unsigned width, unsigned height) noexcept {
    if (!valid_sharpen(sharpness) || sharpness <= 0.f || !width || !height) return false;
    out.values[0] = std::exp2(-kSharpenMaxStops * (1.f - sharpness));
    out.values[1] = 1.f / float(width);
    out.values[2] = 1.f / float(height);
    out.values[3] = 0.f;
    return true;
}
} // namespace x3::temporal
