#pragma once
// FP16 HDR scene path, stages 1 and 2 (docs/architecture/hdr-scene-path.md,
// sections "Stage 1 implementation" and "Stage 2 implementation"): the owned
// A16B16G16R16F scene target that replaces the game's A8R8G8B8 RT0 inside the
// scene, the attach-time capability gate with its four-format MRT self test,
// the write-back of the FP16 image into the game's real RT0 -- the stage-1
// identity tonemap (a point-sampled copy, alpha carried, values clamped by
// the 8-bit target) or, with X3M_HDR_TONEMAP=agx, the AgX transform of
// agx.hlsl (decode, clamp, exposure, look; alpha carried) -- the exposure
// meter (a 4x-per-axis log-luminance reduction chain over the FP16 target
// into a 1x1 R32F, read back one frame later through a two-surface ring and
// adapted on the host by exposure.h) and the must-unwind ladder behind the
// write-back (tonemap draw -> identity draw -> StretchRect copy -> restore the
// binding). Owned by MotionOutput, which decides WHEN to redirect, flush
// and end (the logical-binding shim and the frame policy live there); this
// class only performs device work. Every device call goes through the
// native vtable slots supplied at attach, so the proxy's hooks, the state
// shadow and the scene selector never observe them. No game shader bytes.
#include <d3d9.h>
#include <cstddef>
#include <cstdint>
#include "exposure.h"
#include "../temporal/agx.h"

