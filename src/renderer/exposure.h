#pragma once
// Exposure model of the FP16 HDR scene path, stage 2 (docs/architecture/
// hdr-scene-path.md section 3 and "Stage 2 implementation", the space-aware
// meter): the host-side port of tools/analysis/exposure_reference.py with the
// same names, parameters and defaults, so the two cannot drift (verification/
// analysis/test_exposure_port.py compiles this module natively and compares
// it with the reference). No D3D types: the GPU reduction chain lives in
// hdr_pass.cpp and hands its tile image (one log2-luminance mean and one
// log2-luminance maximum per tile) to meter_statistics, whose result
// ExposureState::step consumes.
//
//   tile        (mean of log2 L, max of log2 L) over the tile's pixels, L clamped to [floor, clip]
//   weight      w(tile) = edge + (1 - edge) * (1 + cos(pi * r / r_corner)) / 2   (r: distance from the frame centre)
//   lit         tile.mean >= log2(meter_bg)                (the black sky is excluded)
//   neutral     lit < meter_min_lit * tiles                (nothing to meter: EV 0)
//   d           = log2(key) - weighted median(lit tile means)
//   ev_key      = (d >= 0 ? d : d * key_pull) + ev_offset  (neutral: ev_offset)
//   ev_limit    = log2(white_target * tonemap_white) - p99(tile max)   (unweighted)
//   ev_fresh    = clamp(min(ev_key, ev_limit), ev_min, ev_max)
//   ev_target   = |ev_fresh - ev_target| > ev_deadband ? ev_fresh : ev_target   (held otherwise)
//   tau         = ev_target < ev_adapted ? tau_down : tau_up
//   ev_adapted += (ev_target - ev_adapted) * (1 - exp2(-dt / (tau ln 2)))
//   exposure    = exp2(ev_adapted);  k = exposure (TAA luminance weighting)
//
// The tonemap of frame n consumes the EV adapted from frame n-1's meter.
#include <cstddef>

