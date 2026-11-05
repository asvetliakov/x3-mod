#pragma once
// Weapon effects, step 1 (docs/architecture/weapon-effects.md 5.1 and 5.1.1): the GPU side of the shield hit. One class
// owns the patch and decal program pairs (src/effects/weapon_{shell,decal}_{vs,ps}.hlsl with the shared hex law
// src/effects/weapon_hex_patch.hlsl) and their declarations, created at the arming and released before Reset
// (ensure_resources() recreates them after a successful one). No vertex or index buffer: a unit polar cap (cap_rings x
// cap_segments quads around a centre vertex, built once at attach in the pass's own memory) goes through
// DrawIndexedPrimitiveUP, one draw per patch, and the vertex program bends it onto the owner's ellipsoid as the
// spherical cap the stage computed (the CPU-side cull: only the cap bounding the patch radius is rasterized, so the
// cost scales with the patch, not with the owner); each decal's four vertices go through DrawPrimitiveUP. The pass holds
// no DEFAULT-pool resource and allocates nothing per frame.
//
// run() draws the frame's patches and decals inside the temporal resolve's stage bracket (TemporalPass FrameInputs::
// stage_callback, after the plumes and the own sun when they draw: RT0 the FP16 scene the resolve reads, RT1+ and the
// depth surface unbound, the scene open; the resolve normalizes again afterwards), so it sets only what it draws with:
// the programs, the declarations, the constants, the lane at s0 (point, clamp), CLIPPING, CULLMODE (per patch the
// winding that keeps the outward faces, from the sign of det A; NONE for the decals), Z off,
// ONE/ONE additive, COLORWRITEENABLE 15 (alpha 0 is written by the programs: the destination alpha is kept under
// ONE/ONE). Occlusion is the lane alone (the depth is unbound before the callback). Documented D3D9 only; capabilities
// checked at attach (shader model 3, FP16 post-pixel-shader blending on the adapter format, the blend caps, the three
// cull modes, the cap's primitive and index limits).
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct WeaponEffectsCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned shell_vs_slots = 0, shell_ps_slots = 0, decal_vs_slots = 0, decal_ps_slots = 0;
    HRESULT fp16_blending = S_FALSE;
};
// One patch draw: an owner's ellipsoid (view space), the cap to rasterize, the lattice of one anchor region, its cell
// map (the lit cells with their own clocks) and the region's newest hits still in their ring (the ring).
// Unit-sphere vectors are in the owner's frame (the ellipsoid's local coordinates).
// The cell map: 128 cells, 32 pixel registers of four packed keys and 32 of their clocks (weapon_hex_patch.hlsl;
// the patch program uses c2..c91 of ps_3_0's 224, weapon_effects_pass.cpp hex_block).
constexpr unsigned weapon_patch_hits = 4, weapon_patch_cells = 128;
constexpr int weapon_cell_key_bias = 2048; // a cell key (kx, ky) packs as (kx + bias) * 4096 + (ky + bias), |k| < bias
struct WeaponPatch {
    float centre[3]{};    // view space
    float axes[9]{};      // A0, A1, A2 (view space): the owner's local axes x its radius
    float soft = 1.f;     // soft radius (view units)
    float colour[3]{1.f, 1.f, 1.f};
    float cap_dir[3]{0.f, 0.f, 1.f}, cap_u[3]{1.f, 0.f, 0.f}, cap_v[3]{0.f, 1.f, 0.f}; // right-handed (u x v = dir)
    float cap_angle = 0.f; // the cap's half-angle (radians, <= pi)
    float anchor[3]{0.f, 0.f, 1.f}, anchor_u[3]{1.f, 0.f, 0.f}, anchor_v[3]{0.f, 1.f, 0.f}; // the lattice's tangent frame
    float surface = 1.f;  // the stereographic scale: the ellipsoid's mean axis (view units)
    float seed = 0.f;     // the anchor's index (the cell hash's third input)
    float cell = 1.f;     // hex cell, centre to centre (view units)
    float radius = 1.f;   // the footprint radius (rings + 0.5) cells (view units, planar): ring speed and width
    float reach = 1.f;    // a hit's footprint: cell centres within this planar distance of the hit cell's centre
    unsigned hits = 0;    // 0..4 ring hits, newest first
    float hit[weapon_patch_hits][2]{};      // hit points in the lattice plane (view units)
    float hit_cell[weapon_patch_hits][2]{}; // the hit cells' centres (view units): the ring
    float age_s[weapon_patch_hits]{};       // game seconds
    unsigned cells = 0;   // 0..weapon_patch_cells lit cells (unique keys; a region's spill draws carry disjoint cells)
    float cell_key[weapon_patch_cells]{};   // packed keys (weapon_cell_key_bias)
    float cell_since[weapon_patch_cells]{}; // game seconds since the cell was lit (>= 0)
    std::uint8_t cell_centre[weapon_patch_cells]{}; // 1: lit as its hit's centre cell (the marker level)
    unsigned seams = 0;   // bit k: the owner's anchor region k draws a cap overlapping this one (the light is shared
                          // across the seam, weapon_shell_ps.hlsl); 0: no seam
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
    static constexpr unsigned cap_rings = 8, cap_segments = 32;
    static constexpr unsigned cap_vertices = 1 + cap_rings * cap_segments;                     // 257
    static constexpr unsigned cap_triangles = cap_segments + (cap_rings - 1) * cap_segments * 2; // 480
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
    const float* cap_vertex_data() const noexcept { return &cap_[0][0]; } // (cos phi, sin phi, rho) per vertex
    const std::uint16_t* cap_indices() const noexcept { return indices_; }
#if defined(X3M_WEAPON_EFFECTS_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the next draw, bit 2 every draw,
    // bit 3 refuses the program creation (attach: program_create, no object kept), bit 4 draws the patches with CULLMODE
    // NONE (the winding cull's same-run comparison).
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
    bool reset_pending_ = false, cap_built_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* shell_vs_ = nullptr;
    IDirect3DPixelShader9* shell_ps_ = nullptr;
    IDirect3DVertexShader9* decal_vs_ = nullptr;
    IDirect3DPixelShader9* decal_ps_ = nullptr;
    IDirect3DVertexDeclaration9* shell_declaration_ = nullptr;
    IDirect3DVertexDeclaration9* decal_declaration_ = nullptr;
    float cap_[cap_vertices][3]{};
    std::uint16_t indices_[cap_triangles * 3]{};
};
} // namespace x3m::renderer
