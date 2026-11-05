#pragma once
// Weapon effects, step 1 (docs/architecture/weapon-effects.md 5.1): the GPU side of the shield hit. One class owns the
// shell and decal program pairs (src/effects/weapon_{shell,decal}_{vs,ps}.hlsl, revived from the dropped effects stage
// 9375a1e7) and their declarations, created at the arming and released before Reset (ensure_resources() recreates
// them after a successful one). No vertex or index buffer: the unit icosphere (162 vertices, 320 triangles, built once
// at attach in the pass's own memory) goes through DrawIndexedPrimitiveUP and each decal's four vertices through
// DrawPrimitiveUP, so the pass holds no DEFAULT-pool resource and allocates nothing per frame.
//
// run() draws the frame's shells and decals inside the temporal resolve's stage bracket (TemporalPass FrameInputs::
// stage_callback, after the plumes and the own sun when they draw: RT0 the FP16 scene the resolve reads, RT1+ and the
// depth surface unbound, the scene open; the resolve normalizes again afterwards), so it sets only what it draws with:
// the programs, the declarations, the constants, the lane at s0 (point, clamp), CLIPPING, CULLMODE (per shell the
// winding that keeps the outward faces, from the sign of det A; NONE for the decals), Z off,
// ONE/ONE additive, COLORWRITEENABLE 15 (alpha 0 is written by the programs: the destination alpha is kept under
// ONE/ONE). Occlusion is the lane alone (the depth is unbound before the callback). Documented D3D9 only; capabilities
// checked at attach (shader model 3, FP16 post-pixel-shader blending on the adapter format, the blend caps, the three
// cull modes).
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct WeaponEffectsCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned shell_vs_slots = 0, shell_ps_slots = 0, decal_vs_slots = 0, decal_ps_slots = 0;
    HRESULT fp16_blending = S_FALSE;
};
// One shell: the owner's shell in view space and up to four hits (newest first).
struct WeaponShell {
    float centre[3]{};   // view space
    float axes[9]{};     // A0, A1, A2 (view space), the owner's local axes x the shell radius
    float soft = 1.f;    // soft radius (view units)
    float tint[3]{1.f, 1.f, 1.f};
    unsigned hits = 0;   // 1..4
    float dir[4][3]{};   // hit directions on the unit sphere (owner frame)
    float age_s[4]{};    // game seconds
    float amp[4]{};      // ring amplitudes
};
struct WeaponDecal {
    float centre[3]{}; // view space
    float radius = 0.f; // view units
    float age_s = 0.f;
    float tint[3]{1.f, 1.f, 1.f};
    float amp = 0.f;
};
struct WeaponLook {
    float rim_gain, rim_power, hex_scale, hex_gain;
    float ripple_s, ring_speed, ring_width, flash_s, flash_gain, flash_sharpness;
    float decal_ring_speed, decal_ring_width, decal_hex_scale, soft;
    unsigned ring_count;
};
WeaponLook default_weapon_look() noexcept; // the header constants of weapon_effects_core.h
struct WeaponEffectsFrame {
    UINT width = 0, height = 0;          // the lane's (= the target's) size
    IDirect3DTexture9* lane = nullptr;   // the completed RT2 (borrowed)
    bool four_channel = false;           // the lane's .b carries the view depth (else R32F device z/w)
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0; // the jittered projection
    float m22 = 0, m32 = 0;              // R32F lane depth law z = m32 / (d - m22)
    float near_z = 1.f;
    const WeaponShell* shells = nullptr;
    unsigned shell_count = 0;
    const WeaponDecal* decals = nullptr;
    unsigned decal_count = 0;
    WeaponLook look = default_weapon_look();
};
enum class WeaponEffectsStep : unsigned { None, Validate, Resources, State, Shells, Decals };
struct WeaponEffectsReport {
    HRESULT operation = S_FALSE;
    WeaponEffectsStep failed = WeaponEffectsStep::None;
    unsigned calls = 0, shells = 0, decals = 0;
    bool drew = false;
};
class WeaponEffectsPass {
public:
    static constexpr unsigned sphere_vertices = 162, sphere_triangles = 320;
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
    const float* sphere_positions() const noexcept { return &sphere_[0][0]; } // the fixture's winding / coverage checks
    const std::uint16_t* sphere_indices() const noexcept { return indices_; }
#if defined(X3M_WEAPON_EFFECTS_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the next draw, bit 2 every draw,
    // bit 3 refuses the program creation (attach: program_create, no object kept), bit 4 draws the shells with CULLMODE
    // NONE (the winding cull's same-run timing comparison).
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
    bool reset_pending_ = false, sphere_built_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* shell_vs_ = nullptr;
    IDirect3DPixelShader9* shell_ps_ = nullptr;
    IDirect3DVertexShader9* decal_vs_ = nullptr;
    IDirect3DPixelShader9* decal_ps_ = nullptr;
    IDirect3DVertexDeclaration9* shell_declaration_ = nullptr;
    IDirect3DVertexDeclaration9* decal_declaration_ = nullptr;
    float sphere_[sphere_vertices][3]{};
    std::uint16_t indices_[sphere_triangles * 3]{};
};
} // namespace x3m::renderer
