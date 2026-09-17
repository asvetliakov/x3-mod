#pragma once
// The sun as a world POSITION (docs/reverse-engineering/camera-and-lights.md,
// "Directional lights: source, space and count"): the engine has no sun
// direction, only the brightest directional light node's position, and writes
// LightDir_Dir0 = normalize(light - node) per mesh part. sun_light_poll.h reads
// that position once per frame (exact executable only); this header turns it
// into one direction per cascade, normalize(light - cascade centre), and decides
// per frame whether the poll or the constant latch (shadow_replay_sun.h, the
// portable source) feeds the cascades:
//  * the poll is cross-checked against the shader constants: for up to
//    point_sun_checks_per_frame routed draws whose LightDir_Dir0 agrees with the
//    latch, normalize(light - draw origin) must lie within
//    point_sun_agreement_sin (0.1 degrees; the engine's 1/65536 quantisation is
//    0.0008 degrees, another light is degrees away) of that draw's constant;
//  * the frame's source is decided once, at the first consumer (the draw-time
//    box test or the scene end), and holds for the whole frame (the masks and
//    the bases come from the same suns): the poll when it read a light this
//    frame, the light lies outside every cascade's volume, and the position is
//    validated: checked this frame without a disagreement, or bit-equal to the
//    position the last checked frame validated (a frame without samples);
//  * a disagreement ends the validation and keeps the latch as the source for
//    point_sun_cooldown_frames; every fallback is counted by reason;
//  * swim: a cascade's direction is HELD, bit for bit, while the ideal
//    direction at its current (unsnapped) centre stays within 1 / size radians
//    of it, so between re-derivations the basis is exactly as stable under
//    texel snapping as the one-sun basis. A re-derivation (the camera moved
//    distance / size across the light: 3,800 units at the measured 1.57e7-unit
//    sun and 4096 texels) turns the basis by at most 2 / size radians: the
//    shadow of a caster D units light-ward of its receiver moves D * 2 / size,
//    at most one texel of that cascade for D <= its half-extent. A cascade
//    re-deriving adopts the next smaller cascade's held direction when that is
//    within its own threshold, so camera-centred cascades normally share one
//    direction bit for bit (the one-transform bounds path and one draw-rows
//    product per draw).
// Units: light node positions are engine integers; world (view) units are
// integers x the gameplay context scale 0.01 (camera-state-and-frame-routine.md,
// "View-unit scale"; run111: world translation -123845.57 for node -12384560).
// Pure CPU state, no allocation, no D3D, no libm.
#include <cstdint>
#include "../renderer/shadow_replay_projection.h"

namespace x3m::shadow_replay {
constexpr double point_sun_context_scale = .01;
constexpr double point_sun_agreement_sin = 1.7453292e-3; // sin(0.1 degrees)
constexpr unsigned point_sun_checks_per_frame = 8;
constexpr unsigned point_sun_cooldown_frames = 120;
enum class PointSunReason : unsigned { Point = 0, Off = 1, Unavailable = 2, NoLight = 3, Camera = 4, Near = 5, Unchecked = 6, Disagrees = 7, Cooldown = 8, Count = 9 };
constexpr const char* point_sun_reason_name(PointSunReason r) noexcept {
    switch (r) {
    case PointSunReason::Point: return "point"; case PointSunReason::Off: return "off"; case PointSunReason::Unavailable: return "unavailable";
    case PointSunReason::NoLight: return "no_light"; case PointSunReason::Camera: return "camera"; case PointSunReason::Near: return "near";
    case PointSunReason::Unchecked: return "unchecked"; case PointSunReason::Disagrees: return "disagrees"; case PointSunReason::Cooldown: return "cooldown";
    default: return "?";
    }
}
// Small angles in degrees from a squared sine without libm (diagnostics): asin(s) = s + s^3 / 6 + 3 s^5 / 40.
inline double point_sun_degrees(double sin2, bool opposed = false) noexcept {
    const double s = renderer::sqrt_sd(sin2 < 0. ? 0. : sin2 > 1. ? 1. : sin2);
    const double a = (s + s * s * s / 6. + 3. * s * s * s * s * s / 40.) * 57.29577951308232;
    return opposed ? 180. - a : a;
}
struct PointSun {
    // This frame.
    bool polled = false, light_valid = false;
    std::int32_t native[3]{};
    double light[3]{};                 // world units
    PointSunReason poll_reason = PointSunReason::Off;
    unsigned checks = 0, disagreements = 0;
    double worst_sin2 = 0.; bool worst_opposed = false;
    int decided = 0;                   // 0 undecided, 1 the poll, -1 the latch
    PointSunReason reason = PointSunReason::Off;
    double distance = 0.;              // camera -> light at the decision
    unsigned rederived = 0;            // cascades whose held direction changed this frame
    // Carried.
    bool validated = false; std::int32_t validated_native[3]{};
    unsigned cooldown = 0;
    bool held_valid[renderer::shadow_cascade_max]{};
    double held[renderer::shadow_cascade_max][3]{};
    float suns[renderer::shadow_cascade_max * 4]{}; // the held directions as the basis takes them (w = 0)
    std::uint64_t frames_point = 0, frames_latch[unsigned(PointSunReason::Count)]{}, rederivations = 0;

