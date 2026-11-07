#pragma once
// Weapon effects, step 1 (docs/architecture/weapon-effects.md 5.1 and 5.1.1): the GPU side of the shield hit. One class
// owns the patch and decal program pairs (src/effects/weapon_{shell,decal}_{vs,ps}.hlsl with the shared hex law
// src/effects/weapon_hex_patch.hlsl) and their declarations, created at the arming and released before Reset
// (ensure_resources() recreates them after a successful one). No vertex or index buffer: a patch draw is one face of an
// owner as one box (eight corners, twelve triangles; the index list is built once at attach), the owner-frame bounds
// of the face's cells in its lattice plane and their anchors' depths -+ the extent along its axis, written per draw
// and drawn through DrawIndexedPrimitiveUP; the vertex program places it in view space through the owner's rows. The
// patch is a screen-space decal: the pixel program reads the lane depth under the pixel, rebuilds the scene point and
// its normal in the owner frame, weighs the face by the normal's component on its axis (triplanar), takes the
// pixel's cell on the face's lattice and looks it up in the draw's cell map (pixel constants: 128 packed keys, their
// clocks and their anchors' depths), lighting it within the extent of its anchor along the axis, so the hull itself
// is the surface and the sky draws nothing. A ship's patch (WeaponPatch::bubble) is drawn on its bubble instead
// (weapon_bubble_ps.hlsl): per pixel the ray meets the owner-frame ellipsoid analytically, its near and far points take
// the face's cell from the same map and the lane only occludes (a point draws where it is nearer the eye than the
// lane). Each decal's four vertices go through DrawPrimitiveUP. The pass holds no DEFAULT-pool resource and allocates
// nothing per frame.
//
// run() draws the frame's patches and decals inside the temporal resolve's stage bracket (TemporalPass FrameInputs::
// stage_callback, after the plumes and the own sun when they draw: RT0 the FP16 scene the resolve reads, RT1+ and the
// depth surface unbound, the scene open; the resolve normalizes again afterwards), so it sets only what it draws with:
// the programs, the declarations, the constants, the lane at s0 (point, clamp), CLIPPING (the boxes' near plane),
// CULLMODE per patch (only the box's back faces draw, so each pixel is shaded once per box, the eye inside one too;
// the winding follows the sign of the owner frame's determinant in view space; NONE for the decals), Z off,
// ONE/ONE additive, COLORWRITEENABLE 15 (alpha 0 is written by the programs: the destination alpha is kept under
// ONE/ONE). Documented D3D9 only; capabilities checked at attach (shader model 3, FP16 post-pixel-shader blending on
// the adapter format, the blend caps, CULLNONE / CW / CCW, the cells' primitive and index limits).
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct WeaponEffectsCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned shell_vs_slots = 0, shell_ps_slots = 0, decal_vs_slots = 0, decal_ps_slots = 0, bubble_ps_slots = 0;
    unsigned sprite_vs_slots = 0, sprite_ps_slots = 0, glow_vs_slots = 0, glow_ps_slots = 0; // step 2 (hull hit)
    unsigned fireball_vs_slots = 0, fireball_ps_slots = 0; // the fireball impostor (the hull hit's burst)
    unsigned shape_vs_slots = 0, ring_ps_slots = 0, filament_ps_slots = 0; // the hull classes' ring and lightning
    unsigned lightning_vs_slots = 0;
    // The lightning's tangent plane from the lane (weapon_lightning_vs.hlsl): vertex texture fetch (point filtered) of
    // the four-channel (A32B32G32R32F) and the R32F lane, IDirect3D9::CheckDeviceFormat with D3DUSAGE_QUERY_VERTEXTEXTURE
    // at attach; without it the segments keep the stage's plane (the contact's direction from the owner's origin).
    bool lightning_vtf_four = false, lightning_vtf_r32f = false;
    bool scorch_enabled = false; // the multiply blend (ZERO / INVSRCCOLOR) the scorches need (attach caps)
    // Step 2, requested at attach (shields,hull): the hull programs and batch limits; false keeps the shield effect.
    bool hull_enabled = false;
    const char* hull_reason = "not_requested";
    // Step 4, requested at attach (shields,hull,bolts): the capsule programs, their declaration and the dynamic vertex
    // and static index buffers of the in-place bolt draw; false keeps the game's bullets (the hull and shields draw).
    bool bolt_enabled = false;
    const char* bolt_reason = "not_requested";
    unsigned bolt_vs_slots = 0, bolt_ps_slots = 0, bolt_flag_ps_slots = 0;
    bool bolt_rim_undo = false; // D3DPBLENDCAPS_DESTCOLOR (source): a failed capsule pass divides the rim back out
    // Step 3, requested at attach with the hull (the explosions modes): the death programs beside the hull's: the halo
    // (weapon_halo_ps.hlsl, required: boom_enabled false keeps every death native) and the shock (weapon_shock_ps.hlsl,
    // optional: its refusal leaves shock_enabled false and shock_reason, and only the tiers with a shock lose it).
    bool boom_enabled = false, shock_enabled = false;
    const char* boom_reason = "not_requested";
    const char* shock_reason = "not_requested";
    unsigned shock_ps_slots = 0, halo_ps_slots = 0;
    HRESULT fp16_blending = S_FALSE;
};
// Step 2, the hull hit (docs/architecture/weapon-look.md 2.5, 2.7, 2.8, 2.14, 3.1): two batched primitives, each one
// DrawIndexedPrimitiveUP of the frame's elements from the stage's vertex array (the index lists are built once at
// attach; no DEFAULT-pool resource).
// The sprite batch (flashes and sparks): one quad per element, four vertices, the kind in the colour's alpha
// (D3DCOLOR, linear 0..1: the heat ramp or a tint): 0 a streak, else a flash whose white core is alpha / 255 of the
// quad's radius.
// - A flash's corners are expanded by the stage: a = the corner in clip space (ndc x z, ndc y z, view z - near z,
//   view z), b = (u, v) the disc coordinate, its radiance and its radius (view units, the analytic depth); c unused.
// - A streak (a spark) carries its two closed-form ends and the vertex program expands it (weapon_sprite_vs.hlsl;
//   the CPU twin stage::streak_corners): a = the head (this frame's position, view space) and its radiance, b = the
//   tail (the previous frame's position through the previous frame's view) and its width (view units), c = the corner
//   (u -1 tail / +1 head, x the spark's rest-length factor: its sign the end, its magnitude the factor; v -1 / +1
//   across) and the radiance a diluted streak never drops below.
// 2048 sparks (look 2.11), a flash and a heavy entry streak per live hull hit (the ring's 256 each), a pulse flash and
// a core per beam spot (64 each): the sparks' own cap binds first (review 2026-11-07).
constexpr unsigned weapon_sprite_quads = 2048 + 2 * 256 + 2 * 64;
struct WeaponSpriteVertex {
    float a[4];
    float b[4];
    float c[3];
    std::uint32_t colour;
};
// The glow primitive (look 2.8): E = colour x saturate(1 - d^2 / R^2)^2 on the lane's surface point at distance d from
// the light, no normal, the sky dark; one quad per element, the light sphere's exact screen rectangle (four corners in
// view space at the sphere's front, the whole screen when it reaches the near plane; stage::sphere_rect), the light's
// centre and R, its radiance colour and its lobes carried by every corner (an ember's lobed silhouette: R x rho(phi), the
// burst's lobe noise at its rotation and seed; a negative seed: round). Drawn through the sprite batch's quad index list.
constexpr unsigned weapon_glow_boxes = 512; // a glow and an ember per live hull hit (the ring's 256)
static_assert(weapon_glow_boxes <= weapon_sprite_quads, "the glows share the sprite quads' index list");
struct WeaponGlowVertex {
    float corner[3];
    float light[4];   // the light's view-space centre, R
    float colour[3];  // linear radiance
    float lobe[2];    // the lobes' rotation (radians) and seed (< 0: round)
};
// The fireball impostor (look 2.14; the hull hit's hot-gas burst, step 3's explosions at full size): one quad per
// fireball, the sphere's exact screen rectangle as the glow's (the corners in view space at the sphere's front, the whole
// screen when it reaches the near plane), each corner carrying the sphere (view-space centre, radius) and its look: the
// heat T of the body at this age, its radiance E, its age fraction x (the erosion's) and the noise's seed; its tint and
// share (share < 0: smoke, the tint its darkening colour); its disc: rotation (radians), the lobes' seed, the noise's
// outward advection (cells) and its third octave's weight (0 .. 1 by its projected size). The pixel program
// (weapon_fireball_ps.hlsl, docs/architecture/hull-burst-look.md section 2) intersects the pixel's ray with the sphere,
// integrates the radial density along the chord up to the lane (the part inside a hull is not drawn), erodes it by two
// octaves of advected 2-D value noise and the third at min(its weight, WeaponEffectsFrame::fireball_octaves 3 ? 1 : 0)
// inside its lobed silhouette and shades it on
// the heat ramp. The smokes (section 5) draw the same program under the scorches' multiply blend, over the lane's
// surface only. Drawn through the sprite batch's quad index list.
constexpr unsigned weapon_fireball_boxes = 256; // a burst per live hull hit (the ring's 256)
constexpr unsigned weapon_smoke_boxes = 256;    // a smoke per live hull hit
// Step 3 (docs/architecture/explosion-look.md 2.3): the death shocks, one fireball-layout quad each (sphere = the shell's
// centre and outer radius, look.x its width, look.y E x alpha, tint.rgb its heat colour), weapon_shock_ps.hlsl.
constexpr unsigned weapon_shock_boxes = 32;
// The death bodies' halos (weapon_halo_ps.hlsl with the glow's vertex program: the light in air about a body), one glow
// box each.
constexpr unsigned weapon_halo_boxes = 32;
static_assert(weapon_fireball_boxes <= weapon_sprite_quads && weapon_smoke_boxes <= weapon_sprite_quads,
              "the fireballs and smokes share the sprite quads' index list");
