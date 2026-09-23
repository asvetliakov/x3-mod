#pragma once
// d3d9-free parameters of the stored fog's dust motes (docs/architecture/fog-dust-motes.md): the launch option
// X3M_FOG_DUST_MOTES=N,SIZE,STREAK, the tunables X3M_FOG_MOTES_<NAME> (read once at init), the unit seed lattice and
// the vertex layout. Plain integer and float arithmetic only (SSE2 in the proxy build).
#include <cstdint>
namespace x3m::renderer {
constexpr unsigned fog_mote_count_min = 64, fog_mote_count_max = 8192;
constexpr float fog_mote_size_min = 2.f, fog_mote_size_max = 16.f, fog_mote_streak_max = 512.f;
// X3M_FOG_DUST_MOTES absent under the stored range: 1300,3,128 with MAX_PX 8 (below) since 2026-09-23 after Run 70 B/B2
// (the user also accepted SIZE 2); 0 is the explicit off.
constexpr unsigned fog_mote_default_count = 1300;
constexpr float fog_mote_default_size = 3.f, fog_mote_default_streak = 128.f;
constexpr unsigned fog_mote_vs_rows = 12;          // c0..c11 of the mote vertex program, one upload
constexpr unsigned fog_mote_vertex_bytes = 20;     // unit seed xyz, corner xy (float)
struct FogMoteTuning {
    unsigned count = 0;                            // N; 0: the option is off and nothing exists
    float size = 4.f, streak = 128.f;              // minimum capsule width and streak cap, pixels
    float radius = 1000.f, near_fade = 25.f;       // window radius R (cube side 2R) and near fade, render units
    float max_px = 8.f, gain = 1.f, soft = .02f, drift = 20.f;
    std::uint32_t seed = 1;
};
struct FogMoteField { const char* name; float FogMoteTuning::* field; float minimum, maximum; };
// X3M_FOG_MOTES_<NAME>; MAX_PX is raised to SIZE when below it (the range's lower end is SIZE). SEED is an integer.
constexpr FogMoteField fog_mote_fields[] = {
    {"RADIUS", &FogMoteTuning::radius, 200.f, 5000.f}, {"NEAR", &FogMoteTuning::near_fade, 5.f, 200.f},
    {"MAX_PX", &FogMoteTuning::max_px, fog_mote_size_min, 64.f}, {"GAIN", &FogMoteTuning::gain, 0.f, 8.f},
    {"SOFT", &FogMoteTuning::soft, 0.f, .1f}, {"DRIFT", &FogMoteTuning::drift, 0.f, 200.f},
};
// A value outside its range (or NaN) keeps the default; true when it was taken.
inline bool fog_mote_set(FogMoteTuning& tuning, const FogMoteField& field, float value) noexcept {
    if (!(value >= field.minimum && value <= field.maximum)) return false;
    tuning.*field.field = value; return true;
}
// The launch triple: N 0 (explicit off) or 64..8192, SIZE 2..16, STREAK 0..512. False for anything else.
inline bool fog_mote_option(unsigned count, float size, float streak, FogMoteTuning& tuning) noexcept {
    if (count != 0 && (count < fog_mote_count_min || count > fog_mote_count_max)) return false;
    if (!(size >= fog_mote_size_min && size <= fog_mote_size_max) || !(streak >= 0.f && streak <= fog_mote_streak_max)) return false;
    tuning.count = count; tuning.size = size; tuning.streak = streak;
    return true;
}
inline void fog_mote_normalize(FogMoteTuning& tuning) noexcept { if (tuning.max_px < tuning.size) tuning.max_px = tuning.size; }
// lowbias32 (Wellons): the seed lattice is a pure function of (index, SEED), no RNG state.
inline std::uint32_t fog_mote_hash(std::uint32_t x) noexcept {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
// Unit seed of mote `index` in [0,1)^3, 24-bit exact; the vertex program scales it by the cube side 2R.
inline void fog_mote_seed(std::uint32_t index, std::uint32_t seed, float out[3]) noexcept {
    const std::uint32_t salt = fog_mote_hash(seed ^ 0x9e3779b9u);
    for (unsigned a = 0; a < 3; ++a) out[a] = float(fog_mote_hash(index * 3u + a + salt) >> 8) * (1.f / 16777216.f);
}
} // namespace x3m::renderer
