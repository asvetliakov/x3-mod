#pragma once
// CPU-side ABI for resolve.hlsl. This module owns no D3D interfaces or GPU state.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace x3::temporal {
struct ResolveConstants {
    float clip_to_previous[4][4]{}; // row-major rows, column-vector multiplication
    float size_jitter[4]{};
    float history[4]{};
    // Depth tolerance max(absolute, relative * |expected|), HDR limit, minimum W.
    float rejection[4]{0.0001f, 0.02f, 65000.0f, 0.000001f};
    // motion enabled, reactive masks enabled, strict sky history term (0 off, 3
    // on: under policy 2 a far-plane pixel on its own path accepts sentinel
    // history taps only, docs/architecture/seta-motion.md; the mask and snapshot
    // programs upload their own mode in this lane), depth-sentinel policy (0 off,
    // 1 sentinel pixels current-only, 2 camera path at the far plane).
    float options[4]{};
    // c22 (kLuminanceRegister; c8..c21 belong to the AgX block, agx.h): x is k
    // of the reversible luminance weighting w = 1 / (1 + k * luma) the resolve
    // applies to the current colour, its 3x3 neighbourhood and every history
    // tap before the statistics, the clip and the blend, inverting afterwards
    // (docs/architecture/hdr-scene-path.md, section 3). 0 is the exact
    // identity (the 8-bit route, and the migration test); the HDR route uploads
    // the exposure multiplier the write-back applies to the resolved image.
    // y is 0 (no remaining program reads it; the global filtered current
    // sample was removed 2026-09-23, cleanup batch 6); z is the alpha-history
    // flag and w A of the far stabiliser's masked filter, both set by the pass.
    float luminance[4]{};
};
static_assert(sizeof(ResolveConstants) == 9 * 4 * sizeof(float));
constexpr unsigned kResolveRegisterCount = 8; // c0..c7, uploaded as one block
constexpr unsigned kLuminanceRegister = 22;   // ResolveConstants::luminance
// c24 of the flicker-suppression variants (resolve_thin.hlsl, resolve_age.hlsl;
// c23 is the sharpen's): thin-clip S, age wmax, speed LO, 1 / (HI - LO). The
// alpha-history flag is c22.z. The plain programs read neither.
constexpr unsigned kFlickerRegister = 24;
// c25 of the age variants (resolve_age.hlsl, resolve_far*.hlsl), uploaded with c24 as one
// two-register block: x = the exit floor squared (px^2) of the strict sky history's exit
// reset (docs/architecture/seta-sky-hull-share-decay.md), kSkyHistoryExitOff when the
// option is off or the strict term is not in effect; yzw = A, B, F of the motion history
// weight (docs/architecture/taa-motion-history-weight.md): the age variants cap the history
// keep weight at saturate(max(F, p^2 A + B)), p^2 the squared translation parallax of the
// pixel's correspondence against the rotation-only camera path (px^2, the band term's
// quantity). Off uploads 0, 1, 1: the cap is exactly 1 and min(keep, 1) is keep bit for bit.
// The thin (non-age) programs read c24 only. The exit option is 0 (off) or within
// [kSkyHistoryExitMin, band threshold]: at the band threshold the mark and the band refusal
// coincide, so nothing is left to reset.
constexpr unsigned kExitRegister = 25;
static_assert(kExitRegister == kFlickerRegister + 1);
constexpr float kSkyHistoryExitMin = .125f, kSkyHistoryExitOff = 1e30f;
inline bool valid_sky_history_exit(float px, float band_px) noexcept {
    return std::isfinite(px) && (px == 0 || (px >= kSkyHistoryExitMin && px <= band_px));
}
// The lane the age variants read. With the floor off or the strict term absent every
// pixel's mark is 0 (a band pixel below the band threshold never reaches 1e30 px^2 of
// parallax), so the age target holds the same positive counts as before the option. yzw
// are written as the motion weight's off triple; prepare_motion_weight (after this) sets them.
inline void prepare_exit(float out[4], float px, bool strict_sky_term) noexcept {
    out[0] = strict_sky_term && px > 0 ? px * px : kSkyHistoryExitOff;
    out[1] = 0.f;
    out[2] = out[3] = 1.f;
}
// Motion history weight (X3M_TAA_MOTION_WEIGHT=F[,V0,V1]): F 0 is off; else 0.5 <= F < 1 with
// 0 <= V0 < V1 <= 64 px/frame. The cap is 1 at or below V0 px/frame of parallax, F at or above
// V1, linear in px^2 between (no square root): A = -(1 - F) / (V1^2 - V0^2), B = 1 - A V0^2.
constexpr float kMotionWeightMin = .5f, kMotionWeightSpeedMax = 64.f;
inline bool valid_motion_weight(float f, float v0, float v1) noexcept {
    if (!std::isfinite(f) || f < 0) return false;
    if (f == 0) return true;
    return f >= kMotionWeightMin && f < 1 && std::isfinite(v0) && std::isfinite(v1) && v0 >= 0 && v1 > v0 &&
           v1 <= kMotionWeightSpeedMax;
}
// c25.yzw. Off (or an invalid triple: fail closed to the identity) uploads A 0, B 1, F 1.
inline void prepare_motion_weight(float out[4], float f, float v0, float v1) noexcept {
    if (!valid_motion_weight(f, v0, v1) || f == 0) {
        out[1] = 0.f;
        out[2] = out[3] = 1.f;
        return;
    }
    const float a = -(1.f - f) / (v1 * v1 - v0 * v0);
    out[1] = a;
    out[2] = 1.f - a * v0 * v0;
    out[3] = f;
}
constexpr float kAdaptiveWeightMax = .99f;                          // upper bound of WMAX
constexpr float kAdaptiveLoDefault = .1f, kAdaptiveHiDefault = .5f; // px/frame
constexpr float kAgeLimit = 64.f;                                   // the age target saturates here
inline bool valid_thin_clip(float s) noexcept {
    return std::isfinite(s) && s >= 0 && s <= 1;
}
// wmax 0 is off; otherwise weight <= wmax <= 0.99 and 0 <= lo < hi, finite.
inline bool valid_adaptive_weight(float wmax, float lo, float hi, float weight) noexcept {
    if (!std::isfinite(wmax) || wmax < 0) return false;
    if (wmax == 0) return true;
    return wmax >= weight && wmax <= kAdaptiveWeightMax && std::isfinite(lo) && std::isfinite(hi) && lo >= 0 &&
           hi > lo && hi <= 64;
}
inline void prepare_flicker(float out[4], float thin_clip, float wmax, float lo, float hi) noexcept {
    // With the adaptive weight off the gate fields are not validated: upload zeros, never the caller's values.
    out[0] = thin_clip;
    out[1] = wmax;
    out[2] = wmax > 0 ? lo : 0.f;
    out[3] = wmax > 0 ? 1.f / (hi - lo) : 0.f;
}
// Far stabiliser (docs/architecture/taa-distant-line-fade.md section 9). The gate is a pixel footprint in world units:
// view distance z = F * p00 * width / 2 has footprint F units per pixel, and the projection maps it to device depth
// d = p22 + p32 / z. farw = saturate((d - d0) * inv) rises from 0 at footprint f0 to 1 at f1. Returns false (the caller
// uploads inv = 0: mask off) unless the projection is the engine's perspective form (p00 > 0, p22 > 0, p32 < 0) and
// 0 < f0 < f1 give finite 0 <= d0 < d1.
// Speed gate of the far weight (px/frame): full W_FAR at or below LO, the base weight at or above HI. The defaults are
// narrow on purpose (taa-distant-line-fade.md section 10): at W 0.985 the history is resampled about 65 times, which
// softens far detail as soon as it slides (replay: gradient energy x 0.78 at 0.085 px/frame under the first 0.5 .. 2
// gate).
constexpr float kFarWeightMax = .99f, kFarSpeedLo = .03f, kFarSpeedHi = .25f, kFarSpeedMax = 64.f,
                kFarFootprintMax = 1e6f;
