#include "exposure.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace x3m::renderer {
namespace {
constexpr float kLn2 = 0.69314718055994530942f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDecodeGamma = 2.2f;
float clampf(float x, float lo, float hi) noexcept { return x < lo ? lo : x > hi ? hi : x; }
} // namespace

bool parse_meter_parameter(const wchar_t* text, std::size_t length,
                           float low, float high, float& output) noexcept {
    if (!text || !length || length >= 32 || text[length] != L'\0') return false;
    wchar_t* end = nullptr;
    const float value = std::wcstof(text, &end);
    if (end == text || end != text + length || !std::isfinite(value) || value < low || value > high) return false;
    output = value;
    return true;
}

float decode_channel(float engine, ExposureDecode mode) noexcept {
    if (mode == ExposureDecode::None) return engine;
    const float e = engine > 0.f ? engine : 0.f;
    if (mode == ExposureDecode::Gamma22) return std::pow(e, kDecodeGamma);
    return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}
float luma(float r, float g, float b) noexcept { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

float meter_level0(const float rgb_engine[3], ExposureDecode mode, const ExposureParams& p) noexcept {
    const float L = luma(decode_channel(rgb_engine[0], mode), decode_channel(rgb_engine[1], mode), decode_channel(rgb_engine[2], mode));
    return std::log2(clampf(L, p.meter_floor, p.meter_clip));
}
bool meter_clipped(const float rgb_engine[3], ExposureDecode mode, const ExposureParams& p) noexcept {
    return luma(decode_channel(rgb_engine[0], mode), decode_channel(rgb_engine[1], mode), decode_channel(rgb_engine[2], mode)) > p.meter_clip;
}
double reduce_mean(const float* values, std::size_t count) noexcept {
    if (!values || !count) return 0.;
    double sum = 0.;
    for (std::size_t i = 0; i < count; ++i) sum += values[i];
    return sum / double(count);
}
bool reduce_chain(float* mean, float* max, float* scratch, unsigned& width, unsigned& height, unsigned factor, unsigned tile_max) noexcept {
    if (!mean || !scratch || !width || !height || factor < 2 || !tile_max) return false;
    const float scale = 1.f / float(factor * factor);
    bool first = true;   // level 0 always folds one reduction, as the GPU does
    while (first || width > tile_max || height > tile_max) {
        first = false;
        const unsigned nw = (width + factor - 1) / factor, nh = (height + factor - 1) / factor;
        // Two passes over the same scratch: the mean channel, then the max channel.
        for (unsigned channel = 0; channel < (max ? 2u : 1u); ++channel) {
            float* level = channel ? max : mean;
            for (unsigned y = 0; y < nh; ++y)
                for (unsigned x = 0; x < nw; ++x) {
                    float acc = 0.f, peak = -INFINITY;
                    for (unsigned ty = 0; ty < factor; ++ty) {
                        const unsigned sy = y * factor + ty < height ? y * factor + ty : height - 1;
                        for (unsigned tx = 0; tx < factor; ++tx) {
                            const unsigned sx = x * factor + tx < width ? x * factor + tx : width - 1;
                            const float v = level[std::size_t(sy) * width + sx];
                            acc += v; peak = v > peak ? v : peak;
                        }
                    }
                    scratch[std::size_t(y) * nw + x] = channel ? peak : acc * scale;
                }
            for (std::size_t i = 0; i < std::size_t(nw) * nh; ++i) level[i] = scratch[i];
        }
        width = nw; height = nh;
    }
    return true;
}

void tile_weights(float* out, unsigned width, unsigned height, float edge_weight) noexcept {
    if (!out || !width || !height) return;
    const float edge = clampf(std::isfinite(edge_weight) ? edge_weight : 0.35f, 0.f, 1.f);
    const float corner = std::sqrt(0.5f);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const float dx = (float(x) + 0.5f) / float(width) - 0.5f, dy = (float(y) + 0.5f) / float(height) - 0.5f;
            const float r = std::sqrt(dx * dx + dy * dy) / corner;
            const float fall = 0.5f * (1.f + std::cos(kPi * (r < 1.f ? r : 1.f)));
            out[std::size_t(y) * width + x] = edge + (1.f - edge) * fall;
        }
}
MeterStatistics meter_statistics(const float* mean_log, const float* max_log, const float* weights, unsigned tiles, TileSample* scratch, const ExposureParams& p) noexcept {
    MeterStatistics m;
    const float floor_log = std::log2(p.meter_floor > 0.f ? p.meter_floor : 1e-4f);
    m.lit_median_log = m.lit_mean_log = m.p99_max_log = m.avg_log_l = floor_log;
    if (!mean_log || !max_log || !tiles || !scratch) return m;
    const float bg_log = std::log2(p.meter_bg > 0.f ? p.meter_bg : 1.f / 512.f);
    m.tiles = tiles;
    // The lit tiles' means and weights gather at the front of the scratch (the
    // selection's working copy); non-finite tiles read as the floor.
    double sum_all = 0., sum_lit = 0., weight_lit = 0.;
    for (unsigned i = 0; i < tiles; ++i) {
        const float v = std::isfinite(mean_log[i]) ? mean_log[i] : floor_log;
        sum_all += v;
        if (v >= bg_log) {
            const float w = weights ? weights[i] : 1.f;
            scratch[m.lit++] = TileSample{v, w}; sum_lit += double(v) * double(w); weight_lit += w;
        }
    }
    m.avg_log_l = float(sum_all / double(tiles));
    m.lit_fraction = float(m.lit) / float(tiles);
    m.lit_weight = float(weight_lit);
    m.neutral = double(m.lit) < double(p.meter_min_lit) * double(tiles);
    if (m.lit && weight_lit > 0.) {
        m.lit_mean_log = float(sum_lit / weight_lit);
        // The weighted median: the first sample of the ascending order at
        // which the cumulative weight reaches half the total (ties by value are
        // interchangeable, so the sort's order among equal values is immaterial).
        std::sort(scratch, scratch + m.lit, [](const TileSample& a, const TileSample& b) { return a.value < b.value; });
        const double half = weight_lit * 0.5;
        double cumulative = 0.; unsigned i = 0;
        for (; i < m.lit; ++i) { cumulative += scratch[i].weight; if (cumulative >= half) break; }
        m.lit_median_log = scratch[i < m.lit ? i : m.lit - 1].value;
    }
    for (unsigned i = 0; i < tiles; ++i) scratch[i].value = std::isfinite(max_log[i]) ? max_log[i] : floor_log;
    const unsigned p99 = std::min(tiles - 1u, unsigned((std::uint64_t(tiles) * 99u) / 100u));
    std::nth_element(scratch, scratch + p99, scratch + tiles, [](const TileSample& a, const TileSample& b) { return a.value < b.value; });
    m.p99_max_log = scratch[p99].value;
    return m;
}
float ev_key(float lit_median_log, const ExposureParams& p) noexcept {
    if (!(p.key > 0.f) || !std::isfinite(lit_median_log)) return p.ev_offset;
    const float d = std::log2(p.key) - lit_median_log;
    const float pull = clampf(p.key_pull, 0.f, 1.f);
    return (d >= 0.f ? d : d * pull) + p.ev_offset;
}
float ev_limit(float p99_max_log, const ExposureParams& p) noexcept {
    if (!(p.white_target > 0.f) || !std::isfinite(p99_max_log)) return p.ev_max;
    return std::log2(p.white_target * kTonemapWhite) - p99_max_log;
}
ExposureTarget exposure_target(const MeterStatistics& m, const ExposureParams& p) noexcept {
    ExposureTarget t;
    t.ev_key = m.neutral ? p.ev_offset : ev_key(m.lit_median_log, p);
    t.ev_limit = ev_limit(m.p99_max_log, p);
    const float ev = t.ev_key < t.ev_limit ? t.ev_key : t.ev_limit;
    t.ev_target = clampf(std::isfinite(ev) ? ev : 0.f, p.ev_min, p.ev_max);
    return t;
}
float ev_target(const MeterStatistics& m, const ExposureParams& p) noexcept { return exposure_target(m, p).ev_target; }
float apply_deadband(float held, float fresh, bool first, const ExposureParams& p) noexcept {
    if (first || !std::isfinite(held)) return fresh;
    const float band = std::isfinite(p.ev_deadband) && p.ev_deadband > 0.f ? p.ev_deadband : 0.f;
    return std::fabs(fresh - held) > band ? fresh : held;
}
float clamp_dt(float dt, const ExposureParams& p) noexcept {
    if (!std::isfinite(dt)) return p.dt_max;
    return clampf(dt, p.dt_min, p.dt_max);
}
float adapt_rate(float dt, float tau) noexcept {
    if (!(tau > 0.f)) return 1.f;
    return 1.f - std::exp2(-dt / (tau * kLn2));
}
float adapt(float ev_adapted, float ev_target_value, float dt, const ExposureParams& p) noexcept {
    dt = clamp_dt(dt, p);
    const float tau = ev_target_value < ev_adapted ? p.tau_down : p.tau_up;
    const float ev = ev_adapted + (ev_target_value - ev_adapted) * adapt_rate(dt, tau);
    return clampf(ev, p.ev_min, p.ev_max);
}
float exposure_multiplier(float ev) noexcept { return std::exp2(ev); }
float resolve_ev(ExposureMode mode, float ev_manual, float ev_adapted) noexcept {
    return mode == ExposureMode::Manual ? (std::isfinite(ev_manual) ? ev_manual : 0.f) : ev_adapted;
}

