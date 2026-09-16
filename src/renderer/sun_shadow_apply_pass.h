#pragma once
// Scene-end sun-shadow application (docs/architecture/legacy-sun-application.md,
// section 2): one full-screen quad multiplying the owning FP16 scene target by
// 1 - (1 - f) s under ZERO/SRCCOLOR blending, s from the route's RT2.g share
// and f from a 3x3 PCF of the same frame's cascade-0 replay map
// (src/temporal/sun_shadow_apply_ps.hlsl). The pass owns only its two
// programs, the quad declaration and a D3DSBT_ALL block; the caller
// (MotionOutput's scene-end hook) supplies RT2, the map, the replay frame's
// view -> sun rows, the jittered projection latch and the target, and
// serializes rendering, Reset and teardown. Documented D3D9 only; the format
// gates are CheckDeviceFormat queries, as AmbientOcclusionPass.
#include <cstdint>
#include <d3d9.h>
namespace x3m::renderer {
struct SunShadowApplyCaps {
    bool enabled = false;
    const char* reason = "detached"; // gate name or creation failure
    HRESULT formats = S_FALSE, programs = S_FALSE;
    unsigned program_slots = 0;      // conservative ps_3_0 slot count of the embedded program
};
struct SunShadowApplyParams {
    // The route's jittered projection latch (camera_reprojection.h; the AO
    // law): view z = m32 / (d - m22), x = (ndc.x - m20) z / m00, y = (ndc.y - m21) z / m11.
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
    float rows[12]{};                 // shadow_replay_view_rows of the replay's own frame
    unsigned jitter_index = 0;        // rotates the 3x3 kernel (8 steps of pi / 8)
    float exponent = 1.f;             // 1 for original shading, 1 / 2.2 for converted materials
    float bias_constant = .001f;      // normalized sun depth subtracted from every compare
    float bias_max = .01f;            // clamp of the receiver-plane term per tap, and the tap bias where the plane fit is dropped
    float planar_step = .05f;         // relative view-depth change per pixel above which the plane fit is dropped
};
struct SunShadowApplyFrame {
    IDirect3DTexture9* depth_share = nullptr; // RT2 G32R32F, width x height (.r = z/w with -1 sentinel, .g = share)
    IDirect3DTexture9* map = nullptr;         // the replay's R32F square map of this frame
    IDirect3DSurface9* target = nullptr;      // owning scene target of the attached format
    UINT width = 0, height = 0;
    SunShadowApplyParams params{};
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
};
enum class SunShadowApplyStage : unsigned { None, Validate, Block, Capture, Normalize, Scene, Constants, Apply, EndScene, Restore };
struct SunShadowApplyResult {
    HRESULT operation = S_FALSE, restore = S_FALSE;
    SunShadowApplyStage failed = SunShadowApplyStage::None;
    bool applied = false;             // the quad drew into the target
    bool skipped = false;             // a missing or invalid input: nothing touched, execute returns S_FALSE
    const char* skipped_reason = "";  // detached, reset_pending, input, params, format, device
    unsigned map_size = 0;
};
class SunShadowApplyPass {
public:
    SunShadowApplyPass() = default;
    ~SunShadowApplyPass();
    SunShadowApplyPass(const SunShadowApplyPass&) = delete;
    SunShadowApplyPass& operator=(const SunShadowApplyPass&) = delete;
    // Device and native table borrowed (no AddRef). Gates shader model 3,
    // the program's slot count, ZERO/SRCCOLOR blend caps, G32R32F/R32F
    // textures and post-pixel-shader blending on target_format, then creates
    // the programs and the declaration (surviving Reset). A refusal leaves
    // the pass detached with caps().reason set.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format, D3DFORMAT target_format) noexcept;
    const SunShadowApplyCaps& caps() const noexcept { return caps_; }
    // One quad: capture the owned block and the bindings, normalize, upload
    // the constants, draw, restore. Any missing input returns S_FALSE with
    // result.skipped and touches no device state; a failed device call
    // restores and names its stage; a lost device stops restoration.
    HRESULT execute(const SunShadowApplyFrame&, SunShadowApplyResult*) noexcept;
    void before_reset() noexcept;              // releases the block, refuses execute until after_reset(SUCCEEDED)
    void after_reset(HRESULT) noexcept;
    void detach() noexcept;                    // full teardown including programs
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept;      // persistent interfaces held
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept {
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT ensure_block() noexcept;
    HRESULT normalize(IDirect3DSurface9* target, UINT w, UINT h) noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    SunShadowApplyCaps caps_{};
    D3DFORMAT target_format_ = D3DFMT_UNKNOWN;
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DPixelShader9* apply_ = nullptr;
    IDirect3DVertexShader9* quad_vs_ = nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    UINT render_targets_ = 0, streams_ = 0;
    bool reset_pending_ = false;
};
} // namespace x3m::renderer