// Far clip (X3M_TAA_FAR_CLIP; docs/architecture/taa-mask-fold.md section 4.2 addendum "far clip"): on the camera-gate
// resolve a pixel outside the thin region whose farw * openC exceeds this threshold clips its history against the 7x7
// min / max of the current colour instead of the 3x3 variance clip. kFarClipThreshold (0) takes every pixel with any
// far weight (taa-thin-classification.md section 3); kFarClipOff (2, above any product of two openness values) is the
// 3x3 clip everywhere. Valid thresholds are finite, in [0, 2].
constexpr float kFarClipThreshold = 0.f, kFarClipOff = 2.f;
inline bool valid_far_clip(float t) noexcept {
    return std::isfinite(t) && t >= 0 && t <= kFarClipOff;
}
inline bool valid_far_speed_gate(float lo, float hi) noexcept {
    return std::isfinite(lo) && std::isfinite(hi) && lo >= 0 && hi > lo && hi <= kFarSpeedMax;
}
// 0 is off; otherwise within [weight, 0.99], and only over a history that is kept at all (weight > 0).
inline bool valid_far_weight(float w, float weight) noexcept {
    return std::isfinite(w) && (w == 0 || (weight > 0 && w >= weight && w <= kFarWeightMax));
}
inline bool far_gate(float p00, float p22, float p32, unsigned width, float f0, float f1, float& d0,
                     float& inv) noexcept {
    d0 = inv = 0;
    if (!std::isfinite(p00) || !std::isfinite(p22) || !std::isfinite(p32) || !(p00 > 0) || !(p22 > 0) || !(p32 < 0) ||
        !width || !std::isfinite(f0) || !std::isfinite(f1) || !(f0 > 0) || !(f1 > f0) || f1 > kFarFootprintMax)
        return false;
    const double scale = double(p00) * width * .5, near0 = double(p22) + double(p32) / (double(f0) * scale),
                 near1 = double(p22) + double(p32) / (double(f1) * scale);
    if (!(near0 >= 0) || !(near1 > near0) || !(near1 <= 1.5)) return false;
    const float a = float(near0), b = float(near1);
    if (!(b > a)) return false; // the two depths collapse in float: no gate
    d0 = a;
    inv = 1.f / (b - a);
    return std::isfinite(inv);
}
constexpr float kLuminanceMaxK = 65504.f; // FP16 max; the weighted domain stays finite
constexpr float kCurrentFilterMax = 4.f;  // A of exp(-A d^2) (the far stabiliser's filter); the centre weight stays >=
                                          // exp(-2)