    void reset() noexcept { *this = PointSun{}; }
    void begin_frame() noexcept {
        polled = light_valid = false; poll_reason = PointSunReason::Off; checks = disagreements = 0; worst_sin2 = 0.; worst_opposed = false;
        decided = 0; reason = PointSunReason::Off; distance = 0.; rederived = 0;
    }
    // The frame's poll result: a light (engine integers) or why there is none.
    void set_poll(const std::int32_t* position, PointSunReason why) noexcept {
        polled = true; poll_reason = why; light_valid = position != nullptr && why == PointSunReason::Point;
        if (!light_valid) return;
        for (unsigned i = 0; i < 3; ++i) { native[i] = position[i]; light[i] = double(position[i]) * point_sun_context_scale; }
    }
    bool wants_check() const noexcept { return light_valid && checks < point_sun_checks_per_frame; }
    // Squared sine of the angle between a and b, and whether they are opposed; false on a degenerate input.
    static bool sine2(const double a[3], const double b[3], double& sin2, bool& opposed) noexcept {
        const double c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        const double aa = a[0] * a[0] + a[1] * a[1] + a[2] * a[2], bb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
        if (!(aa > 0.) || !(bb > 0.) || !(aa < 1e300) || !(bb < 1e300)) return false;
        sin2 = (c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) / (aa * bb);
        opposed = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] <= 0.;
        return sin2 == sin2;
    }
    // One routed draw: its LightDir_Dir0 constant against normalize(light - origin). True when they agree.
    bool check(const float constant[4], const double origin[3]) noexcept {
        if (!wants_check()) return true;
        const double to_light[3] = {light[0] - origin[0], light[1] - origin[1], light[2] - origin[2]};
        const double c[3] = {double(constant[0]), double(constant[1]), double(constant[2])};
        double sin2 = 1.; bool opposed = true;
        const bool known = sine2(to_light, c, sin2, opposed);
        ++checks;
        if (!known) { sin2 = 1.; opposed = true; }
        if (opposed ? !worst_opposed || sin2 < worst_sin2 : !worst_opposed && sin2 > worst_sin2) { worst_sin2 = sin2; worst_opposed = opposed; }
        const bool agrees = known && !opposed && sin2 <= point_sun_agreement_sin * point_sun_agreement_sin;
        if (!agrees) ++disagreements;
        return agrees;
    }
    double agreement_degrees() const noexcept { return checks ? point_sun_degrees(worst_sin2, worst_opposed) : -1.; }
    // The world position the origin (0, 0, 0, 1) of a draw's clip rows maps to (renderer::shadow_replay_light_rows' law).
    static bool draw_origin(const renderer::CameraState& camera, const float rows[16], double out[3]) noexcept {
        if (!camera.valid || !rows || !(camera.m00 > 0.f) || !(camera.m11 > 0.f)) return false;
        const double v[3] = {double(rows[3]) / camera.m00 - double(camera.t[0]), double(rows[7]) / camera.m11 - double(camera.t[1]), double(rows[15]) - double(camera.t[2])};
        for (unsigned i = 0; i < 3; ++i) {
            out[i] = v[0] * double(camera.r[i * 3]) + v[1] * double(camera.r[i * 3 + 1]) + v[2] * double(camera.r[i * 3 + 2]);
            if (!(out[i] == out[i]) || out[i] > 1e15 || out[i] < -1e15) return false;
        }
        return true;
    }
    // The frame's source, decided once. `enabled`: the option and the cascades are on.
    bool decide(bool enabled, const renderer::CameraState& camera, const renderer::ShadowCascadeSet& set) noexcept {
        if (decided) return decided > 0;
        decided = -1;
        if (!enabled || !set.count) reason = PointSunReason::Off;
        else if (!polled || !light_valid) reason = polled ? poll_reason : PointSunReason::Unavailable;
        else if (cooldown) reason = PointSunReason::Cooldown;
        else if (disagreements) reason = PointSunReason::Disagrees;
        else if (!camera.valid) reason = PointSunReason::Camera;
        else if (!checks && !(validated && validated_native[0] == native[0] && validated_native[1] == native[1] && validated_native[2] == native[2])) reason = PointSunReason::Unchecked;
        else {
            double position[3], forward[3], to_light[3], d2 = 0.;
            for (unsigned i = 0; i < 3; ++i) {
                position[i] = 0.; forward[i] = double(camera.r[i * 3 + 2]);
                for (unsigned j = 0; j < 3; ++j) position[i] -= double(camera.t[j]) * double(camera.r[i * 3 + j]);
                to_light[i] = light[i] - position[i]; d2 += to_light[i] * to_light[i];
            }
            distance = renderer::sqrt_sd(d2);
            const auto& last = set.cascades[set.count - 1];
            // The light must lie outside every cascade's volume (an orthographic map of a light inside it means nothing).
            if (!(distance > double(last.depth_toward_light) + double(last.half_extent)) || !(distance < 1e15)) reason = PointSunReason::Near;
            else {
                reason = PointSunReason::Point; decided = 1;
                for (unsigned k = 0; k < set.count; ++k) {
                    const auto& cascade = set.cascades[k];
                    double ideal[3], n2 = 0.;
                    for (unsigned i = 0; i < 3; ++i) { ideal[i] = light[i] - (position[i] + forward[i] * double(cascade.forward_offset)); n2 += ideal[i] * ideal[i]; }
                    const double n = renderer::sqrt_sd(n2);
                    for (double& v : ideal) v /= n;
                    const double limit = cascade.size ? 1. / double(cascade.size) : 0., limit2 = limit * limit;
                    double sin2 = 1.; bool opposed = true;
                    if (held_valid[k] && sine2(held[k], ideal, sin2, opposed) && !opposed && sin2 <= limit2) continue;
                    const bool adopt = k && sine2(held[k - 1], ideal, sin2, opposed) && !opposed && sin2 <= limit2;
                    for (unsigned i = 0; i < 3; ++i) { held[k][i] = adopt ? held[k - 1][i] : ideal[i]; suns[k * 4 + i] = adopt ? suns[(k - 1) * 4 + i] : float(ideal[i]); }
                    suns[k * 4 + 3] = 0.f; held_valid[k] = true; ++rederived; ++rederivations;
                }
            }
        }
        if (decided < 0) for (bool& v : held_valid) v = false; // the next point frame derives afresh
        return decided > 0;
    }
    // The scene end, after decide(): carries the validation, counts the frame.
    void end_frame() noexcept {
        if (cooldown) --cooldown;
        if (disagreements) { validated = false; cooldown = point_sun_cooldown_frames; }
        else if (checks && light_valid) { validated = true; for (unsigned i = 0; i < 3; ++i) validated_native[i] = native[i]; }
        if (decided > 0) ++frames_point; else ++frames_latch[unsigned(reason) < unsigned(PointSunReason::Count) ? unsigned(reason) : 0u];
    }
    const float* sun(unsigned cascade) const noexcept { return decided > 0 && cascade < renderer::shadow_cascade_max ? suns + cascade * 4 : nullptr; }
};
} // namespace x3m::shadow_replay
