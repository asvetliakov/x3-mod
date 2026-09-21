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
//  * the source is hysteretic: once a position has been validated, a frame
//    that cannot vouch for its own poll yet (a changed position before the
//    first checkable draw, or no poll at all) keeps `point` on the LAST
//    VALIDATED position for up to point_sun_carry_frames consecutive frames
//    (30) instead of switching; only unavailable / no_light, near and
//    disagrees (then cooldown) switch to the latch;
//  * a disagreement ends the validation and keeps the latch as the source for
//    point_sun_cooldown_frames; a frame that had already decided `point` when
//    the disagreement arrived finishes as `point` (its masks and bases agree)
//    and the switch happens on the next frame; every fallback is counted by reason;
//  * swim: a cascade's direction is HELD, bit for bit, while the ideal
//    direction at its current (unsnapped) centre stays within 1 / size radians
//    of it, so between re-derivations the basis is exactly as stable under
//    texel snapping as the one-sun basis. A re-derivation (the camera moved
//    distance / size across the light: 3,800 units at the measured 1.57e7-unit
//    sun and 4096 texels) turns the basis by at most 2 / size radians ABOUT A
//    HELD ANCHOR: the texel grid passes through `anchor`, which a re-derivation
//    moves to the old grid's point nearest the current centre (the old basis'
//    snapped centre), so the grid phase at the centre is preserved (the new
//    snapped centre lies within 1.5 texels x turn of an old grid point:
//    < 1e-3 texel) and a texel r units from the centre moves r x turn: at most
//    one texel at the cascade's edge (r = half-extent, turn = 2 / size), in
//    proportion inside. Separately the shadow of a caster D units light-ward
//    of its receiver moves D x 2 / size, at most one texel for D <= the
//    half-extent. (Anchored at the world origin instead, the grid would move
//    |centre| x turn: 49 units at 1e5 units out, a new phase for every edge.)
//    A retained map keeps its anchor implicitly: its basis stores the snapped
//    centre and axes its rows are built from. A cascade re-deriving adopts the
//    next smaller cascade's held direction when that is within its own
//    threshold, so camera-centred cascades normally share one direction bit
//    for bit (the one-transform bounds path and one draw-rows product per draw).
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
constexpr unsigned point_sun_carry_frames = 30; // consecutive frames decided on the last validated position
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
    unsigned rederived_mask = 0;       // ... and which ones (bit k = cascade k; X3M_SHADOW_SUN_TRACE reports it per frame)
    bool carried_frame = false;        // decided on the last validated position, not on this frame's poll
    double used[3]{};                  // the position the decision used
    // Carried.
    bool validated = false; std::int32_t validated_native[3]{}; double validated_light[3]{};
    unsigned cooldown = 0, carried = 0;
    bool held_valid[renderer::shadow_cascade_max]{};
    double held[renderer::shadow_cascade_max][3]{};
    double anchor[renderer::shadow_cascade_max][3]{}; // the texel grid's anchor per cascade (shadow_replay_basis)
    float suns[renderer::shadow_cascade_max * 4]{}; // the held directions as the basis takes them (w = 0)
    std::uint64_t frames_point = 0, frames_latch[unsigned(PointSunReason::Count)]{}, rederivations = 0;

    void reset() noexcept { *this = PointSun{}; }
    void begin_frame() noexcept {
        polled = light_valid = false; poll_reason = PointSunReason::Off; checks = disagreements = 0; worst_sin2 = 0.; worst_opposed = false;
        decided = 0; reason = PointSunReason::Off; distance = 0.; rederived = 0; rederived_mask = 0; carried_frame = false;
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
        double scratch[9];
        const double* wv = renderer::camera_world_basis(camera, scratch);
        for (unsigned i = 0; i < 3; ++i) {
            out[i] = v[0] * wv[i * 3] + v[1] * wv[i * 3 + 1] + v[2] * wv[i * 3 + 2];
            if (!(out[i] == out[i]) || out[i] > 1e15 || out[i] < -1e15) return false;
        }
        return true;
    }
    // The frame's source, decided once. `enabled`: the option and the cascades are on.
    bool decide(bool enabled, const renderer::CameraState& camera, const renderer::ShadowCascadeSet& set) noexcept {
        if (decided) return decided > 0;
        decided = -1;
        // This frame's poll vouches for itself when a draw has checked it, or when it is the validated position.
        const bool fresh = polled && light_valid && (checks || (validated && validated_native[0] == native[0] && validated_native[1] == native[1] && validated_native[2] == native[2]));
        const bool carry = !fresh && validated && carried < point_sun_carry_frames; // hysteresis: the last validated position
        if (!enabled || !set.count) reason = PointSunReason::Off;
        else if (polled && !light_valid) reason = poll_reason;
        else if (cooldown) reason = PointSunReason::Cooldown;
        else if (disagreements) reason = PointSunReason::Disagrees;
        else if (!camera.valid) reason = PointSunReason::Camera;
        else if (!fresh && !carry) reason = polled ? PointSunReason::Unchecked : PointSunReason::Unavailable;
        else {
            carried_frame = !fresh;
            double position[3], forward[3], to_light[3], d2 = 0., scratch[9];
            const double* wv = renderer::camera_world_basis(camera, scratch);
            for (unsigned i = 0; i < 3; ++i) {
                used[i] = fresh ? light[i] : validated_light[i];
                position[i] = 0.; forward[i] = wv[i * 3 + 2];
                for (unsigned j = 0; j < 3; ++j) position[i] -= double(camera.t[j]) * wv[i * 3 + j];
                to_light[i] = used[i] - position[i]; d2 += to_light[i] * to_light[i];
            }
            distance = renderer::sqrt_sd(d2);
            const auto& last = set.cascades[set.count - 1];
            // The light must lie outside every cascade's volume (an orthographic map of a light inside it means nothing).
            if (!(distance > double(last.depth_toward_light) + double(last.half_extent)) || !(distance < 1e15)) reason = PointSunReason::Near;
            else {
                reason = PointSunReason::Point; decided = 1;
                for (unsigned k = 0; k < set.count; ++k) {
                    const auto& cascade = set.cascades[k];
                    double ideal[3], centre[3], n2 = 0.;
                    for (unsigned i = 0; i < 3; ++i) { centre[i] = position[i] + forward[i] * double(cascade.forward_offset); ideal[i] = used[i] - centre[i]; n2 += ideal[i] * ideal[i]; }
                    const double n = renderer::sqrt_sd(n2);
                    for (double& v : ideal) v /= n;
                    const double limit = cascade.size ? 1. / double(cascade.size) : 0., limit2 = limit * limit;
                    double sin2 = 1.; bool opposed = true;
                    if (held_valid[k] && sine2(held[k], ideal, sin2, opposed) && !opposed && sin2 <= limit2) continue;
                    // The grid's new anchor: the old grid's point beside the current centre (the old basis' snapped
                    // centre), so the phase at the centre survives the turn; a first derivation anchors at the centre.
                    renderer::ShadowReplayBasis old{};
                    const bool keep = held_valid[k] && renderer::shadow_replay_basis(camera, suns + k * 4, cascade, old, anchor[k]);
                    for (unsigned i = 0; i < 3; ++i) anchor[k][i] = keep ? old.center_d[i] : centre[i];
                    const bool adopt = k && sine2(held[k - 1], ideal, sin2, opposed) && !opposed && sin2 <= limit2;
                    for (unsigned i = 0; i < 3; ++i) { held[k][i] = adopt ? held[k - 1][i] : ideal[i]; suns[k * 4 + i] = adopt ? suns[(k - 1) * 4 + i] : float(ideal[i]); }
                    suns[k * 4 + 3] = 0.f; held_valid[k] = true; ++rederived; rederived_mask |= 1u << k; ++rederivations;
                }
            }
        }
        if (decided < 0 && reason != PointSunReason::Camera) for (bool& v : held_valid) v = false; // a real switch: the next point frame derives afresh (a frame without a camera is refused anyway)
        return decided > 0;
    }
    // The scene end, after decide(): carries the validation, counts the frame.
    void end_frame() noexcept {
        if (cooldown) --cooldown;
        if (disagreements) { validated = false; cooldown = point_sun_cooldown_frames; carried = 0; }
        else if (checks && light_valid) { validated = true; carried = 0; for (unsigned i = 0; i < 3; ++i) { validated_native[i] = native[i]; validated_light[i] = light[i]; } }
        else if (carried_frame) ++carried;
        if (decided > 0) ++frames_point; else ++frames_latch[unsigned(reason) < unsigned(PointSunReason::Count) ? unsigned(reason) : 0u];
    }
    const float* sun(unsigned cascade) const noexcept { return decided > 0 && cascade < renderer::shadow_cascade_max ? suns + cascade * 4 : nullptr; }
    const double* grid_anchor(unsigned cascade) const noexcept { return decided > 0 && cascade < renderer::shadow_cascade_max ? anchor[cascade] : nullptr; }
    const double* grid_anchors() const noexcept { return decided > 0 ? anchor[0] : nullptr; } // three per cascade
};
} // namespace x3m::shadow_replay
