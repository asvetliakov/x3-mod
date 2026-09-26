#pragma once
// d3d9-free arithmetic of the volumetric sun fog (docs/architecture/volumetric-fog.md,
// "Stage 1 implementation"), compiled natively by
// verification/analysis/test_fog_pass_math.py: option ranges, the
// strength ladder (the fixture seam's; its Ctrl+Alt+F10 key was removed 2026-09-26), the sun radiance from the tracked light's colour words, the
// capability decision and the sector latch of the automatic rule.
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace x3m::renderer {
constexpr float fog_strength_default = .02f, fog_strength_min = 0.f, fog_strength_max = .1f; // tau_max
constexpr float fog_anisotropy_default = .3f, fog_anisotropy_min = 0.f, fog_anisotropy_max = .9f; // Henyey-Greenstein g
constexpr float fog_radius_default = 10000.f; // view units; 99.98 % of the density lies inside cascade 3 (84k units)
// volumetric_fog_step (a fixture seam since the Ctrl+Alt+F10 key was removed 2026-09-26) steps through these; a launcher value between two steps moves to the next one above it.
constexpr float fog_strength_steps[] = {.005f, .01f, .02f, .03f, .05f};
inline float fog_strength_next(float current) noexcept {
    for (float step : fog_strength_steps) if (step > current + 1e-6f) return step;
    return fog_strength_steps[0];
}
// E_sun in linear light from the engine's sun light node colour words
// (sun_light_poll.h: int16 at node +0x150/152/154; / 256 = the LightDir_Color0
// constant the hull programs multiply by N.L). Those programs shade
// albedo x Color0 x N.L in engine space, i.e. a Lambert surface under the
// irradiance E = pi x decode(Color0): the same quantity the mock estimated
// from the image as 2 pi x p90 of lit luma (a 0.5-albedo surface). Words are
// clamped to [0, 4 x 256]. False (out = 0) when no channel is positive.
inline bool fog_sun_radiance(const std::int32_t colour[3], float out[3]) noexcept {
    bool any = false;
    for (unsigned i = 0; i < 3; ++i) {
        double c = double(colour[i]) / 256.;
        c = c < 0. ? 0. : c > 4. ? 4. : c;
        out[i] = float(3.14159265358979 * std::pow(c, 2.2));
        any = any || out[i] > 0.f;
    }
    return any;
}
// The neutral stand-in while the light node cannot be read (foreign
// executable, poll off): colour words 256 (Color0 = 1), E = pi.
inline void fog_sun_radiance_fallback(float out[3]) noexcept { const std::int32_t white[3] = {256, 256, 256}; fog_sun_radiance(white, out); }
// Upper bound of the radiance one pixel can gain (the fixture's energy bound):
// hue <= 4, F <= 1, phase <= p_HG(cos = 1).
inline double fog_phase(double g, double cosine) noexcept {
    return (1. - g * g) / (4. * 3.14159265358979 * std::pow(1. + g * g - 2. * g * cosine, 1.5));
}
// No geometry receiver plane exists inside the volume. The caller supplies the
// cascade's constant world-texel bias, not the surface plane-fit clamp.
inline bool fog_shadow_current(std::uint64_t frame, std::uint64_t map_frame) noexcept {
    return map_frame != ~std::uint64_t(0) && map_frame == frame;
}
inline bool fog_shadow_rows(const float rows[12], float bias) noexcept {
    if (!std::isfinite(bias) || bias < 0.f || bias > 1.f) return false;
    for (unsigned r=0;r<3;++r) {
        double norm=0;
        for(unsigned c=0;c<4;++c) {
            if (!std::isfinite(rows[4*r+c])) return false;
            if(c<3) norm+=double(rows[4*r+c])*rows[4*r+c];
        }
        if (!(norm>0) || norm>1e12) return false;
    }
    return true;
}
struct FogCapabilityInputs {
    std::uint32_t pixel_shader_version = 0, vertex_shader_version = 0;
    std::uint32_t ps30_instruction_slots = 0, largest_program_slots = 0;
    std::uint32_t simultaneous_targets = 0, max_streams = 0;
    std::int32_t a8r8g8b8_target = -1, fp16_target_blending = -1, r32f_texture = -1;
    bool blend_srcalpha = false, blend_invsrcalpha = false;
    bool stretch_rect = false; // D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES
};
inline const char* fog_capability(const FogCapabilityInputs& in) noexcept {
    if (in.pixel_shader_version < 0xffff0300u) return "ps_3_0";
    if (in.vertex_shader_version < 0xfffe0300u) return "vs_3_0";
    if (in.largest_program_slots == 0 || in.ps30_instruction_slots < in.largest_program_slots) return "ps_slots";
    if (in.simultaneous_targets == 0 || in.simultaneous_targets > 4 || in.max_streams == 0) return "device_caps";
    if (in.a8r8g8b8_target < 0) return "a8r8g8b8_target";
    if (in.fp16_target_blending < 0) return "fp16_target_blending";
    if (in.r32f_texture < 0) return "r32f_texture";
    if (!in.blend_srcalpha || !in.blend_invsrcalpha) return "blend_factors";
    if (!in.stretch_rect) return "stretch_rect";
    return nullptr;
}
// Automatic rule (the note's Decision, PS-presence form): a sector has fog
// while the engine draws its nebulafog cards (pixel program
// f7e0b6647a3bfa62: only a background record with NumDustInstances > 0 ever
// instantiates them; 1-4 of the 8-16 instances are on screen per frame and
// each fades over ~40 frames, sector-fog.md section 10). The latch holds the
// sector "foggy" for fog_card_hold frames after the last card bind and ramps
// the medium's weight over fog_card_ramp frames in both directions, so neither
// a card-free view nor a gate jump pops. No private layout, no executable gate.
constexpr std::uint64_t fog_card_pixel_hash = 0xf7e0b6647a3bfa62ull;
constexpr std::uint64_t fog_card_hold = 600, fog_card_ramp = 90;
// What the caller does with a failed transaction. A lost device is never the
// pass's fault: nothing is counted and nothing is disabled, the frame after
// Reset retries (targets return lazily). A target allocation that failed for
// any other reason disables the pass for the session; any other failure counts
// toward fog_failure_limit consecutive ones, cleared by a success or a Reset.
enum class FogFailureAction : unsigned { Retry, Count, DisableSession };
constexpr unsigned fog_failure_limit = 3;
inline FogFailureAction fog_failure_action(bool device_lost, bool targets_stage, unsigned consecutive_before) noexcept {
    if (device_lost) return FogFailureAction::Retry;
    if (targets_stage || consecutive_before + 1 >= fog_failure_limit) return FogFailureAction::DisableSession;
    return FogFailureAction::Count;
}
class FogSectorLatch {
public:
    void card(std::uint64_t frame) noexcept { seen_ = true; last_card_ = frame; }
    // A camera cut at this frame's scene end (the route's cut detector: a gate
    // jump, a load, a view switch): the hold ends at once unless a card was
    // bound in this very frame (the cards are the scene's last draws, so a cut
    // inside a fog sector keeps the latch); the weight still ramps down.
    void cut(std::uint64_t frame) noexcept { if (last_card_ != frame) seen_ = false; }
    // Once per frame at the scene end.
    float update(std::uint64_t frame) noexcept {
        const bool on = seen_ && frame >= last_card_ && frame - last_card_ <= fog_card_hold;
        const std::uint64_t elapsed = primed_ && frame > last_update_ ? frame - last_update_ : 1;
        const float step = float(elapsed > fog_card_ramp ? fog_card_ramp : elapsed) / float(fog_card_ramp);
        weight_ = on ? (weight_ + step > 1.f ? 1.f : weight_ + step) : (weight_ - step < 0.f ? 0.f : weight_ - step);
        last_update_ = frame; primed_ = true;
        return weight_;
    }
    float weight() const noexcept { return weight_; }
    bool cards_recent(std::uint64_t frame) const noexcept { return seen_ && frame >= last_card_ && frame - last_card_ <= fog_card_hold; }
    void reset() noexcept { *this = {}; }
private:
    std::uint64_t last_card_ = 0, last_update_ = 0;
    float weight_ = 0.f;
    bool seen_ = false, primed_ = false;
};
} // namespace x3m::renderer
