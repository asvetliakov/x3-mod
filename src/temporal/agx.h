#pragma once
// CPU-side ABI for agx.hlsl (stage 2 of docs/architecture/hdr-scene-path.md).
// This module owns no D3D interfaces or GPU state; src/renderer/hdr_pass.cpp
// uploads the block as c8..c21 before the AgX write-back draw. The constants
// are the ones tools/analysis/agx_reference.py defines, and
// verification/analysis/test_agx_reference.py parses this file to keep the two
// identical. Provenance of every number is in that module's docstring
// (Wrensch's Minimal AgX, a reduction of Sobotka's AgX; looks from the same
// source; decode per §2).
#include <cmath>
#include <cstddef>

namespace x3::temporal {
// agx.hlsl binds c8..c21; the resolve owns c0..c7 (resolve.h).
constexpr unsigned kAgxFirstRegister = 8;
constexpr unsigned kAgxRegisterCount = 14;

constexpr float kAgxMinEv = -12.47393f;
constexpr float kAgxMaxEv = 4.026069f;
constexpr float kAgxLumaWeights[3] = {0.2126f, 0.7152f, 0.0722f}; // static const in the shader
constexpr float kAgxClampOff = 65504.f;                            // FP16 max: X3M_HDR_CLAMP unset
constexpr float kAgxDecodeGamma = 2.2f;
// Display dither of the 8-bit store (display_dither.hlsl, X3M_HDR_DITHER):
// +-0.5 code, i.e. an amplitude of one code. c8.z of the AgX programs and
// c23.w of taa_sharpen_ps.hlsl carry it; every other value there is 0.
constexpr float kDisplayDitherAmplitude = 1.f / 255.f;

enum class AgxDecode { gamma22, srgb, none };
enum class AgxLook { none, golden, punchy };

struct AgxConstants {
    float exposure[4]{1.f, kAgxClampOff, 0.f, 0.f};   // c8: exp2(EV), clamp max, display dither amplitude (0 or kDisplayDitherAmplitude), -
    float decode[4]{kAgxDecodeGamma, 0.f, 0.f, 0.f};   // c9: gamma exponent, srgb flag, none flag, -
    float inset[3][4]{                                 // c10..c12: M_in rows, w unused
        {0.842479062253094f, 0.0784335999999992f, 0.0792237451477643f, 0.f},
        {0.0423282422610123f, 0.878468636469772f, 0.0791661274605434f, 0.f},
        {0.0423756549057051f, 0.0784336f, 0.879142973793104f, 0.f}};
    float outset[3][4]{                                // c13..c15: M_out rows, w unused
        {1.19687900512017f, -0.0980208811401368f, -0.0990297440797205f, 0.f},
        {-0.0528968517574562f, 1.15190312990417f, -0.0989611408712536f, 0.f},
        {-0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f, 0.f}};
    float log_range[4]{kAgxMinEv, 1.f / (kAgxMaxEv - kAgxMinEv), kAgxMaxEv, 0.f}; // c16
    float contrast_hi[4]{15.5f, -40.14f, 31.96f, -6.868f};   // c17: x^6, x^5, x^4, x^3
    float contrast_lo[4]{0.4298f, 0.1191f, -0.00232f, 0.f};  // c18: x^2, x^1, x^0, -
    float look_slope[4]{1.f, 1.f, 1.f, 1.f};                 // c19: slope rgb, saturation
    float look_offset[4]{0.f, 0.f, 0.f, 0.f};                // c20: offset rgb, -
    float look_power[4]{1.f, 1.f, 1.f, 0.f};                 // c21: power rgb, -
};
static_assert(sizeof(AgxConstants) == kAgxRegisterCount * 4 * sizeof(float));

// Look triples (§3): golden slope (1, 0.9, 0.5) power 0.8 saturation 0.8;
// punchy slope 1 power 1.35 saturation 1.4; none is the identity CDL.
inline void set_look(AgxConstants& out, AgxLook look) noexcept {
    float slope[3]{1.f, 1.f, 1.f}, power = 1.f, sat = 1.f;
    if(look == AgxLook::golden) { slope[1] = 0.9f; slope[2] = 0.5f; power = 0.8f; sat = 0.8f; }
    else if(look == AgxLook::punchy) { power = 1.35f; sat = 1.4f; }
    for(unsigned i = 0; i < 3; ++i) {
        out.look_slope[i] = slope[i]; out.look_offset[i] = 0.f; out.look_power[i] = power;
    }
    out.look_slope[3] = sat; out.look_offset[3] = 0.f; out.look_power[3] = 0.f;
}

inline void set_decode(AgxConstants& out, AgxDecode mode) noexcept {
    out.decode[0] = mode == AgxDecode::gamma22 ? kAgxDecodeGamma : 1.f;
    out.decode[1] = mode == AgxDecode::srgb ? 1.f : 0.f;
    out.decode[2] = mode == AgxDecode::none ? 1.f : 0.f;
    out.decode[3] = 0.f;
}

// exposure_multiplier is exp2(EV_adapted) of the previous frame (exposure.h,
// stage 2); clamp_max <= 0 means X3M_HDR_CLAMP unset and uploads kAgxClampOff.
// Everything else in the block is fixed by the reference; the display
// dither (exposure[2]) is left at 0 (set_dither below).
inline bool prepare(AgxConstants& out, float exposure_multiplier, float clamp_max,
                    AgxDecode decode, AgxLook look) noexcept {
    if(!std::isfinite(exposure_multiplier) || exposure_multiplier <= 0
       || exposure_multiplier > kAgxClampOff || !std::isfinite(clamp_max)) return false;
    out.exposure[0] = exposure_multiplier;
    out.exposure[1] = clamp_max > 0 ? (clamp_max < kAgxClampOff ? clamp_max : kAgxClampOff) : kAgxClampOff;
    out.exposure[2] = out.exposure[3] = 0.f;
    set_decode(out, decode);
    set_look(out, look);
    return true;
}

// The display dither of the AgX write-back into the 8-bit target (c8.z). Only
// the production write-back sets it; the capability self test and a write
// into an FP16 staging target keep 0.
inline void set_dither(AgxConstants& out, bool dither) noexcept {
    out.exposure[2] = dither ? kDisplayDitherAmplitude : 0.f;
}
} // namespace x3::temporal
