#pragma once
#include <cstddef>
#include <cstdint>
#include "sse_scalar.h"
#include "engine_effects_core.h"
#include "../renderer/fog_transmittance.h"

// Portable core of the engine plumes, phase 2 (docs/architecture/engine-effects-modern.md sections 3-6): the strength
// presets and their parser, the Ctrl+Alt+F6 press latch, the tint of a record, and the CPU builder that turns the
// frame's glow-jet records (engine_effects_core.h Record) into the vertices of the stage's one indexed draw. No Windows
// dependency: the host tests compile it. No x87: float work through SSE scalars, no float returned by value from a
// function that may stay out of line.
//
// Per nozzle, two quads (8 vertices, 12 indices) in the camera's view space, projected by the vertex program; the look
// is the Engine Exhaust Lab's (docs/architecture/engine-effects-modern.md "Plume look redesign", the constants in
// Look below), drawn analytically by src/effects/engine_plume_ps.hlsl:
// - the axial billboard: it contains the plume axis (record.axis = -(model z), the side the glow mesh extends to) and
//   is turned about that axis to face the camera; local (x along the axis from the nozzle, y across) in world units,
//   the nozzle width n = Look::nozzle_width x value (X3M_ENGINE_PLUME_NOZZLE, default 0.5), the length L = z x value x
//   the length pulse (a per-nozzle value noise of the stage's clock, 1 +- Look::pulse, evaluated once per seed byte and
//   frame). The quad is a trapezoid, linear in x, that encloses the body (its edge, eroded outwards by up to 0.48 erode
//   of the local width, through the linear upper bound of the width profile, the cylinder-to-cone line), the halo's
//   window (halo_reach x the nozzle's halo sigma, constant along the plume as in the mock-up) and the nozzle ring, plus
//   one pixel;
// - the nozzle disc: camera-facing (the view plane), the end-on representation of the whole plume (after flight C): the
//   body integrated along the axis (8 samples of the law, Look tables), its radiance I x the view integration
//   disc_kappa x L / n x |axis . to_camera|, a halo of the nozzle's sigma carrying disc_halo x L / n, the ring; the
//   total soft-capped at disc_cap x the side view's peak (the body's crest one shock period in). Facing f =
//   |axis . to_camera|: the disc's weight smoothstep(0.3, 0.7, f) (none under 0.3: a side view draws no disc), the
//   axial quad's 1 - (1 - axial_floor) x that weight (half from 0.7 up: the foreshortened plume keeps its length), so the
//   total radiance stays near the side view's at every angle.
// value = record.size (|model x| of the c4-6 rows: the body's LOD-0 value x the context scale); a main jet's value is
// raised to sub_floor x the largest main-jet value of its ship (the records' parent node, Ring::parent) for the plume's
// size and length, not its position (capital sub-engines; ShipFloor). RCS jets (v/00566,
// flag_steering) take the same quads with L = z * value (short by construction: z runs 0.01..1.0 on steering) and
// their radiance x min(z, 1); below z 0.02 they are not drawn.
// Screen rules: a nozzle whose value projects under 1.5 px is not drawn; the nozzle width is at least 3 px and a main
// jet's L at least 6 px (the 3x3-clip survival rule of the motes); the plume's projected width (twice its widest drawn
// half-width, the body's eroded edge or the halo's reach, at the axis point nearest the camera) is clamped to 0.12 H by
// shrinking it about the nozzle, and its radiance fades 1 -> 0.5 over the last 20 % before the clamp (the own ship's
// plume in chase view; any plume that close).
// Flow: the noise field translates along the axis by a phase in nozzle widths accumulated on the CPU once per frame
// (FlowPhase at flow_rate: the mock-up's speed at s = 1 without the pulse), so it moves at one speed whatever the
// pulsed, throttle-dependent L.
// Occlusion depth (the pixel program): the nearest axis point's view depth, the nozzle's view z (intensity[3]) plus
// the axis's view z component (intensity[2]) x local u clamped to [0, L] (exact anywhere on the billboard, whose side
// vector has a view z component off-centre), pulled towards the camera by 0.5 value x max(0, axis . to_camera); the
// disc's is the nozzle's (its intensity[2] is the soft cap).
// Mouth (after flight C): the ring and the halo combine as a soft maximum; the shock cells ramp in over the first period
// (the mouth is no crest); where the disc is drawn the axial quad hands its mouth over to it inside the disc's footprint
// (Look::handover_inner..outer nozzle widths of screen-plane distance, by the disc's weight).
// View filter: only the records of the scene view are drawn (ViewFilter: recorded in the scene phase with the scene
// view's camera handle: the one the own ship's jets were recorded under, else the frame's most frequent handle among
// the scene-phase records); the rest are counted
// skipped_other_view (a target-monitor view would otherwise be projected with the scene camera).
// Fog (phase 3): with View::fog on, the tint is multiplied per channel by the stored-density look's mean transmittance
// at the nozzle's distance (renderer/fog_transmittance.h), the same factor the nozzle's ribbon takes, and the vertex
// carries that transmittance for the pixel program's white-hot core and ring colours.
namespace x3m::engine_plumes {
namespace ee = x3m::engine_effects::core;

// --------------------------------------------------------------------------- presets
// X3M_ENGINE_EFFECTS_PRESET=restrained|default|strong (ini engine_effects_preset), default "default"; Ctrl+Alt+F6
// cycles them at run time. Each scales I_core, I_halo and the halo sigma.
enum class Preset : std::uint8_t { restrained = 0, standard = 1, strong = 2 };
constexpr unsigned preset_count = 3;
constexpr Preset default_preset = Preset::standard;
inline const char* preset_name(Preset p) noexcept {
    return p == Preset::restrained ? "restrained" : p == Preset::strong ? "strong" : "default";
}
inline void preset_scale(Preset p, float* out) noexcept {
    *out = p == Preset::restrained ? .6f : p == Preset::strong ? 1.5f : 1.f;
}
inline Preset next_preset(Preset p) noexcept {
    return Preset((unsigned(p) + 1u) % preset_count);
}
// Exactly one of the three words in lower case (the engine_effects option's rule: mixed case, padding or another word
// is refused); `n` characters of narrow or wide text.
template <class Char> inline bool parse_preset(const Char* text, std::size_t n, Preset* out) noexcept {
    static const char* const words[preset_count] = {"restrained", "default", "strong"};
    if (!text) return false;
    for (unsigned w = 0; w < preset_count; ++w) {
        std::size_t len = 0;
        while (words[w][len]) ++len;
        if (n != len) continue;
        bool same = true;
        for (std::size_t i = 0; i < n && same; ++i) same = text[i] == Char(words[w][i]);
        if (same) {
            *out = Preset(w);
            return true;
        }
    }
    return false;
}
template <class Char> inline bool parse_preset(const Char* text, Preset* out) noexcept {
    if (!text) return false;
    std::size_t n = 0;
    while (text[n] && n < 16) ++n;
    if (text[n]) return false;
    return parse_preset(text, n, out);
}

// --------------------------------------------------------------------------- hotkey
// Ctrl+Alt+F6 with Shift up, edge-triggered on F6's own latch: a held F6 never becomes a press by changing modifiers,
// and an unfocused window neither fires nor arms (the latch follows the key while unfocused, so focus coming back with
// F6 held is no press).
struct PresetKey {
    bool f6_down = false;
    bool step(bool focused, bool control, bool alt, bool shift, bool f6) noexcept {
        const bool press = focused && control && alt && !shift && f6 && !f6_down;
        f6_down = f6;
        return press;
    }
};

// --------------------------------------------------------------------------- look
// The plume look: the Engine Exhaust Lab's settings the user chose (tools/effects/engine_exhaust_lab.html: "bulge=1.15
// taper=0.45 tail=0.7 ring=0.6 turb=0.6 flow=3 erode=0.57 pulse=0.25 shock=0.5 period=0.16 cfade=0.6 heat=0.7 core=0.45
// halo=1.1 hb=0.35"; after flight C the ring 0.3 and the nozzle 0.5), in one block: the CPU builder reads it and the
// pass uploads it to the pixel program (c3..c15, pixel_constants). Lengths across are in nozzle widths, lengths along
// in L.
struct Look {
    // The nozzle width in value. The mock-up's length law is L = 4 (0.25 + 1.75 s) nozzle widths and the game's
    // L = z value with z = 0.25 + 1.75 s, so value / 4 is the mock-up's proportions; flight C (Run 120 A) chose value / 2
    // (0.25 read as a needle): L = 2 z nozzle widths, 4 at full throttle. Load-time knob X3M_ENGINE_PLUME_NOZZLE (ini
    // engine_plume_nozzle, 0.1..1.0; parse_nozzle).
    float nozzle_width = .5f;
    float bulge = 1.15f;  // the mouth bulge, x the nozzle width
    float taper = .45f;   // 0 cylinder .. 1 cone
    float tail = .7f;     // tail softness
    float ring = .3f;     // nozzle ring brightness (flight C: 0.6 -> 0.3, the mouth outshone the body)
    float turb = .6f;     // turbulence (radiance modulation)
    float flow = 3.f;     // flow speed: the mock-up's scroll, 0.35 flow L / 1.6 nozzle widths per second at s = 1 (flow_rate)
    float erode = .57f;   // edge erosion
    float pulse = .25f;   // length pulse: L x (1 +- pulse), mean 1
    float shock = .5f;    // shock diamond strength; ramped in over the first period (flight C: the mouth is no crest)
    float period = .16f;  // their period, x L
    float cfade = .6f;    // their fade along the plume
    float heat = .7f;     // the white-hot core
    float core = .45f;    // core radius, x the local width
    float halo = 1.1f;    // halo width: e-fold 0.5 halo nozzle widths at the nozzle (x the preset)
    float hb = .35f;      // halo brightness
    // The mock-up's tail narrowing, w x (1 - 0.6 taper smoothstep(0.6, 1, u)); the halo keeps the nozzle's sigma along
    // the whole plume, as in the mock-up.
    float tail_narrowing = .6f;
    float core_low = 1.2f, core_high = 4.f; // I(s) = lerp(1.2, 4.0, s)
    float halo_low = .3f, halo_high = 1.f;  // the halo x lerp(0.3, 1, s)
    float ring_low = .4f, ring_high = 1.f;  // the ring x lerp(0.4, 1, s)
    float ring_falloff = 14.f;              // the ring's axial falloff, per nozzle width
    float pulse_rate = 3.f;                 // the length pulse's noise, per second
    // After flight C (docs/architecture/engine-effects-modern.md, "After flight C").
    // The end-on disc: the body integrated along the axis has radiance I x disc_kappa x L / n x the view factor (the
    // ratio of the side view's body energy to the integrated profile's, per nozzle width of length: 1.8 from the law,
    // verification/results/engine-effects/plume_end_on_model.py); its halo I_halo x disc_halo x L / n x the view factor;
    // the total soft-capped at disc_cap x the side view's peak. The facing band: the disc from disc_low to full at
    // disc_high; the axial quad's weight there drops to axial_floor.
    float disc_kappa = 1.8f;
    float disc_halo = 3.f;
    float disc_cap = 1.5f;
    float disc_low = .3f, disc_high = .7f;
    float axial_floor = .5f;
    // The mouth's hand-over: the axial quad x (1 - disc weight x (1 - smoothstep(inner, outer, d))), d the screen-plane
    // distance from the nozzle in nozzle widths (the disc's integrated body ends near 0.6).
    float handover_inner = .3f, handover_outer = .8f;
    // Capital sub-engines: a main jet's value is at least sub_floor x the largest main-jet value of its ship.
    float sub_floor = .45f;
};
constexpr Look default_look{};
constexpr float soft_core = .15f, soft_halo = 1.f; // SOFT x value (the pixel program's lane terms: body and ring, halo)
constexpr float halo_reach = 2.25f;         // the halo window's zero, x the halo sigma (the quads reach it); the window
                                            // tapers over its last 0.5 sigma (the halo is the mock-up's inside it)
constexpr float occlusion_bias = .5f;       // x value x max(0, axis . to_camera): the exhaust facing the camera clears its hull
constexpr float chase_cap = .12f;           // x H: the largest projected plume
constexpr float chase_fade_band = .2f;      // the last 20 % before the cap
constexpr float chase_fade_floor = .5f;     // the radiance at and past the cap
constexpr float min_nozzle_px = 3.f, min_length_px = 6.f, cull_px = 1.5f;
constexpr float steering_min_z = .02f;
constexpr float clock_wrap = 1024.f;        // seconds: the pixel program's clock wraps (float precision of the noise)
constexpr double phase_wrap = 4096.;        // nozzle widths: the flow phase wraps (one discontinuity of the noise per wrap)
constexpr float nozzle_min = .1f, nozzle_max = 1.f; // X3M_ENGINE_PLUME_NOZZLE's accepted range (x value)
constexpr unsigned max_nozzles = ee::ring_capacity; // 1,024: one ring
constexpr unsigned vertices_per_nozzle = 8, indices_per_nozzle = 12;
constexpr unsigned max_vertices = max_nozzles * vertices_per_nozzle; // 8,192: 16-bit indices
static_assert(max_vertices <= 65536u, "16-bit indices");
// The width profile (nozzle widths): w(u) = min(line(u), bulge(u)) x narrowing(u), line(u) the cylinder-to-cone line
// 0.5 bulge + (0.04 - 0.5 bulge) taper u, an upper bound of w on [0, 1] (the quad follows it); w(0) and w(1).
inline void width_line(const Look& k, float u, float* out) noexcept {
    *out = .5f * k.bulge + (.04f - .5f * k.bulge) * k.taper * u;
}
inline void width_at_nozzle(const Look& k, float* out) noexcept {
    const float line = .5f * k.bulge, bulge = .5f * k.bulge * (1.f - .55f) + .5f * k.bulge * k.taper;
    *out = line < bulge ? line : bulge;
}
inline void width_at_tip(const Look& k, float* out) noexcept { // the bulge term's exp(-9) neglected: the line is lower
    float line = 0.f;
    width_line(k, 1.f, &line);
    const float narrowing = 1.f - k.tail_narrowing * k.taper;
    *out = line * (narrowing > .05f ? narrowing : .05f);
}
// Scalar math of the law on the CPU without x87 (the tables below and the side view's peak): smoothstep, exp(-x) (the
// fog's series, relative error under 1e-5) and cos (range-reduced Taylor series to x^10, error under 1e-6).
namespace law {
inline void smooth(float e0, float e1, float x, float* out) noexcept {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    *out = t * t * (3.f - 2.f * t);
}
inline void exp_neg(float x, float* out) noexcept {
    x3m::renderer::fog_exp_negative(x, out);
}
inline void cos(float x, float* out) noexcept {
    const float pi = 3.14159265f, two_pi = 6.28318531f;
    float r = x - two_pi * float(x3m::scalar::floor(double(x / two_pi + .5f))); // [-pi, pi]
    float sign = 1.f;
    if (r < 0.f) r = -r;
    if (r > .5f * pi) {
        r = pi - r;
        sign = -1.f;
    }
    const float r2 = r * r;
    *out = sign * (1.f - r2 * (.5f - r2 * (1.f / 24.f - r2 * (1.f / 720.f - r2 * (1.f / 40320.f - r2 * (1.f / 3628800.f))))));
}
// The law's functions of u alone (engine_plume_ps.hlsl): the width w(u) (nozzle widths), the tail, the shock cells'
// amplitude without the throttle (ramped in over the first period) and the white-hot core's heat.
inline void width(const Look& k, float u, float* out) noexcept {
    float e9 = 0.f, e4 = 0.f, ramp = 0.f, narrow = 0.f;
    exp_neg(9.f * u, &e9);
    exp_neg(4.f * u, &e4);
    smooth(0.f, .25f, u, &ramp);
    smooth(.6f, 1.f, u, &narrow);
    const float c0 = .5f * k.bulge, c1 = (.04f - .5f * k.bulge) * k.taper, c2 = .5f * k.bulge * k.taper;
    const float b = c0 * (1.f - .55f * e9) * (1.f + .35f * ramp * e4) + c2, line = c0 + c1 * u;
    const float n = 1.f - k.tail_narrowing * k.taper * narrow;
    *out = (line < b ? line : b) * (n > .05f ? n : .05f);
}
inline void tail(const Look& k, float u, float* out) noexcept {
    float fade = 0.f, e = 0.f;
    smooth(.75f + (.25f - .75f) * k.tail, 1.f, u, &fade);
    exp_neg(u * 1.2f * k.tail, &e);
    *out = (1.f - fade) * e;
}
inline void cell(const Look& k, float u, float* out) noexcept {
    float c = 0.f, e = 0.f, ramp = 0.f;
    cos(6.2831853f * u / k.period, &c);
    exp_neg(u * 5.f * k.cfade, &e);
    smooth(0.f, k.period, u, &ramp);
    *out = k.shock * c * e * ramp;
}
inline void heat(const Look& k, float u, float* out) noexcept {
    float h = 0.f;
    smooth(0.f, .55f, u, &h);
    *out = k.heat * (1.f - h);
}
} // namespace law
// The end-on disc's integration along the axis: the law at u_k = (k + 0.5) / 8 (the pixel program's c8..c15: w, tail,
// cell, heat), and the side view's peak per I_core on the axis: 1.6 (the white-hot core) x the larger of the mouth's
// tail (1: the cells ramp in) and the first crest's, at u = period, tail x (1 + s x cell) (peak_axis_at).
constexpr unsigned disc_samples = 8;
struct LookTables {
    float disc[disc_samples][4]{};
    float crest_tail = 0.f, crest_cell = 0.f;
};
inline void look_tables(const Look& k, LookTables* out) noexcept {
    for (unsigned i = 0; i < disc_samples; ++i) {
        const float u = (float(i) + .5f) / float(disc_samples);
        law::width(k, u, &out->disc[i][0]);
        law::tail(k, u, &out->disc[i][1]);
        law::cell(k, u, &out->disc[i][2]);
        law::heat(k, u, &out->disc[i][3]);
    }
    law::tail(k, k.period, &out->crest_tail);
    law::cell(k, k.period, &out->crest_cell);
}
inline void peak_axis_at(const LookTables& t, float s, float* out) noexcept {
    const float crest = t.crest_tail * (1.f + s * t.crest_cell);
    *out = 1.6f * (crest > 1.f ? crest : 1.f);
}
// The pixel program's look constants c3..c15 (engine_plume_ps.hlsl), 52 floats (the flow is the frame's phase in c0.z,
// the halo's sigma the nozzle's).
constexpr unsigned pixel_constant_floats = 52;
inline void pixel_constants(const Look& k, float out[pixel_constant_floats]) noexcept {
    const float c[20] = {.5f * k.bulge, (.04f - .5f * k.bulge) * k.taper, .5f * k.bulge * k.taper, k.tail_narrowing * k.taper,
                         1.f / k.period, k.erode * 1.6f * .6f, k.turb * 2.2f, .75f + (.25f - .75f) * k.tail,
                         k.shock, 6.2831853f / k.period, 5.f * k.cfade, 1.2f * k.tail,
                         k.heat, 1.f / (1.4f * k.core), .46f * k.bulge, 1.f / 240.f,
                         k.handover_inner, k.handover_outer, k.ring_falloff, 2.f};
    for (unsigned i = 0; i < 20; ++i) out[i] = c[i];
    LookTables t;
    look_tables(k, &t);
    for (unsigned i = 0; i < disc_samples; ++i)
        for (unsigned j = 0; j < 4; ++j) out[20 + i * 4 + j] = t.disc[i][j];
}
// The flow's speed in nozzle widths per second: the mock-up's scroll 0.35 flow L / 1.6 (its noise runs at 1.6 per nozzle
// width along the axis) at the design length of s = 1 without the pulse, L = 2 value = 2 / nozzle_width nozzle widths
// (4 at the default 0.5: 2.625 nozzle widths per second at flow 3); the same speed in value units for any nozzle width.
inline void flow_rate(const Look& k, float* out) noexcept {
    const float L1 = k.nozzle_width > 0.f ? 2.f / k.nozzle_width : 0.f;
    *out = .35f * k.flow * L1 / 1.6f;
}
// The flow phase (nozzle widths): advanced once per frame by the stage clock's step x flow_rate, wrapped at phase_wrap;
// the pixel program translates the noise field along the axis by it (c0.z).
struct FlowPhase {
    double nozzle_widths = 0.;
    void advance(double dt, float rate) noexcept {
        if (!(dt > 0.) || !(dt < 1e9) || !(rate > 0.f) || !(rate < 1e6f)) return;
        nozzle_widths += dt * double(rate);
        if (nozzle_widths >= phase_wrap) nozzle_widths -= phase_wrap * x3m::scalar::floor(nozzle_widths / phase_wrap);
    }
    void wrapped(float* out) const noexcept { *out = float(nozzle_widths); }
};
// X3M_ENGINE_PLUME_NOZZLE: the whole text one plain decimal number (digits with at most one point; no sign, exponent or
// padding) in [nozzle_min, nozzle_max]; anything else is refused (the caller keeps default_look.nozzle_width). `n`
// characters of narrow or wide text.
template <class Char> inline bool parse_nozzle(const Char* text, std::size_t n, float* out) noexcept {
    if (!text || !n || n > 12) return false;
    std::uint64_t mantissa = 0, scale = 1;
    bool point = false, digits = false;
    for (std::size_t i = 0; i < n; ++i) {
        const Char c = text[i];
        if (c == Char('.') && !point) {
            point = true;
            continue;
        }
        if (c < Char('0') || c > Char('9')) return false;
        mantissa = mantissa * 10u + std::uint64_t(c - Char('0'));
        if (point) scale *= 10u;
        digits = true;
    }
    if (!digits) return false;
    const float v = float(double(mantissa) / double(scale));
    if (!(v >= nozzle_min) || !(v <= nozzle_max)) return false;
    *out = v;
    return true;
}

// Normalised linear tints of the clusters (tools/effects/engine_bodies.py CLUSTERS through the sRGB EOTF, divided by
// the largest channel), for a record without a table colour; grey and white are neutral.
inline const float* cluster_tint(unsigned cluster) noexcept {
    static const float tints[ee::cluster_count][3] = {
        {.1049f, .1049f, 1.f}, {.0287f, .6456f, 1.f}, {1.f, .0694f, .0694f}, {.1659f, 1.f, .4289f}, {1.f, 1.f, .0033f},
        {1.f, .1967f, .0441f}, {1.f, .1553f, .9596f}, {.6941f, .2667f, 1.f}, {1.f, 1.f, 1.f},       {1.f, .7911f, .6973f},
        {.0265f, 1.f, .0265f}, {.1350f, .6747f, 1.f},  {1.f, 1.f, 1.f}};
    return tints[cluster < ee::cluster_count ? cluster : ee::default_cluster];
}

// One vertex of the stage's VB: 72 bytes (FLOAT3, 3 x FLOAT4, 3 x D3DCOLOR).
struct Vertex {
    float position[3];  // view space (x right, y up, z forward), before the jittered projection
    float local[4];     // x, y (world units), L (pulsed, world; the disc: the ring's radiance), the nozzle width n (world)
    float shape[4];     // halo sigma at the nozzle (nozzle widths, x the preset), value (the SOFT base), occlusion bias
                        // (view units), kind (0 axial, 1 disc)
    float intensity[4]; // axial: I_core, I_halo (x weights), the axis's view z component; disc: the integrated body's
                        // radiance per unit of the mean profile, the halo's, the soft cap (x the disc's weight); the
                        // nozzle's view z
    std::uint32_t tint;   // 0xAARRGGBB of the mean colour, largest channel 255
    std::uint32_t params; // 0xAARRGGBB: R the throttle s, G the noise seed, B I_ring / I_core / 2, A the disc's weight
    std::uint32_t fog;    // 0xAARRGGBB of the fog transmittance per channel (white without fog): the white-hot core's
                          // and the ring's colours take it (the tint carries it already); A sin(view) = sqrt(1 - f^2)
};
static_assert(sizeof(Vertex) == 72, "the stage's vertex stride");

// The camera of the frame: world -> view rows (view_j = dot(p, rows[j].xyz) + rows[j].w), the projection's scale terms
// (the jitter terms move, they do not size) and the target height.
struct View {
    float rows[12]{};
    float m00 = 0.f, m11 = 0.f;
    float height = 0.f;
    float near_z = 1.f;
    x3m::renderer::FogTransmittanceLaw fog{}; // phase 3: off unless this frame's density composite applied
};
struct BuildStats {
    unsigned nozzles = 0, vertices = 0, discs = 0, steering = 0, capped = 0, faded = 0;
    unsigned floored = 0, ships = 0; // main jets raised by their ship's floor; distinct known parents (ShipFloor)
    unsigned culled_rows = 0, culled_behind = 0, culled_small = 0, culled_idle = 0, culled_capacity = 0;
    unsigned fogged = 0;        // nozzles whose colours took the fog transmittance (phase 3)
    float fog_min = 1.f;        // the smallest channel transmittance applied this frame
    unsigned skipped_other_view = 0;
};
// The per-record view tags beside the records (engine_effects_core.h Ring camera / scene) and the scene view's camera
// handle: a record is drawn when it was recorded in the scene phase with that handle.
struct ViewFilter {
    const std::uint32_t* camera = nullptr;
    const std::uint8_t* scene = nullptr;
    std::uint32_t handle = 0;
};
// The scene view's camera handle. Own rule first: the most frequent handle among the scene-phase records tagged as the
// own ship's jets (Ring::own; the own ship flies in the main view, so its camera is the scene view even when a target
// monitor's jets, recorded in the scene phase too, outnumber them). Majority rule only without such a record: the
// most frequent handle among all scene-phase records. Ties: the first seen; at most 8 distinct handles are tallied,
// later ones count against nothing. False when no record is in the scene phase. `own` may be null (no tags).
enum class ViewRule : std::uint8_t { none = 0, own = 1, majority = 2 };
inline const char* view_rule_name(ViewRule r) noexcept {
    return r == ViewRule::own ? "own" : r == ViewRule::majority ? "majority" : "none";
}
inline bool tally_scene_view(const std::uint32_t* camera, const std::uint8_t* scene, const std::uint8_t* own,
                             unsigned count, std::uint32_t* out) noexcept {
    std::uint32_t handles[8];
    unsigned votes[8], distinct = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!scene[i] || (own && !own[i])) continue;
        unsigned k = 0;
        while (k < distinct && handles[k] != camera[i]) ++k;
        if (k == distinct) {
            if (distinct == 8) continue;
            handles[distinct] = camera[i];
            votes[distinct++] = 0;
        }
        ++votes[k];
    }
    if (!distinct) return false;
    unsigned best = 0;
    for (unsigned k = 1; k < distinct; ++k)
        if (votes[k] > votes[best]) best = k;
    *out = handles[best];
    return true;
}
inline bool scene_view_camera(const std::uint32_t* camera, const std::uint8_t* scene, const std::uint8_t* own,
                              unsigned count, std::uint32_t* out, ViewRule* rule = nullptr) noexcept {
    ViewRule chosen = ViewRule::none;
    if (own && tally_scene_view(camera, scene, own, count, out))
        chosen = ViewRule::own;
    else if (tally_scene_view(camera, scene, nullptr, count, out))
        chosen = ViewRule::majority;
    if (rule) *rule = chosen;
    return chosen != ViewRule::none;
}