struct WeaponFireballVertex {
    float corner[3];
    float sphere[4];  // view-space centre, radius
    float look[4];    // heat T, radiance E, age fraction x, seed
    float tint[4];    // a weapon tint (linear) and its share of the colour (0: the heat ramp alone; < 0: smoke)
    float disc[4];    // rotation (radians), lobe seed, advection (noise cells), octaves (2 or 3)
    float core = 0.f; // step 3: the white-hot core's gain g (0: none; the hull bursts and smokes; boom::core_*)
};
// The hull classes' shaped glows (docs/architecture/hull-impact-classes.md 3.5, 3.6): the glow primitive's box (the
// light sphere's screen rectangle, its centre and R) with a profile: the surface ring (weapon_ring_ps.hlsl: the sphere
// shell of radius shape.x and width shape.y cut by the lane's surface; weapon_shape_vs.hlsl, through the sprite batch's
// quad index list).
constexpr unsigned weapon_ring_quads = 64;
struct WeaponShapeVertex {
    float corner[3];
    float light[4];  // the box's view-space centre and radius
    float colour[3]; // linear radiance
    float shape[4];  // ring: r(t), 1 / w, noise seed (< 0 none), wash gain; lightning: core and corona half widths (px),
                     // the ends' view depths (a ring's light[3] carries its wash rate, weapon_ring_ps.hlsl; a segment's
                     // light its ends in pixels)
    float frame[9];  // ring: the owner's rows with the shot's stretch
};
// The ion's lightning (hull-impact-classes.md 4.6; stage::ion_segment_quad): one quad per segment of a channel or branch.
// The vertex carries the hit's contact point and tangent axes (view) and the segment's ends in that plane;
// weapon_lightning_vs.hlsl turns the plane onto the lane's surface normal at the contact pixel (its depth's one-pixel
// differences, the side nearer in depth per axis; the stage's plane where the lane cannot give one), projects the ends
// and lays the quad along the segment in pixels, the corona's half width + 1 px past it on every side;
// weapon_filament_ps.hlsl draws the lane under it within the depth window, the core and corona profile in the pass's
// constant colours x the vertex's radiance. Corner k: its end (A for -along, B for +along) by the sign of c[3], its side
// by the sign of d[3] (0: A -+, 1: B ++, 2: A --, 3: B +-). 64 bytes a vertex. weapon_filament_boxes: segments a frame
// (about 40 an ion hit, at most 80), through the sprite batch's quad index list.
constexpr unsigned weapon_filament_boxes = 1024;
static_assert(weapon_filament_boxes <= weapon_sprite_quads, "the lightning shares the sprite quads' index list");
struct WeaponLightningVertex {
    float a[4]; // the contact point (view), the radiance (x the pass's core and corona colours)
    float b[4]; // the segment's ends in the tangent plane (u, v of A, u, v of B; in units of the axes)
    float c[4]; // the plane's first axis (view units a plane unit), the core's half width (px) signed along
    float d[4]; // the plane's second axis, the depth window (view units) signed across
};
// The scorches (hull-impact-classes.md 3.7): glow rectangles drawn with the glow programs under ZERO / INVSRCCOLOR (the
// scene x (1 - the program's output)) before every additive layer; their colour s(t) x scorch_colour.
constexpr unsigned weapon_scorch_boxes = 128;
// Step 4, the bolts (docs/architecture/bolt-muzzle-look.md 5): one capsule quad per bolt, drawn in place of the frame's
// late bullet draw by draw_bolts from the stage's WeaponBoltQuads (weapon_effects_bolt_stage.h lays every quantity on the
// CPU: the frustum cull needs the projection anyway, and the programs stay one pass-through vertex program and one
// distance-field pixel program). Each corner's clip position and the bolt's seven constant interpolants (the same at the
// four corners): p0 head px xy and the axis's unit (head -> back) px; p1 the visible axis length (core + tail), the core
// and the tail px lengths, the drawn streak length from the head; p2 the streak's unit (head -> previous head), the
// stretch's start (px from the head), the energy factor g_s; p3 the body width at the head and the back end, the halo
// sigma at both; p4 the tint (max 1) and the core's whiteness h; p5 the style code (0 tracer, 1 slug, 2 orb, 3 wave; +4
// heavy), the fade, the tail root (of the body width) and the tail gain; p6 the noise seed, the foreshortening c, the
// wave chord px and the rim flag. 128 bytes a vertex, four a quad (weapon_bolt_vs.hlsl / weapon_bolt_ps.hlsl).
constexpr unsigned weapon_bolt_quads = 512 + 32; // the tap list's 512 and the beams' and spares' (bolt::list_capacity)
struct WeaponBoltVertex {
    float clip[4];
    float p[7][4];
};
struct WeaponBoltQuad {
    float clip[4][4]; // corners: head-left, head-right, back-left, back-right (a strip-free quad: indices 0 1 2, 2 1 3)
    float p[7][4];
};
// The draw: the frame's quads in kind order (line kinds first, then orbs, then waves; within the line and orb kinds the
// rim-eligible quads first, so the rim pass draws two prefixes), the pixel constants and the far flag's variant.
struct WeaponBoltDraw {
    const WeaponBoltQuad* quads = nullptr;
    unsigned lines = 0, orbs = 0, waves = 0;     // [0, lines) line kinds, [lines, lines + orbs) orbs, then waves
    unsigned rim_lines = 0, rim_orbs = 0;        // the rim-eligible prefixes of the line and orb ranges
    float look[4][4]{};                          // c0..c3 of weapon_bolt_ps.hlsl (stage::bolt_pixel_constants)
    bool far_flag = false;                       // the caller bound RT1 (masked) and RT2 (GREEN): the MRT variant
    bool rim = true;                             // the multiply rim's first pass (core::bolt::rim and its gate)
};
enum class WeaponBoltStep : unsigned { None, Validate, Resources, Save, Lock, State, Rim, Capsules, Restore };
struct WeaponBoltReport {
    HRESULT operation = S_FALSE;
    WeaponBoltStep failed = WeaponBoltStep::None;
    unsigned draws = 0, quads = 0, rim_quads = 0, calls = 0;
    bool drew = false, restored = true;          // restored false: a restore call failed (the caller drops its shadow)
    bool rim_undone = false;                     // the capsule pass failed after the rim: the rim's band divided back out
};
// One patch draw: the cells of one face of an owner (or a share of them past one draw; a fixed lattice in the owner's
// frame perpendicular to the face's axis), their box and the face's newest hits still in their ring. Plane and axis
// are view units in the owner's frame: view = centre + view_u q.x + view_v q.y + view_a depth; the pixels light the
// lane's surface inside the box where it falls in a cell of the map.
constexpr unsigned weapon_patch_hits = 4, weapon_patch_cells = 128;
constexpr int weapon_cell_key_bias = 2048; // a cell key (kx, ky) packs as (kx + bias) * 4096 + (ky + bias), |k| < bias
// An in-place heap sort of n indices by their keys, ascending (no allocation, O(n log n); the cell map's order).
inline void sort_by_key(std::uint8_t* idx, unsigned n, const float* key) noexcept {
    auto sift = [&](unsigned root, unsigned end) {
        for (;;) {
            unsigned child = 2 * root + 1;
            if (child >= end) return;
            if (child + 1 < end && key[idx[child]] < key[idx[child + 1]]) ++child;
            if (!(key[idx[root]] < key[idx[child]])) return;
            const std::uint8_t t = idx[root];
            idx[root] = idx[child];
            idx[child] = t;
            root = child;
        }
    };
    if (n < 2) return;
    for (unsigned i = n / 2; i-- > 0;) sift(i, n);
    for (unsigned end = n - 1; end > 0; --end) {
        const std::uint8_t t = idx[0];
        idx[0] = idx[end];
        idx[end] = t;
        sift(0, end);
    }
}
struct WeaponPatch {
    float centre[3]{};    // the owner's centre (view space)
    float view_u[3]{1.f, 0.f, 0.f}, view_v[3]{0.f, 1.f, 0.f}, view_a[3]{0.f, 0.f, 1.f}; // the lattice's u, v, axis (view)
    float axes[3][3]{{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}}; // the owner's local axes in view (owner_frame)
    float colour[3]{1.f, 1.f, 1.f};
    float anchor[3]{0.f, 0.f, 1.f}, anchor_u[3]{1.f, 0.f, 0.f}, anchor_v[3]{0.f, 1.f, 0.f}; // the region frame (owner)
    float seed = 0.f;     // the region's index (the cell hash's third input)
    float cell = 1.f;     // hex cell, centre to centre (view units)
    float radius = 1.f;   // the footprint radius (rings + 0.5) cells (view units, planar): ring speed and width
    float reach = 1.f;    // a hit's footprint: cell centres within this planar distance of the hit cell's centre
    float extent = 1.f;   // reach + two cell circumradii: the surface lights only within this of the cell's anchor
    unsigned hits = 0;    // 0..4 ring hits, newest first (every draw of the region carries them)
    float hit[weapon_patch_hits][2]{};      // hit points in the lattice plane (view units)
    float hit_cell[weapon_patch_hits][2]{}; // the hit cells' centres (view units): the ring
    float age_s[weapon_patch_hits]{};       // game seconds
    unsigned cells = 0;   // 0..weapon_patch_cells cells (unique keys; a region's spill draws carry disjoint cells)
    float cell_key[weapon_patch_cells]{};   // packed keys (weapon_cell_key_bias)
    float cell_since[weapon_patch_cells]{}; // game seconds since the cell was lit; < 0: not lit yet (a ring passes)
    float cell_anchor[weapon_patch_cells]{}; // along the face's axis (view units): the depth of the cell's anchor hit
    std::uint8_t cell_centre[weapon_patch_cells]{}; // 1: lit as its hit's centre cell (the marker level)
    float box[6]{};       // the draw's box in the face's frame (u, v, a; view units): min x3, max x3
    float newest_s = 0.f; // the owner's newest hit's age (game seconds): the screen budget drops the oldest first
    std::uint32_t owner = 0, owner_node = 0; // diagnostics (the weapon_patch row): the owner object, its node and its
                                             // radius (view units)
    float owner_radius = 0.f;
    // The ship bubble (5.1.1 "Placement"): 1 draws the cells on the ellipsoid of centre bubble_c and half-extents
    // bubble_e (owner frame, view units; weapon_bubble_ps.hlsl, its near and far side, the lane only occluding); 0 the
    // hull decal (weapon_shell_ps.hlsl, the stations). The box then bounds the bubble's sheet over the cells.
    std::uint8_t bubble = 0;
    std::uint8_t eye_sphere = 0; // the bubble is the own ship's eye sphere in the cockpit (diagnostics)
    float bubble_c[3]{}, bubble_e[3]{1.f, 1.f, 1.f};
};
// A flat patch at a hit point (an owner without a usable ellipsoid): a camera-facing quad covering the hit's footprint
// with the same hex law, the hit at its centre (the one hit's cells are evaluated in the program).
struct WeaponDecal {
    float centre[3]{};  // view space
    float radius = 0.f; // the footprint radius (rings + 0.5) cells (view units)
    float reach = 0.f;  // rings cells (view units)
    float cell = 1.f;   // view units
    float age_s = 0.f;
    float colour[3]{1.f, 1.f, 1.f};
    float soft = 1.f;   // the two-sided surface fade (view units)
};
// The frame's look (weapon_effects_core.h's patch constants by default; the fixture overrides gains to isolate a term).
struct WeaponLook {
    float ripple_speed, ring_width, ring_gain;  // footprint radii per game second, footprint radii, radiance
    float centre_gain, centre_fill;             // the hit cell's radiance over a ring cell's, its fill / edge
    float fade_s, stagger;                      // a lit cell's mean fade (game seconds), per-cell spread
    float edge_width, fill_ratio, cell_gain;    // cells (< 0.5), fill / edge, edge radiance
    float bubble_near_gain, bubble_far_gain;    // the bubble's near and far side (1, core::bubble_far_gain)
};
WeaponLook default_weapon_look() noexcept; // the header constants of weapon_effects_core.h
struct WeaponEffectsFrame {
    UINT width = 0, height = 0;          // the lane's (= the target's) size
    IDirect3DTexture9* lane = nullptr;   // the completed RT2 (borrowed)
    bool four_channel = false;           // the lane's .b carries the view depth (else R32F device z/w)
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0; // the jittered projection
    float m22 = 0, m32 = 0;              // R32F lane depth law z = m32 / (d - m22)
    float near_z = 1.f;
    const WeaponPatch* patches = nullptr;
    unsigned patch_count = 0;
    const WeaponDecal* decals = nullptr;
    unsigned decal_count = 0;
    // Step 2: the sprite quads and the glow rectangles (four vertices each) the stage wrote.
    const WeaponSpriteVertex* sprites = nullptr;
    unsigned sprite_quads = 0;
    const WeaponGlowVertex* glows = nullptr;
    unsigned glow_boxes = 0;
    // The hull hit's bursts (fireball impostors) and heavy rings (sprite quads), four vertices each.
    const WeaponFireballVertex* fireballs = nullptr;
    unsigned fireball_boxes = 0;
    unsigned fireball_octaves = 3;       // 2 or 3: at most this many octaves (core::look::fireball_octaves, 2 past the budget)
    const WeaponFireballVertex* smokes = nullptr; // the bursts' smoke (fireball impostors under the scorch blend)
    unsigned smoke_boxes = 0;
    const WeaponShapeVertex* rings = nullptr; // the surface rings
    unsigned ring_quads = 0;
    const WeaponLightningVertex* filaments = nullptr; // the ion class's lightning segments
    unsigned filament_boxes = 0;
    const WeaponGlowVertex* scorches = nullptr; // multiply decals, drawn first
    unsigned scorch_boxes = 0;
    const WeaponFireballVertex* shocks = nullptr; // step 3: the death shocks (after the fireballs, additive)
    unsigned shock_boxes = 0;
    const WeaponGlowVertex* halos = nullptr; // step 3: the death bodies' halos (after the shocks, additive)
    unsigned halo_boxes = 0;
    WeaponLook look = default_weapon_look();
};
enum class WeaponEffectsStep : unsigned { None, Validate, Resources, State, Patches, Decals, Glows, Sprites, Fireballs,
                                          Rings, Filaments, Scorches, Smokes, Shocks, Halos };
