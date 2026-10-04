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
//   the nozzle width n = Look::nozzle_width x value (X3M_ENGINE_PLUME_NOZZLE, default 0.5), the length L = max(z,
//   idle_length) x value x the length pulse (a per-nozzle value noise of the stage's clock, 1 +- Look::pulse, evaluated once per seed byte and
//   frame). The quad is a trapezoid, linear in x, that encloses the body (its edge, eroded outwards by up to
//   0.7 erode (0.6 + 0.8 u) of the local width, through a linear upper bound over the width profile's
//   cylinder-to-cone line), the halo's
//   window (halo_reach x the nozzle's halo sigma, constant along the plume as in the mock-up) and the nozzle ring, plus
//   one pixel;
// - the nozzle disc: camera-facing (the view plane), the end-on representation of the whole plume (after flight C): the
//   body integrated along the axis (8 samples of the law, Look tables), its radiance I x the view integration
//   kappa(detail) x L / n x |axis . to_camera|, a halo of the nozzle's sigma carrying disc_halo x L / n, the ring at
//   1 + (max(1, disc_ring min(1, (L / n) / 2)) - 1) x the detail level times its side radiance (disc_ring 3); the
//   total soft-capped at disc_cap (1.0) x the side view's peak (the body's crest one shock period in). Facing f =
//   |axis . to_camera|: the disc's weight smoothstep(0.3, 0.7, f) (none under 0.3: a side view draws no disc), the
//   axial quad's 1 - (1 - axial_floor) x that weight (half from 0.7 up: the foreshortened plume keeps its length), so the
//   total radiance stays near the side view's at every angle.
// value = record.size (|model x| of the c4-6 rows: the body's LOD-0 value x the context scale); a main jet's value is
// raised to k(R) x its ship's radius R (the root node's subtree radius in the record's units, Ring::parent_radius),
// at most floor_cap x its own value, for the plume's size and length, not its position (the plume floor). RCS jets (v/00566,
// flag_steering) take the same quads with L = z * value (short by construction: z runs 0.01..1.0 on steering) and
// their radiance x min(z, 1); below z 0.02 they are not drawn.
// Screen rules: a nozzle whose value projects under 1.5 px is not drawn; the nozzle width is at least 2 px and a main
// jet's L at least 4 px (after flight E: a far ship's plume is a faint spark, not a blot; it was 3 / 6, the 3x3-clip
// survival rule of the motes); the distance law (after flight E, Look::far_*): a plume whose projected nozzle width is
// under far_px_full (12 px) scales its radiance (core, halo, ring and disc alike) by far_low + (1 - far_low) x
// smoothstep(far_px_min, far_px_full, px), 0.15 at 2 px and below, 1 at 12 px (distance_weight; counted far_nozzles); the
// end-on disc alone also by disc_far_low + (1 - disc_far_low) x smoothstep(disc_px_min, disc_px_full, its drawn nozzle
// width) (after Run 127: 0.5 at 20 px and below, 1 from 160 px; disc_distance_weight; not the own ship's jets); the
// plume's projected body width (twice the body's
// eroded edge or the nozzle ring, whichever is wider, at the axis point nearest the camera; not the halo's reach) is
// held to 0.12 H by shrinking the axial quad about the nozzle (width and length x k), and its radiance fades 1 -> 0.5
// over the last 20 % before the clamp, the end-on disc's 1 -> 0.4 (chase_disc_floor; any plume that close, the own
// ship's in chase view only when its body is, after the review of flight C; a floor of 0.6 over the body's fade until
// flight G). Since Run 125 the end-on disc and its halo keep the natural (unshrunk, floored) nozzle width (shrunk with
// the body, a capital's disc was a spot inside its own nozzle plate), its projected radius (its quad's half-size: the
// ring, the eroded edge or the halo's reach) held to disc_cap_px x H (0.35) by scaling the disc alone; the axial quad's
// mouth hand-over measures the disc's width (the axial n over the disc's n in the head colour's alpha, 127 = 1). Since
// Run 128 the disc's nozzle width is the natural one x disc_radius (0.5; the dot floor after the scale): its whole profile
// and its quad at half the radius, the 0.35 H cap and the hand-over on the scaled width (c19.x = 1 / disc_radius
// restores the alpha's ratio), its distance law on the unscaled width.
// Flow: the noise field translates along the axis by a phase in nozzle widths, per vertex (shape.w), so it moves at one
// speed whatever the pulsed, throttle-dependent L. Each nozzle accumulates its own phase in the per-nozzle memory
// (Transients, keyed by the record's identity): every frame it advances by the frame accumulator's step (FlowPhase at
// flow_rate, the mock-up's speed at s = 1 without the pulse, x the travel look's flow) x the nozzle's flow_factor
// (flow_reference / value in [flow_slow, 1]: the same world speed from value 500 up, capitals crawl at 0.3; gap 4 of
// docs/architecture/engine-exhaust-gap-analysis.md), so a change of the factor (the floor's radius appearing, a far
// record turning into a scene one) changes the speed, never the position; a new key (and a record without a slot)
// takes the shared phase, the accumulator x factor.
// After the gap analysis (phase 2 and 3): a main jet's length is at least idle_length value (gap 10); the body's colour
// runs from the head colour (the peak colour at the mean's luminance) to the mean (gap 5); the halo spills through the
// lane around the nozzle (gap 3, the pixel program); a steering or brake body's rising z adds a decaying attack to its
// radiance (gap 6, Transients); under SETA the travel weight lengthens and brightens the main jets (gap 7, TravelRamp).
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
// halo=1.1 hb=0.35"; after flight C the ring 0.3 and the nozzle 0.5; after flight E hb 0.20), in one block: the CPU builder reads it and the
// pass uploads it to the pixel program (c3..c19, pixel_constants). Lengths across are in nozzle widths, lengths along
// in L. Since the revised look law (docs/architecture/engine-exhaust-look-critique.md section 3 and "Implemented") the
// knobs keep their numbers and directions, five with a new mapping: turb weights the streaks by the radius (the core
// steady), erode drives the streaks' erosion growing along the plume and the tail's tongues, shock sets the cells'
// gap depth min(1, 1.7 shock) (crests at the body, dark gaps), core the hot core's Gaussian radius 0.45 core of the
// local width, halo the halo's e-fold 0.32 halo nozzle widths.
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
    float turb = .6f;     // turbulence: the streaks' radiance modulation, 2.2 turb S2 (0.4 + 0.6 radial)
    float flow = 3.f;     // flow speed: the mock-up's scroll, 0.35 flow L / 1.6 nozzle widths per second at s = 1 (flow_rate)
    float erode = .57f;   // edge erosion: 1.6 erode S2 (0.6 + 0.8 u); the tail's tongues erode x 0.35 / 0.57 (pixel_constants)
    float pulse = .25f;   // length pulse: L x (1 +- pulse), mean 1
    float shock = .5f;    // shock cells: the gaps carved to 1 - min(1, 1.7 shock); ramped in over the first half period
    float period = .16f;  // their period, x L
    float cfade = .6f;    // their fade along the plume
    float heat = .7f;     // the white-hot core (0.1 from flight F until Run 126 A, reverted: it dimmed the red heads)
    float core = .45f;    // the hot core's radius: exp(-(radial / (0.45 core))^2), sigma 0.2 of the local width
    float halo = 1.1f;    // halo width: e-fold halo_sigma (0.32) x halo nozzle widths (x the preset)
    float hb = .20f;      // halo brightness (after flight E: 0.35 -> 0.20, the plume's and the disc's halo)
    // The outer flame (the single law, docs/architecture/engine-exhaust-look-critique.md section 6, "One law"): a sheath
    // of the darker, more saturated tint (0.5 tint^2 / its largest channel x the tint's), 4 outer m (1 - m) with
    // m = smoothstep(0.1, 1.2, radial), inside its own eroded edge 1 - smoothstep(0.65, 1.2, radial), growing along the
    // plume 0.6 + 0.4 smoothstep(0.1, 0.5, u), x the detail level: the fat saturated flame around the thin hot core
    // (plume_one_law_model.py: the 10 % half-width at u 0.2 0.90..1.0 of the slab law's at the 40 px bands). In the
    // end-on disc's samples it is the annulus around the hot centre.
    float outer = 1.25f;
    // The core's radius at the detail level 0, x the revised law's (the peaked profile's 0.32 and the hot core's 0.45
    // core): k(d) = core_widen + (1 - core_widen) d. 3.74: the d-0 law's side energy equal to the previous slab law's at
    // s 1, L / n 4 (0.99..1.00 at s 0 / 0.5 / 1, the 10 % half-width 0.98 of it; plume_one_law_model.py), so the far
    // dots keep the slab law's energy by the widening, not by a gain.
    float core_widen = 3.74f;
    // The mock-up's tail narrowing, w x (1 - 0.6 taper smoothstep(0.6, 1, u)); the halo keeps the nozzle's sigma along
    // the whole plume, as in the mock-up.
    float tail_narrowing = .6f;
    // I(s) = lerp(core_low, core_high, s). The flight-F mouth-whiteness change (heat 0.1, head_min 0.42, I(s) x 1.04:
    // 1.248 / 4.16) was reverted after Run 126 A (2026-10-04, user decision): it dimmed the red plume heads (red total
    // 0.78 of before) for a side-view gain nobody noticed; the Run123 values are back
    // (docs/architecture/engine-exhaust-look-critique.md section 6, "Mouth whiteness (after flight F)").
    float core_low = 1.2f, core_high = 4.f;
    // After flight D: the mouth terms (the ring, the halo, the disc's ring and halo) follow the body's throttle curve,
    // x I(s) / core_high (the halo hb x I / core_high, the ring ring x I / core_high), and the body ramps in over the
    // first mouth_ramp of its length from 1 - mouth_dip at the nozzle (x (1 - dip (1 - smoothstep(0, ramp, u))), in
    // law::tail): the mouth's total stays at most 0.85 of the body's peak at every throttle (run_engine_plumes.py, mouth case).
    float mouth_dip = .5f, mouth_ramp = .3f;
    float ring_falloff = 14.f;              // the ring's axial falloff, per nozzle width
    float pulse_rate = 3.f;                 // the length pulse's noise, per second
    // After flight C (docs/architecture/engine-effects-modern.md, "After flight C").
    // The end-on disc: the body integrated along the axis has radiance I x disc_kappa x L / n x the view factor (the
    // ratio of the side view's body energy to the integrated profile's, per nozzle width of length: 4.13 with the outer
    // sheath (the disc's samples keep the peaked profile without it), 3.33 from the revised law's peaked profile, 1.8
    // from the slab law before it; verification/results/engine-effects/
    // plume_end_on_model.py); its halo I_halo x disc_halo x L / n x the view factor (1.0 since Run 128; 2.82 before: the
    // slab law's ratio of the disc's halo energy to the side view's, 6.3, kept for the tight halo); the total soft-capped at disc_cap x the side
    // view's peak; the ring seen end-on at disc_ring x its side radiance (3: the ring shows between the integrated body
    // and the halo; at the doubled width x 1.8 left no bump on the cyan disc, FP16 gate 1.00, and x 2.7 none on the
    // resolved 02b image). Since the single law (docs/architecture/engine-exhaust-look-critique.md section 6, "One law")
    // the disc's samples carry the outer sheath as the end-on annulus around the hot centre, and kappa runs from
    // disc_kappa_smooth at the detail level 0 (the widened smooth profile: 1.84, the slab law's 1.8 recovered) to
    // disc_kappa at 1 (energy-matched 1.47 with the sheath in the samples; 3.0: past it the soft cap saturates the
    // centre, 3.6 added 0.02 of the end-on energy), the ring's x disc_ring x min(1, (L / n) / 2) (short plumes: the
    // ring no longer dominates; at least x 1) and its width disc_ring_width blend in with the detail level (a detail-0
    // disc: the ring x 1 at the side's width). The facing band: the
    // disc from disc_low to full at disc_high; the axial quad's weight there drops to axial_floor.
    float disc_kappa = 3.f;
    float disc_kappa_smooth = 1.84f;
    // The end-on annulus's own weight: the outer sheath in the disc's samples x disc_sheath, so the annulus, not the
    // centre (under the soft cap and the hue bound), carries the end-on energy: the end-on total 0.74 of the detail-0
    // law's at the same size (gate 0.7..0.9; 2.0 gave 0.75 and broke the 20 px disc's rim variation).
    float disc_sheath = 1.6f;
    // The end-on halo's gain at the detail level 1: 1.0 since Run 128 (2.82 until then, the slab law's halo ratio; the
    // halo scaled by L / n end-on wrapped each of a capital's nozzles in a soft blob, and the Split Ocelot's ten stern
    // discs merged into one glowing cluster on screenshots/engines6.png; the user asked for the excess glow to go, not
    // for a colour change). The detail-0 gain stays 3 (the smooth law's); the halo's e-fold is the side's sigma0.
    // docs/architecture/engine-exhaust-look-critique.md section 6, "Excess end-on glow (after Run 128)".
    float disc_halo = 1.f;
    // The end-on disc's radius (after Run 128): its radial coordinate runs in disc_radius x n instead of n, so the whole
    // disc profile (integrated body, hot centre, the ring at its 0.5-0.55 radial step, the sheath's annulus, the halo's
    // e-fold) and its quad shrink by it; the radiance per pixel is unchanged (the energy goes with the area, ~x 0.25).
    // Why: at 1.0 a huge nozzle's disc stayed bright (engine R >= 2.4) out to 0.45 n and reached 5 % of its centre at
    // 0.70 n on the Run 126 / 128 captures (verification/results/run416-engine-glow/), twice the hull's own nozzle ring
    // (0.30 n on a huge nozzle, 0.45 n on a big3), and neighbours 0.59-0.70 n apart merged into one glowing mass before
    // bloom (screenshots/engines6.png). At 0.5 the disc's edge meets the ring on a huge nozzle and sits inside it on a
    // big3. The dot floor (min_nozzle_px) applies after the scale; the disc's distance law keeps the unscaled width;
    // the own ship's disc shrinks too (accepted). docs/architecture/engine-exhaust-look-critique.md section 6, "Disc
    // radius (after Run 128)".
    float disc_radius = .5f;
    // After Run 125 (the Split Ocelot's stern nozzles at 2.4 km as white rings with pink centres): the soft cap 1.0 x
    // the side view's peak (1.5 until then: a red disc's centre reached ~2.2 at s 1, past the flight's display white
    // ~2.1 at EV +1, so every end-on red disc clipped white at its centre) and the end-on ring x 3 its side radiance
    // (x 8 until then, at the side's width: a bright white rim); docs/architecture/engine-exhaust-look-critique.md
    // section 6, "End-on brightness".
    float disc_cap = 1.f;
    // The end-on ring at the side's width since the single law (the sheath's annulus fills what read as a dark annulus
    // under the tuning pass's narrow x 3 ring; the doubled width left no bump on the cyan disc over the annulus): x 3
    // its side radiance since Run 125 (x 8 before: ring gate cyan 1.21 / red 1.76 on the FP16 frame).
    float disc_ring = 3.f;
    float disc_ring_width = 1.f;
    // The L / n of the disc's body and halo gains is held to disc_length_max: a thin nozzle (X3M_ENGINE_PLUME_NOZZLE
    // 0.1: L / n 20 at full throttle) would otherwise saturate the soft cap into a flat disc. 8 = twice the default
    // 0.5's 4 at full throttle; the nozzle 0.25 reaches it at full throttle.
    float disc_length_max = 8.f;
    float disc_low = .3f, disc_high = .7f;
    float axial_floor = .5f;
    // The mouth's hand-over: the axial quad x (1 - disc weight x (1 - smoothstep(inner, outer, d))), d the screen-plane
    // distance from the nozzle in nozzle widths (the disc's integrated body ends near 0.6).
    float handover_inner = .3f, handover_outer = .8f;
    // The plume floor (after flight D): a main jet's value is at least k(R) x its ship's radius R (the root node's
    // subtree radius in record units, Ring::parent_radius), at most floor_cap x its own value. k(R) runs through three
    // anchors, linear in ln R between them and flat outside (floor_ratio_at): 0.35 at R <= 150 (fighters), 0.25 at 500,
    // 0.10 at R >= 5,000 (capitals), so small ships' plumes grow and capitals' stay (the Mayhem fleet's largest main
    // nozzle / R is about 0.09 at every size, so one k cannot do both; verification/results/engine-effects/
    // floor_ratio_effects.py). floor_scale multiplies the whole curve: load-time knob X3M_ENGINE_PLUME_FLOOR (ini
    // engine_plume_floor, 0..3; parse_floor); 0 turns the floor off. Default 0.5 after flight E (Run 122 A, run408).
    float floor_scale = .5f;
    float floor_r[3] = {150.f, 500.f, 5000.f}; // record units (run406: value x 0.01)
    float floor_k[3] = {.35f, .25f, .10f};
    float floor_cap = 4.f;
    // The distance law (after flight E: far jets reach the stage as records once the small-parts cull hands them over):
    // the radiance of a plume whose projected nozzle width is under far_px_full scales by far_low + (1 - far_low) x
    // smoothstep(far_px_min, far_px_full, px) (distance_weight), so far ships read as faint sparks.
    float far_px_min = 2.f, far_px_full = 12.f, far_low = .15f;
    // The disc's distance law (after Run 127: a Split Ocelot seen from straight behind at 3-4 km read as ten white-centred
    // lamps): the end-on disc's whole radiance (its integrated body, halo, ring and the soft cap of its hot centre; not
    // the axial quad, the side view or the ribbons) scales by disc_far_low + (1 - disc_far_low) x smoothstep(disc_px_min,
    // disc_px_full, px), px the disc's drawn (natural, floored) nozzle width (disc_distance_weight): 0.5 at 20 px and
    // below, ~0.6 at 65 px (the Ocelot's huge nozzle at 4 km on a 1440-row screen), 1 from 160 px; under 12 px the far
    // law multiplies on top, the chase fade's chase_disc_floor too. Why: a glowing surface keeps its radiance with
    // distance (only its solid angle shrinks), so a distant disc stays as bright per pixel as a near one and reads as a
    // lamp; modern games roll distant emitters off through exposure and bloom, which this stage does not see, and the
    // dimming stands in for that roll-off. The own ship's jets (Ring::own, the tag the engine light's table never evicts)
    // are exempt (factor 1): its nozzle is small on screen because the ship is small, not far (34 / 45 px at the chase
    // boom), and its end-on look was accepted on Run 126 A; the far law and the chase fade still apply. Fixed constants
    // (no ini key).
    float disc_far_low = .5f, disc_px_min = 20.f, disc_px_full = 160.f;
    // The detail level d of the single law (docs/architecture/engine-exhaust-look-critique.md section 6, "One law"): by
    // the drawn nozzle width in px (after the near-camera cap, before the dot floor), smoothstep(detail_px_min,
    // detail_px_max, px), carried in the tint's alpha. One law at every d: the core radius x core_widen at 0 to the
    // revised law's at 1, the structure (cells, streak turbulence, erosion, tongues, the rim and tail darkening, the
    // outer sheath) and the head colour x d, so at 0 a smooth soft profile of the slab law's width and energy (a thin
    // core and pixel-scale structure on a plume a few pixels wide lose their peak in the resolve when it moves). The
    // halo follows: at 0 the previous law's e-fold 0.5 halo, exp(-2.2 u), the disc's gain 3.
    float detail_px_min = 8.f, detail_px_max = 40.f;
    // Phase 2 of the gap analysis (docs/architecture/engine-exhaust-gap-analysis.md, gaps 4, 5, 3, 10).
    // Gap 4, the flow in world units: a nozzle of value flow_reference keeps flow_rate (2.625 nozzle widths per second at
    // the default nozzle), a larger one scrolls flow_reference / value of it, at least flow_slow, a smaller one never
    // faster (flow_factor): the same world speed from value 500 to 1,667, capitals crawl at 0.3, never freeze.
    float flow_reference = 500.f; // record units
    float flow_slow = .3f;
    // Gap 10, the idle floor: a main jet's length is max(z, idle_length) x value (z 0.25 at idle gave 0.25 value).
    float idle_length = .5f;
    // Gap 3, the nozzle spill: the halo's lane visibility is at least glow_through within spill_inner nozzle widths of
    // the nozzle (the screen-plane distance), tapering to 0 at spill_reach, for an occluder at most spill_depth x value
    // in front of the nozzle's depth (its own hull, not a ship passing in front); the body and the ring unchanged.
    // The depth guard is also bounded in world (record) units, at most spill_depth_max: a capital's 2 value reached
    // about 2,000 units, so a ship passing that far in front still let the rim through.
    float glow_through = .15f, spill_inner = .8f, spill_reach = 1.f, spill_depth = 2.f, spill_depth_max = 300.f;
};
constexpr Look default_look{};
constexpr float soft_core = .15f, soft_halo = 1.f; // SOFT x value (the pixel program's lane terms: body and ring, halo)
constexpr float halo_reach = 2.25f;         // the halo window's zero, x the halo sigma (the quads reach it); the window
                                            // tapers over its last 0.5 sigma (the halo is the mock-up's inside it)