// The record's colours: the body's normalised mean / peak (engine_bodies.json mean_linear / peak_linear), else the
// cluster tint for both.
inline void record_tint(const ee::Record& r, const ee::Body* body, float mean[3], float peak[3]) noexcept {
    if (body && body->colour) {
        for (unsigned i = 0; i < 3; ++i) {
            mean[i] = body->mean[i];
            peak[i] = body->peak[i];
        }
        return;
    }
    const float* t = cluster_tint(unsigned(r.flags >> ee::cluster_shift) & 15u);
    for (unsigned i = 0; i < 3; ++i) mean[i] = peak[i] = t[i];
}
inline std::uint32_t pack_colour(const float c[3]) noexcept {
    std::uint32_t out = 0xff000000u;
    for (unsigned i = 0; i < 3; ++i) {
        float v = c[i];
        v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
        out |= std::uint32_t(int(v * 255.f + .5f)) << (16u - 8u * i);
    }
    return out;
}
inline std::uint32_t hash32(std::uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline std::uint32_t record_seed(const ee::Record& r) noexcept {
    return hash32(std::uint32_t(r.serial) ^ std::uint32_t(r.serial >> 32) ^ hash32(r.node_handle) ^ (r.model * 0x9e3779b9u));
}
// The mock-up's value noise on the CPU (the pixel program's vnoise / fbm, engine_plume_ps.hlsl): the hash
// frac(x y z (x + y + z)) of p' = frac(p 0.3183099 + (.1, .2, .3)) 17, smoothstep-trilinear between the lattice corners,
// three octaves (0.5, 0.25, 0.125; fbm in [0, 0.875]). For |p| < 2^31 (scalar::floor).
namespace noise {
inline void frac(float x, float* out) noexcept {
    *out = x - float(x3m::scalar::floor(double(x)));
}
inline void hash(float x, float y, float z, float* out) noexcept {
    float a = 0.f, b = 0.f, c = 0.f;
    frac(x * .3183099f + .1f, &a);
    frac(y * .3183099f + .2f, &b);
    frac(z * .3183099f + .3f, &c);
    a *= 17.f;
    b *= 17.f;
    c *= 17.f;
    frac(a * b * c * (a + b + c), out);
}
inline void value(const float p[3], float* out) noexcept {
    float i[3], f[3];
    for (unsigned k = 0; k < 3; ++k) {
        i[k] = float(x3m::scalar::floor(double(p[k])));
        f[k] = p[k] - i[k];
        f[k] = f[k] * f[k] * (3.f - 2.f * f[k]);
    }
    float h[8];
    for (unsigned c = 0; c < 8; ++c) hash(i[0] + float(c & 1u), i[1] + float((c >> 1) & 1u), i[2] + float(c >> 2), &h[c]);
    float x[4];
    for (unsigned c = 0; c < 4; ++c) x[c] = h[2 * c] + (h[2 * c + 1] - h[2 * c]) * f[0];
    const float y0 = x[0] + (x[1] - x[0]) * f[1], y1 = x[2] + (x[3] - x[2]) * f[1];
    *out = y0 + (y1 - y0) * f[2];
}
inline void fbm(const float p[3], float* out) noexcept {
    float a = 0.f, b = 0.f, c = 0.f;
    value(p, &a);
    const float p2[3] = {p[0] * 2.03f + 1.7f, p[1] * 2.03f + 1.7f, p[2] * 2.03f + 1.7f};
    value(p2, &b);
    const float p3[3] = {p[0] * 4.1f + 3.1f, p[1] * 4.1f + 3.1f, p[2] * 4.1f + 3.1f};
    value(p3, &c);
    *out = .5f * a + .25f * b + .125f * c;
}
} // namespace noise
// The nozzle's noise seed (the pixel program's offset: G of params, x 1,861.5 in the noise's third axis).
inline unsigned seed_byte(const ee::Record& r) noexcept {
    return (record_seed(r) >> 8) & 255u;
}
// The length pulse of a nozzle at the stage's clock `seconds`: 1 + pulse (2 fbm / 0.875 - 1), in [1 - pulse,
// 1 + pulse] with mean 1 (the mock-up's fbm(3 t, side, 0) recentred, so the throttle's length law holds on average).
inline void length_pulse(const Look& k, unsigned seed, float seconds, float* out) noexcept {
    const float p[3] = {seconds * k.pulse_rate, float(seed) * 7.3f, 0.f};
    float f = 0.f;
    noise::fbm(p, &f);
    *out = 1.f + k.pulse * (f * (2.f / .875f) - 1.f);
}
// The frame's length pulses, one per seed byte: at most 256 fbm evaluations a frame however many records (build()).
struct PulseCache {
    float value[256];
    std::uint32_t known[8] = {};
    void get(const Look& k, unsigned seed, float seconds, float* out) noexcept {
        seed &= 255u;
        const std::uint32_t bit = 1u << (seed & 31u);
        if (!(known[seed >> 5] & bit)) {
            length_pulse(k, seed, seconds, &value[seed]);
            known[seed >> 5] |= bit;
        }
        *out = value[seed];
    }
};

// --------------------------------------------------------------------------- the stage's clock
// Seconds for the plumes' flow and pulse and the ribbons' pool: the performance counter between stage runs, except
// that a step ending on an F8 capture frame or on the frame after one advances by at most the last ordinary step
// (itself held to 0.1 s). The capture's readbacks stall the wall clock by seconds; counted in full, that evicted every
// ribbon (0.3 s fade) on capture frames 2-8 (run403) and jumped the flow. Every other gap (a load screen, frames the
// stage does not run) advances in full, so the ribbons' 0.3 s gap rule is unchanged.
struct StageClock {
    double seconds = 0.;
    double last_step = 0.; // the last step's advance (0: none); the flow phase takes it
    std::uint64_t last = 0;
    double ordinary_step = 1. / 60.;
    bool started = false, last_capture = false;
    static constexpr double max_capture_step = .1;
    void step(std::uint64_t counter, std::uint64_t frequency, bool capture) noexcept {
        if (!frequency) return;
        last_step = 0.;
        if (started && counter >= last) {
            double dt = double(counter - last) / double(frequency);
            if (capture || last_capture)
                dt = dt < ordinary_step ? dt : ordinary_step;
            else
                ordinary_step = dt < max_capture_step ? dt : max_capture_step;
            seconds += dt;
            last_step = dt;
        }
        started = true;
        last = counter;
        last_capture = capture;
    }
    // The pixel program's time: wrapped at clock_wrap seconds (one discontinuity of the noise per wrap).
    void wrapped(float* out) const noexcept {
        const double w = double(clock_wrap);
        *out = float(seconds - w * x3m::scalar::floor(seconds / w));
    }
};

namespace detail {
inline void to_view(const View& v, const float p[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j)
        out[j] = p[0] * v.rows[j * 4] + p[1] * v.rows[j * 4 + 1] + p[2] * v.rows[j * 4 + 2] + v.rows[j * 4 + 3];
}
inline void rotate(const View& v, const float d[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j) out[j] = d[0] * v.rows[j * 4] + d[1] * v.rows[j * 4 + 1] + d[2] * v.rows[j * 4 + 2];
}
inline void normalise(float d[3], float* length) noexcept {
    const float n = x3m::scalar::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    *length = n;
    if (n > 0.f)
        for (unsigned i = 0; i < 3; ++i) d[i] /= n;
}
inline void cross(const float a[3], const float b[3], float out[3]) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline bool finite3(const float* v) noexcept {
    return ee::finite_f(v[0]) && ee::finite_f(v[1]) && ee::finite_f(v[2]);
}
} // namespace detail

// Pixels per world unit at view depth z (vertical focal length over z).
inline void pixels_per_unit(const View& v, float z, float* out) noexcept {
    *out = v.m11 * v.height * .5f / (z > v.near_z ? z : v.near_z);
}

// The quad list's indices for `nozzles` nozzles: two quads of four vertices each, two triangles per quad.
inline void write_indices(std::uint16_t* out, unsigned nozzles) noexcept {
    for (unsigned q = 0; q < nozzles * 2u; ++q) {
        const std::uint16_t b = std::uint16_t(q * 4u);
        const std::uint16_t t[6] = {b, std::uint16_t(b + 1), std::uint16_t(b + 2), std::uint16_t(b + 2), std::uint16_t(b + 1),
                                    std::uint16_t(b + 3)};
        for (unsigned i = 0; i < 6; ++i) out[q * 6u + i] = t[i];
    }
}

// The facing weights: the disc's smoothstep(disc_low, disc_high, f) and the axial quad's 1 - (1 - axial_floor) x it.
inline void facing_weights(const Look& k, float facing_abs, float* disc, float* axial) noexcept {
    law::smooth(k.disc_low, k.disc_high, facing_abs, disc);
    *axial = 1.f - (1.f - k.axial_floor) * *disc;
}

// One nozzle's eight vertices. False: not drawn (stats says why). `tables` look_tables(look); `seconds` the stage's
// clock (the length pulse); `floor_value` the ship's floor for this record (0: none; ShipFloor), raising the value of
// the plume's size and length, never its position; `pulses` (null: evaluated here) the frame's pulse per seed byte.
inline bool build_nozzle(const ee::Record& r, const ee::Body* body, const View& view, const Look& look,
                         const LookTables& tables, float scale, float seconds, float floor_value, Vertex* out,
                         BuildStats* stats, PulseCache* pulses = nullptr) noexcept {
    if ((r.flags & ee::flag_rows_unknown) || !detail::finite3(r.origin) || !detail::finite3(r.axis) ||
        !ee::finite_f(r.size) || !(r.size > 0.f) || !ee::finite_f(r.z) || !ee::finite_f(r.s)) {
        ++stats->culled_rows;
        return false;
    }
    const bool steering = (r.flags & ee::flag_steering) != 0;
    if (steering && r.z < steering_min_z) {
        ++stats->culled_idle;
        return false;
    }
    float o[3], a[3], al = 0.f;
    detail::to_view(view, r.origin, o);
    detail::rotate(view, r.axis, a);
    detail::normalise(a, &al);
    if (!(al > 0.f) || !detail::finite3(o)) {
        ++stats->culled_rows;
        return false;
    }
    float value = r.size;
    const bool raised = !steering && floor_value > value && ee::finite_f(floor_value);
    if (raised) value = floor_value;
    const unsigned seed = seed_byte(r);
    float pulse = 1.f;
    if (pulses)
        pulses->get(look, seed, seconds, &pulse);
    else
        length_pulse(look, seed, seconds, &pulse);
    float L = (r.z > 0.f ? r.z : 0.f) * value * pulse;
    // The quad's reach in nozzle widths: over the width line the body's eroded edge (1 + 0.48 erode of the local
    // width), at the nozzle at least the ring (its radius plus three of its sigmas); the halo window's reach
    // (halo_reach x sigma0, the nozzle's sigma, constant along the plume) everywhere. `extent` the widest of them at the
    // nozzle: the plume's drawn half-width the near-camera cap holds.
    float line0 = 0.f;
    width_line(look, 0.f, &line0);
    const float sigma0 = .5f * look.halo * scale; // nozzle widths
    const float body_reach = 1.f + .48f * look.erode;
    const float ring_outer = .46f * look.bulge + 3.f * .0645497f;
    float spread = body_reach;
    if (spread * line0 < ring_outer) spread = ring_outer / line0;
    const float halo_units = halo_reach * sigma0;
    const float extent = spread * line0 > halo_units ? spread * line0 : halo_units;
    // Behind the camera: the whole axial quad (nozzle to tip, plus its widest half-width) beyond the near plane.
    const float margin = extent * look.nozzle_width * value;
    const float tip_z = o[2] + a[2] * L;
    if ((o[2] < view.near_z && tip_z < view.near_z) && (o[2] + margin < view.near_z && tip_z + margin < view.near_z)) {
        ++stats->culled_behind;
        return false;
    }
    float ppu = 0.f;
    pixels_per_unit(view, o[2], &ppu);
    if (value * ppu < cull_px) {
        ++stats->culled_small;
        return false;
    }
    // The near-camera cap (the own ship in chase view): the plume's drawn width 2 extent n (the body's eroded edge or
    // the halo's reach, x the preset through sigma0) at the axis point nearest the camera (the tip when the exhaust
    // approaches it) is held to 0.12 H by shrinking the whole plume about the nozzle; its radiance fades 1 -> 0.5 over
    // the last 20 % before the cap. Its length is free: a distant capital's long plume is not shortened.
    float k = 1.f, near_weight = 1.f;
    {
        const float half = extent * look.nozzle_width * value; // world units, x k with the plume
        const float f = view.m11 * view.height * .5f, cap = chase_cap * view.height;
        const float toward = a[2] < 0.f ? -a[2] : 0.f; // approach to the camera per unit of length
        float near_depth = o[2] - toward * L;
        if (near_depth < view.near_z) near_depth = view.near_z;
        const float q = 2.f * half * f / near_depth / cap;
        if (q > 1.f - chase_fade_band) {
            float t = (q - (1.f - chase_fade_band)) * (1.f / chase_fade_band);
            t = t > 1.f ? 1.f : t;
            near_weight = 1.f - (1.f - chase_fade_floor) * t;
            ++stats->faded;
        }
        if (q > 1.f) {
            // Shrinking also moves the tip away: solve 2 half k f / (o_z - toward L k) = cap for k.
            const float denominator = 2.f * half * f + cap * toward * L;
            float kk = denominator > 0.f && o[2] > view.near_z ? cap * o[2] / denominator : 0.f;
            if (!(kk > 0.f) || kk > 1.f) kk = 1.f / q;
            k = kk;
            ++stats->capped;
        }
    }
    value *= k;
    L *= k;
    // Minimum screen sizes (after the cap: a capped plume is large anyway).
    float n = look.nozzle_width * value;
    if (n * ppu < min_nozzle_px) n = min_nozzle_px / ppu;
    if (!steering && L * ppu < min_length_px) L = min_length_px / ppu;
    // Radiance: I(s) x preset, the RCS weight z, the chase fade; the halo and the ring relative to it.
    const float s = r.s < 0.f ? 0.f : r.s > 1.f ? 1.f : r.s;
    float weight = near_weight;
    if (steering) weight *= r.z < 1.f ? r.z : 1.f;
    const float level = look.core_low + (look.core_high - look.core_low) * s;
    const float i_core = level * scale * weight;
    const float i_halo = look.hb * (look.halo_low + (look.halo_high - look.halo_low) * s) * scale * weight;
    float ring = look.ring * (look.ring_low + (look.ring_high - look.ring_low) * s) / level * .5f;
    ring = ring < 0.f ? 0.f : ring > 1.f ? 1.f : ring;
    std::uint32_t params = (std::uint32_t(int(s * 255.f + .5f)) << 16) | (seed << 8) | std::uint32_t(int(ring * 255.f + .5f));
    float mean[3], peak[3], transmittance[3] = {1.f, 1.f, 1.f};
    record_tint(r, body, mean, peak);
    if (view.fog.on) {
        x3m::renderer::fog_transmittance(view.fog, x3m::scalar::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]), transmittance);
        for (unsigned i = 0; i < 3; ++i) {
            mean[i] *= transmittance[i];
            stats->fog_min = transmittance[i] < stats->fog_min ? transmittance[i] : stats->fog_min;
        }
        ++stats->fogged;
    }
    const std::uint32_t tint = pack_colour(mean);
    std::uint32_t fog = pack_colour(transmittance) & 0x00ffffffu;
    // Facing: e = unit vector from the nozzle to the camera; the axial quad's side n = a x e, a fallback when the axis
    // points along the line of sight.
    float e[3] = {-o[0], -o[1], -o[2]}, el = 0.f;
    detail::normalise(e, &el);
    if (!(el > 0.f)) {
        e[0] = e[1] = 0.f;
        e[2] = -1.f;
    }
    const float facing = a[0] * e[0] + a[1] * e[1] + a[2] * e[2]; // +1: the exhaust points at the camera
    const float bias = occlusion_bias * value * (facing > 0.f ? facing : 0.f);
    const float facing_abs = facing < 0.f ? -facing : facing;
    float disc_weight = 0.f, axial_weight = 1.f;
    facing_weights(look, facing_abs, &disc_weight, &axial_weight);
    // The mouth's hand-over (the pixel program): the axial quad gives way to the disc inside the disc's footprint, by
    // the disc's weight (params A) over the screen-plane distance from the nozzle, (x sin(view), y) with sin(view) =
    // sqrt(1 - f^2) (fog A).
    {
        const float sine = x3m::scalar::sqrt(facing_abs < 1.f ? 1.f - facing_abs * facing_abs : 0.f);
        params |= std::uint32_t(int(disc_weight * 255.f + .5f)) << 24;
        fog |= std::uint32_t(int((sine < 1.f ? sine : 1.f) * 255.f + .5f)) << 24;
    }
    float side[3], sl = 0.f;
    detail::cross(a, e, side);
    detail::normalise(side, &sl);
    if (!(sl > 1e-4f)) {
        const float up[3] = {0.f, 1.f, 0.f}, right[3] = {1.f, 0.f, 0.f};
        detail::cross(a, a[1] * a[1] < .81f ? up : right, side);
        detail::normalise(side, &sl);
    }
    // The axial trapezoid, linear in x, extended behind the nozzle and past the tip by the halo's reach (at least the
    // ring's 0.05 nozzle widths behind): at each end the wider of the body's spread x n x the width line (extrapolated
    // linearly) and the halo's reach, + 1 px. max(line, constant) is convex in x, so the chord between the ends encloses
    // it; a plume shorter than its back reach takes the nozzle's width over the whole quad.
    const float pixel = 1.f / ppu;
    const float slope = (.04f - .5f * look.bulge) * look.taper; // d line / d u
    const float reach = halo_units * n;
    const float back = n * (halo_units > .05f ? halo_units : .05f) + pixel;
    const float front = L + reach + pixel;
    float body_back = spread * n * line0, body_front = body_back;
    const float width0 = (body_back > reach ? body_back : reach) + pixel;
    if (back < L) {
        body_back = spread * n * (line0 - slope * back / L);
        body_front = spread * n * (line0 + slope * front / L);
    }
    const float width_back = (body_back > reach ? body_back : reach) + pixel;
    const float width_front = (body_front > reach ? body_front : reach) + pixel;
    const float corners[4][2] = {{-back, -width_back}, {-back, width_back}, {front, -width_front}, {front, width_front}};
    for (unsigned c = 0; c < 4; ++c) {
        Vertex& v = out[c];
        const float x = corners[c][0], y = corners[c][1];
        for (unsigned j = 0; j < 3; ++j) v.position[j] = o[j] + a[j] * x + side[j] * y;
        v.local[0] = x;
        v.local[1] = y;
        v.local[2] = L;
        v.local[3] = n;
        v.shape[0] = sigma0;
        v.shape[1] = value;
        v.shape[2] = bias;
        v.shape[3] = 0.f;
        v.intensity[0] = i_core * axial_weight;
        v.intensity[1] = i_halo * axial_weight;
        v.intensity[2] = a[2];
        v.intensity[3] = o[2];
        v.tint = tint;
        v.params = params;
        v.fog = fog;
    }
    // The disc, the end-on plume, weighted by smoothstep(disc_low, disc_high, f); under disc_low it collapses to one point
    // (no pixel). Its radiance: the body integrated along the axis, I x disc_kappa x L / n x f per unit of the mean
    // sampled profile (the pixel program's sum over c8..c15 / 8), its halo I_halo x disc_halo x L / n x f, the ring's
    // peak, and the soft cap disc_cap x the side view's peak on the axis (peak_axis_at); all x the disc's weight, so the
    // pixel program's cap x (1 - exp(-total / cap)) scales with it.
    const bool disc_drawn = disc_weight > 0.f;
    const float half = width0 * (disc_drawn ? 1.f : 0.f);
    const float length_widths = n > 0.f ? L / n : 0.f;
    float axis_peak = 0.f;
    peak_axis_at(tables, s, &axis_peak);
    const float disc_body = disc_weight * i_core * look.disc_kappa * length_widths * facing_abs;
    const float disc_halo = disc_weight * i_halo * look.disc_halo * length_widths * facing_abs;
    const float disc_cap = disc_weight * look.disc_cap * i_core * axis_peak;
    const float disc_ring = disc_weight * i_core * ring * 2.f;
    const float dc[4][2] = {{-half, -half}, {-half, half}, {half, -half}, {half, half}};
    for (unsigned c = 0; c < 4; ++c) {
        Vertex& v = out[4 + c];
        v.position[0] = o[0] + dc[c][0];
        v.position[1] = o[1] + dc[c][1];
        v.position[2] = o[2];
        v.local[0] = dc[c][0];
        v.local[1] = dc[c][1];
        v.local[2] = disc_ring;
        v.local[3] = n;
        v.shape[0] = sigma0;
        v.shape[1] = value;
        v.shape[2] = bias;
        v.shape[3] = 1.f;
        v.intensity[0] = disc_body;
        v.intensity[1] = disc_halo;
        v.intensity[2] = disc_cap;
        v.intensity[3] = o[2];
        v.tint = tint;
        v.params = params;
        v.fog = fog;
    }
    if (disc_drawn) ++stats->discs;
    if (steering) ++stats->steering;
    if (raised) ++stats->floored;
    return true;
}

