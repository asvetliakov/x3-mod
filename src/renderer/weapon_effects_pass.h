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
// is the surface and the sky draws nothing. Each decal's four vertices go through DrawPrimitiveUP. The pass holds no
// DEFAULT-pool resource and allocates nothing per frame.
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
    unsigned shell_vs_slots = 0, shell_ps_slots = 0, decal_vs_slots = 0, decal_ps_slots = 0;
    HRESULT fp16_blending = S_FALSE;
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
    WeaponLook look = default_weapon_look();
};
enum class WeaponEffectsStep : unsigned { None, Validate, Resources, State, Patches, Decals };
struct WeaponEffectsReport {
    HRESULT operation = S_FALSE;
    WeaponEffectsStep failed = WeaponEffectsStep::None;
    unsigned calls = 0, patches = 0, decals = 0;
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
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
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
#if defined(X3M_WEAPON_EFFECTS_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the next draw, bit 2 every draw,
    // bit 3 refuses the program creation (attach: program_create, no object kept).
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
#endif
private:
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT create_objects() noexcept;
    void release_objects() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    WeaponEffectsCaps caps_{};
    bool reset_pending_ = false, indices_built_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* shell_vs_ = nullptr;
    IDirect3DPixelShader9* shell_ps_ = nullptr;
    IDirect3DVertexShader9* decal_vs_ = nullptr;
    IDirect3DPixelShader9* decal_ps_ = nullptr;
    IDirect3DVertexDeclaration9* shell_declaration_ = nullptr;
    IDirect3DVertexDeclaration9* decal_declaration_ = nullptr;
    CellVertex vertices_[patch_vertices]{}; // one draw's box, rewritten per draw
    std::uint16_t indices_[patch_triangles * 3]{};
};
} // namespace x3m::renderer
