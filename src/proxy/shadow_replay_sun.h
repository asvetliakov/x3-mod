#pragma once
// The frame's one validated sun (docs/verification/directional-shadows.md,
// "Run 38 A (run111) diagnosis", cause 1). The engine keeps LightDir_Dir0
// (world space, object -> light) at a register that depends on the bound pixel
// program (c4, c5 or c0); the route resolves it per program from the constant
// table at creation (src/renderer/shader_constant_register.h, strictly by the
// name LightDir_Dir0: Dir1 is a second light) and samples the shadowed
// register at every routed z-writing draw (routed draws passed the main-scene
// gate, so a monitor or menu scene contributes nothing). The engine computes
// the value per mesh part as normalize(light - node)
// (docs/reverse-engineering/camera-and-lights.md, "Directional lights: source,
// space and count"), so samples of one sector spread: 0.753 degrees measured
// across run111's nodes. This latch turns them into ONE sun per sector, shared
// by the bounds test, every cascade's replay and the apply:
//  * a sample counts only when it is unit length within 1e-3 with w = 0;
//  * the sun latches when two samples from different draws agree (the first
//    of the two is the value; a lone sample only waits, so one stray register
//    value cannot become the sun), and it stays exactly that value while
//    samples agree within sun_agreement_cos (1.5 degrees, twice the measured
//    spread): the basis never follows whichever draw came first (no swim);
//  * a frame without a sample reuses it (the sun is world-fixed);
//  * a frame whose samples all disagree is refused (Changing); when the same
//    candidate persists for sun_relatch_frames consecutive such frames (a
//    sector transit) it becomes the latched sun (Relatched: that frame is
//    still refused, its masks were made for the old sun; the owner voids
//    everything retained);
//  * before the latch there is no sun (None): the frame is refused.
// Pure CPU state, no allocation, no D3D.
#include <cstdint>

namespace x3m::shadow_replay {
constexpr unsigned sun_register_limit = 32;   // pixel float registers c0..c31 are shadowed for the per-program sun
constexpr float sun_agreement_cos = .999657f; // 1.5 degrees: twice the measured 0.753-degree per-node spread, far below any other light
constexpr float sun_unit_tolerance = 1e-3f;   // |length - 1|
constexpr unsigned sun_relatch_frames = 8;
enum class SunVerdict : unsigned { Sampled = 0, Retained = 1, Relatched = 2, Changing = 3, None = 4 };
constexpr const char* sun_verdict_name(SunVerdict v) noexcept {
    switch (v) { case SunVerdict::Sampled: return "sampled"; case SunVerdict::Retained: return "retained"; case SunVerdict::Relatched: return "relatched";
                 case SunVerdict::Changing: return "changing"; default: return "none"; }
}
constexpr bool sun_verdict_usable(SunVerdict v) noexcept { return v == SunVerdict::Sampled || v == SunVerdict::Retained; }
struct SunLatch {
    float sun[4]{};            // the validated sun
    bool valid = false;
    int source_register = -1;  // register and program of the sample that latched it
    std::uint64_t source_program = 0;
    float candidate[4]{};      // a persisting disagreement
    int candidate_register = -1;
    std::uint64_t candidate_program = 0;
    unsigned candidate_frames = 0;
    float pending[4]{};        // before the latch: the sample waiting for a second draw to agree
    int pending_register = -1;
    std::uint64_t pending_program = 0;
    bool pending_valid = false;
    struct Counts { std::uint32_t samples = 0, agree = 0, disagree = 0, invalid = 0, no_register = 0, unlatched = 0; } frame{};
    bool candidate_this_frame = false;

    static bool unit(const float v[4]) noexcept {
        float n = 0.f;
        for (unsigned i = 0; i < 3; ++i) { if (!(v[i] == v[i]) || v[i] > 2.f || v[i] < -2.f) return false; n += v[i] * v[i]; }
        constexpr float lo = (1.f - sun_unit_tolerance) * (1.f - sun_unit_tolerance), hi = (1.f + sun_unit_tolerance) * (1.f + sun_unit_tolerance);
        return n >= lo && n <= hi && v[3] == 0.f; // a direction: unit length, w = 0
    }
    static bool agrees(const float a[4], const float b[4]) noexcept {
        const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        const float aa = a[0] * a[0] + a[1] * a[1] + a[2] * a[2], bb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
        return d > 0.f && d * d >= sun_agreement_cos * sun_agreement_cos * aa * bb;
    }
    void reset() noexcept { *this = SunLatch{}; }
    void begin_frame() noexcept { frame = {}; candidate_this_frame = false; }
    // One routed draw whose program has no LightDir_Dir0 (or an unwritten register).
    void no_register() noexcept { ++frame.no_register; }
    // One sample. True when it agrees with (or became) the latched sun.
    bool sample(const float v[4], int reg, std::uint64_t program = 0) noexcept {
        ++frame.samples;
        if (!unit(v)) { ++frame.invalid; return false; }
        if (!valid) {
            if (pending_valid && agrees(pending, v)) { // the second draw agrees: the first one's value is the sun
                for (unsigned i = 0; i < 4; ++i) sun[i] = pending[i];
                valid = true; source_register = pending_register; source_program = pending_program; candidate_frames = 0; pending_valid = false; ++frame.agree;
                return true;
            }
            for (unsigned i = 0; i < 4; ++i) pending[i] = v[i];
            pending_register = reg; pending_program = program; pending_valid = true; ++frame.unlatched;
            return false;
        }
        if (agrees(sun, v)) { ++frame.agree; return true; }
        ++frame.disagree;
        if (!candidate_this_frame) {
            candidate_this_frame = true;
            if (!candidate_frames || !agrees(candidate, v)) { for (unsigned i = 0; i < 4; ++i) candidate[i] = v[i]; candidate_frames = 0; }
            candidate_register = reg; candidate_program = program;
        }
        return false;
    }
    // The scene end: the frame's verdict; usable verdicts replay with frame_sun().
    SunVerdict end_frame() noexcept {
        if (!valid) return SunVerdict::None;
        if (frame.agree) { candidate_frames = 0; return SunVerdict::Sampled; }
        if (!frame.disagree) return SunVerdict::Retained;
        if (++candidate_frames >= sun_relatch_frames) {
            for (unsigned i = 0; i < 4; ++i) sun[i] = candidate[i];
            source_register = candidate_register; source_program = candidate_program; candidate_frames = 0;
            return SunVerdict::Relatched;
        }
        return SunVerdict::Changing;
    }
    const float* frame_sun() const noexcept { return valid ? sun : nullptr; }
};
} // namespace x3m::shadow_replay