// --------------------------------------------------------------------------- the ship floor
// Capital sub-engines: the frame's records grouped by ship (the jet node's parent, node+0x18 = the ship's root node,
// docs/reverse-engineering/engine-effects.md; Ring::parent, 0 unknown) in an open-addressing map (linear probing,
// at least twice the records' slots, at most 2,048 for the ring's 1,024 records); each ship's largest main-jet value
// (RCS jets neither count nor take the floor). One pass to fill, one lookup per record in build(); the table is
// cleared only over the slots in use.
struct ShipFloor {
    static constexpr unsigned slots_max = 2048;
    std::uint32_t key[slots_max];
    float top[slots_max];
    unsigned mask = 0, used = 0;
    void reset(unsigned records) noexcept {
        unsigned n = 16;
        while (n < 2u * records && n < slots_max) n <<= 1;
        mask = n - 1u;
        used = 0;
        for (unsigned i = 0; i < n; ++i) key[i] = 0;
    }
    unsigned slot(std::uint32_t parent) const noexcept {
        unsigned i = hash32(parent) & mask;
        while (key[i] && key[i] != parent) i = (i + 1u) & mask;
        return i;
    }
    void note(std::uint32_t parent, float value) noexcept {
        if (!parent || !mask) return;
        const unsigned i = slot(parent);
        if (key[i]) {
            if (value > top[i]) top[i] = value;
        } else if (used + 1u < mask) { // one slot stays empty: every probe ends
            key[i] = parent;
            top[i] = value;
            ++used;
        }
    }
    void largest(std::uint32_t parent, float* out) const noexcept {
        *out = 0.f;
        if (!parent || !mask) return;
        const unsigned i = slot(parent);
        if (key[i]) *out = top[i];
    }
};

