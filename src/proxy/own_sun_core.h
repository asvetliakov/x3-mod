#pragma once
// Own sun, steps 1, 1b and 1c (docs/architecture/own-sun-pass.md, the AP minimal mode, section 14; the look:
// docs/architecture/own-sun-look.md): the pure part. The mod draws its own sun (a small hot core in a dominant soft glow,
// six soft rays) in the scene-end stage slot (after the fog pass, before the TAA resolve) and the flare probe answers "hidden" for the sun's
// eligible record, so the engine destroys its own lens chain. No Windows/D3D dependency, no
// allocation, no engine call: verification/analysis/test_own_sun.py compiles exactly this header.
//
// Two CPU contracts:
//   * answer(), Toggle, frame_active(): integer only. answer() runs inside the probe thunk at 0x00471630, in the middle
//     of the engine's record loop, where the x87 stack and MXCSR belong to the engine.
//   * project(), radiance(), fog_scalar(), quad(), pixel_constants(): float (SSE2 in the DLL), once per frame inside the
//     temporal resolve's stage bracket under a full CPU-state boundary. No float is returned by value (an out-of-line
//     i386 float return is an x87 st0 load), no CRT math.
#include "../renderer/fog_transmittance.h"
#include <cstdint>
#ifdef X3M_OWN_SUN_HOST_REFERENCE
#include <cmath>
#endif

