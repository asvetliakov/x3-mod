#include "exposure.h"
#include <cmath>

namespace x3m::renderer {
namespace {
constexpr float kLn2 = 0.69314718055994530942f;
constexpr float kDecodeGamma = 2.2f;
float clampf(float x, float lo, float hi) noexcept { return x < lo ? lo : x > hi ? hi : x; }
} // namespace

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
float reduce_chain(float* level, float* scratch, unsigned width, unsigned height, unsigned factor) noexcept {
    if (!level || !scratch || !width || !height || factor < 2) return 0.f;
    const float scale = 1.f / float(factor * factor);
    while (width > 1 || height > 1) {
        const unsigned nw = (width + factor - 1) / factor, nh = (height + factor - 1) / factor;
        for (unsigned y = 0; y < nh; ++y)
            for (unsigned x = 0; x < nw; ++x) {
                float acc = 0.f;
                for (unsigned ty = 0; ty < factor; ++ty) {
                    const unsigned sy = y * factor + ty < height ? y * factor + ty : height - 1;
                    for (unsigned tx = 0; tx < factor; ++tx) {
                        const unsigned sx = x * factor + tx < width ? x * factor + tx : width - 1;
                        acc += level[std::size_t(sy) * width + sx];
                    }
                }
                scratch[std::size_t(y) * nw + x] = acc * scale;
            }
        for (std::size_t i = 0; i < std::size_t(nw) * nh; ++i) level[i] = scratch[i];
        width = nw; height = nh;
    }
    return level[0];
}

float ev_target(float avg_log_l, const ExposureParams& p) noexcept {
    if (!(p.key > 0.f) || !std::isfinite(avg_log_l)) return clampf(0.f, p.ev_min, p.ev_max);
    return clampf(std::log2(p.key) - avg_log_l + p.ev_offset, p.ev_min, p.ev_max);
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
void ExposureState::reset() noexcept { ev_adapted_ = ev_initial_; ev_target_ = 0.f; avg_log_l_ = 0.f; dt_ = 0.f; steps_ = 0; }
float ExposureState::step(float avg_log_l, float dt) noexcept {
    avg_log_l_ = avg_log_l; dt_ = clamp_dt(dt, params_);
    ev_target_ = x3m::renderer::ev_target(avg_log_l, params_);
    ev_adapted_ = adapt(ev_adapted_, ev_target_, dt_, params_);
    ++steps_;
    return ev();
}
} // namespace x3m::renderer