namespace x3m::renderer {
// Attach-time verdict: caps.reason is "ok" or the first failed check.
struct HdrCaps {
    bool enabled = false;
    const char* reason = "off";
    HRESULT fp16_target = S_FALSE;       // CheckDeviceFormat RENDERTARGET, A16B16G16R16F texture
    HRESULT fp16_blending = S_FALSE;     // ... D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING
    HRESULT fp16_filter = S_FALSE;       // ... D3DUSAGE_QUERY_FILTER (stage 2 bloom only; logged, not a gate)
    HRESULT fp16_sampling = S_FALSE;     // ... usage 0 (the write-back samples the target)
    HRESULT stretch_conversion = S_FALSE;// CheckDeviceFormatConversion A16B16G16R16F -> main format (unwind step 2)
    bool mrt_blending = false;           // D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING (informational)
    unsigned self_test_targets = 0;      // formats the self test drew into (2 or 3)
    char self_test_detail[448] = "-";
    // Stage 2: the tonemap and the meter gate themselves inside the enabled
    // feature (a refusal falls back to the identity write-back, never
    // disables X3M_HDR). reason "ok", "off" (not requested), "shader",
    // "self_test"; meter_reason "ok", "off" (manual exposure or identity),
    // "r32f_target", "r32f_sampling", "shader", "self_test".
    bool tonemap = false;
    const char* tonemap_reason = "off";
    bool meter = false;
    const char* meter_reason = "off";
    HRESULT r32f_target = S_FALSE, r32f_sampling = S_FALSE, tonemap_shader = S_FALSE, meter_shader = S_FALSE;
};
enum class HdrTonemap : unsigned { Identity = 0, Agx = 1 };
// Stage-2 switches (X3M_HDR_TONEMAP, X3M_HDR_DECODE, X3M_HDR_LOOK,
// X3M_HDR_CLAMP, X3M_HDR_EXPOSURE, X3M_HDR_EV_MANUAL, X3M_HDR_EV and the
// exposure parameters). Defaults reproduce stage 1: identity write-back,
// nothing metered. fixed_dt > 0 replaces the QPC interval (seconds; the
// fixtures' deterministic adaptation, X3M_HDR_DT_MS).
struct HdrConfig {
    HdrTonemap tonemap = HdrTonemap::Identity;
    x3::temporal::AgxDecode decode = x3::temporal::AgxDecode::gamma22;
    x3::temporal::AgxLook look = x3::temporal::AgxLook::none;
    float clamp_max = 0.f;               // <= 0: off (65504 uploaded)
    ExposureMode exposure = ExposureMode::Auto;
    float ev_manual = 0.f;
    ExposureParams params{};
    float fixed_dt = 0.f;
};
const char* hdr_tonemap_name(HdrTonemap tonemap) noexcept;
const char* hdr_look_name(x3::temporal::AgxLook look) noexcept;
const char* hdr_decode_name(x3::temporal::AgxDecode decode) noexcept;
const char* hdr_exposure_name(ExposureMode mode) noexcept;
enum class HdrWritebackSource : unsigned { None = 0, Shader = 1, Stretch = 2, Restore = 3 };
// One write-back: which rung of the ladder produced the 8-bit image and the
// HRESULT of every attempted rung. unwind is set whenever the shader copy did
// not complete cleanly (draw or restoration), even if a later rung succeeded.
// Stage 2: `tonemap` says the AgX program produced the image; `fallback`
// that the tonemap draw failed and the identity draw was taken instead (an
// unwind, reason "tonemap"); `meter` is the chain's result when it ran
// (S_FALSE: not run), `ticks_meter` its CPU time inside the draw bracket.
struct HdrWriteback {
    HdrWritebackSource source = HdrWritebackSource::None;
    bool unwind = false;
    const char* unwind_reason = "none"; // draw | restore | stretch | bind | lost | tonemap
    HRESULT draw = S_FALSE, restore = S_FALSE, stretch = S_FALSE, bind = S_FALSE;
    std::uint64_t ticks_draw = 0, ticks_stretch = 0, ticks_bind = 0;
    bool tonemap = false, fallback = false;
    HRESULT tonemap_draw = S_FALSE, meter = S_FALSE;
    std::uint64_t ticks_meter = 0;
};
// The meter readback and adaptation step taken at a latch (begin_frame).
struct HdrFrameBegin {
    bool stepped = false;                // a meter of the previous frame was consumed
    HRESULT readback = S_FALSE;          // LockRect of the ring surface (S_FALSE: nothing pending)
    float avg_log_l = 0.f, dt = 0.f;     // what the step consumed (dt after the clamp)
    std::uint64_t ticks_readback = 0;
};
// Fault injection points of the fixture seam (verification/probe/
// motion_output_fixture.cpp, case 4 of the design's section 7): each kind
// fires `count` times. Production builds never fire one (fault() is constant
// false there); the enumeration exists in both so the call sites compile once.
enum class HdrFault : unsigned {
    None = 0, CapsTarget = 1, CapsBlending = 2, SelfTest = 3, Draw = 4, Lost = 5,
    Stretch = 6, Restore = 7, TargetCreate = 8, Bind = 9, Clear = 10, // Clear: the latching Clear reports failure (consumed by MotionOutput)
    TonemapDraw = 11, TonemapShader = 12, Meter = 13 // stage 2: the tonemap draw fails, its creation fails at attach, the chain fails
};
class HdrPass {
public:
    HdrPass() = default;
    ~HdrPass();
    HdrPass(const HdrPass&) = delete;
    HdrPass& operator=(const HdrPass&) = delete;
    // Stage-2 switches; effective at the next attach.
    void configure(const HdrConfig& config) noexcept { config_ = config; }
    const HdrConfig& config() const noexcept { return config_; }
    // Device is BORROWED. `native` is the device's original method table; every
    // call goes through it. Runs the capability gate (section 5 of the design)
    // and the self test (with_depth: FP16 + RGBA32F + R32F, else FP16 + RGBA32F)
    // outside any application scene (own BeginScene/EndScene bracket); creates
    // the write-back shader (one device reference) and, when configured, the
    // tonemap and meter programs (each gated on its own, falling back to the
    // identity write-back). On failure of the feature nothing is kept.
    void attach(IDirect3DDevice9* device, void* const* native, const D3DCAPS9& caps, D3DFORMAT main_format, bool with_depth) noexcept;
    bool enabled() const noexcept { return caps_.enabled; }
    const HdrCaps& caps() const noexcept { return caps_; }
    // The AgX write-back is in use (configured, created, self-tested and not
    // disabled after repeated draw failures); the meter chain runs with it in
    // auto exposure.
    bool tonemap_active() const noexcept { return caps_.tonemap && tonemap_shader_ && tonemap_failures_ < tonemap_failure_limit; }
    bool meter_active() const noexcept { return tonemap_active() && caps_.meter && config_.exposure == ExposureMode::Auto; }
    const ExposureState& exposure() const noexcept { return exposure_; }
    // At the latch of a frame (after the redirect bound): copies the previous
    // frame's 1x1 meter from its ring target to system memory and locks it
    // (the lagged 4-byte readback, a Present after the chain wrote it) and
    // adapts the EV the coming write-back's tonemap consumes;
    // `now_ticks`/`frequency` is the QPC stamp of this latch (dt against the
    // previous one, or fixed_dt).
    HdrFrameBegin begin_frame(std::uint64_t now_ticks, std::uint64_t frequency, bool timing) noexcept;
    // Bytes of the meter chain's default-pool levels (0 without a chain).
    std::uint64_t chain_bytes() const noexcept;
    unsigned chain_levels() const noexcept { return chain_count_ + (chain_ring_[0] ? 1u : 0u); }
    // Re-runs the self test (the recovery probe after an unwind). scene_open:
    // the application's scene is open, so no bracket of our own.
    bool recheck(bool scene_open, char* detail, std::size_t detail_size) noexcept;
    // Lazily (re)creates the FP16 target at the given size (no MSAA). Returns
    // the creation HRESULT; a failure leaves no target. Level-0 surface only is
    // retained (one device reference in both reference models).
    HRESULT ensure_target(UINT width, UINT height) noexcept;
    void release_target() noexcept;
    IDirect3DSurface9* target() const noexcept { return target_; }
    UINT width() const noexcept { return width_; }
    UINT height() const noexcept { return height_; }
    std::uint64_t target_bytes() const noexcept { return std::uint64_t(width_) * height_ * 8; }
    // SetRenderTarget(0, surface) through the native slot with the viewport
    // and scissor rectangle preserved (D3D9 resets both on a target bind).
    HRESULT bind(IDirect3DSurface9* surface, std::uint64_t* ticks) noexcept;
    // The write-back ladder: the identity copy draw of the FP16 target into
    // `main` (state saved and restored explicitly), else StretchRect(target ->
    // main, POINT) when the conversion is granted, else nothing; RT0 is left
    // bound to `final_rt0` (the target for a flush, `main` for an end) with the
    // viewport and scissor preserved. `write`: false only rebinds (nothing drew
    // into the target since the last write-back). `timing`: stamp the rungs.
    HdrWriteback write_back(IDirect3DSurface9* main, IDirect3DSurface9* final_rt0, bool scene_open, bool write, bool timing) noexcept;
    // Device references held (target surface, the shaders, the meter chain's
    // level surfaces, ring targets and readback surfaces).
    unsigned references() const noexcept;
    void before_reset() noexcept { release_target(); }
    void shutdown() noexcept;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    void set_fault(HdrFault kind, unsigned count) noexcept { fault_ = kind; fault_count_ = count; }
    bool take_fault(HdrFault kind) noexcept { return fault(kind); }
    // FP16 target as floats (4 per pixel), row-major.
    HRESULT fixture_readback(float* out, std::size_t floats, UINT* width, UINT* height) noexcept;
#endif
private:
    struct SavedState;
    // What one copy draw runs: the program (identity or tonemap), its
    // constants (c8..c21 when set) and whether the meter chain precedes it.
    struct Program {
        IDirect3DPixelShader9* shader = nullptr;
        const float* constants = nullptr;
        bool meter = false;
        HRESULT meter_result = S_FALSE;
        std::uint64_t ticks_meter = 0;
        bool timing = false;
    };
    static constexpr unsigned tonemap_failure_limit = 3;
    static constexpr unsigned chain_max_levels = 8;   // 4^8 = 65536 px per axis
    static constexpr unsigned constant_count = x3::temporal::kAgxFirstRegister + x3::temporal::kAgxRegisterCount; // c0..c21 saved
    template<class Fn> Fn call(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    bool self_test(bool with_depth, bool scene_open, char* detail, std::size_t detail_size) noexcept;
    HRESULT save(SavedState& saved) noexcept;
    HRESULT restore(const SavedState& saved, IDirect3DSurface9* rt0) noexcept;
    HRESULT copy_draw(IDirect3DSurface9* source_target, IDirect3DTexture9* source_texture, IDirect3DSurface9* destination,
                      UINT width, UINT height, IDirect3DSurface9* final_rt0, HRESULT* restoration,
                      HRESULT injected_draw, bool injected_restore, Program* program = nullptr) noexcept;
    HRESULT meter_chain(IDirect3DTexture9* scene_texture, UINT width, UINT height, HRESULT injected) noexcept;
    HRESULT ensure_chain(UINT width, UINT height) noexcept;
    void release_chain() noexcept;
    void prepare_constants() noexcept;
    HRESULT mrt_draw(IDirect3DSurface9* rt0, IDirect3DSurface9* rt1, IDirect3DSurface9* rt2, IDirect3DPixelShader9* shader,
                     UINT width, UINT height, bool additive, HRESULT* restoration) noexcept;
    std::uint64_t stamp(bool timing) const noexcept;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    bool fault(HdrFault kind) noexcept { if (fault_ != kind || !fault_count_) return false; --fault_count_; return true; }
#else
    static constexpr bool fault(HdrFault) noexcept { return false; }
#endif
    IDirect3DDevice9* device_ = nullptr;
    void* const* native_ = nullptr;
    D3DCAPS9 caps9_{};
    D3DFORMAT main_format_ = D3DFMT_UNKNOWN;
    bool with_depth_ = false;
    HdrCaps caps_{};
    HdrConfig config_{};
    IDirect3DPixelShader9* shader_ = nullptr;   // embedded ps_3_0 identity copy
    IDirect3DSurface9* target_ = nullptr;       // level 0 of the owned FP16 texture (its container is obtained per use)
    UINT width_ = 0, height_ = 0;
    UINT last_width_ = 0, last_height_ = 0;     // dimensions of the last created target (exposure reset on change)
    // Stage 2: the AgX program and its constant block, the meter programs,
    // the chain levels (level-0 surfaces of R32F textures, containers
    // obtained per use), the 1x1 ring targets and their system-memory
    // readback surfaces, the host adaptation state.
    IDirect3DPixelShader9* tonemap_shader_ = nullptr;
    IDirect3DPixelShader9* meter_level0_shader_ = nullptr;
    IDirect3DPixelShader9* meter_reduce_shader_ = nullptr;
    x3::temporal::AgxConstants agx_{};
    IDirect3DSurface9* chain_[chain_max_levels]{};
    UINT chain_width_[chain_max_levels]{}, chain_height_[chain_max_levels]{};
    unsigned chain_count_ = 0;                  // levels before the 1x1 ring
    IDirect3DSurface9* chain_ring_[2]{};        // 1x1 R32F render targets (level 0 of textures)
    IDirect3DSurface9* chain_readback_[2]{};    // 1x1 R32F system-memory surfaces
    unsigned chain_slot_ = 0;                   // ring slot the current frame's chain writes
    bool chain_pending_[2]{};                   // ring[slot] holds a meter not yet copied and consumed
    ExposureState exposure_{};
    unsigned tonemap_failures_ = 0;
    std::uint64_t latch_ticks_ = 0;             // QPC of the previous begin_frame (0: none)
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    HdrFault fault_ = HdrFault::None;
    unsigned fault_count_ = 0;
#endif
};
} // namespace x3m::renderer