constexpr float halo_sigma = .32f;          // the halo's e-fold at the nozzle, x Look::halo nozzle widths (x the preset;
                                            // the revised law: 0.35 nozzle widths at halo 1.1, 0.5 x halo before)
constexpr float outer_reach = 1.2f;         // the outer sheath's edge ends at this radial (engine_plume_ps.hlsl)
constexpr float shock_gap = 1.7f;           // the cells' gap depth min(1, shock_gap x Look::shock): 0.85 at shock 0.5;
                                            // with the half-period ramp-in the first gap is 0.33 of the crest, 0.74 by
                                            // u 0.4 (the critique's 0.8 over a whole period left the body lane's gap at
                                            // 0.68 of its mean in u 0.1..0.3, the gate 0.6; at 1.0 the carved white core
                                            // dominates a red plume's high-passed luma, anisotropy 1.8..2.3 against 3)
constexpr float head_min = .75f;           // the head colour's luminance scale is at least this (head_colour; 0.42
                                            // from flight F until its revert after Run 126 A)
constexpr float occlusion_bias = .5f;       // x value x max(0, axis . to_camera): the exhaust facing the camera clears its hull
constexpr float chase_cap = .12f;           // x H: the largest projected plume
constexpr float chase_fade_band = .2f;      // the last 20 % before the cap
constexpr float chase_fade_floor = .5f;     // the radiance at and past the cap
constexpr float chase_disc_floor = .4f;     // the end-on disc's radiance at and past the cap: this x its unfaded
constexpr float disc_cap_px = .35f;         // x H: the largest projected disc radius (its quad's half-size; since Run 125)
constexpr float min_nozzle_px = 2.f, min_length_px = 4.f, cull_px = 1.5f; // the dot floor (after flight E: 3 / 6)
constexpr float steering_min_z = .02f;
constexpr float clock_wrap = 1024.f;        // seconds: the pixel program's clock wraps (float precision of the noise)
constexpr double phase_wrap = 4096.;        // nozzle widths: a nozzle's flow phase wraps (one discontinuity of its noise per
                                            // wrap, at least every 26 minutes)
