#pragma once
// Own sun, step 1 (docs/architecture/own-sun-pass.md, the AP minimal mode): the pure part. The mod draws its own sun
// disc and corona in the scene-end stage slot (after the fog pass, before the TAA resolve) and the flare probe answers
// "hidden" for the sun's eligible record, so the engine destroys its own lens chain. No Windows/D3D dependency, no
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

namespace x3m::own_sun::core {
// ---- Settings (X3M_OWN_SUN, X3M_SUN_DISC_RADIUS, X3M_SUN_DISC_RADIANCE; tools/config/schema.py) ----
// The disc's radius as a fraction of the viewport (target) height, so its angular size follows the engine's vertical
// field of view, which the engine keeps fixed as far as is known (0.0667 = 51 px at 768 rows, the run223 size at
// 1280x768; 96 px at 1440 rows), and the disc's radiance K in FP16 engine space (8; note sections 2 and 14 for what the
// exposure meter does with it). The pixels are square: the disc is round at every aspect.
constexpr float radius_default_u = .0667f, radius_min_u = .005f, radius_max_u = .25f;
constexpr float radiance_default = 8.f, radiance_min = 0.f, radiance_max = 64.f;
// The look (note section 2): the disc whitened like the plume core, the corona under the stabiliser's emitter bound E = 1.
constexpr float white_mix = .6f, corona_gain = .8f;
// The quad: side 2 x 3 r around the sun (the corona's exp(-3 (r - 1)) is exp(-6) at its edge, subtracted in the program).
constexpr float quad_extent = 3.f;
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
    d.half_x = quad_extent * 2.f * d.radius_px / float(width);
    d.half_y = quad_extent * 2.f * d.radius_px / float(height);
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
// fog's E_sun reads them; m = max_ch(c), the hue c / m. Disc K lerp(c / m, white, 0.6) m T: the whitening keeps the hue
// of a bright sun and the brightness follows the colour as vanilla's colour-multiplied chain does (a #323232 sun is a
// dim grey disc, not a white one). Corona K_h (K / 8) c T: it scales with sun_disc_radiance too. A black sun (every word
// 0, FL's #000000 sectors), T = 0 (a fully fogged sky) or K = 0 draws nothing.
struct Radiance {
    float disc[3]{}, corona[3]{};
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
            r.disc[i] = k * (hue + (1.f - hue) * white_mix) * m * t;
            r.corona[i] = corona_gain * (k / radiance_default) * r.colour[i] * t;
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
    float lx, ly;     // offset from the centre in disc radii
};
static_assert(sizeof(Vertex) == 24, "the declaration follows the vertex");
// A triangle strip: (-,-), (-,+), (+,-), (+,+).
inline void quad(const Disc& d, Vertex out[4]) noexcept {
    for (unsigned k = 0; k < 4; ++k) {
        const float sx = k & 2 ? 1.f : -1.f, sy = k & 1 ? 1.f : -1.f;
        out[k] = Vertex{d.x + sx * d.half_x, d.y + sy * d.half_y, 0.f, 1.f, sx * quad_extent, sy * quad_extent};
    }
}
// c0 the lane texel, c1 / c2 the knight offsets as lane uv, c3 the disc, c4 the corona (src/effects/own_sun_ps.hlsl).
constexpr unsigned pixel_registers = 5;
inline void pixel_constants(unsigned width, unsigned height, const Radiance& r, float out[pixel_registers * 4]) noexcept {
    const float dx = width ? 1.f / float(width) : 0.f, dy = height ? 1.f / float(height) : 0.f;
    const float c[pixel_registers * 4] = {dx, dy, 0.f, 0.f,
                                          2.f * dx, dy, dx, 2.f * dy,
                                          -2.f * dx, dy, -dx, 2.f * dy,
                                          r.disc[0], r.disc[1], r.disc[2], 0.f,
                                          r.corona[0], r.corona[1], r.corona[2], 0.f};
    for (unsigned i = 0; i < pixel_registers * 4; ++i) out[i] = c[i];
}

// ---- Host reference of the pixel program (the host test's expectation; the GPU fixture's too) ----
// The program's law at one pixel for a given open fraction (0..9 sentinel taps), per channel.
struct Pixel {
    float rgb[3]{};
};
inline void reference_pixel(const Radiance& r, float distance_radii, unsigned open_taps, float exp_term, Pixel* out) noexcept {
    // exp_term = exp(-3 (distance - 1)) supplied by the caller (no CRT math here).
    float e = exp_term > 1.f ? 1.f : exp_term;
    float h = e - .00247875218f;
    h = h > 0.f ? h : 0.f;
    float s = (distance_radii - .9f) / .1f;
    s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
    const float d = 1.f - s * s * (3.f - 2.f * s);
    const float open = float(open_taps > 9u ? 9u : open_taps) / 9.f;
    for (unsigned i = 0; i < 3; ++i) out->rgb[i] = (r.corona[i] * h + (r.disc[i] - r.corona[i] * h) * d) * open;
}
} // namespace x3m::own_sun::core