constexpr float kHistoryWeightDefault = .9f;                       // c5.z of the live route
constexpr float kHistoryWeightMin = .5f, kHistoryWeightMax = .98f; // launcher range of X3M_TAA_HISTORY_WEIGHT
inline bool valid_current_filter(float a) noexcept {
    return std::isfinite(a) && a >= 0 && a <= kCurrentFilterMax;
}

// Invalidate on camera cut, device loss/reset, resize, scene/camera regime changes,
// missing motion, exposure convention changes, and any failed resolve. Calling
// completed() is allowed only after color/depth AND any required reactive mask
// histories succeed. Unavailable coverage must not complete usable history.
struct HistoryState {
    std::uint32_t width = 0, height = 0;
    // Stable scene/camera-regime + resource-generation token. NOT a per-frame
    // counter or depth-clear count: ordinary frame rendering retains history.
    std::uint64_t epoch = 0;
    bool valid = false;
    void invalidate() noexcept { valid = false; }
    void begin(std::uint32_t w, std::uint32_t h, std::uint64_t e) noexcept {
        if (w != width || h != height || e != epoch) valid = false;
        width = w;
        height = h;
        epoch = e;
        if (!w || !h) valid = false;
    }
    void completed() noexcept { valid = width != 0 && height != 0; }
};
// Populate constants from pixel displacement, whose positive Y points down.
// Matrix must exclude jitter; equal-sized histories are required. The current
// jitter (c4.zw) is consumed by the camera path only. The previous jitter is
// still packed into c5.xy for ABI stability but the shader does not read it:
// history is the accumulated output on the unjittered grid and is sampled at
// the previous unjittered position, so no previous-jitter term exists.
// sentinel_camera (with depth_sentinel_reactive) reprojects sentinel pixels
// through the camera matrix at the far plane instead of keeping them
// current-only; only valid when matrix_rows is a real camera reprojection.
// sentinel_strict_sky (with sentinel_camera) uploads the strict sky term c7.z = 3:
// geometry history never proves a far-plane pixel whose 3x3 holds no geometry;
// ignored otherwise.
// luminance_k is the weighting constant (c22.x): finite, 0 <= k <= 65504.
inline bool prepare(ResolveConstants& out, const HistoryState& state, const float* matrix_rows, float current_x,
                    float current_y, float previous_x, float previous_y, float weight, bool motion_enabled,
                    bool reactive_enabled = false, bool depth_sentinel_reactive = false, bool sentinel_camera = false,
                    float luminance_k = 0.f, bool sentinel_strict_sky = false) noexcept {
    if (!matrix_rows || !state.width || !state.height || !std::isfinite(weight) || weight < 0 || weight > 1 ||
        !std::isfinite(current_x) || !std::isfinite(current_y) || !std::isfinite(previous_x) ||
        !std::isfinite(previous_y) || !std::isfinite(luminance_k) || luminance_k < 0 || luminance_k > kLuminanceMaxK)
        return false;
    for (unsigned i = 0; i < 16; ++i) {
        if (!std::isfinite(matrix_rows[i]) || std::fabs(matrix_rows[i]) > 1e15f) return false;
        out.clip_to_previous[i / 4][i % 4] = matrix_rows[i];
    }
    if (!std::isfinite(out.rejection[0]) || out.rejection[0] < 0 || !std::isfinite(out.rejection[1]) ||
        out.rejection[1] < 0 || !std::isfinite(out.rejection[2]) || out.rejection[2] <= 0 || out.rejection[2] > 65000 ||
        !std::isfinite(out.rejection[3]) || out.rejection[3] <= 0)
        return false;
    out.size_jitter[0] = 1.f / state.width;
    out.size_jitter[1] = 1.f / state.height;
    out.size_jitter[2] = current_x / state.width;
    out.size_jitter[3] = current_y / state.height;
    out.history[0] = previous_x / state.width;
    out.history[1] = previous_y / state.height;
    out.history[2] = weight;
    out.history[3] = state.valid ? 1.f : 0.f;
    out.options[0] = motion_enabled ? 1.f : 0.f;
    out.options[1] = reactive_enabled ? 1.f : 0.f;
    out.options[2] = depth_sentinel_reactive && sentinel_camera && sentinel_strict_sky ? 3.f : 0.f;
    out.options[3] = depth_sentinel_reactive ? (sentinel_camera ? 2.f : 1.f) : 0.f;
    out.luminance[0] = luminance_k;
    out.luminance[1] = out.luminance[2] = out.luminance[3] = 0.f;
    return true;
}
} // namespace x3::temporal