constexpr double flow_wrap = 1099511627776.; // 2^40 nozzle widths: the frame's flow accumulator (never in practice)
// Gap 5, two-tone colour: the head colour is the body's peak colour scaled to the mean's luminance (Rec. 709 weights)
// when it is brighter, so the plume gets whiter at the head, not brighter (the table's peak is the whiter colour at
// 1.0-3.6x the mean's luminance by cluster: verification/results/engine-effects/plume_two_tone_colours.py).
constexpr float luma_r = .2126f, luma_g = .7152f, luma_b = .0722f;
// Gap 6, the RCS puff attack and retro flare: a steering or brake body whose z rises gains radiance x (1 + attack_gain x
// saturate(dz / (attack_rate x dt))), dt in game ms since the nozzle was last seen (wall x the SETA rate), decaying
// linearly to 1 over attack_decay seconds; no shape change; main jets unaffected (their z is remembered, so a main jet
// pushed into brake flares). The memory: transient_slots per-nozzle slots (last z, the attack, the flow phase) keyed by
// the record's identity (the ribbon pool's map shape), transient_probe slots probed, a slot unseen for transient_hold
// free.
constexpr float attack_gain = .5f, attack_rate = .004f, attack_decay = .12f; // -, z per game ms, seconds
constexpr unsigned transient_slots = 512, transient_probe = 8;
constexpr float transient_hold = .5f;       // seconds
// Gap 7, the travel look under SETA (docs/reverse-engineering/engine-effects.md section 8): with the requested warp above
// 1.0 the travel weight ramps 0 -> 1 over travel_rise seconds (smoothstep), and back over the same after the warp has
// been 1.0 for travel_hold seconds; at weight 1 a main jet's L x travel_length, its radiance x travel_radiance, the
// ribbons' T x travel_trail, the flow x travel_flow. While SETA is on the ramp's step is held to travel_max_step (a
// stall completes no rise); with SETA off it takes the true elapsed time (a gap without plume frames releases at once).
constexpr float travel_length = 2.f, travel_radiance = 1.25f, travel_trail = 2.f, travel_flow = 1.5f;
constexpr float travel_rise = .5f, travel_hold = .3f, travel_max_step = .1f; // seconds
constexpr float nozzle_min = .1f, nozzle_max = 1.f; // X3M_ENGINE_PLUME_NOZZLE's accepted range (x value)
constexpr float floor_min = 0.f, floor_max = 3.f;   // X3M_ENGINE_PLUME_FLOOR's accepted range (x the k(R) curve)
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
// ln x for x > 0 without x87: x = m 2^e with m in [1, 2), ln m = 2 atanh((m - 1) / (m + 1)) to the 9th power
// (|z| <= 1/3: error under 2e-6); 0 for x <= 0 or non-finite.
inline void ln(float x, float* out) noexcept {
    *out = 0.f;
    if (!(x > 0.f) || !(x <= 3.4e38f)) return;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, 4);
    const int e = int((bits >> 23) & 255u) - 127;
    if (e == -127) return; // denormal: not a radius
    bits = (bits & 0x007fffffu) | 0x3f800000u;
    float m = 1.f;
    std::memcpy(&m, &bits, 4);
    const float z = (m - 1.f) / (m + 1.f), z2 = z * z;
    *out = float(e) * .69314718f + 2.f * z * (1.f + z2 * (1.f / 3.f + z2 * (1.f / 5.f + z2 * (1.f / 7.f + z2 * (1.f / 9.f)))));
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
// The law's functions of u alone (engine_plume_ps.hlsl): the width w(u) (nozzle widths), the tail (without the
// tongues' noise), the shock cells' carving depth a (1 - c) without the throttle (ramped in over the first half period:
// the crest at u 0 is the body's own level, the mouth ramp keeps the mouth under the body; the body x (1 - s x it)
// inside radial 0.3) and the white-hot core's heat.
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
    float fade = 0.f, e = 0.f, ramp = 0.f;
    smooth(.75f + (.25f - .75f) * k.tail, 1.f, u, &fade);
    exp_neg(u * 1.2f * k.tail, &e);
    smooth(0.f, k.mouth_ramp > 1e-3f ? k.mouth_ramp : 1e-3f, u, &ramp);
    *out = (1.f - fade) * e * (1.f - k.mouth_dip * (1.f - ramp));
}
inline void cell(const Look& k, float u, float* out) noexcept {
    float c = 0.f, e = 0.f, ramp = 0.f;
    cos(6.2831853f * u / k.period, &c);
    exp_neg(u * 5.f * k.cfade, &e);
    smooth(0.f, .5f * k.period, u, &ramp);
    const float crest = .5f + .5f * c, gap = shock_gap * k.shock;
    *out = (gap < 1.f ? gap : 1.f) * e * ramp * (1.f - crest * crest * crest);
}
inline void heat(const Look& k, float u, float* out) noexcept {
    float h = 0.f;
    smooth(.05f, .3f, u, &h);
    *out = k.heat * (1.f - h);
}
// The hot core's radiance boost on the axis, 1 + 0.6 (1 - 0.5 smoothstep(0.2, 0.8, u)): the core cools along the plume.
inline void core_boost(float u, float* out) noexcept {
    float c = 0.f;
    smooth(.2f, .8f, u, &c);
    *out = 1.f + .6f * (1.f - .5f * c);
}
} // namespace law
// The end-on disc's integration along the axis: the law at u_k = (k + 0.5) / 8 (the pixel program's c8..c15: 1 / w,
// tail, carve, heat), and the side view's peak per I_core on the axis: max over u of the hot core's boost (1.6 cooling
// to 1.3) x tail x (1 - s x carve), sampled at 513 points of u in [0, 1] for s = 0, 1/8, .., 1 (since the mouth ramp the peak is no
// longer the mouth's tail 1 or the first crest), linear in s between them (peak_axis_at; the maximum of lines in s is
// convex, so the chord is at most slightly above it).
constexpr unsigned disc_samples = 8, peak_steps = 8, peak_u_samples = 512;
struct LookTables {
    float disc[disc_samples][4]{};
    // The side peak per I_core on the axis by the throttle (j / peak_steps) and the detail level (i / peak_steps; the
    // cells carve x the level): max over u of boost x tail x (1 - s d carve), bilinear in peak_axis_at.
    float axis_peak[peak_steps + 1][peak_steps + 1]{};
};
inline void look_tables(const Look& k, LookTables* out) noexcept {
    for (unsigned i = 0; i < disc_samples; ++i) {
        const float u = (float(i) + .5f) / float(disc_samples);
        float w = 0.f;
        law::width(k, u, &w);
        out->disc[i][0] = 1.f / w; // w >= 0.05 x the narrowed line: finite
        law::tail(k, u, &out->disc[i][1]);
        law::cell(k, u, &out->disc[i][2]);
        law::heat(k, u, &out->disc[i][3]);
    }
    for (unsigned j = 0; j <= peak_steps; ++j)
        for (unsigned d = 0; d <= peak_steps; ++d) out->axis_peak[j][d] = 0.f;
    for (unsigned i = 0; i <= peak_u_samples; ++i) {
        const float u = float(i) / float(peak_u_samples);
        float tl = 0.f, cl = 0.f, boost = 0.f;
        law::tail(k, u, &tl);
        law::cell(k, u, &cl);
        law::core_boost(u, &boost);
        for (unsigned j = 0; j <= peak_steps; ++j)
            for (unsigned d = 0; d <= peak_steps; ++d) {
                const float sd = float(j) / float(peak_steps) * float(d) / float(peak_steps);
                const float v = boost * tl * (1.f - sd * cl);
                if (v > out->axis_peak[j][d]) out->axis_peak[j][d] = v;
            }
    }
}
// The side view's axis peak per I_core at throttle s and the detail level, bilinear in the table.
inline void peak_axis_at(const LookTables& t, float s, float* out, float detail = 1.f) noexcept {
    const float x = (s < 0.f ? 0.f : s > 1.f ? 1.f : s) * float(peak_steps);
    const float y = (detail < 0.f ? 0.f : detail > 1.f ? 1.f : detail) * float(peak_steps);
    unsigned j = unsigned(x), d = unsigned(y);
    if (j >= peak_steps) j = peak_steps - 1u;
    if (d >= peak_steps) d = peak_steps - 1u;
    const float f = x - float(j), g = y - float(d);
    const float a = t.axis_peak[j][d] + (t.axis_peak[j + 1][d] - t.axis_peak[j][d]) * f;
    const float b = t.axis_peak[j][d + 1] + (t.axis_peak[j + 1][d + 1] - t.axis_peak[j][d + 1]) * f;
    *out = a + (b - a) * g;
}
// The pixel program's look constants c3..c19 (engine_plume_ps.hlsl), 68 floats (the flow phase is the nozzle's, in the
// vertex; the halo's sigma the nozzle's): c3..c7 the law, c8..c15 the disc's samples, c16 the mouth ramp (dip, end) and
// the spill's glow_through and 1 / (spill_depth), c17 the spill's inner and outer reach (nozzle widths), 1 /
// spill_depth_max (world units), the tail's tongues (erode x 0.35 / 0.57: 0.35 at the chosen erode); c18 the core's
// widening at the detail level 0 (core_widen), the outer sheath's weight 4 outer, the end-on ring's radial scale
// 1 / disc_ring_width (at the detail level 1), the end-on annulus's weight disc_sheath; c19 1 / disc_radius (the axial
// quad's mouth hand-over: the head colour's alpha carries its n over the disc's x disc_radius), three zeros.
constexpr unsigned pixel_constant_floats = 68;
// `t` look_tables(k), computed once where the look is fixed (the proxy at load: MotionOutput::plumes_tables_).
inline void pixel_constants(const Look& k, const LookTables& t, float out[pixel_constant_floats]) noexcept {
    const float c[20] = {.5f * k.bulge, (.04f - .5f * k.bulge) * k.taper, .5f * k.bulge * k.taper, k.tail_narrowing * k.taper,
                         2.f / k.period, k.erode * 1.6f, k.turb * 2.2f, .75f + (.25f - .75f) * k.tail,
                         shock_gap * k.shock < 1.f ? shock_gap * k.shock : 1.f, 6.2831853f / k.period, 5.f * k.cfade, 1.2f * k.tail,
                         k.heat, 1.f / (.45f * k.core), .46f * k.bulge, 1.f / 240.f,
                         k.handover_inner, k.handover_outer, k.ring_falloff, 2.f};
    for (unsigned i = 0; i < 20; ++i) out[i] = c[i];
    for (unsigned i = 0; i < disc_samples; ++i)
        for (unsigned j = 0; j < 4; ++j) out[20 + i * 4 + j] = t.disc[i][j];
    out[52] = k.mouth_dip;
    out[53] = k.mouth_ramp > 1e-3f ? k.mouth_ramp : 1e-3f;
    out[54] = k.glow_through > 0.f ? (k.glow_through < 1.f ? k.glow_through : 1.f) : 0.f;
    out[55] = k.spill_depth > 1e-3f ? 1.f / k.spill_depth : 1e3f;
    out[56] = k.spill_inner;
    out[57] = k.spill_reach > k.spill_inner + 1e-3f ? k.spill_reach : k.spill_inner + 1e-3f;
    out[58] = k.spill_depth_max > 1e-3f ? 1.f / k.spill_depth_max : 1e3f;
    out[59] = k.erode * (.35f / .57f);
    out[60] = k.core_widen > 1e-3f ? k.core_widen : 1e-3f;
    out[61] = 4.f * k.outer;
    out[62] = k.disc_ring_width > 1e-3f ? 1.f / k.disc_ring_width : 1e3f;
    out[63] = k.disc_sheath > 0.f ? k.disc_sheath : 0.f;
    out[64] = k.disc_radius > 0.f ? 1.f / k.disc_radius : 1.f; // as build_nozzle: NaN or <= 0 the unscaled disc
    out[65] = out[66] = out[67] = 0.f;
}
inline void pixel_constants(const Look& k, float out[pixel_constant_floats]) noexcept {
    LookTables t;
    look_tables(k, &t);
    pixel_constants(k, t, out);
}
// The flow's speed in nozzle widths per second: the mock-up's scroll 0.35 flow L / 1.6 (its noise runs at 1.6 per nozzle
// width along the axis) at the design length of s = 1 without the pulse, L = 2 value = 2 / nozzle_width nozzle widths
// (4 at the default 0.5: 2.625 nozzle widths per second at flow 3); the same speed in value units for any nozzle width.
inline void flow_rate(const Look& k, float* out) noexcept {
    const float L1 = k.nozzle_width > 0.f ? 2.f / k.nozzle_width : 0.f;
    *out = .35f * k.flow * L1 / 1.6f;
}
// The frame's flow accumulator (flow_rate's nozzle widths, unwrapped in double): advanced once per frame by the stage
// clock's step x flow_rate (x the travel look's flow factor); a nozzle's phase advances by its step x the nozzle's
// flow_factor (Transients::advance_phase), a new one starts at it x flow_factor (nozzle_phase).
struct FlowPhase {
    double nozzle_widths = 0.;
    void advance(double dt, float rate) noexcept {
        if (!(dt > 0.) || !(dt < 1e9) || !(rate > 0.f) || !(rate < 1e6f)) return;
        nozzle_widths += dt * double(rate);
        if (nozzle_widths >= flow_wrap) nozzle_widths -= flow_wrap * x3m::scalar::floor(nozzle_widths / flow_wrap);
    }
};
// Gap 4: a nozzle's share of the frame's flow, flow_reference / value clamped to [flow_slow, 1] (value: the plume's own,
// floored, before the near-camera cap; 1 for a non-positive or non-finite one).
inline void flow_factor(const Look& k, float value, float* out) noexcept {
    float f = value > 0.f && value < 3.4e38f ? k.flow_reference / value : 1.f;
    const float slow = k.flow_slow > 0.f && k.flow_slow < 1.f ? k.flow_slow : 1.f;
    *out = !(f < 1.f) ? 1.f : f < slow ? slow : f;
}
// The shared phase of a nozzle in its nozzle widths, wrapped at phase_wrap: flow x factor in double, so the pixel
// program's float keeps 4096 / 2^23 nozzle widths of precision whatever the session's length. A nozzle's first frame
// in the memory (and a record without a slot) takes it.
inline void nozzle_phase(double flow, float factor, float* out) noexcept {
    double p = flow * double(factor);
    if (!(p >= 0.) || !(p < 4e12)) p = 0.; // scalar::floor needs p / phase_wrap < 2^31
    p -= phase_wrap * x3m::scalar::floor(p / phase_wrap);
    *out = float(p);
}
// X3M_ENGINE_PLUME_NOZZLE / X3M_ENGINE_PLUME_FLOOR: the whole text one plain decimal number (digits with at most one
// point; no sign, exponent or padding) in [low, high]; anything else is refused (the caller keeps the default_look
// value). `n` characters of narrow or wide text.
template <class Char> inline bool parse_plain_decimal(const Char* text, std::size_t n, float low, float high, float* out) noexcept {
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
    if (!(v >= low) || !(v <= high)) return false;
    *out = v;
    return true;
}
template <class Char> inline bool parse_nozzle(const Char* text, std::size_t n, float* out) noexcept {
    return parse_plain_decimal(text, n, nozzle_min, nozzle_max, out);
}
template <class Char> inline bool parse_floor(const Char* text, std::size_t n, float* out) noexcept {
    return parse_plain_decimal(text, n, floor_min, floor_max, out);
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

// One vertex of the stage's VB: 76 bytes (FLOAT3, 3 x FLOAT4, 4 x D3DCOLOR).
struct Vertex {
    float position[3];  // view space (x right, y up, z forward), before the jittered projection
    float local[4];     // x, y (world units), L (pulsed, world; the disc: the ring's radiance), the nozzle width n (world)
    float shape[4];     // halo sigma at the nozzle (nozzle widths, x the preset), value (the SOFT base), occlusion bias
                        // (view units), the nozzle's flow phase (nozzle widths, nozzle_phase; gap 4)
    float intensity[4]; // axial: I_core, I_halo (x weights), the axis's view z component; disc: the integrated body's
                        // radiance per unit of the mean profile, the halo's, the soft cap (x the disc's weight); the
                        // nozzle's view z
    std::uint32_t tint;   // 0xAARRGGBB of the mean colour (the tail's, the halo's and the ring's), largest channel 255; A the
                          // revised law's detail level (Look::detail_px_min..max)
    std::uint32_t params; // 0xAARRGGBB: R the throttle s, G the noise seed, B I_ring / I_core / 2, A the disc's weight
    std::uint32_t fog;    // 0xAARRGGBB of the fog transmittance per channel (white without fog): the white-hot core's
                          // and the ring's colours take it (the tint carries it already); A sin(view) = sqrt(1 - f^2)
    std::uint32_t peak;   // 0xAARRGGBB: RGB the head colour (gap 5: the body's peak at the mean's luminance, head_colour,
                          // with the fog); A the kind (0 axial, 255 disc)
};
static_assert(sizeof(Vertex) == 76, "the stage's vertex stride");

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
    unsigned discs_capped = 0;  // discs scaled to disc_cap_px x H (since Run 125)
    unsigned floored = 0;       // main jets raised to k(R) x their ship's radius
    unsigned floor_unknown = 0; // main jets without a ship radius (no floor) while the floor is on
    unsigned merged = 0;        // records dropped as a smaller co-located layer of another (merge_layers)
    unsigned far_nozzles = 0;   // nozzles drawn under Look::far_px_full (the distance law scaled their radiance)
    unsigned attacks = 0;       // steering or brake records under an attack (gap 6), drawn or not
    unsigned transient_overflow = 0; // records without a slot in the per-nozzle memory (no attack; the shared phase)
    unsigned flow_factor_changes = 0; // nozzles whose phase advanced under another flow factor than their last frame's
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
// Gap 5: the plume's head colour, the peak colour scaled down to the mean's luminance when it is brighter (never up),
// the scale at least head_min (the revised law: a peak 2.6x the mean's luminance, the red cluster's, stayed a grey-pink
// at 0.39; 0.75 of the peak, as before flight F: the flight-F 0.42 was reverted after Run 126 A).
inline void head_colour(const float mean[3], const float peak[3], float out[3]) noexcept {
    const float lm = luma_r * mean[0] + luma_g * mean[1] + luma_b * mean[2];
    const float lp = luma_r * peak[0] + luma_g * peak[1] + luma_b * peak[2];
    float k = lp > lm && lp > 0.f ? lm / lp : 1.f;
    k = k > head_min ? k : head_min;
    for (unsigned i = 0; i < 3; ++i) out[i] = peak[i] * k;
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

// --------------------------------------------------------------------------- transients (gap 6)
// A record's identity across frames: bit 63 set for the object_lifetime node serial, clear for node handle + model
// (never 0: 0 marks a free slot). The ribbon pool keys its map the same way (engine_ribbons_core.h record_key).
inline std::uint64_t identity_key(const ee::Record& r) noexcept {
    if ((r.flags & ee::flag_serial) && r.serial) return r.serial | (std::uint64_t(1) << 63);
    const std::uint64_t k = (std::uint64_t(r.model & 0x7fffffffu) << 32) | r.node_handle;
    return k ? k : 1u;
}
// The per-nozzle memory (gaps 4 and 6), keyed by the record's identity: the last z of every drawn nozzle and of every
// steering or brake body (a main jet's too, so a main jet pushed into brake rises from its own z), the attack's
// envelope, and the nozzle's own flow phase. transient_slots slots probed transient_probe from the key's home (no
// deletion: a slot unseen for transient_hold is free and the first free one in the window takes a new key; a full
// window counts overflow, and the record draws without attack on the shared phase). begin() once per frame with the
// stage clock's step; touch() finds or takes a record's slot; update_z() the z and the attack, advance_phase() the
// phase. A steering or brake body is touched before the idle cull (a puff rising from z 0.01 is seen), a main jet only
// when drawn. Render thread only. 48 bytes a slot, 24 KB.
struct Transients {
    struct Slot {
        std::uint64_t key = 0;
        double flow = 0.;        // the frame accumulator (Dynamics::flow) at the phase's last advance
        double phase = 0.;       // the nozzle's own flow phase, nozzle widths in [0, phase_wrap)
        float z = 0.f;
        float boost = 0.f;       // the envelope's height at the attack (0..attack_gain)
        float attack_age = 1e9f; // seconds since the attack
        float idle = 1e9f;       // seconds since the slot's z was last updated
        float factor = 0.f;      // the flow factor of the phase's last advance (0: no phase yet)
        std::uint32_t seen = 0;  // the begin() count of the z's last update
        bool z_known = false;
    };
    Slot slots[transient_slots];
    std::uint32_t frame = 0;     // begin() count (a key seen twice in a frame keeps its first z update)
    float step = 0.f;            // this frame's step, seconds
    unsigned overflow = 0;       // this frame's records without a slot
    unsigned factor_changes = 0; // this frame's phase advances under another factor than the slot's last
    void clear() noexcept {
        for (auto& s : slots) s = Slot{};
        step = 0.f;
        overflow = factor_changes = 0;
    }
    // A frame: every slot ages by `seconds` (the stage clock's step; non-finite or negative: 0).
    void begin(float seconds) noexcept {
        step = seconds > 0.f && seconds < 1e6f ? seconds : 0.f;
        ++frame;
        overflow = factor_changes = 0;
        for (auto& s : slots) {
            if (!s.key) continue;
            s.idle += step;
            s.attack_age += step;
            if (s.idle > transient_hold) s.key = 0;
        }
    }
    static unsigned home_of(std::uint64_t key) noexcept {
        return unsigned(std::uint32_t(key) ^ std::uint32_t(key >> 32)) * 0x9e3779b1u >> 23; // 9 bits
    }
    // The slot of identity `key` (taken from the window's first free one when absent: no z, no phase yet); null for key
    // 0 or a full window (counted overflow).
    Slot* touch(std::uint64_t key) noexcept {
        if (!key) return nullptr;
        const unsigned home = home_of(key);
        Slot* free_slot = nullptr;
        for (unsigned p = 0; p < transient_probe; ++p) {
            Slot& s = slots[(home + p) & (transient_slots - 1u)];
            if (s.key == key) return &s;
            if (!s.key && !free_slot) free_slot = &s;
        }
        if (!free_slot) {
            ++overflow;
            return nullptr;
        }
        *free_slot = Slot{};
        free_slot->key = key;
        free_slot->idle = 0.f;
        return free_slot;
    }
    const Slot* find(std::uint64_t key) const noexcept {
        if (!key) return nullptr;
        const unsigned home = home_of(key);
        for (unsigned p = 0; p < transient_probe; ++p) {
            const Slot& s = slots[(home + p) & (transient_slots - 1u)];
            if (s.key == key) return &s;
        }
        return nullptr;
    }
    // The slot's z this frame (the frame's first update counts; a later one of the same key keeps it). `transient` (a
    // steering or brake body): a rising z starts an attack, its rise taken over the game time since the slot's last
    // update (game_ms / step x idle: a nozzle seen again after a gap does not flash for the whole gap's rise), and
    // `*factor` is its radiance factor 1 + boost x (1 - age / attack_decay); `game_ms` this frame's step in game
    // milliseconds (0: no attack, z still remembered). A slot without a z starts at this one (no attack).
    void update_z(Slot& s, float z, bool transient, float game_ms, float* factor) noexcept {
        *factor = 1.f;
        if (!ee::finite_f(z)) return;
        if (!s.z_known || s.seen != frame) {
            const float dz = z - s.z;
            if (s.z_known && transient && dz > 0.f && game_ms > 0.f) {
                const float since = step > 0.f && s.idle > step ? game_ms * (s.idle / step) : game_ms;
                float rise = dz / (attack_rate * since);
                rise = rise > 1.f ? 1.f : rise;
                const float current = s.attack_age < attack_decay ? s.boost * (1.f - s.attack_age / attack_decay) : 0.f;
                const float fresh = attack_gain * rise;
                if (fresh >= current) {
                    s.boost = fresh;
                    s.attack_age = 0.f;
                }
            }
            s.z = z;
            s.z_known = true;
            s.idle = 0.f;
            s.seen = frame;
        }
        if (transient && s.attack_age < attack_decay) *factor = 1.f + s.boost * (1.f - s.attack_age / attack_decay);
    }
    // The radiance factor of the steering or brake record of identity `key` at `z` (touch + update_z): 1 without a slot.
    void attack(std::uint64_t key, float z, float game_ms, float* factor) noexcept {
        *factor = 1.f;
        if (Slot* s = touch(key)) update_z(*s, z, true, game_ms, factor);
    }
    // The slot's flow phase this frame: the first advance takes the shared phase (nozzle_phase(flow, factor)), every
    // later one adds the accumulator's step since the slot's last advance x this frame's factor (a decreasing or
    // non-finite accumulator adds nothing); wrapped at phase_wrap. A factor other than the last advance's counts in
    // factor_changes.
    void advance_phase(Slot& s, double flow, float factor, float* out) noexcept {
        if (!(s.factor > 0.f)) {
            float p = 0.f;
            nozzle_phase(flow, factor, &p);
            s.phase = double(p);
        } else {
            const double d = flow - s.flow;
            if (d > 0. && d < 1e9) s.phase += d * double(factor);
            if (!(s.phase >= 0.) || !(s.phase < 4e12)) s.phase = 0.;
            if (s.phase >= phase_wrap) s.phase -= phase_wrap * x3m::scalar::floor(s.phase / phase_wrap);
            if (factor != s.factor) ++factor_changes;
        }
        s.flow = flow;
        s.factor = factor;
        *out = float(s.phase);
    }
    // Read only (the heat shimmer's rects, after the stage's frame): the phase advance_phase would give the key at
    // `flow` and `factor`; false without a slot or a phase.
    bool phase_of(std::uint64_t key, double flow, float factor, float* out) const noexcept {
        const Slot* s = find(key);
        if (!s || !(s->factor > 0.f)) return false;
        double p = s->phase;
        const double d = flow - s->flow;
        if (d > 0. && d < 1e9) p += d * double(factor);
        if (!(p >= 0.) || !(p < 4e12)) p = 0.;
        if (p >= phase_wrap) p -= phase_wrap * x3m::scalar::floor(p / phase_wrap);
        *out = float(p);
        return true;
    }
};

// --------------------------------------------------------------------------- SETA (gap 7)
// The SETA factor's two dwords as read (docs/reverse-engineering/engine-effects.md section 8: cfg+0xcc the requested
// warp, cfg+0xd0 the load governor's multiplier, both 16.16): accepted only with 0 < warp <= 0x640000 and
// 0x4ccc <= mult <= 0x10000, else 1.0 (fail closed). `engaged`: warp > 0x10000 (SETA on); `rate`: the effective game-time
// rate ((warp x mult + 0x8000) >> 16) / 65536.
constexpr std::uint32_t seta_one = 0x10000u, seta_warp_max = 0x640000u, seta_mult_min = 0x4cccu;
// The read (engine_effects.cpp seta_read): cfg = *(u32*)seta_slot_va (identity anchor 0x00401c19 in
// executable_identity.h), then one bounded 8-byte read at cfg + seta_warp_offset (warp, then mult at +0xd0). The two
// offsets are bound once by the whole-instruction compare of the tick's reads at seta_site_va: `mov edx,[ecx+0xd0]`,
// `mov eax,[ecx+0xcc]` (verification/results/engine-effects/seta_frame_step.py rows 0x004d1ef0 / 0x004d1ef6).
constexpr std::uintptr_t seta_slot_va = 0x00606f34, seta_site_va = 0x004d1ef0;
constexpr std::uint32_t seta_warp_offset = 0xcc;
constexpr unsigned seta_site_length = 12;
constexpr unsigned char expected_seta_site[seta_site_length] = {0x8b, 0x91, 0xd0, 0x00, 0x00, 0x00,
                                                                0x8b, 0x81, 0xcc, 0x00, 0x00, 0x00};
static_assert(seta_warp_offset + 4 == 0xd0, "warp and mult are adjacent dwords");
inline bool seta_decode(std::uint32_t warp, std::uint32_t mult, bool* engaged, float* requested, float* rate) noexcept {
    *engaged = false;
    *requested = *rate = 1.f;
    if (!warp || warp > seta_warp_max || mult < seta_mult_min || mult > seta_one) return false;
    const std::uint64_t effective = (std::uint64_t(warp) * mult + 0x8000u) >> 16;
    *engaged = warp > seta_one;
    *requested = float(double(warp) / 65536.);
    *rate = float(double(effective) / 65536.);
    return true;
}
// The travel ramp: engaged at once on a SETA frame, released after travel_hold seconds without one; the weight runs
// 0 -> 1 over travel_rise seconds while engaged and back while released. A SETA frame's step is held to
// travel_max_step, so a stall completes no rise; a frame without SETA takes its true step, so a gap without plume frames
// (the stage's clock spans it) releases a stale ramp at once. step() returns true when `engaged` changed (one log row).
struct TravelRamp {
    bool engaged = false;
    float linear = 0.f;   // 0..1
    float released = 0.f; // seconds without SETA while engaged
    bool step(bool seta, float seconds) noexcept {
        float dt = seconds > 0.f && seconds < 1e6f ? seconds : 0.f;
        if (seta && dt > travel_max_step) dt = travel_max_step;
        const bool was = engaged;
        if (seta) {
            engaged = true;
            released = 0.f;
        } else if (engaged) {
            released += dt;
            if (released >= travel_hold) engaged = false;
        }
        linear += (engaged ? dt : -dt) / travel_rise;
        linear = linear < 0.f ? 0.f : linear > 1.f ? 1.f : linear;
        return engaged != was;
    }
    // The applied weight, smoothstep of the linear ramp.
    void weight(float* out) const noexcept { *out = linear * linear * (3.f - 2.f * linear); }
};

// The frame's time-dependent inputs of build() (null: flow 0, no travel, no attack).
struct Dynamics {
    double flow = 0.;                 // FlowPhase::nozzle_widths (flow_rate's nozzle widths, unwrapped)
    float travel = 0.f;               // TravelRamp::weight, 0..1
    float game_ms = 0.f;              // this frame's step in game milliseconds (the attack's normalisation)
    Transients* transients = nullptr; // the per-nozzle memory (null: no attack, the shared phase); build() calls
                                      // begin(step) once
    float step = 0.f;                 // this frame's step, seconds (the stage clock's)
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

// The distance law: the radiance factor of a plume whose projected nozzle width is `nozzle_px` pixels, far_low +
// (1 - far_low) x smoothstep(far_px_min, far_px_full, nozzle_px): far_low at far_px_min and below, 1 at far_px_full and
// above (the ribbons take the same factor from their nozzle's width at the head).
inline void distance_weight(const Look& k, float nozzle_px, float* out) noexcept {
    float t = 1.f;
    if (nozzle_px < k.far_px_full) law::smooth(k.far_px_min, k.far_px_full, nozzle_px, &t);
    *out = k.far_low + (1.f - k.far_low) * t;
}
// The disc's distance law: the end-on disc's radiance factor at a drawn disc nozzle width of `disc_px` pixels,
// disc_far_low + (1 - disc_far_low) x smoothstep(disc_px_min, disc_px_full, disc_px) (after Run 127).
inline void disc_distance_weight(const Look& k, float disc_px, float* out) noexcept {
    float t = 1.f;
    if (disc_px < k.disc_px_full) law::smooth(k.disc_px_min, k.disc_px_full, disc_px, &t);
    *out = k.disc_far_low + (1.f - k.disc_far_low) * t;
}
// The facing weights: the disc's smoothstep(disc_low, disc_high, f) and the axial quad's 1 - (1 - axial_floor) x it.
inline void facing_weights(const Look& k, float facing_abs, float* disc, float* axial) noexcept {
    law::smooth(k.disc_low, k.disc_high, facing_abs, disc);
    *axial = 1.f - (1.f - k.axial_floor) * *disc;
}

// One nozzle's eight vertices. False: not drawn (stats says why). `tables` look_tables(look); `seconds` the stage's
// clock (the length pulse); `floor_value` the plume floor for this record (0: none; build()), raising the value of
// the plume's size and length, never its position; `pulses` (null: evaluated here) the frame's pulse per seed byte;
// `dynamics` (null: none) the frame's flow accumulator, travel weight and attack memory (begun by the caller);
// `own_ship` the record is the own ship's jet (Ring::own): its disc is exempt from the disc's distance law.
inline bool build_nozzle(const ee::Record& r, const ee::Body* body, const View& view, const Look& look,
                         const LookTables& tables, float scale, float seconds, float floor_value, Vertex* out,
                         BuildStats* stats, PulseCache* pulses = nullptr, const Dynamics* dynamics = nullptr,
                         bool own_ship = false) noexcept {
    if ((r.flags & ee::flag_rows_unknown) || !detail::finite3(r.origin) || !detail::finite3(r.axis) ||
        !ee::finite_f(r.size) || !(r.size > 0.f) || !ee::finite_f(r.z) || !ee::finite_f(r.s)) {
        ++stats->culled_rows;
        return false;
    }
    const bool steering = (r.flags & ee::flag_steering) != 0;
    // Gap 6: a steering or brake body's attack, remembered before the idle cull (a puff rises from z 0.01); its slot
    // carries the flow phase too.
    Transients* const memory = dynamics ? dynamics->transients : nullptr;
    Transients::Slot* slot = nullptr;
    float attack = 1.f;
    if ((steering || (r.flags & ee::flag_brake)) && memory) {
        slot = memory->touch(identity_key(r));
        if (slot) memory->update_z(*slot, r.z, true, dynamics->game_ms, &attack);
        if (attack > 1.f) ++stats->attacks;
    }
    // Gap 7: the travel weight on the main jets (not RCS).
    float travel = 0.f;
    if (dynamics && !steering && dynamics->travel > 0.f) travel = dynamics->travel < 1.f ? dynamics->travel : 1.f;
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
    // The length: z value x the pulse, a main jet's z at least idle_length (gap 10), x the travel look's length (gap 7).
    float zl = r.z > 0.f ? r.z : 0.f;
    if (!steering && zl < look.idle_length) zl = look.idle_length;
    float L = zl * value * pulse * (1.f + (travel_length - 1.f) * travel);
    // Gap 4: the nozzle's flow factor from its own value (floored, before the near-camera cap); its phase below, once
    // the nozzle passes the culls.
    float flow_speed = 1.f;
    if (dynamics) flow_factor(look, value, &flow_speed);
    // The quad's reach in nozzle widths: the body's eroded edge, radial < R + 0.7 erode (0.6 + 0.8 u) of the local
    // width, R = outer_reach (the outer sheath's own edge ends at radial 1.2; the core's at 1) (the streaks' erosion
    // 1.6 erode S2 (0.6 + 0.8 u) at S2's minimum -0.4375), bounded by the line in u
    // edge(u) = edge0 + edge_slope u >= (R + 0.42 erode) line(u) + 0.56 erode u line0 (line(u) <= line0), at the nozzle
    // at least the ring (its radius plus three of its sigmas); the halo window's reach (halo_reach x sigma0 (the
    // previous law's 0.5 halo, the widest the detail level gives, for the culling margin; the quad below takes the
    // nozzle's own), the
    // nozzle's sigma, constant along the plume) everywhere. `extent` the widest of them (the culling margin). The
    // near-camera cap keys on `spread x line0`, the body's half-width at the nozzle as before the revised law
    // (1 + 0.48 erode of the width line, or the ring), which bounds the revised mouth's 1 + 0.42 erode: the cap and
    // its fade are unchanged.
    float line0 = 0.f;
    width_line(look, 0.f, &line0);
    const float sigma_widest = .5f * look.halo * scale; // nozzle widths: the halo's e-fold at detail 0
    const float ring_outer = .46f * look.bulge + 3.f * .0645497f;
    float spread = 1.f + .48f * look.erode;
    if (spread * line0 < ring_outer) spread = ring_outer / line0;
    const float erode = look.erode > 0.f ? look.erode : 0.f;
    const float slope = (.04f - .5f * look.bulge) * look.taper; // d line / d u
    float edge0 = (outer_reach + .42f * erode) * line0;
    if (edge0 < ring_outer) edge0 = ring_outer;
    const float edge_slope = (outer_reach + .42f * erode) * slope + .56f * erode * line0;
    const float edge1 = edge0 + edge_slope;
    float extent = edge0 > edge1 ? edge0 : edge1;
    if (extent < halo_reach * sigma_widest) extent = halo_reach * sigma_widest;
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
    // The nozzle's own flow phase (its slot, advanced at this frame's factor; a main jet takes its slot here, with its
    // z), or the shared one without the memory or a slot.
    float flow_phase = 0.f;
    if (dynamics) {
        if (memory && !slot) {
            slot = memory->touch(identity_key(r));
            float unused = 1.f;
            if (slot) memory->update_z(*slot, r.z, false, 0.f, &unused);
        }
        if (slot)
            memory->advance_phase(*slot, dynamics->flow, flow_speed, &flow_phase);
        else
            nozzle_phase(dynamics->flow, flow_speed, &flow_phase);
    }
    // The near-camera cap: the plume's body width 2 spread line0 n (the body's eroded edge or the ring; the halo's
    // faint reach, 1.7 x wider at the default look, does not count: keyed on it the fade bit on 64 % of run405's plume
    // frames, the own ship's in chase view) at the axis point nearest the camera (the tip when the exhaust approaches
    // it) is held to 0.12 H by shrinking the axial quad about the nozzle (width and length x k, the value with them);
    // its radiance fades 1 -> 0.5 over the last 20 % before the cap, the disc's 1 -> chase_disc_floor over the same
    // band (after flight G; a floor of 0.6 above the body's fade before). Its length is free: a distant capital's long
    // plume is not shortened. Since Run 125 the end-on disc and its halo keep the natural nozzle width (value_natural:
    // k down to 0.26 on the Split Ocelot's huge nozzles at 1,800-2,450 units drew its disc as a spot inside the hull's
    // nozzle plate); its integrated body keeps L / n of the shrunk pair, the same ratio.
    float k = 1.f, near_weight = 1.f, near_disc = 1.f;
    {
        const float half = spread * line0 * look.nozzle_width * value; // world units, x k with the plume
        const float f = view.m11 * view.height * .5f, cap = chase_cap * view.height;
        const float toward = a[2] < 0.f ? -a[2] : 0.f; // approach to the camera per unit of length
        float near_depth = o[2] - toward * L;
        if (near_depth < view.near_z) near_depth = view.near_z;
        const float q = 2.f * half * f / near_depth / cap;
        if (q > 1.f - chase_fade_band) {
            float t = (q - (1.f - chase_fade_band)) * (1.f / chase_fade_band);
            t = t > 1.f ? 1.f : t;
            near_weight = 1.f - (1.f - chase_fade_floor) * t;
            near_disc = 1.f - (1.f - chase_disc_floor) * t;
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
    const float value_natural = value; // the end-on disc's and its halo's nozzle width (since Run 125)
    value *= k;
    L *= k;
    // The distance law on the projected nozzle width before the dot floor (after flight E).
    float n = look.nozzle_width * value;
    float far_weight = 1.f;
    const float nozzle_px = n * ppu;
    distance_weight(look, nozzle_px, &far_weight);
    if (nozzle_px < look.far_px_full) ++stats->far_nozzles;
    // Minimum screen sizes, the dot floor (after the cap: a capped plume is large anyway).
    if (n * ppu < min_nozzle_px) n = min_nozzle_px / ppu;
    if (!steering && L * ppu < min_length_px) L = min_length_px / ppu;
    float n_natural = look.nozzle_width * value_natural;
    if (n_natural * ppu < min_nozzle_px) n_natural = min_nozzle_px / ppu;
    // The disc's geometric nozzle width: the natural one x disc_radius (after Run 128), the dot floor after the scale.
    const float disc_radius = look.disc_radius > 0.f ? look.disc_radius : 1.f; // NaN or <= 0: the unscaled disc
    float n_disc_natural = look.nozzle_width * value_natural * disc_radius;
    if (n_disc_natural * ppu < min_nozzle_px) n_disc_natural = min_nozzle_px / ppu;
    // Radiance: I(s) x preset, the RCS weight z, the chase fade, the distance law; the halo and the ring relative to it
    // (the disc's terms all follow i_core and i_halo).
    const float s = r.s < 0.f ? 0.f : r.s > 1.f ? 1.f : r.s;
    float weight = near_weight * far_weight * attack * (1.f + (travel_radiance - 1.f) * travel);
    if (steering) weight *= r.z < 1.f ? r.z : 1.f;
    const float level = look.core_low + (look.core_high - look.core_low) * s;
    const float i_core = level * scale * weight;
    // The mouth terms on the body's throttle curve (after flight D): the halo hb x I(s) / core_high, the ring's radiance
    // ring x I(s) / core_high (params: I_ring / I_core / 2, constant in s).
    const float curve = look.core_high > 0.f ? level / look.core_high : 0.f;
    const float i_halo = look.hb * curve * scale * weight;
    float ring = look.core_high > 0.f ? look.ring / look.core_high * .5f : 0.f;
    ring = ring < 0.f ? 0.f : ring > 1.f ? 1.f : ring;
    std::uint32_t params = (std::uint32_t(int(s * 255.f + .5f)) << 16) | (seed << 8) | std::uint32_t(int(ring * 255.f + .5f));
    float mean[3], peak[3], head[3], transmittance[3] = {1.f, 1.f, 1.f};
    record_tint(r, body, mean, peak);
    head_colour(mean, peak, head);
    if (view.fog.on) {
        x3m::renderer::fog_transmittance(view.fog, x3m::scalar::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]), transmittance);
        for (unsigned i = 0; i < 3; ++i) {
            mean[i] *= transmittance[i];
            head[i] *= transmittance[i];
            stats->fog_min = transmittance[i] < stats->fog_min ? transmittance[i] : stats->fog_min;
        }
        ++stats->fogged;
    }
    // The detail level in the tint's alpha (the drawn nozzle width, before the dot floor); the halo's e-fold and the
    // disc's halo gain follow it from the previous law's (0.5 halo, 3) to the revised (halo_sigma halo, disc_halo).
    float detail = 1.f;
    if (look.detail_px_max > look.detail_px_min) law::smooth(look.detail_px_min, look.detail_px_max, nozzle_px, &detail);
    const float sigma0 = (.5f + (halo_sigma - .5f) * detail) * look.halo * scale; // nozzle widths
    const float halo_units = halo_reach * sigma0;
    const std::uint32_t tint = (pack_colour(mean) & 0x00ffffffu) | (std::uint32_t(int(detail * 255.f + .5f)) << 24);
    const std::uint32_t head_axial = pack_colour(head) & 0x00ffffffu, head_disc = head_axial | 0xff000000u;
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
    // ring's 0.05 nozzle widths behind): at each end the wider of the body's eroded edge bound n x edge(u) (extrapolated
    // linearly) and the halo's reach, + 1 px. max(line, constant) is convex in x, so the chord between the ends encloses
    // it; a plume shorter than its back reach takes the bound's widest over the whole quad. The disc's half-size: the
    // nozzle's ring or eroded edge, or the halo's reach.
    const float pixel = 1.f / ppu;
    const float reach = halo_units * n;
    const float back = n * (halo_units > .05f ? halo_units : .05f) + pixel;
    const float front = L + reach + pixel;
    float body_back = n * (edge0 > edge1 ? edge0 : edge1), body_front = body_back;
    // The disc's erosion at the along-mean growth 1, w_k <= line0; the outer sheath's annulus to outer_reach.
    float disc_edge = (outer_reach + .7f * erode) * line0;
    const float disc_ring_outer = .46f * look.bulge + 3.f * .0645497f * (look.disc_ring_width > 1.f ? look.disc_ring_width : 1.f);
    if (disc_edge < disc_ring_outer) disc_edge = disc_ring_outer;
    // The disc's half-size at its natural nozzle width x disc_radius (the halo's reach with it).
    const float reach_natural = halo_units * n_disc_natural;
    const float width0 = (n_disc_natural * disc_edge > reach_natural ? n_disc_natural * disc_edge : reach_natural) + pixel;
    if (back < L) {
        body_back = n * (edge0 - edge_slope * back / L);
        body_front = n * (edge0 + edge_slope * front / L);
        body_front = body_front > 0.f ? body_front : 0.f;
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
        v.shape[3] = flow_phase;
        v.intensity[0] = i_core * axial_weight;
        v.intensity[1] = i_halo * axial_weight;
        v.intensity[2] = a[2];
        v.intensity[3] = o[2];
        v.tint = tint;
        v.params = params;
        v.fog = fog;
        v.peak = head_axial;
    }
    // The disc, the end-on plume, weighted by smoothstep(disc_low, disc_high, f); under disc_low it collapses to one point
    // (no pixel). Its radiance: the body integrated along the axis, I x disc_kappa x L / n x f per unit of the mean
    // sampled profile (the pixel program's sum over c8..c15 / 8), its halo I_halo x disc_halo x L / n x f, the ring's
    // peak, and the soft cap disc_cap x the side view's peak on the axis (peak_axis_at); all x the disc's weight, so the
    // pixel program's cap x (1 - exp(-total / cap)) scales with it.
    // The near fade takes the disc to chase_disc_floor of its unfaded radiance at the cap (its own fade, not the body's
    // 0.5: after flight G the own ship's end-on disc in chase view read as a clipped disc at the old floor 0.6, which
    // was a lower bound over the body's fade); L / n held to disc_length_max.
    const bool disc_drawn = disc_weight > 0.f;
    // The disc's projected radius held to disc_cap_px x H: the disc alone scales (its quad and nozzle width), its
    // radiance law is scale-free in nozzle widths.
    float disc_scale = 1.f;
    if (disc_drawn && width0 * ppu > disc_cap_px * view.height) {
        disc_scale = disc_cap_px * view.height / (width0 * ppu);
        ++stats->discs_capped;
    }
    const float half = width0 * disc_scale * (disc_drawn ? 1.f : 0.f), n_disc = n_disc_natural * disc_scale;
    // The axial quad's hand-over at the mouth measures the disc's width: its n over the disc's, x disc_radius (at most
    // 1) in the head colour's alpha, 0..127 (under the disc kind's 0.5); the pixel program scales its screen-plane
    // distance by it and by 1 / disc_radius (c19.x).
    {
        float ratio = n_disc > 0.f ? n * disc_radius / n_disc : 1.f;
        ratio = ratio > 0.f ? (ratio < 1.f ? ratio : 1.f) : 0.f; // NaN -> 0
        const std::uint32_t alpha = std::uint32_t(int(ratio * 127.f + .5f)) << 24;
        for (unsigned c = 0; c < 4; ++c) out[c].peak = head_axial | alpha;
    }
    float length_widths = n > 0.f ? L / n : 0.f;
    if (length_widths > look.disc_length_max) length_widths = look.disc_length_max;
    float axis_peak = 0.f; // the cap's side peak: only a drawn disc reads it (the bilinear lookup costs ~25 ns a nozzle)
    if (disc_drawn) peak_axis_at(tables, s, &axis_peak, detail);
    // The disc's distance law on its drawn natural nozzle width (before the disc_cap_px scale, which caps at 0.35 H, far
    // past disc_px_full); evaluated only for a drawn disc, not for the own ship's jets (after Run 127: exempt).
    float disc_far = 1.f;
    if (disc_drawn && !own_ship) disc_distance_weight(look, n_natural * ppu, &disc_far);
    // i_core and i_halo carry near_weight (>= 0.5) and the far law.
    const float disc_level = disc_weight * (near_disc / near_weight) * disc_far;
    // kappa by the detail level: the smooth law's at 0, the structured law's at 1 (Look::disc_kappa_smooth, disc_kappa).
    const float kappa = look.disc_kappa_smooth + (look.disc_kappa - look.disc_kappa_smooth) * detail;
    const float disc_body = disc_level * i_core * kappa * length_widths * facing_abs;
    const float disc_halo = disc_level * i_halo * (3.f + (look.disc_halo - 3.f) * detail) * length_widths * facing_abs;
    const float disc_cap = disc_level * look.disc_cap * i_core * axis_peak;
    // The end-on ring: x 1 at the detail level 0 (the smooth law's), x disc_ring x min(1, (L / n) / 2) at 1 (at least
    // x 1: a short plume's ring no longer dominates its disc); the pixel program widens it by the same level.
    float ring_end_on = look.disc_ring * (length_widths < 2.f ? .5f * length_widths : 1.f);
    if (ring_end_on < 1.f) ring_end_on = 1.f;
    const float disc_ring = disc_level * i_core * ring * 2.f * (1.f + (ring_end_on - 1.f) * detail);
    const float dc[4][2] = {{-half, -half}, {-half, half}, {half, -half}, {half, half}};
    for (unsigned c = 0; c < 4; ++c) {
        Vertex& v = out[4 + c];
        v.position[0] = o[0] + dc[c][0];
        v.position[1] = o[1] + dc[c][1];
        v.position[2] = o[2];
        v.local[0] = dc[c][0];
        v.local[1] = dc[c][1];
        v.local[2] = disc_ring;
        v.local[3] = n_disc;
        v.shape[0] = sigma0;
        v.shape[1] = value_natural; // the soft-depth fades at the disc's natural size (the body keeps the shrunk value)
        v.shape[2] = bias;
        v.shape[3] = flow_phase;
        v.intensity[0] = disc_body;
        v.intensity[1] = disc_halo;
        v.intensity[2] = disc_cap;
        v.intensity[3] = o[2];
        v.tint = tint;
        v.params = params;
        v.fog = fog;
        v.peak = head_disc;
    }
    if (disc_drawn) ++stats->discs;
    if (steering) ++stats->steering;
    if (raised) ++stats->floored;
    return true;
}

// The frame's records into `out` (capacity in nozzles); returns the nozzles written (8 vertices each). `body` maps a
// record's table index to its entry (null: none); `seconds` the stage's clock (wrapped, StageClock::wrapped); `filter`
// (null: every record) keeps the scene view's records; `look` (null: default_look) the plume look; `tables` (null:
// computed here) look_tables(*look), cached where the look is fixed; `radii` (beside the records, Ring::parent_radius;
// null: no floor) each record's ship radius in its own units; `own` (beside the records, Ring::own; null: none) the own
// ship's jets, exempt from the disc's distance law.
//
// The plume floor of a main jet (not RCS, not brake- or steering-pushed): min(max(value, k(R) x R), floor_cap x value)
// (floored_value), from the ship's own radius at draw time, so it does not depend on which of the ship's nozzles the
// game culled this frame; a radius of 0 (unknown) takes none.
// k(R): floor_scale x the three-anchor curve (floor_r / floor_k: flat below the first and above the last radius, linear in
// ln R between neighbours); 0 when floor_scale is 0 (the floor off) or R is not a positive finite radius.
inline void floor_ratio_at(const Look& k, float radius, float* out) noexcept {
    *out = 0.f;
    if (!(k.floor_scale > 0.f) || !ee::finite_f(k.floor_scale) || !(radius > 0.f) || !ee::finite_f(radius)) return;
    float ratio = k.floor_k[2];
    if (radius <= k.floor_r[0])
        ratio = k.floor_k[0];
    else if (radius < k.floor_r[2]) {
        const unsigned i = radius < k.floor_r[1] ? 0u : 1u;
        float a = 0.f, b = 0.f;
        law::ln(radius / k.floor_r[i], &a);
        law::ln(k.floor_r[i + 1] / k.floor_r[i], &b);
        float t = b > 0.f ? a / b : 1.f;
        t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
        ratio = k.floor_k[i] + (k.floor_k[i + 1] - k.floor_k[i]) * t;
    }
    *out = k.floor_scale * ratio;
}
// The floor's target for a main jet of value `size` on a ship of radius R: min(k(R) x R, floor_cap x size) (0: none).
inline void floor_target(const Look& k, float size, float radius, float* out) noexcept {
    float ratio = 0.f;
    floor_ratio_at(k, radius, &ratio);
    float f = ratio * radius;
    const float cap = k.floor_cap * size;
    if (f > cap) f = cap;
    *out = ee::finite_f(f) && f > 0.f ? f : 0.f;
}
// The value a record draws at under the floor (the engine_draw census's value_eff): its size, raised for a main jet to
// the floor's target; RCS and brake- or steering-pushed bodies and a radius of 0 keep their size.
inline void floored_value(const Look& k, const ee::Record& r, float radius, float* out) noexcept {
    *out = r.size;
    if (r.flags & (ee::flag_steering | ee::flag_brake)) return;
    float f = 0.f;
    floor_target(k, r.size, radius, &f);
    if (f > r.size) *out = f;
}
// Co-located layers (after flight G, Run 124 / run412): the game draws some nozzles as two glow records of one parent,
// an outer and an inner layer (the Split Scorpion's fx_engine_xtc_red_nor 10 and _tiny 5.04, 6.9 units apart, one
// axis), and the floor raises both (23.5, 20.2): two plumes and two end-on discs at one nozzle. A record is dropped
// when another record of the same parent (the ship's root node, Ring::parent; 0 unknown: never merged) and the same
// kind (steering / brake flags) is larger (size; the smaller between merge_size_min and merge_size_ratio of it, so
// equal twins stay apart), parallel (axis dot >= merge_axis_dot) and its origin within the larger's natural width (its
// pre-floor size: the glow body spans +-0.5 size; the plume's nozzle width is 0.5 of it); the larger keeps its own
// floored value. The lower bound since Run 125 (run413): the Split Ocelot's side nozzles (fx_engine_xtc_red_big3 187.5
// beside _huge 939, ratio 0.2) have their own rim and plate geometry and drew no plume once merged; the layers of one
// nozzle (the Scorpion's nor + tiny, ratio 0.50) have none. Two cases only: the window [0.35, 0.75] sits between them.
// Records are bucketed by parent (a hash of 2,048 heads), so the pair tests run within one ship's nozzles. `drop`
// (count bytes) gets 1 per dropped record; returns their number. Records past ee::ring_capacity are not merged.
constexpr float merge_axis_dot = .95f, merge_size_ratio = .75f, merge_size_min = .35f;
constexpr unsigned merge_buckets = 2048;
inline unsigned merge_layers(const ee::Record* records, unsigned count, const std::uint32_t* parents,
                             std::uint8_t* drop) noexcept {
    for (unsigned i = 0; i < count; ++i) drop[i] = 0;
    if (!records || !parents || count < 2) return 0;
    if (count > ee::ring_capacity) count = ee::ring_capacity;
    constexpr std::uint16_t none = 0xffffu;
    std::uint16_t head[merge_buckets], next[ee::ring_capacity];
    for (unsigned b = 0; b < merge_buckets; ++b) head[b] = none;
    constexpr std::uint32_t kind = ee::flag_steering | ee::flag_brake;
    for (unsigned i = 0; i < count; ++i) {
        next[i] = none;
        const ee::Record& r = records[i];
        if (!parents[i] || !(r.size > 0.f) || !ee::finite_f(r.size) || !detail::finite3(r.origin) || !detail::finite3(r.axis))
            continue;
        const unsigned b = (parents[i] * 2654435761u) >> 21; // 11 bits: merge_buckets
        next[i] = head[b];
        head[b] = std::uint16_t(i);
    }
    unsigned merged = 0;
    for (unsigned b = 0; b < merge_buckets; ++b)
        for (unsigned i = head[b]; i != none; i = next[i])
            for (unsigned j = next[i]; j != none; j = next[j]) {
                if (parents[i] != parents[j] || ((records[i].flags ^ records[j].flags) & kind)) continue;
                const bool i_large = records[i].size >= records[j].size;
                const ee::Record& large = records[i_large ? i : j];
                const ee::Record& small = records[i_large ? j : i];
                const unsigned dropped = i_large ? j : i;
                if (drop[dropped] || small.size > merge_size_ratio * large.size || small.size < merge_size_min * large.size)
                    continue;
                const float dot = large.axis[0] * small.axis[0] + large.axis[1] * small.axis[1] + large.axis[2] * small.axis[2];
                if (!(dot >= merge_axis_dot)) continue;
                const float dx = large.origin[0] - small.origin[0], dy = large.origin[1] - small.origin[1],
                            dz = large.origin[2] - small.origin[2];
                if (!(dx * dx + dy * dy + dz * dz <= large.size * large.size)) continue;
                drop[dropped] = 1;
                ++merged;
            }
    return merged;
}
using BodyLookup = const ee::Body* (*)(int index);
inline unsigned build(const ee::Record* records, unsigned count, BodyLookup body, const View& view, Preset preset,
                      float seconds, Vertex* out, unsigned capacity, BuildStats* stats, const ViewFilter* filter = nullptr,
                      const Look* look = nullptr, const LookTables* tables = nullptr, const float* radii = nullptr,
                      const Dynamics* dynamics = nullptr, const std::uint32_t* parents = nullptr,
                      const std::uint8_t* own = nullptr) noexcept {
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
    LookTables computed;
    if (!tables) {
        look_tables(k, &computed);
        tables = &computed;
    }
    auto shown = [&](unsigned i) { return !filter || (filter->scene[i] && filter->camera[i] == filter->handle); };
    // The plume floor: RCS jets and the brake / steering-pushed main bodies neither take it.
    constexpr std::uint32_t unfloored = ee::flag_steering | ee::flag_brake;
    const bool floors = radii && k.floor_scale > 0.f && ee::finite_f(k.floor_scale);
    PulseCache pulses;
    if (dynamics && dynamics->transients) dynamics->transients->begin(dynamics->step);
    // Co-located layers of one nozzle draw once (merge_layers; `parents` null: none).
    std::uint8_t drop[ee::ring_capacity];
    const bool merging = parents && count > 1;
    if (merging) st.merged = merge_layers(records, count, parents, drop);
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
        if (merging && i < ee::ring_capacity && drop[i]) continue;
        const ee::Record& r = records[i];
        const ee::Body* b = body && r.body >= 0 ? body(r.body) : nullptr;
        float floor_value = 0.f;
        if (floors && !(r.flags & unfloored)) {
            const float radius = radii[i];
            if (radius > 0.f && ee::finite_f(radius))
                floor_target(k, r.size, radius, &floor_value);
            else
                ++st.floor_unknown;
        }
        if (build_nozzle(r, b, view, k, *tables, scale, seconds, floor_value, out + written * vertices_per_nozzle, &st, &pulses,
                         dynamics, own && own[i]))
            ++written;
    }
    if (dynamics && dynamics->transients) {
        st.transient_overflow = dynamics->transients->overflow;
        st.flow_factor_changes = dynamics->transients->factor_changes;
    }
    st.nozzles = written;
    st.vertices = written * vertices_per_nozzle;
    return written;
}
} // namespace x3m::engine_plumes
