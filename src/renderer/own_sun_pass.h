#pragma once
// Own sun, step 1 (docs/architecture/own-sun-pass.md sections 2-4, 9 and 10): the GPU side of the mod's procedural sun.
// One class owns the vs_3_0 / ps_3_0 pair (src/effects/own_sun_{vs,ps}.hlsl) and their declaration, created at the
// arming (not at the first drawn sun) and released before Reset; ensure_resources() recreates them after a successful
// one. No vertex buffer: the four vertices go through DrawPrimitiveUP (24 bytes each, built on the stack by
// own_sun_core.h quad()), so the pass holds no DEFAULT-pool resource and allocates nothing per frame.
//
// run() draws one camera-facing quad of 34 x 34 core radii at the projected sun inside the temporal resolve's stage bracket
// (TemporalPass FrameInputs::stage_callback: RT0 the FP16 scene the resolve reads, RT1+ and the depth surface unbound,
// every render state at the pass's baseline, the scene open; the resolve normalizes again afterwards), so it sets only
// what it draws with: the programs, the declaration, ten pixel constants, the lane at s0 (point, clamp), CLIPPING,
// CULLMODE NONE, Z off, ONE/ONE additive. No SetRenderTarget, no RT2 write, no depth test: occlusion is the pixel
// program's nine-tap lane clip. Documented D3D9 only; capabilities are checked at attach (shader model 3, FP16
// post-pixel-shader blending on the adapter format, BLENDOP and ONE blend caps, CULLNONE, the compiled programs' slots).
#include "../proxy/own_sun_core.h"
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct OwnSunCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned vs_slots = 0, ps_slots = 0;
    HRESULT fp16_blending = S_FALSE; // the CheckDeviceFormat result
};
struct OwnSunFrame {
    UINT width = 0, height = 0;         // the lane's (= the target's) size
    IDirect3DTexture9* lane = nullptr; // the completed RT2 (borrowed)
    own_sun::core::Disc disc{};        // projected, skip == None
    own_sun::core::Radiance radiance{}; // not black
    float rays = own_sun::core::rays_default; // sun_rays: the rays' share, 0..1
};
enum class OwnSunStep : unsigned { None, Validate, Resources, State, Draw };
struct OwnSunReport {
    HRESULT operation = S_FALSE;
    OwnSunStep failed = OwnSunStep::None;
    unsigned calls = 0;
    bool drew = false;
};
class OwnSunPass {
public:
    OwnSunPass() = default;
    ~OwnSunPass();
    OwnSunPass(const OwnSunPass&) = delete;
    OwnSunPass& operator=(const OwnSunPass&) = delete;
    // Checks the capabilities and creates the programs; a refusal names its reason in caps().reason and holds nothing
    // (D3DERR_NOTAVAILABLE for a missing capability).
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    void detach() noexcept;
    const OwnSunCaps& caps() const noexcept { return caps_; }
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    HRESULT ensure_resources() noexcept;
    bool resources_ready() const noexcept { return device_ && caps_.enabled && !reset_pending_ && vs_ && ps_ && declaration_; }
    // Draws the quad (see the header comment for the state contract). S_FALSE with no device call when the frame has
    // nothing to draw (a skipped disc or a black sun); a failed call leaves the report's failed step; D3DERR_DEVICELOST
    // codes the device returned are passed through (the internal reset-pending flag fails with E_FAIL).
    HRESULT run(const OwnSunFrame&, OwnSunReport*) noexcept;
    unsigned references() const noexcept;
#if defined(X3M_OWN_SUN_FIXTURE) || defined(X3M_MOTION_OUTPUT_FIXTURE)
    // Fixture faults: bit 0 refuses the FP16 blending capability at attach, bit 1 fails the draw once, bit 2 fails
    // every draw.
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
    void* const* vtable_ = nullptr; // borrowed
    OwnSunCaps caps_{};
    bool reset_pending_ = false;
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
};
} // namespace x3m::renderer