namespace x3m::own_sun::core {
// ---- Settings (X3M_OWN_SUN, X3M_SUN_DISC_RADIUS, X3M_SUN_DISC_RADIANCE, X3M_SUN_RAYS; tools/config/schema.py) ----
// The core's radius as a fraction of the viewport (target) height, so its angular size follows the engine's vertical
// field of view, which the engine keeps fixed as far as is known (step 1b, Run219: 0.012 = 17 px at 1440 rows, 9 px at
// 768; step 1's 0.0667 was about four times vanilla's disc). The core's radiance K in FP16 engine space: 2 decodes
// (gamma 2.2) to 4.6, above the bloom threshold (exposed luminance 1) and under the meter's highlight guard at the +1.3 EV
// Auto cap (14.66 / 2^1.3 = 5.96, K <= 2.25), so the core never sets the exposure (Run219: K 8 pulled it to -1.0 EV;
// verification/results/own-sun-pass/meter_exposure.py). The pixels are square: the core is round at every aspect. The
// rays' share: sun_rays 0..1 scales the rays (0 = none).
constexpr float radius_default_u = .012f, radius_min_u = .005f, radius_max_u = .25f;
constexpr float radiance_default = 2.f, radiance_min = 0.f, radiance_max = 64.f;
constexpr float rays_default = 1.f, rays_min = 0.f, rays_max = 1.f;
// The look (step 1c, own-sun-look.md "Defaults and constants"; every length in core radii; derived constants, not
// settings): the core whitened only a little (the polled colour stays visible), its soft edge from core_inner to 1 core
// radius; the glow is the pure polled colour times intensity, like vanilla's chain, and dominates: a Gaussian corona
// (sigma 1.5, 0.6) plus a Lorentzian halo (half brightness at 6, 0.3) windowed at 24; six soft rays at 0.15 with hashed
// lengths (3.50-13.13), a Gaussian width of max(3 px, 0.2 core radii) without taper and exp(-r / L) saturate(1 - r / L)^2
// along. The glow's sum stays under 1.0 engine at K = 2 (0.781 outside the core, glow_meter.py), kept for the exposure
// meter's guard and for AgX contrast at the core's edge (the resolve's sentinel stabiliser that once set an emitter bound
// E = 1 was retired on 2026-09-25: the sun's sentinel pixels reproject with the far-plane camera history, are clipped by the
// 3x3 box, and the far gate is 0 on the sentinel).
constexpr float white_mix = .25f, core_inner = .6f;
constexpr float corona_gain = .6f, corona_sigma = 1.5f;
constexpr float halo_gain = .3f, halo_radius = 6.f, glow_radius = 24.f;
constexpr unsigned ray_count = 6; // spokes; their lengths and strengths hashed by index (own_sun_ps.hlsl)
constexpr float ray_gain = .15f, ray_length = 14.f, ray_offset = .25f; // offset: spoke units; .25 keeps every spoke off the screen axes (spokes at 45 + 60k and -15 + 60k deg), so the three opposite-spoke lines through the sun never run vertically or horizontally (review of 13692fc8, finding 1)
constexpr float ray_sigma = .2f, ray_sigma_min_px = 3.f;              // the rays' Gaussian width: max(3 px, 0.2 R)
// The quad: 24 core radii to each side (the halo's window).
constexpr float quad_extent_x = glow_radius, quad_extent_y = glow_radius;
// Below 1.5 px of radius the stage draws nothing (the plumes' rule; note section 7).
constexpr float radius_min_px = 1.5f;
// Light node positions are engine integers; world (view) units are integers x the gameplay context scale 0.01
// (shadow_replay_sun_point.h, point_sun_context_scale).
constexpr double context_scale = .01;
// Parses a radius / radiance value already converted to float: out of range or not finite keeps the default.
inline void accept_radius(float value, float* radius) noexcept {
    if (value == value && value >= radius_min_u && value <= radius_max_u) *radius = value;
}
inline void accept_radiance(float value, float* radiance) noexcept {
    if (value == value && value >= radiance_min && value <= radiance_max) *radiance = value;
}
inline void accept_rays(float value, float* rays) noexcept {
    if (value == value && value >= rays_min && value <= rays_max) *rays = value;
}

// ---- The probe answer (integer only; sun_occlusion::core::Decision values) ----
// Hidden (1) for the sun's eligible record (core::eligible: the main view's re-probe of a record owned by a background
// view) while the own sun is active in this frame; every other probe is the original's (2). Returning 1 is always safe:
// the engine's own first gate returns it whenever the flare video option is off; downstream 0x00471660 destroys the
// record's bodies (RE note section 13).
constexpr int answer_hidden = 1, answer_original = 2;
inline int answer(bool frame_active, bool eligible) noexcept {
    return frame_active && eligible ? answer_hidden : answer_original;
}

// ---- The comparison hotkey (Ctrl+Shift+F9; comparison-hotkeys.md, "Own sun") ----
// One press edge flips the own sun for the whole frame: the probe answer and the stage together. Starts on; a held key
// is one press.
struct Toggle {
    bool on = true, down = false;
    // True on a press edge (the state flipped).
    bool step(bool pressed) noexcept {
        const bool edge = pressed && !down;
        down = pressed;
        if (edge) on = !on;
        return edge;
    }
};
// The frame's decision, latched at Present for the next frame (both the answer and the stage read it): the setting on,
// the hotkey on, and a stage that could draw in the last resolve (attached, armed). Otherwise the engine's chain stays.
inline bool frame_active(bool configured, bool key_on, bool stage_ready) noexcept {
    return configured && key_on && stage_ready;
}

// ---- Projection of the polled sun with the frame camera ----
// The camera of renderer::CameraState: view_j = sum_i world_i r[i*3+j] + t_j, clip x = view_x m00 + view_z m20,
// clip y = view_y m11 + view_z m21, w = view_z. The quad uses the jittered m20 / m21 (the routed draws' projection, so
// the disc is on the lane's raster: m20 + 2 jx / W, m21 - 2 jy / H); u / v are the unjittered position (the log, and the
// sun_visibility lane_u / lane_v it compares with).
struct Camera {
    float r[9]{}, t[3]{};
    float m00 = 0.f, m11 = 0.f, m20 = 0.f, m21 = 0.f;
};
enum class Skip : std::uint8_t { None = 0, Size, Camera, Behind, Small, Offscreen };
inline const char* skip_name(Skip s) noexcept {
    switch (s) {
    case Skip::None: return "none";
    case Skip::Size: return "size";
    case Skip::Camera: return "camera";
    case Skip::Behind: return "behind";
    case Skip::Small: return "small";
    case Skip::Offscreen: return "offscreen";
    }
    return "?";
}
struct Disc {
    Skip skip = Skip::Size;
    float u = 0.f, v = 0.f;           // unjittered back-buffer uv of the centre
    float x = 0.f, y = 0.f;           // jittered NDC of the centre (the quad's)
    float half_x = 0.f, half_y = 0.f; // the quad's half extent in NDC (3 r)
    float radius_px = 0.f;
    float view_z = 0.f;
};
inline bool finite(double v) noexcept {
    return v == v && v < 1e30 && v > -1e30;
}
// position: the poll's integer world position; radius_u: the configured radius / viewport height; jx / jy: this
// frame's TAA jitter in pixels (+x right, +y down on the raster; 0 without jitter).
inline Disc project(const std::int32_t position[3], const Camera& camera, unsigned width, unsigned height,
                    float radius_u, float jx, float jy) noexcept {
    Disc d;
    if (!width || !height) return d;
    if (!(camera.m00 > 0.f) || !(camera.m11 > 0.f) || !finite(camera.m20) || !finite(camera.m21)) {
        d.skip = Skip::Camera;
        return d;
    }
    double view[3] = {camera.t[0], camera.t[1], camera.t[2]};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) view[j] += double(position[i]) * context_scale * double(camera.r[i * 3 + j]);
    if (!finite(view[0]) || !finite(view[1]) || !finite(view[2])) {
        d.skip = Skip::Camera;
        return d;
    }
    d.view_z = float(view[2]);
    if (!(view[2] > 1e-3)) {
        d.skip = Skip::Behind;
        return d;
    }
    const double inv = 1. / view[2];
    const double x = (view[0] * camera.m00) * inv + camera.m20, y = (view[1] * camera.m11) * inv + camera.m21;
    const double xj = x + 2. * double(jx) / double(width), yj = y - 2. * double(jy) / double(height);
    if (!finite(x) || !finite(y)) {
        d.skip = Skip::Camera;
        return d;
    }
    d.u = float(.5 + .5 * x);
    d.v = float(.5 - .5 * y);
    d.x = float(xj);
    d.y = float(yj);
    const float r = radius_u >= radius_min_u && radius_u <= radius_max_u ? radius_u : radius_default_u;
    d.radius_px = r * float(height);
    d.half_x = quad_extent_x * 2.f * d.radius_px / float(width);
    d.half_y = quad_extent_y * 2.f * d.radius_px / float(height);
    if (d.radius_px < radius_min_px) {
        d.skip = Skip::Small;
        return d;
    }
    // Wholly outside the viewport: nothing drawn (the quad would be clipped anyway; this saves the device calls).
    if (d.x - d.half_x >= 1.f || d.x + d.half_x <= -1.f || d.y - d.half_y >= 1.f || d.y + d.half_y <= -1.f) {
        d.skip = Skip::Offscreen;
        return d;
    }
    d.skip = Skip::None;
    return d;
}