struct WeaponEffectsReport {
    HRESULT operation = S_FALSE;
    WeaponEffectsStep failed = WeaponEffectsStep::None;
    unsigned calls = 0, patches = 0, decals = 0, sprite_quads = 0, glow_boxes = 0, fireball_boxes = 0, ring_quads = 0;
    unsigned filament_boxes = 0, scorch_boxes = 0, smoke_boxes = 0, shock_boxes = 0, halo_boxes = 0;
    bool drew = false;
};
class WeaponEffectsPass {
public:
    // A patch draw's box: eight corners (u, v, a in the face's frame, view units), twelve triangles.
    static constexpr unsigned patch_vertices = 8, patch_triangles = 12;
    struct CellVertex {
        float corner[4]; // the box corner in the face's frame (u, v, a), 1
    };
    WeaponEffectsPass() = default;
    ~WeaponEffectsPass();
    WeaponEffectsPass(const WeaponEffectsPass&) = delete;
    WeaponEffectsPass& operator=(const WeaponEffectsPass&) = delete;
    // hull (shields,hull): also the sprite batch, the glow and the fireball (their programs and the batch limits); their refusal
    // leaves the pass enabled for the patches with caps().hull_enabled false.
    // booms (the explosions modes, with hull): the death programs (caps().boom_enabled; a refusal keeps deaths native).
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format,
                   bool hull = false, bool bolts = false, bool booms = false) noexcept;
    void detach() noexcept;
    const WeaponEffectsCaps& caps() const noexcept { return caps_; }
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    HRESULT ensure_resources() noexcept;
    bool resources_ready() const noexcept;
    // S_FALSE with no device call when nothing is to be drawn; D3DERR_DEVICELOST codes pass through.
    HRESULT run(const WeaponEffectsFrame&, WeaponEffectsReport*) noexcept;
    unsigned references() const noexcept;
    const std::uint16_t* cell_indices() const noexcept { return indices_; }
    // Step 4: the in-place bolt draw inside the game's scene at its late bullet draw (docs/architecture/bolt-muzzle-look.md
    // 5.1): every state it sets is read first through the documented Get* calls (the device is non-pure under the proxy)
    // and put back after, in reverse order: the programs, the declaration, stream 0 and its frequency (refused unless 1),
    // the indices, the pixel constants c0..c3 and the render states of the two passes. The rim pass (ZERO / INVSRCCOLOR,
    // COLORWRITEENABLE1 and 2 = 0) draws the rim-eligible prefixes, then the capsule pass (ONE / ONE, the masks the
    // caller left: RT1 0 and RT2 GREEN with the far flag) draws every quad; LESSEQUAL with Z write off, CULL NONE. The
    // quads are written into the dynamic vertex buffer under one DISCARD lock. S_FALSE with no device call when there
    // is nothing to draw; a lost device passes through; a failed restore reports restored = false.
    HRESULT draw_bolts(const WeaponBoltDraw&, WeaponBoltReport*) noexcept;
