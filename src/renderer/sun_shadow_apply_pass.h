#pragma once
// Scene-end sun-shadow application (docs/architecture/legacy-sun-application.md,
// section 2): one full-screen quad multiplying the owning FP16 scene target by
// 1 - (1 - f) s under ZERO/SRCCOLOR blending, s from the route's RT2.g share,
// the receiver's view depth from RT2.b (A32B32G32R32F) or the z/w law on
// RT2.r (G32R32F), and f from a 3x3 PCF of the same frame's cascade-0 replay map
// (src/temporal/sun_shadow_apply_ps.hlsl). The pass owns only its two
// programs, the quad declaration and a D3DSBT_ALL block; the caller
// (MotionOutput's scene-end hook) supplies RT2, the map, the replay frame's
// view -> sun rows, the jittered projection latch and the target, and
// serializes rendering, Reset and teardown. Documented D3D9 only; the format
// gates are CheckDeviceFormat queries, as AmbientOcclusionPass.
#include <cstdint>
#include <d3d9.h>
#include "shadow_replay_projection.h"
namespace x3m::renderer {
struct SunShadowApplyCaps {
    bool enabled = false;
    const char* reason = "detached"; // gate name or creation failure
    HRESULT formats = S_FALSE, programs = S_FALSE;
    unsigned program_slots = 0;      // conservative ps_3_0 slot count of the embedded program
    bool cascades = false;           // the cascade program was requested, fits the device and was created
    unsigned cascade_slots = 0;
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
// Bias in world units (legacy-sun-application.md, section 2, "Bias"): the
// constant subtracted from every compare is bias_units plus one world texel
// of the map (2 half_extent / size: what a receiver-plane fit expects to be
// off by across a texel); the receiver-plane clamp, which is also the
// non-planar fallback, is clamp_texels world texels (the plane term
// extrapolates the depth slope over at most ~1.9 texels, so its bound scales
// with the texel, not with the depth range; X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS);
// both divided by the map's depth range 2 depth_half into normalized sun
// depth. The defaults resolve to exactly the pre-tunable constants (0.001
// and 0.01) at the default cascade 250 / 512 / 1024: 0.53571875 + 0.48828125
// = 1.024 = 0.001 x 1024 and 20.97152 x 0.48828125 = 10.24 = 0.01 x 1024.
// Double arithmetic, one rounding to float per value.
constexpr double sun_shadow_bias_units_default = 0.53571875, sun_shadow_bias_units_min = 0., sun_shadow_bias_units_max = 1000.;
constexpr double sun_shadow_bias_clamp_texels_default = 20.97152, sun_shadow_bias_clamp_texels_min = 1., sun_shadow_bias_clamp_texels_max = 64.;
struct SunShadowBias { float constant = 0.f, max = 0.f; float texel_world = 0.f; };
inline bool sun_shadow_apply_bias(double bias_units, double clamp_texels, double half_extent, double depth_half, unsigned size, SunShadowBias& out) noexcept {
    out = {};
    if (!(bias_units >= 0.) || !(clamp_texels > 0.) || !(half_extent > 0.) || !(depth_half > 0.) || !size) return false;
    const double texel = 2. * half_extent / double(size), constant = (bias_units + texel) / (2. * depth_half);
    out.constant = float(constant); out.max = float(clamp_texels * texel / (2. * depth_half)); out.texel_world = float(texel);
    return out.constant >= 0.f && out.max >= 0.f && out.max <= 1.f && out.constant <= 1.f;
}
// Cascades (docs/architecture/shadow-cascades.md, section 2;
// src/temporal/sun_shadow_cascade_apply_ps.hlsl): per cascade the view -> sun
// rows of the map's *retained* basis composed with the current camera, the
// map, and the bias resolved against that cascade's texel and depth range. A
// null map or valid = false is an absent cascade (lit).
struct SunShadowCascadeInput {
    IDirect3DTexture9* map = nullptr;
    float rows[12]{};
    float bias_constant = 0.f, bias_max = 0.f;
    bool valid = false;
};
struct SunShadowCascadeFrame {
    IDirect3DTexture9* depth_share = nullptr;       // RT2 G32R32F or A32B32G32R32F (as SunShadowApplyFrame)
    IDirect3DSurface9* target = nullptr;
    UINT width = 0, height = 0;
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
    unsigned jitter_index = 0;
    float exponent = 1.f, planar_step = .05f;
    unsigned count = 0;                                   // configured cascades, 1..shadow_cascade_max
    SunShadowCascadeInput cascades[shadow_cascade_max]{};
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
};
struct SunShadowApplyFrame {
    IDirect3DTexture9* depth_share = nullptr; // RT2 G32R32F or A32B32G32R32F, width x height (.r = z/w with -1 sentinel, .g = share, .b = view depth on the wide format)
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
    unsigned cascades_bound = 0;      // execute_cascades: maps sampled (valid cascades)
    bool linear_depth = false;        // the receiver depth came from RT2.b (A32B32G32R32F), not the z/w law on .r
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
    // (an A32B32G32R32F RT2 is admitted per frame; its creation by the route
    // is the lane's own CheckDeviceFormat gate, sun_share_lane_inc.h)
    // the programs and the declaration (surviving Reset). A refusal leaves
    // the pass detached with caps().reason set.
    // `cascades` additionally gates and creates the cascade program (its slot
    // count against MaxPixelShader30InstructionSlots, five samplers); without
    // it the pass is exactly the single-map pass.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format, D3DFORMAT target_format, bool cascades = false) noexcept;
    const SunShadowApplyCaps& caps() const noexcept { return caps_; }
    // One quad: capture the owned block and the bindings, normalize, upload
    // the constants, draw, restore. Any missing input returns S_FALSE with
    // result.skipped and touches no device state; a failed device call
    // restores and names its stage; a lost device stops restoration.
    HRESULT execute(const SunShadowApplyFrame&, SunShadowApplyResult*) noexcept;
    // The same transaction with the cascade program; skips with reason
    // "cascades" when the pass was attached without it and "absent" when no
    // cascade is valid (nothing to darken: the frame stays byte-identical).
    HRESULT execute_cascades(const SunShadowCascadeFrame&, SunShadowApplyResult*) noexcept;
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
    HRESULT normalize(IDirect3DSurface9* target, UINT w, UINT h, IDirect3DPixelShader9* program, UINT samplers) noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    SunShadowApplyCaps caps_{};
    D3DFORMAT target_format_ = D3DFMT_UNKNOWN;
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DPixelShader9* apply_ = nullptr;
    IDirect3DPixelShader9* cascade_apply_ = nullptr;
    IDirect3DVertexShader9* quad_vs_ = nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    UINT render_targets_ = 0, streams_ = 0;
    bool reset_pending_ = false;
};
} // namespace x3m::renderer