// ---- Radiance (FP16 engine space) ----
// c = colour / 256 (the poll's words, the code values the engine uploads as light colour), clamped to [0, 4] as the
// fog's E_sun reads them; m = max_ch(c), the hue c / m. Core K lerp(c / m, white, 0.25) m T: a slightly whitened hue at
// the colour's brightness (a #323232 sun is a dim grey core). Glow tint c T K / K_default: the pure polled colour, so the
// corona, halo and rays keep its hue ratio exactly. A black sun (every word 0, FL's #000000 sectors), T = 0 (a fully
// fogged sky) or K = 0 draws nothing.
struct Radiance {
    float core[3]{}, glow[3]{};
    float colour[3]{}; // c, for the log
    bool black = true;
};
inline void radiance(const std::int32_t colour[3], float k, float fog_t, Radiance* out) noexcept {
    Radiance r;
    bool lit = false;
    for (unsigned i = 0; i < 3; ++i) {
        float c = float(colour[i]) / 256.f;
        c = c > 0.f ? (c < 4.f ? c : 4.f) : 0.f;
        r.colour[i] = c;
        lit = lit || c > 0.f;
    }
    const float t = fog_t > 0.f ? (fog_t < 1.f ? fog_t : 1.f) : 0.f; // NaN -> 0
    r.black = !lit || !(t > 0.f) || !(k > 0.f);
    if (!r.black) {
        float m = r.colour[0] > r.colour[1] ? r.colour[0] : r.colour[1];
        m = m > r.colour[2] ? m : r.colour[2];
        for (unsigned i = 0; i < 3; ++i) {
            const float hue = r.colour[i] / m;
            r.core[i] = k * (hue + (1.f - hue) * white_mix) * m * t;
            r.glow[i] = (k / radiance_default) * r.colour[i] * t;
        }
    }
    *out = r;
}
// The fog's analytic far transmittance toward the sky, as one scalar (renderer::fog_sky_lens_gain at gain 1: the column's
// T at the cap reduced by its Rec. 709 luminance, clamped to [0, 1]); 1 in a frame without the density composite.
inline void fog_scalar(const renderer::FogTransmittanceLaw& law, float* t) noexcept {
    float applied = 1.f;
    renderer::fog_sky_lens_gain(law, 1.f, t, &applied);
}