// The frame's records into `out` (capacity in nozzles); returns the nozzles written (8 vertices each). `body` maps a
// record's table index to its entry (null: none); `seconds` the stage's clock (wrapped, StageClock::wrapped); `filter`
// (null: every record) keeps the scene view's records; `look` (null: default_look) the plume look; `parents` (beside
// the records, Ring::parent) and `ships` (the caller's scratch, ShipFloor) both or neither: the sub-engine floor.
using BodyLookup = const ee::Body* (*)(int index);
inline unsigned build(const ee::Record* records, unsigned count, BodyLookup body, const View& view, Preset preset,
                      float seconds, Vertex* out, unsigned capacity, BuildStats* stats, const ViewFilter* filter = nullptr,
                      const Look* look = nullptr, const std::uint32_t* parents = nullptr, ShipFloor* ships = nullptr) noexcept {
    BuildStats local{};
    BuildStats& st = stats ? *stats : local;
    st = BuildStats{};
    if (!records || !out || !(view.m11 > 0.f) || !(view.height > 0.f) || !(view.near_z > 0.f)) return 0;
    for (unsigned i = 0; i < 12; ++i)
        if (!ee::finite_f(view.rows[i])) return 0;
    if (!ee::finite_f(seconds)) seconds = 0.f;
    const Look& k = look ? *look : default_look;
    float scale = 1.f;
    preset_scale(preset, &scale);
    LookTables tables;
    look_tables(k, &tables);
    auto shown = [&](unsigned i) { return !filter || (filter->scene[i] && filter->camera[i] == filter->handle); };
    // The ship floor: each known ship's largest main-jet value among the drawn view's records.
    const bool floors = parents && ships && k.sub_floor > 0.f;
    if (floors) {
        ships->reset(count);
        for (unsigned i = 0; i < count; ++i) {
            const ee::Record& r = records[i];
            if (!shown(i) || (r.flags & (ee::flag_steering | ee::flag_rows_unknown)) || !ee::finite_f(r.size) || !(r.size > 0.f))
                continue;
            ships->note(parents[i], r.size);
        }
        st.ships = ships->used;
    }
    PulseCache pulses;
    unsigned written = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (written >= capacity || written >= max_nozzles) {
            st.culled_capacity += count - i;
            break;
        }
        if (!shown(i)) {
            ++st.skipped_other_view;
            continue;
        }
        const ee::Record& r = records[i];
        const ee::Body* b = body && r.body >= 0 ? body(r.body) : nullptr;
        float floor_value = 0.f;
        if (floors && !(r.flags & ee::flag_steering)) {
            ships->largest(parents[i], &floor_value);
            floor_value *= k.sub_floor;
        }
        if (build_nozzle(r, b, view, k, tables, scale, seconds, floor_value, out + written * vertices_per_nozzle, &st, &pulses))
            ++written;
    }
    st.nozzles = written;
    st.vertices = written * vertices_per_nozzle;
    return written;
}
} // namespace x3m::engine_plumes
