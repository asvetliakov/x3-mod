#pragma once
// Volumetric sun fog at the scene end (docs/architecture/volumetric-fog.md,
// section 3 and "Stage 1 implementation"): a camera-local sun-lit medium with
// shafts from the sun cascade maps. One transaction of up to four quads and
// one StretchRect on a borrowed D3D9 device through native vtable slots, like
// AmbientOcclusionPass:
//   1. march      half resolution, A8R8G8B8: the lit fraction F of 16 jittered
//                 samples against up to three cascade maps (src/fog/fog_march_ps.hlsl);
//   2. copy       StretchRect of the owning FP16 scene target to an FP16 scratch;
//   3. sky hue    every sky_period-th frame: scratch + RT2 -> 8x8 -> the 1x1
//                 smoothed mean sky colour (src/fog/fog_sky_*_ps.hlsl);
//   4. composite  full resolution into the scene target, exact in linear light
//                 (src/fog/fog_composite_ps.hlsl).
// The pass writes its own targets and the owning scene target only; RT1.. and
// the depth surface are unbound for the transaction and returned. It does not
// decide whether the sector has fog, read the sun or choose the frame: the
// caller (MotionOutput's scene-end hook) supplies RT2, the cascade maps with
// their view -> sun rows, the projection latch, the sun's view direction and
// linear radiance and the target, and serializes rendering, Reset and
// teardown. Documented D3D9 only; the gates are caps fields and
// CheckDeviceFormat queries. fog_pass_math.h holds the d3d9-free arithmetic.
#include <cstdint>
#include <d3d9.h>
#include "fog_pass_math.h"
namespace x3m::renderer {
struct FogCaps {
    bool enabled = false;
    const char* reason = "detached"; // gate name or creation failure
    HRESULT formats = S_FALSE, programs = S_FALSE;
    unsigned largest_program_slots = 0;
};
struct FogCascadeInput {
    IDirect3DTexture9* map = nullptr; // R32F square sun-space depth map
    float rows[12]{};                 // view -> sun rows of the map's retained basis under the current camera
    float bias = 0.f;                 // normalized sun depth subtracted from the reference (the apply's clamp)
    bool valid = false;               // false or a null map: the cascade is skipped (its samples fall through, then lit)
};
constexpr unsigned fog_cascade_max = 3;
struct FogParams {
    // The route's jittered projection latch plus the quad pixel-centre term (as
    // SunShadowCascadeFrame): x = (ndc.x - m20) z / m00, y = (ndc.y - m21) z / m11.
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
    float tau_max = 0.f;              // optical depth of the whole medium, (0, fog_strength_max]
    float radius = fog_radius_default;// e-folding distance of the density, view units
    float anisotropy = fog_anisotropy_default; // Henyey-Greenstein g, [0, fog_anisotropy_max]
    float margin = 1.f;               // cascade select margin (shadow_cascade_select_margin)
    float sun_view[3]{};              // unit view-space direction toward the sun
    float sun_radiance[3]{};          // linear E_sun RGB (fog_sun_radiance)
    float decode_exponent = 2.2f;     // engine -> linear exponent of the scene target (1: none)
    unsigned jitter_index = 0;        // TAA phase, offsets the march noise
    bool update_sky = true;           // run the two sky-hue quads this frame (always run while the history is empty)
    float sky_blend = .25f;           // weight of a sky update once the history exists
};
struct FogFrame {
    IDirect3DTexture9* depth_share = nullptr; // RT2: A32B32G32R32F (.b view depth), G32R32F or R32F (.r device depth, -1 sentinel)
    IDirect3DSurface9* target = nullptr;      // owning A16B16G16R16F scene target
    UINT width = 0, height = 0;
    unsigned count = 0;                       // cascades supplied, 0..fog_cascade_max
    FogCascadeInput cascades[fog_cascade_max]{};
    FogParams params{};
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
    bool caller_queries_idle = false;         // positive knowledge, as for TemporalPass
};
enum class FogStage : unsigned { None, Validate, Targets, Block, Capture, Normalize, Scene, March, Copy, SkyLevel, SkyReduce, Composite, EndScene, Restore };
struct FogResult {
    HRESULT operation = S_FALSE, restore = S_FALSE;
    FogStage failed = FogStage::None;
    bool applied = false;       // the composite drew into the target
    bool sky_updated = false;
    unsigned cascades_bound = 0;
    unsigned device_calls = 0;  // native device/interface calls of this execute (the CPU cost driver)
    IDirect3DTexture9* lit = nullptr; // borrowed half-resolution F; valid until the next execute/before_reset/detach
    UINT half_width = 0, half_height = 0;
};
class FogPass {
public:
    FogPass() = default;
    ~FogPass();
    FogPass(const FogPass&) = delete;
    FogPass& operator=(const FogPass&) = delete;
    // Device and native table borrowed (no AddRef). Gates shader model 3, the
    // largest program's slot count, SRCALPHA/INVSRCALPHA, A8R8G8B8 and
    // A16B16G16R16F render-target textures with post-pixel-shader blending on
    // the latter and RT-to-RT StretchRect, then creates the four programs, the
    // quad vertex program and its declaration (all surviving Reset). A refusal
    // leaves the pass detached with caps().reason set; nothing is retained.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    const FogCaps& caps() const noexcept { return caps_; }
    // Default-pool targets of a width x height frame: F (ceil half, A8R8G8B8),
    // the FP16 scene scratch, the 8x8 sky level and the 1x1 sky history.
    // Re-created on a size change; a failure releases every partial target.
    HRESULT prepare(UINT width, UINT height) noexcept;
    // One transaction. A failed step restores and publishes nothing
    // (result.failed names it); a lost device stops restoration. A failure
    // before the composite leaves the scene target untouched.
    HRESULT execute(const FogFrame&, FogResult*) noexcept;
    void before_reset() noexcept;   // releases the targets and the block; refuses execute until after_reset(SUCCEEDED)
    void after_reset(HRESULT) noexcept;
    void detach() noexcept;         // full teardown including programs
    bool resources_ready(UINT width, UINT height) const noexcept {
        return caps_.enabled && !reset_pending_ && block_ && width && height && width_ == width && height_ == height &&
            lit_surface_ && scratch_surface_ && sky_level_surface_ && sky_surface_;
    }
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept;  // persistent interfaces held
    unsigned allocations() const noexcept { return allocations_; }
    bool sky_seeded() const noexcept { return sky_seeded_; }
#ifdef X3M_FOG_PASS_FIXTURE
    IDirect3DTexture9* fixture_sky() const noexcept { return sky_; }
    IDirect3DTexture9* fixture_sky_level() const noexcept { return sky_level_; }
#endif
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT ensure_block() noexcept;
    HRESULT normalize() noexcept;
    HRESULT quad(UINT w, UINT h) noexcept;
    HRESULT bind_target(IDirect3DSurface9* target, UINT w, UINT h, IDirect3DPixelShader9* program) noexcept;
    void release_targets() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    FogCaps caps_{};
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DPixelShader9 *march_ = nullptr, *composite_ = nullptr, *sky_level0_ = nullptr, *sky_reduce_ = nullptr;
    IDirect3DVertexShader9* quad_vs_ = nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    IDirect3DTexture9 *lit_ = nullptr, *scratch_ = nullptr, *sky_level_ = nullptr, *sky_ = nullptr;
    IDirect3DSurface9 *lit_surface_ = nullptr, *scratch_surface_ = nullptr, *sky_level_surface_ = nullptr, *sky_surface_ = nullptr;
    UINT width_ = 0, height_ = 0, half_width_ = 0, half_height_ = 0, render_targets_ = 0, streams_ = 0;
    unsigned allocations_ = 0;
    mutable unsigned calls_ = 0;
    bool reset_pending_ = false, sky_seeded_ = false;
};
} // namespace x3m::renderer