float taa_k(float exposure) noexcept { return std::isfinite(exposure) && exposure >= 0.f ? exposure : 0.f; }
float luma_weight(float luma_value, float k) noexcept { return 1.f / (1.f + k * luma_value); }
void weight_color(float rgb[3], float k) noexcept {
    const float w = luma_weight(luma(rgb[0], rgb[1], rgb[2]), k);
    for (unsigned i = 0; i < 3; ++i) rgb[i] *= w;
}
void unweight_color(float rgb_weighted[3], float k) noexcept {
    const float d = 1.f - k * luma(rgb_weighted[0], rgb_weighted[1], rgb_weighted[2]);
    for (unsigned i = 0; i < 3; ++i) rgb_weighted[i] /= d;
}

ExposureState::ExposureState(const ExposureParams& params, ExposureMode mode, float ev_manual, float ev_initial) noexcept
    : params_(params), mode_(mode), ev_manual_(ev_manual), ev_initial_(ev_initial), ev_adapted_(ev_initial) {}
void ExposureState::configure(const ExposureParams& params, ExposureMode mode, float ev_manual) noexcept {
    params_ = params; mode_ = mode; ev_manual_ = std::isfinite(ev_manual) ? clampf(ev_manual, params.ev_min, params.ev_max) : 0.f;
}
void ExposureState::reset() noexcept { ev_adapted_ = ev_initial_; ev_target_ = 0.f; target_ = ExposureTarget{}; meter_ = MeterStatistics{}; dt_ = 0.f; steps_ = 0; }
float ExposureState::step(const MeterStatistics& meter, float dt) noexcept {
    meter_ = meter; dt_ = clamp_dt(dt, params_);
    target_ = exposure_target(meter, params_);
    ev_target_ = apply_deadband(ev_target_, target_.ev_target, steps_ == 0, params_);
    ev_adapted_ = adapt(ev_adapted_, ev_target_, dt_, params_);
    ++steps_;
    return ev();
}
} // namespace x3m::renderer