namespace x3m::renderer {
// Switch defaults (section 3 and the stage-2 meter section): X3M_HDR_KEY,
// X3M_HDR_EV (offset), X3M_HDR_EV_MIN/MAX, X3M_HDR_ADAPT_UP/DOWN,
// X3M_HDR_METER_BG, X3M_HDR_METER_MIN_LIT, X3M_HDR_WHITE_TARGET,
// X3M_HDR_KEY_PULL, X3M_HDR_EV_DEADBAND, X3M_HDR_METER_EDGE_WEIGHT; the dt
// clamp, meter floor and clip are fixed.
struct ExposureParams {
    float key = 0.18f;
    float ev_offset = 0.f;
    float ev_min = -3.f;
    float ev_max = 2.f;
    float tau_up = 0.4f;
    float tau_down = 1.2f;
    float dt_min = 1.f / 240.f;
    float dt_max = 1.f / 5.f;
    float meter_floor = 1e-4f;
    float meter_clip = 64.f;
    float meter_bg = 1.f / 512.f; // tiles whose geometric-mean luminance is below this are background
    float meter_min_lit = 0.01f;  // lit fraction below which the target is neutral (EV 0 + offset)
    float white_target = 0.9f;    // fraction of the tonemapper's white the p99 tile maximum may reach
    float key_pull = 0.25f;       // fraction of the key rule applied when the lit median is brighter than the key
    float ev_deadband = 0.25f;    // the held target moves only when the fresh target differs from it by more than this
    float meter_edge_weight = 0.35f; // tile weight at the frame corners (1 at the centre) for the lit statistic
};
enum class ExposureMode : unsigned { Auto = 0, Manual = 1 };
enum class ExposureDecode : unsigned { Gamma22 = 0, Srgb = 1, None = 2 };
// The AgX input that maps to full white: exp2(max_ev), max_ev = 4.026069
// (agx_reference.MAX_EV; the sigmoid saturates there).
inline constexpr float kTonemapWhite = 16.2917424f;
// The chain reduces 4x per axis until neither tile-image axis exceeds this.
inline constexpr unsigned kMeterTileMax = 128;

// Configuration-only parser for the six meter controls. `length` is the
// GetEnvironmentVariableW return value for a 32-wchar buffer, so >=32 means
// missing/truncated storage and must be refused before reading it. A valid
// complete finite value in [low, high] replaces output; refusal leaves it intact.
bool parse_meter_parameter(const wchar_t* text, std::size_t length, float low, float high, float& output) noexcept;

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
// The GPU chain emulated in place: `mean` (and `max`, may be null; both
// row-major width*height) are reduced factor x factor per step with
// edge-clamped taps -- the mean channel averages, the max channel takes the
// maximum -- at least once, then until neither axis exceeds tile_max (1:
// down to 1x1). `scratch`
// must hold ceil(width/factor) * ceil(height/factor) floats; width/height
// return the tile image's size. Returns false on bad arguments.
bool reduce_chain(float* mean, float* max, float* scratch, unsigned& width, unsigned& height, unsigned factor = 4,
                  unsigned tile_max = 1) noexcept;

// --- The space-aware statistic ---------------------------------------------
// What one frame's tile image says, in log2 units (luma_* on the log line are
// exp2 of these): avg_log_l is the mean of every tile mean (the stage-2 1x1
// meter for continuity), lit_median_log the centre-weighted median of the
// lit tiles' means (the value the key rule maps to the key: the first tile
// of the ascending order at which the cumulative weight reaches half the lit
// weight), lit_mean_log their weighted arithmetic mean (a geometric mean of
// luminance), p99_max_log the unweighted 99th percentile of the tile maxima
// (index tiles*99/100 of the ascending order). neutral: fewer than
// meter_min_lit of the tiles (by count) are lit. Non-finite tile values are
// read as the floor.
struct MeterStatistics {
    unsigned tiles = 0, lit = 0;
    float avg_log_l = 0.f;
    float lit_fraction = 0.f;
    float lit_median_log = 0.f;
    float lit_mean_log = 0.f;
    float p99_max_log = 0.f;
    float lit_weight = 0.f; // the lit tiles' total weight
    bool neutral = true;
};
// One lit tile of the selection's working copy.
struct TileSample {
    float value;
    float weight;
};
// The centre weights of a width x height tile image (row-major, `out` holds
// width*height floats): a raised cosine of the distance from the frame
// centre, 1 at the centre, edge_weight at the corners.
void tile_weights(float* out, unsigned width, unsigned height, float edge_weight) noexcept;
// `weights` may be null (every tile weighs 1); `scratch` must hold `tiles`
// samples (the working copy).
MeterStatistics meter_statistics(const float* mean_log, const float* max_log, const float* weights, unsigned tiles,
                                 TileSample* scratch, const ExposureParams& p) noexcept;
// The fresh target of one statistic: ev_target = clamp(min(ev_key, ev_limit)).
struct ExposureTarget {
    float ev_key = 0.f, ev_limit = 0.f, ev_target = 0.f;
};
ExposureTarget exposure_target(const MeterStatistics& m, const ExposureParams& p) noexcept;
// The dead band: the held target `held` moves to `fresh` only when the two
// differ by more than ev_deadband (`first`: no held target yet, take fresh).
float apply_deadband(float held, float fresh, bool first, const ExposureParams& p) noexcept;
// The key rule alone on one log2 luminance (the lit median): the lift towards
// the key in full, the pull down scaled by key_pull, plus the offset.
float ev_key(float lit_median_log, const ExposureParams& p) noexcept;
// The highlight limit alone: log2(white_target * kTonemapWhite) - p99_max_log
// (ev_max when white_target is not positive).
float ev_limit(float p99_max_log, const ExposureParams& p) noexcept;

// --- Adaptation --------------------------------------------------------------
float ev_target(const MeterStatistics& m, const ExposureParams& p) noexcept;
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
    // One frame: the statistic of the previous frame's tile image and the
    // interval since that frame's boundary (seconds, clamped inside). Returns
    // the EV the next tonemap consumes.
    float step(const MeterStatistics& meter, float dt) noexcept;
    // The EV the tonemap consumes now (manual: ev_manual; auto: ev_adapted).
    float ev() const noexcept { return resolve_ev(mode_, ev_manual_, ev_adapted_); }
    float exposure() const noexcept { return exposure_multiplier(ev()); }
    float k() const noexcept { return taa_k(exposure()); }
    float ev_adapted() const noexcept { return ev_adapted_; }
    float ev_target() const noexcept { return ev_target_; }       // the held target the adaptation drives towards
    float ev_fresh() const noexcept { return target_.ev_target; } // this step's target before the dead band
    float ev_key() const noexcept { return target_.ev_key; }
    float ev_limit() const noexcept { return target_.ev_limit; }
    float avg_log_l() const noexcept { return meter_.avg_log_l; }
    const MeterStatistics& meter() const noexcept { return meter_; }
    float dt() const noexcept { return dt_; }
    unsigned steps() const noexcept { return steps_; }
    ExposureMode mode() const noexcept { return mode_; }
    const ExposureParams& params() const noexcept { return params_; }

private:
    ExposureParams params_{};
    ExposureMode mode_ = ExposureMode::Auto;
    float ev_manual_ = 0.f, ev_initial_ = 0.f;
    float ev_adapted_ = 0.f, ev_target_ = 0.f, dt_ = 0.f;
    ExposureTarget target_{};
    MeterStatistics meter_{};
    unsigned steps_ = 0;
};
} // namespace x3m::renderer
