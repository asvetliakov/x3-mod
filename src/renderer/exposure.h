#pragma once
// Exposure model of the FP16 HDR scene path, stage 2 (docs/architecture/
// hdr-scene-path.md section 3): the host-side port of
// tools/analysis/exposure_reference.py with the same names, parameters and
// defaults, so the two cannot drift (verification/analysis/
// test_exposure_port.py compiles this module natively and compares it with
// the reference). No D3D types: the GPU reduction chain lives in hdr_pass.cpp
// and hands its 1x1 result (avg_log_l) to ExposureState::step.
//
//   ev_target   = clamp(log2(key) - avg_log_l + ev_offset, ev_min, ev_max)
//   tau         = ev_target < ev_adapted ? tau_down : tau_up
//   ev_adapted += (ev_target - ev_adapted) * (1 - exp2(-dt / (tau ln 2)))
//   exposure    = exp2(ev_adapted);  k = exposure (TAA luminance weighting)
//
// The tonemap of frame n consumes the EV adapted from frame n-1's meter.
#include <cstddef>

namespace x3m::renderer {
// Switch defaults (section 3): X3M_HDR_KEY, X3M_HDR_EV (offset),
// X3M_HDR_EV_MIN/MAX, X3M_HDR_ADAPT_UP/DOWN; the dt clamp, meter floor and
// clip are fixed.
struct ExposureParams {
    float key = 0.18f;
    float ev_offset = 0.f;
    float ev_min = -8.f;
    float ev_max = 8.f;
    float tau_up = 0.4f;
    float tau_down = 1.2f;
    float dt_min = 1.f / 240.f;
    float dt_max = 1.f / 5.f;
    float meter_floor = 1e-4f;
    float meter_clip = 64.f;
};
enum class ExposureMode : unsigned { Auto = 0, Manual = 1 };
enum class ExposureDecode : unsigned { Gamma22 = 0, Srgb = 1, None = 2 };

// --- Metering (host references of the GPU chain; not run per frame) --------
// Engine-space value -> scene-linear (section 2), the decode agx.hlsl and the
// meter shader apply.
float decode_channel(float engine, ExposureDecode mode) noexcept;
float luma(float r, float g, float b) noexcept;
// Level-0 texel: log2 of the clamped decoded luminance of one scene pixel.
float meter_level0(const float rgb_engine[3], ExposureDecode mode, const ExposureParams& p) noexcept;
// True when the pixel hit meter_clip.
bool meter_clipped(const float rgb_engine[3], ExposureDecode mode, const ExposureParams& p) noexcept;
// Exact arithmetic mean of `count` level-0 texels (double accumulation).
double reduce_mean(const float* values, std::size_t count) noexcept;
// The GPU chain emulated in place: `level` (row-major width*height, at least
// as large as the chain's first level) is reduced factor x factor per step
// with edge-clamped taps; returns the 1x1 result. `scratch` must hold
// ceil(width/factor) * ceil(height/factor) floats.
float reduce_chain(float* level, float* scratch, unsigned width, unsigned height, unsigned factor = 4) noexcept;

// --- Adaptation --------------------------------------------------------------
float ev_target(float avg_log_l, const ExposureParams& p) noexcept;
float clamp_dt(float dt, const ExposureParams& p) noexcept;
float adapt_rate(float dt, float tau) noexcept;
float adapt(float ev_adapted, float ev_target_value, float dt, const ExposureParams& p) noexcept;
float exposure_multiplier(float ev) noexcept;
float resolve_ev(ExposureMode mode, float ev_manual, float ev_adapted) noexcept;

// --- TAA luminance weighting (stage 3 consumes it; exported only) ----------
float taa_k(float exposure) noexcept;
float luma_weight(float luma_value, float k) noexcept;
void weight_color(float rgb[3], float k) noexcept;
void unweight_color(float rgb_weighted[3], float k) noexcept;

// The per-device adaptation state: ev_adapted, the EV the next tonemap
// consumes and the last meter. `step` is exposure_reference.simulate's loop
// body; `reset` returns to the initial EV (a dimension change).
class ExposureState {
public:
    explicit ExposureState(const ExposureParams& params = ExposureParams{}, ExposureMode mode = ExposureMode::Auto,
                           float ev_manual = 0.f, float ev_initial = 0.f) noexcept;
    void configure(const ExposureParams& params, ExposureMode mode, float ev_manual) noexcept;
    void reset() noexcept;
    // One frame: the meter of the previous frame (avg_log_l) and the interval
    // since that frame's boundary (seconds, clamped inside). Returns the EV
    // the next tonemap consumes.
    float step(float avg_log_l, float dt) noexcept;
    // The EV the tonemap consumes now (manual: ev_manual; auto: ev_adapted).
    float ev() const noexcept { return resolve_ev(mode_, ev_manual_, ev_adapted_); }
    float exposure() const noexcept { return exposure_multiplier(ev()); }
    float k() const noexcept { return taa_k(exposure()); }
    float ev_adapted() const noexcept { return ev_adapted_; }
    float ev_target() const noexcept { return ev_target_; }
    float avg_log_l() const noexcept { return avg_log_l_; }
    float dt() const noexcept { return dt_; }
    unsigned steps() const noexcept { return steps_; }
    ExposureMode mode() const noexcept { return mode_; }
    const ExposureParams& params() const noexcept { return params_; }
private:
    ExposureParams params_{};
    ExposureMode mode_ = ExposureMode::Auto;
    float ev_manual_ = 0.f, ev_initial_ = 0.f;
    float ev_adapted_ = 0.f, ev_target_ = 0.f, avg_log_l_ = 0.f, dt_ = 0.f;
    unsigned steps_ = 0;
};
} // namespace x3m::renderer
