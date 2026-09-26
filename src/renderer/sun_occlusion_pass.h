#pragma once
// Partial sun occlusion, step 1 (docs/architecture/sun-partial-occlusion.md): the GPU half.
//
//   execute()     once per frame, at the start of the lens bracket: one 1-pixel quad that counts
//                 the open taps of the route's RT2 over the sun's disc and smooths the fraction
//                 against the previous result. Two 1x1 A16B16G16R16F render-target textures,
//                 ping-pong (no blending on FP16, no readback, no query, no stall).
//   lens_prepare() per lens-scene draw inside the bracket: names the vertex program's clip rows so the
//                 caller can classify the body (step 2, sun_occlusion::core::classify_body).
//   lens_begin()  per lens-scene draw inside the bracket: substitutes a wrapped copy of the bound
//   lens_end()    pixel shader (lens_visibility_variant.h) that multiplies oC0 by the fraction (a
//                 ghost), or the clip pair (a core body: the vertex wrap emitting the screen position
//                 and the pixel wrap clipping per pixel against RT2), binds the current 1x1 texture (and
//                 RT2) to the samplers the wrap chose, and puts the application's shaders, textures and
//                 sampler states back after the draw.
//
// The pass owns its programs, the quad declaration, recorded state blocks (no D3DSBT_ALL), the two targets, the
// wrapped shaders (per pixel program, and per vertex / pixel pair) and (diagnostic / fixture only) one 1x1
// system-memory surface. Device and native table are borrowed (no AddRef); the caller serializes rendering, Reset and
// teardown. Documented D3D9 only; the format gate is a CheckDeviceFormat query. Pattern of SunShadowApplyPass. Nothing
// allocates per draw: the wrap of a program is built once.
#include <cstdint>
#include <vector>
#include <d3d9.h>
#include "../proxy/sun_occlusion_core.h"
#include "lens_visibility_variant.h"
namespace x3m::renderer {
struct SunOcclusionCaps {
    bool enabled = false;
    const char* reason = "detached"; // gate name or creation failure
    HRESULT formats = S_FALSE, programs = S_FALSE;
    unsigned program_slots = 0;
};
struct SunVisibilityFrame {
    IDirect3DTexture9* depth = nullptr; // RT2 of this frame: R32F, G32R32F or A32B32G32R32F (.r = device depth, -1
                                        // sentinel)
    float u = .5f, v = .5f, radius_u = 0.f, radius_v = 0.f;
    // This frame's TAA jitter in RT2 uv (+jx / width, +jy / height; sun_occlusion::core::jitter_uv): RT2 is on the
    // jittered raster while u / v are unjittered, so every tap reads RT2 at its position + jitter. 0 when off.
    float jitter_u = 0.f, jitter_v = 0.f;
    float alpha = 1.f; // sun_occlusion::core::smoothing_alpha
    float curve = 1.f; // use exponent, 0.25..4
    bool seed = false; // a new record: no smoothing against the history
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
};
enum class SunOcclusionStage : unsigned {
    None,
    Validate,
    Targets,
    Block,
    Capture,
    Normalize,
    Scene,
    Constants,
    Draw,
    EndScene,
    Restore
};
struct SunVisibilityResult {
    HRESULT operation = S_FALSE, restore = S_FALSE;
    SunOcclusionStage failed = SunOcclusionStage::None;
    bool ran = false, seeded = false;
    bool skipped = false;            // nothing touched; execute returned S_FALSE
    const char* skipped_reason = ""; // detached, reset_pending, input, params, format, device
    unsigned device_calls = 0;       // device and state-block calls of this transaction (diagnostic;
                                     // docs/verification/sun-occlusion.md)
};
enum class LensVerdict : std::uint8_t {
    Applied,
    NotReady,
    NoShader,
    Unhashed,
    Blend,
    Variant,
    CacheFull,
    Device,
    Body
};
const char* lens_verdict_name(LensVerdict) noexcept;
// The application's state for one lens draw, served by the caller (the proxy's shadow state; the
// fixture reads it with getters): no device getter runs per draw. `known` is false when any of it
// is not known exactly (a state block is recording, a shadow entry is unset): the draw is refused.
struct LensState {
    bool known = false;
    x3m::sun_occlusion::core::BlendState blend{};
    IDirect3DPixelShader9* shader = nullptr; // the bound program (borrowed; read once, when its wrap is built), null =
                                             // fixed function
    std::uint64_t hash = 0;                  // the caller's identity of that program's bytecode, non-zero
    // Step 2: the bound vertex program, the body's class (core::classify_body, from the matrix register
    // lens_prepare returns) and this frame's RT2 (borrowed for the bracket) with its size.
    IDirect3DVertexShader9* vertex_shader = nullptr;
    std::uint64_t vertex_hash = 0;
    x3m::sun_occlusion::core::Body body = x3m::sun_occlusion::core::Body::Unknown;
    IDirect3DTexture9* depth = nullptr;
    unsigned depth_width = 0, depth_height = 0;
};
// What lens_begin changed, for lens_end. Trivially copyable; lives in the caller's draw record.
struct LensDraw {
    bool applied = false, clipped = false;
    std::uint8_t sampler = 0, depth_sampler = 0, block = 0;
};
class SunOcclusionPass {
public:
    SunOcclusionPass() = default;
    ~SunOcclusionPass();
    SunOcclusionPass(const SunOcclusionPass&) = delete;
    SunOcclusionPass& operator=(const SunOcclusionPass&) = delete;
    // Gates shader model 3, the program's slot count and A16B16G16R16F / R32F render-target
    // textures, then creates the programs and the declaration (surviving Reset).
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    const SunOcclusionCaps& caps() const noexcept { return caps_; }
    // Missing or invalid input returns S_FALSE with result.skipped and touches no device state;
    // a failed device call restores, names its stage and invalidates the fraction.
    HRESULT execute(const SunVisibilityFrame&, SunVisibilityResult*) noexcept;
    bool valid() const noexcept { return valid_; } // the current 1x1 holds a result of this device generation
    void invalidate() noexcept { valid_ = false; } // the next execute seeds; lens draws are NotReady until then
    // Step 2, before the body is classified: scans the vertex program once per vertex / pixel pair (no shader is
    // created here: other records' bodies and the log-only mode never build a wrap) and returns its matrix register
    // (c[K..K+3] = the clip rows) and whether the local origin maps to the rows' .w column, so the caller can
    // classify the body from its constant shadow. Applied with `out` filled; Variant: the vertex program is not
    // the four-dp4 shape (reason in last_variant_refusal()); NotReady / NoShader / Unhashed / CacheFull as
    // lens_begin. `first` is true the one time this pair was scanned (once-per-pair logging).
    struct Prepared {
        unsigned matrix_register = ~0u;
        bool origin_known = false, first = false;
    };
    LensVerdict lens_prepare(const LensState& state, Prepared* out) noexcept;
    // X3M_SUN_OCCLUSION_CORE_F: a clipped core body is also multiplied by the fraction (default on with the override;
    // =0 = clipped only).
    void set_core_fraction(bool on) noexcept { core_f_ = on; }
    // Captures the application's shaders, the wrap samplers' textures and states into a recorded block
    // (which holds the references), applies ours, binds the fraction and the wrap. Body::Ghost: the
    // step-1 pixel wrap, five calls. Body::Core: the clip pair (vertex wrap, clip pixel wrap, RT2 on a
    // second sampler), seven calls. Body::Other / Unknown: LensVerdict::Body, nothing touched.
    LensVerdict lens_begin(const LensState& state, LensDraw& out) noexcept;
    HRESULT lens_end(LensDraw& draw) noexcept; // one Apply of that block
    unsigned last_device_calls() const noexcept {
        return device_calls_;
    } // of the last execute, or of the last lens_begin .. lens_end
    const char* last_variant_refusal() const noexcept { return variant_refusal_; }
    // Diagnostic and fixture only: a synchronous 1x1 GetRenderTargetData of the current result.
    HRESULT readback(float out[4]) noexcept;
    void before_reset() noexcept; // releases the targets, the block and the readback surface; refuses until
                                  // after_reset(SUCCEEDED)
    void after_reset(HRESULT) noexcept;
    void detach() noexcept; // full teardown including programs and wrapped shaders
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept; // persistent interfaces held
    unsigned variants() const noexcept { return variant_count_; }
    unsigned pairs() const noexcept { return pair_count_; }

private:
    struct SavedState;
    struct Variant {
        std::uint64_t hash = 0;
        IDirect3DPixelShader9* shader[3]{};
        std::uint8_t state[3]{}, sampler[3]{}, constant[3]{};
    }; // state: 0 unbuilt, 1 ready, 2 refused
    // Step 2: per vertex / pixel pair, the vertex wrap (one), the clip pixel wraps (per scale mode) and the
    // layout they share. `state`: 0 unbuilt, 1 ready, 2 refused (final for the pass). The soft-edge offsets
    // are baked for `width` x `height`; another RT2 size rebuilds the pixel wraps once.
    struct Pair {
        std::uint64_t vertex_hash = 0, pixel_hash = 0;
        IDirect3DVertexShader9* vertex = nullptr;
        IDirect3DPixelShader9* pixel[3]{};
        std::uint8_t state = 0, vertex_state = 0, pixel_state[3]{}, texcoord = 0, sampler[3]{}, depth_sampler[3]{},
                     constants[3][4]{}; // state: the scan; vertex_state: the created wrap
        bool origin_known = false, core_f = false;
        unsigned matrix_register = 0, width = 0, height = 0;
    };
    static constexpr unsigned variant_capacity = 48, pair_capacity = 16, block_capacity = 8;
    // The recorded pair of blocks a wrap needs, keyed by what it touches: the fraction sampler, the depth sampler (255
    // for the step-1 wrap) and the wrap's `def` registers (255 = none). `saved` is re-captured per draw: the
    // application's pixel shader (and vertex shader for the clip), the samplers' textures and six states each, and the
    // constant registers the wrap's `def`s overwrite on native D3D9 when the program is set; `ours` holds the six
    // sampler states per sampler.
    struct Blocks {
        std::uint8_t sampler = 255, depth_sampler = 255, constants[4] = {255, 255, 255, 255};
        IDirect3DStateBlock9* saved = nullptr;
        IDirect3DStateBlock9* ours = nullptr;
    };
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++device_calls_;
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT ensure_targets() noexcept;
    template <class Sets> HRESULT record(IDirect3DStateBlock9** out, Sets&& sets) noexcept;
    HRESULT record_quad() noexcept;
    HRESULT ensure_blocks() noexcept;
    HRESULT ensure_blocks(unsigned sampler, unsigned depth_sampler, const std::uint8_t constants[4],
                          unsigned* index) noexcept;
    Variant* find(std::uint64_t hash) noexcept;
    bool build(Variant&, unsigned mode, IDirect3DPixelShader9* original) noexcept;
    Pair* find_pair(std::uint64_t vertex_hash, std::uint64_t pixel_hash) noexcept;
    bool read_function(IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps, std::vector<std::uint32_t>& out) noexcept;
    bool scan_pair(Pair&, const LensState& state) noexcept;
    bool build_pair_vertex(Pair&, const LensState& state) noexcept;
    bool build_pair_pixel(Pair&, unsigned mode, const LensState& state) noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    SunOcclusionCaps caps_{};
    IDirect3DStateBlock9* normal_ = nullptr; // the quad's state set with the quad's values
    IDirect3DStateBlock9* saved_ = nullptr;  // the same set, re-captured per frame
    mutable unsigned device_calls_ = 0;
    IDirect3DPixelShader9* program_ = nullptr;
    IDirect3DVertexShader9* quad_vs_ = nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    IDirect3DTexture9* textures_[2]{};
    IDirect3DSurface9* surfaces_[2]{};
    IDirect3DSurface9* readback_ = nullptr;
    Variant variant_[variant_capacity]{};
    unsigned variant_count_ = 0;
    Pair pair_[pair_capacity]{};
    unsigned pair_count_ = 0;
    Blocks blocks_[block_capacity]{};
    std::vector<std::uint32_t> words_, wrapped_, vertex_words_; // build scratch, reused
    const char* variant_refusal_ = "";
    UINT render_targets_ = 0, streams_ = 0;
    unsigned current_ = 0;
    bool valid_ = false, reset_pending_ = false, core_f_ = false;
};
} // namespace x3m::renderer
