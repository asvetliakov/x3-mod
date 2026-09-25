#pragma once
// Effects stage, phase 1 (docs/architecture/effects-modernisation-opus.md sections 2, 3.1, 3.3, 8 and 9): the GPU side
// of the proxy-owned effects stage. One class owns the three vs_3_0 / ps_3_0 pairs (bolt streak, shield shell, ripple
// decal), their declarations, one dynamic vertex buffer (DEFAULT, DYNAMIC | WRITEONLY, one DISCARD lock per frame), the
// static unit icosphere (162 vertices, 320 triangles) and the shared quad index buffer, all created at attach (not at
// first use: a first explosion must not hitch) and released before Reset (recreated by the next attach or ensure).
//
// run() draws inside the caller's state bracket: the temporal resolve calls it through FrameInputs::stage_callback
// after its normalize (RT0 the FP16 scene, RT1+ and the depth surface unbound, every render state at the pass's
// baseline, the scene open) and normalizes again afterwards, so this pass sets only what it draws with: programs,
// declaration, streams, indices, constants, the lane at s0 (point) and the bullet atlas at s1 (linear, mip), CLIPPING,
// CULLMODE NONE and ONE/ONE additive blending. No SetRenderTarget, no depth: occlusion is the lane's soft term (the
// dust motes' rule). Documented D3D9 only; capabilities are checked at attach (shader model 3, FP16 post-pixel-shader
// blending on the adapter format, BLENDOP, ONE blend caps, MaxVertexShaderConst >= 10, index and primitive limits).
#include "../proxy/effects_stage_core.h"
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct EffectsStageCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned bolt_vs_slots = 0, bolt_ps_slots = 0, shell_vs_slots = 0, shell_ps_slots = 0, decal_vs_slots = 0, decal_ps_slots = 0;
    unsigned largest_program_slots = 0;
    HRESULT fp16_blending = S_FALSE; // the CheckDeviceFormat result
};
// The look (engine units, pixels, seconds); defaults from the design note. Validated by valid_tuning().
struct EffectsStageTuning {
    float min_width_px = effects_stage::default_min_width_px;    // W_min, render pixels (full width)
    float min_length_px = effects_stage::default_min_length_px;  // L_min at 1080 lines (scaled by H / 1080 by the caller)
    float halo = effects_stage::default_halo_width;              // halo radius as a multiple of the core radius
    float stretch = effects_stage::default_stretch;              // k_stretch of the velocity streak
    float core_intensity = effects_stage::default_core_intensity;
    float halo_intensity = effects_stage::default_halo_intensity;
    float halo_sigma = 0.35f;                                    // halo e-fold as a fraction of (R - r)
    float tint_floor = 0.05f;
    float soft = effects_stage::default_soft;                    // SOFT: the soft radius as a multiple of the effect radius
    float rim_intensity = 1.5f, ring_intensity = 4.f, rim_power = 2.5f, hex_scale = 12.f;
    float ripple_seconds = effects_stage::shell_ripple_seconds, ring_speed = 3.f, ring_width = 0.15f; // radians per second, radians
    float flash_seconds = effects_stage::shell_flash_seconds, flash_intensity = 8.f, flash_sharpness = 60.f;
    float decal_ring_intensity = 4.f, decal_hex_scale = 6.f, decal_ring_speed = 1.5f, decal_ring_width = 0.12f; // radii per second, radii
};
bool valid_tuning(const EffectsStageTuning&) noexcept;
struct EffectsFrame {
    UINT width = 0, height = 0;
    float view_rows[12]{};                 // world -> view rows (r t)
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0; // the jittered projection routed draws use
    float m22 = 0, m32 = 0;                // depth law of an R32F lane (ignored when four_channel)
    float near_z = 1.f;                    // the nearest view depth drawn
    bool lane_four_channel = false;
    IDirect3DTexture9* lane = nullptr;     // the completed RT2 (borrowed)
    const effects_stage::BoltVertex* bolt_vertices = nullptr; unsigned bolt_instances = 0;
    IDirect3DBaseTexture9* bolt_atlas = nullptr; // the game's bullet texture as the game bound it (the application-visible pointer, borrowed); null: white tint
    const effects_stage::ShellInstance* shells = nullptr; unsigned shell_count = 0;
    const effects_stage::DecalVertex* decal_vertices = nullptr; unsigned decal_count = 0;
    const EffectsStageTuning* tuning = nullptr;
};
enum class EffectsStageStep : unsigned { None, Validate, Resources, Lock, State, Bolts, Shells, Decals };
struct EffectsReport {
    HRESULT operation = S_FALSE;
    EffectsStageStep failed = EffectsStageStep::None;
    unsigned calls = 0, bolts = 0, shells = 0, decals = 0;
    bool drew = false;
};
class EffectsStagePass {
public:
    EffectsStagePass() = default; ~EffectsStagePass();
    EffectsStagePass(const EffectsStagePass&) = delete; EffectsStagePass& operator=(const EffectsStagePass&) = delete;
    // Checks the capabilities and creates every program and buffer; a refusal names its reason in caps().reason.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    void detach() noexcept;
    const EffectsStageCaps& caps() const noexcept { return caps_; }
    // The DEFAULT-pool buffers go before Reset and come back at ensure_resources() (called by run) after it.
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    HRESULT ensure_resources() noexcept;
    bool resources_ready() const noexcept { return device_ && caps_.enabled && !reset_pending_ && dynamic_vb_ && sphere_vb_ && sphere_ib_ && quad_ib_; }
    // Draws the frame's records (see the header comment for the state contract). S_FALSE with nothing drawn when the
    // frame carries no record; a failed call leaves the report's failed step; D3DERR_DEVICELOST codes are passed through.
    HRESULT run(const EffectsFrame&, EffectsReport*) noexcept;
    unsigned references() const noexcept;
    static constexpr unsigned sphere_vertices = 162, sphere_triangles = 320;
    static constexpr unsigned quad_capacity = effects_stage::max_bolts + effects_stage::max_hits; // indexed quads the shared index buffer covers
    static constexpr UINT dynamic_bytes = 65536;
#ifdef X3M_EFFECTS_STAGE_FIXTURE
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the bolt draw once.
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
#endif
private:
    template<class Fn> Fn call(unsigned slot) const noexcept { ++calls_; return reinterpret_cast<Fn>(vtable_[slot]); }
    HRESULT create_programs(const D3DCAPS9&) noexcept;
    HRESULT create_sphere() noexcept;
    HRESULT create_quad_indices() noexcept;
    void release_buffers() noexcept;
    IDirect3DDevice9* device_ = nullptr; void* const* vtable_ = nullptr; // borrowed
    EffectsStageCaps caps_{};
    bool reset_pending_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* bolt_vs_ = nullptr; IDirect3DPixelShader9* bolt_ps_ = nullptr;
    IDirect3DVertexShader9* shell_vs_ = nullptr; IDirect3DPixelShader9* shell_ps_ = nullptr;
    IDirect3DVertexShader9* decal_vs_ = nullptr; IDirect3DPixelShader9* decal_ps_ = nullptr;
    IDirect3DVertexDeclaration9* bolt_declaration_ = nullptr; IDirect3DVertexDeclaration9* shell_declaration_ = nullptr; IDirect3DVertexDeclaration9* decal_declaration_ = nullptr;
    IDirect3DVertexBuffer9* dynamic_vb_ = nullptr; IDirect3DVertexBuffer9* sphere_vb_ = nullptr;
    IDirect3DIndexBuffer9* sphere_ib_ = nullptr; IDirect3DIndexBuffer9* quad_ib_ = nullptr;
};
} // namespace x3m::renderer
