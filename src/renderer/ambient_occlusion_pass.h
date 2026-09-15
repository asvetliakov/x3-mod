#pragma once
// Detached half-resolution GTAO chain (docs/architecture/ambient-occlusion.md,
// step 1b): linearize -> horizon search -> one 2D depth-aware blur ->
// bilateral upsample and multiply application, four quads on a borrowed D3D9 device
// through native vtable slots, like TemporalPass / LinearEmissionPass. This
// class does not hook the route, choose the frame, identify the scene target
// or read pixels back; the caller (later the scene-end hook) supplies the
// route's R32F depth (RT2), the latched projection terms and the owning
// target, and serializes rendering, Reset and teardown.
#include <cstdint>
#include <d3d9.h>
#include "ambient_occlusion_caps.h"
namespace x3m::renderer {
struct AmbientOcclusionCaps {
    bool enabled = false;
    const char* reason = "detached"; // ambient_occlusion_capability's reason, or a creation failure
    HRESULT formats = S_FALSE, programs = S_FALSE;
    unsigned largest_program_slots = 0;
};
struct AmbientOcclusionParams {
    // Projection terms the route latches (camera_reprojection.h, the default
    // scratch m22/m32): x = (ndc.x - m20) z / m00, y = (ndc.y - m21) z / m11,
    // z = m32 / (d - m22).
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
    // Radius in metres; view space is 5 units per metre (1 unit = 0.2 m,
    // docs/reverse-engineering/camera-state-and-frame-routine.md, "Ambient
    // occlusion inputs"), so the shader radius is radius_metres * units_per_metre.
    float radius_metres = 2.f;
    float units_per_metre = 5.f;
    float strength = .5f;          // s of the factor 1 - s (1 - ao)
    float falloff = .615f;         // fraction of the radius the taps fade over (XeGTAO)
    float max_radius_px = 64.f;    // half-resolution screen-space cap
    float depth_tolerance = .05f;  // relative depth tolerance of blur and upsample
    unsigned jitter_index = 0;     // rotates the 4x4 noise (integer, 0..15 used)
};
struct AmbientOcclusionFrame {
    IDirect3DTexture9* depth = nullptr;  // R32F or G32R32F .r, width x height, device z/w with -1 sentinel
    IDirect3DSurface9* target = nullptr; // owning scene target of the attached format; null computes the term only
    UINT width = 0, height = 0;
    AmbientOcclusionParams params{};
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
    bool caller_queries_idle = false; // positive knowledge, as for TemporalPass
    // Debug view (X3M_AO_DEBUG): the apply quad writes the factor
    // pow(1 - s (1 - ao), 1/2.2) as grayscale into the target instead of
    // multiplying (blend left off); every other quad is unchanged.
    bool debug_view = false;
};
enum class AmbientOcclusionStage : unsigned {
    None, Validate, Targets, Block, Capture, Normalize, Scene, Linearize, Gtao, Blur, Apply, EndScene, Restore
};
struct AmbientOcclusionResult {
    HRESULT operation = S_FALSE, restore = S_FALSE;
    AmbientOcclusionStage failed = AmbientOcclusionStage::None;
    bool applied = false;                 // the multiply quad drew into the target
    IDirect3DTexture9* term = nullptr;    // borrowed half-res R16F occlusion term (1 - visibility, 0 = unoccluded); valid until the next execute/before_reset/detach
    UINT half_width = 0, half_height = 0;
};
class AmbientOcclusionPass {
public:
    AmbientOcclusionPass() = default;
    ~AmbientOcclusionPass();
    AmbientOcclusionPass(const AmbientOcclusionPass&) = delete;
    AmbientOcclusionPass& operator=(const AmbientOcclusionPass&) = delete;
    // Device and native table borrowed (no AddRef). Gates (ambient_occlusion_caps.h)
    // and creates the four pixel programs, the quad vertex program and its
    // declaration (all surviving Reset). A failed gate or creation leaves the
    // pass detached with caps().reason set; nothing is retained.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format,
                   D3DFORMAT target_format) noexcept;
    const AmbientOcclusionCaps& caps() const noexcept { return caps_; }
    // Half-resolution targets for a width x height frame (ceil(w/2) x ceil(h/2)):
    // R32F linear depth and two R16F occlusion textures, default pool. Re-created on
    // a size change; a failure releases every partial target (rollback).
    HRESULT prepare(UINT width, UINT height) noexcept;
    // One chain. Captures the owned D3DSBT_ALL block and the bindings, runs
    // the four quads, restores. A failed step restores and publishes nothing
    // (result.failed names it); a lost device stops restoration as in
    // TemporalPass. With frame.target null the apply quad is skipped.
    HRESULT execute(const AmbientOcclusionFrame&, AmbientOcclusionResult*) noexcept;
    // Reset protocol: before_reset releases the targets and the block and
    // refuses execute until after_reset(SUCCEEDED); targets return lazily.
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    void detach() noexcept; // full teardown including programs
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept; // persistent interfaces held
    unsigned allocations() const noexcept { return allocations_; } // successful target set creations
    UINT half_width() const noexcept { return half_width_; }
    UINT half_height() const noexcept { return half_height_; }
#ifdef X3M_AMBIENT_OCCLUSION_FIXTURE
    IDirect3DTexture9* fixture_half_depth() const noexcept { return half_depth_; }
    void fixture_skip_blur(bool skip) noexcept { fixture_skip_blur_ = skip; } // term = raw horizon search
#endif
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept {
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT ensure_block() noexcept;
    HRESULT normalize(UINT w, UINT h) noexcept;
    HRESULT quad(UINT w, UINT h) noexcept;
    HRESULT bind_and_draw(IDirect3DSurface9* target, UINT w, UINT h, IDirect3DPixelShader9* program,
                          IDirect3DBaseTexture9* s0, IDirect3DBaseTexture9* s1, IDirect3DBaseTexture9* s2) noexcept;
    void release_targets() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    AmbientOcclusionCaps caps_{};
    D3DFORMAT target_format_ = D3DFMT_UNKNOWN;
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DPixelShader9 *linearize_ = nullptr, *gtao_ = nullptr, *blur_ = nullptr, *apply_ = nullptr;
    IDirect3DVertexShader9* quad_vs_ = nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    IDirect3DTexture9* half_depth_ = nullptr;
    IDirect3DTexture9* ao_[2]{};
    IDirect3DSurface9* half_depth_surface_ = nullptr;
    IDirect3DSurface9* ao_surfaces_[2]{};
    UINT width_ = 0, height_ = 0, half_width_ = 0, half_height_ = 0, render_targets_ = 0, streams_ = 0;
    unsigned allocations_ = 0;
    bool reset_pending_ = false;
#ifdef X3M_AMBIENT_OCCLUSION_FIXTURE
    bool fixture_skip_blur_ = false;
#endif
};
} // namespace x3m::renderer