// ---- The quad and the pixel program's constants ----
struct Vertex {
    float x, y, z, w; // clip space (w = 1, z = 0: depth is never tested)
    float lx, ly;     // offset from the centre in core radii
};
static_assert(sizeof(Vertex) == 24, "the declaration follows the vertex");
// A triangle strip: (-,-), (-,+), (+,-), (+,+).
inline void quad(const Disc& d, Vertex out[4]) noexcept {
    for (unsigned k = 0; k < 4; ++k) {
        const float sx = k & 2 ? 1.f : -1.f, sy = k & 1 ? 1.f : -1.f;
        out[k] = Vertex{d.x + sx * d.half_x, d.y + sy * d.half_y, 0.f, 1.f, sx * quad_extent_x, sy * quad_extent_y};
    }
}
// src/effects/own_sun_ps.hlsl: c0 the lane texel, c1 / c2 the knight offsets as lane uv, c3 the core (w its inner edge),
// c4 the glow tint, c5 the amplitudes (corona, halo, rays x sun_rays), c6 the core's centre as lane uv (jittered, the
// quad's) and the core radius as lane uv, c7 the spokes (N / 2 pi, offset, L_max, the across scale: 2 pi / N over the
// width in core radii, log2 e folded in), c8 the corona, halo and window shapes (log2 e folded in), c9 the spoke count.
constexpr unsigned pixel_registers = 10;
constexpr float two_pi = 6.28318531f, log2e = 1.44269504f, sqrt_log2e = 1.20112241f; // exp(-x) = exp2(-x log2 e)
// The rays' Gaussian width in core radii for a core of radius_px pixels: max(3 px, 0.2 R).
inline void ray_width_radii(float radius_px, float* width) noexcept {
    const float floor_r = radius_px > 0.f ? ray_sigma_min_px / radius_px : ray_sigma;
    *width = floor_r > ray_sigma ? floor_r : ray_sigma;
}
inline void pixel_constants(unsigned width, unsigned height, const Disc& d, const Radiance& r, float rays,
                            float out[pixel_registers * 4]) noexcept {
    const float dx = width ? 1.f / float(width) : 0.f, dy = height ? 1.f / float(height) : 0.f;
    const float share = rays >= rays_min && rays <= rays_max ? rays : rays_default;
    const float ring = d.radius_px; // the core-visibility taps sit at 0.45 and 0.9 of it (own_sun_ps.hlsl)
    const float n = float(ray_count);
    float sigma = ray_sigma;
    ray_width_radii(d.radius_px, &sigma);
    const float c[pixel_registers * 4] = {dx, dy, 0.f, 0.f,
                                          2.f * dx, dy, dx, 2.f * dy,
                                          -2.f * dx, dy, -dx, 2.f * dy,
                                          r.core[0], r.core[1], r.core[2], core_inner,
                                          r.glow[0], r.glow[1], r.glow[2], 0.f,
                                          corona_gain, halo_gain, ray_gain * share, 0.f,
                                          .5f + .5f * d.x, .5f - .5f * d.y, ring * dx, ring * dy,
                                          n / two_pi, ray_offset, ray_length, two_pi / n / sigma * sqrt_log2e,
                                          log2e / (corona_sigma * corona_sigma), 1.f / (halo_radius * halo_radius), log2e,
                                          1.f / glow_radius,
                                          0.f, n, 1.f / n, 0.f};
    for (unsigned i = 0; i < pixel_registers * 4; ++i) out[i] = c[i];
}