#if defined(X3M_WEAPON_EFFECTS_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the next draw, bit 2 every draw,
    // bit 3 refuses the program creation (attach: program_create, no object kept), bit 4 the hull programs only
    // (attach succeeds with hull_enabled false, hull_reason hull_program_create).
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
    // bit 5: the bolt programs refused at attach (bolt_reason bolt_program_create); bit 6: the next bolt draw's lock
    // fails; bit 7: the next bolt draw's second pass fails (the restore still runs); bit 8: the death programs refused
    // at attach (boom_reason boom_program_create, the hull kept); bit 9: the shock program alone refused (shock_reason
    // shock_program_create, the deaths drawn without their shells).
#endif
private:
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT create_objects() noexcept;
    HRESULT create_hull_objects() noexcept;
    HRESULT create_bolt_objects() noexcept;
    HRESULT create_boom_objects() noexcept;
    void release_bolt_objects() noexcept;
    bool bolt_objects_ready() const noexcept;
    void release_objects() noexcept;
    void release_hull_objects() noexcept;
    bool hull_objects_ready() const noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    WeaponEffectsCaps caps_{};
    bool reset_pending_ = false, indices_built_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* shell_vs_ = nullptr;
    IDirect3DPixelShader9* shell_ps_ = nullptr;
    IDirect3DPixelShader9* bubble_ps_ = nullptr;
    IDirect3DVertexShader9* decal_vs_ = nullptr;
    IDirect3DPixelShader9* decal_ps_ = nullptr;
    IDirect3DVertexDeclaration9* shell_declaration_ = nullptr;
    IDirect3DVertexDeclaration9* decal_declaration_ = nullptr;
    IDirect3DVertexShader9* sprite_vs_ = nullptr;
    IDirect3DPixelShader9* sprite_ps_ = nullptr;
    IDirect3DVertexShader9* glow_vs_ = nullptr;
    IDirect3DPixelShader9* glow_ps_ = nullptr;
    IDirect3DVertexDeclaration9* sprite_declaration_ = nullptr;
    IDirect3DVertexDeclaration9* glow_declaration_ = nullptr;
    IDirect3DVertexShader9* fireball_vs_ = nullptr;
    IDirect3DPixelShader9* fireball_ps_ = nullptr;
    IDirect3DVertexDeclaration9* fireball_declaration_ = nullptr;
    IDirect3DVertexShader9* shape_vs_ = nullptr;
    IDirect3DPixelShader9* ring_ps_ = nullptr;
    IDirect3DPixelShader9* filament_ps_ = nullptr;
    IDirect3DPixelShader9* shock_ps_ = nullptr; // step 3 (released with the hull objects they draw with)
    IDirect3DPixelShader9* halo_ps_ = nullptr;
    IDirect3DVertexDeclaration9* shape_declaration_ = nullptr;
    IDirect3DVertexShader9* lightning_vs_ = nullptr;
    IDirect3DVertexDeclaration9* lightning_declaration_ = nullptr;
    IDirect3DVertexShader9* bolt_vs_ = nullptr;
    IDirect3DPixelShader9* bolt_ps_ = nullptr;
    IDirect3DPixelShader9* bolt_flag_ps_ = nullptr;
    IDirect3DVertexDeclaration9* bolt_declaration_ = nullptr;
    IDirect3DVertexBuffer9* bolt_vb_ = nullptr; // DEFAULT pool, DYNAMIC | WRITEONLY: released before Reset
    IDirect3DIndexBuffer9* bolt_ib_ = nullptr;  // DEFAULT pool, WRITEONLY, the quads' indices written once
    CellVertex vertices_[patch_vertices]{}; // one draw's box, rewritten per draw
    std::uint16_t indices_[patch_triangles * 3]{};
    std::uint16_t sprite_indices_[weapon_sprite_quads * 6]{}; // two triangles per quad (attach time; the glows' too)
};
} // namespace x3m::renderer