#ifdef X3M_OWN_SUN_HOST_REFERENCE
// ---- Host reference of the pixel program (the host test's expectation; CRT math: never compiled into the DLL) ----
// own_sun_ps.hlsl's law at one pixel from the uploaded constants `c`, for a given number of open taps at the pixel (0..9)
// and the core's visible fraction (0..1: the program's weighted 13 taps). corona / halo / rays: the terms before the
// tint (for the shape tests).
struct Pixel {
    float rgb[3]{};
    float corona = 0.f, halo = 0.f, rays = 0.f, core = 0.f; // core: D(r)
};
inline float hlsl_frac(float v) noexcept {
    return v - std::floor(v);
}
inline float saturate(float v) noexcept {
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}
inline float reference_spoke(const float c[pixel_registers * 4], float index, float across, float r) noexcept {
    const float* spokes = c + 28; const float* extent = c + 32; const float* counts = c + 36;
    const float k = index - counts[1] * std::floor(index * counts[2]);
    const float h1 = hlsl_frac(k * (.6180340f + k * .1547f)), h2 = hlsl_frac(k * .7548777f + .31f);
    const float along = r / (spokes[2] * (.25f + .75f * h1 * h1));
    const float fade = saturate(1.f - along);
    const float perpendicular = r * across * spokes[3];
    return (.6f + .4f * h2) * std::exp2(-perpendicular * perpendicular - along * extent[2]) * fade * fade;
}
inline void reference_pixel(const float c[pixel_registers * 4], float px, float py, unsigned open_taps, float visible,
                            Pixel* out) noexcept {
    const float* core = c + 12; const float* glow = c + 16; const float* amp = c + 20; const float* spokes = c + 28;
    const float* extent = c + 32;
    const float r2 = px * px + py * py, r = std::sqrt(r2);
    const float s = saturate((r - core[3]) / (1.f - core[3]));
    const float d = 1.f - s * s * (3.f - 2.f * s);
    const float corona = amp[0] * std::exp2(-r2 * extent[0]);
    const float w = saturate(1.f - r2 * extent[3] * extent[3]);
    const float halo = amp[1] / (1.f + r2 * extent[1]) * w * w;
    const float t = std::atan2(py, px) * spokes[0] + spokes[1];
    const float nearest = std::floor(t + .5f), across = t - nearest, side = across >= 0.f ? 1.f : -1.f;
    const float rays = amp[2] * (reference_spoke(c, nearest, across, r) + reference_spoke(c, nearest + side, across - side, r));
    const float open = float(open_taps > 9u ? 9u : open_taps) / 9.f;
    visible = saturate(visible);
    for (unsigned i = 0; i < 3; ++i) {
        const float g = glow[i] * (corona + halo + rays);
        out->rgb[i] = (g + (core[i] - g) * d) * open * (d + (1.f - d) * visible);
    }
    out->corona = corona;
    out->halo = halo;
    out->rays = rays;
    out->core = d;
}
#endif
} // namespace x3m::own_sun::core
