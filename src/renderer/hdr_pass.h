#pragma once
// FP16 HDR scene path, stage 1 (docs/architecture/hdr-scene-path.md, section
// "Stage 1 implementation"): the owned A16B16G16R16F scene target that
// replaces the game's A8R8G8B8 RT0 inside the scene, the attach-time
// capability gate with its four-format MRT self test, the write-back of the
// FP16 image into the game's real RT0 with the stage-1 identity tonemap (a
// point-sampled copy, alpha carried, values clamped by the 8-bit target) and
// the must-unwind ladder behind it (shader copy -> StretchRect copy -> restore
// the binding). Owned by MotionOutput, which decides WHEN to redirect, flush
// and end (the logical-binding shim and the frame policy live there); this
// class only performs device work. Every device call goes through the
// native vtable slots supplied at attach, so the proxy's hooks, the state
// shadow and the scene selector never observe them. No game shader bytes.
#include <d3d9.h>
#include <cstddef>
#include <cstdint>

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
    char self_test_detail[320] = "-";
};
enum class HdrWritebackSource : unsigned { None = 0, Shader = 1, Stretch = 2, Restore = 3 };
// One write-back: which rung of the ladder produced the 8-bit image and the
// HRESULT of every attempted rung. unwind is set whenever the shader copy did
// not complete cleanly (draw or restoration), even if a later rung succeeded.
struct HdrWriteback {
    HdrWritebackSource source = HdrWritebackSource::None;
    bool unwind = false;
    const char* unwind_reason = "none"; // draw | restore | stretch | bind | lost
    HRESULT draw = S_FALSE, restore = S_FALSE, stretch = S_FALSE, bind = S_FALSE;
    std::uint64_t ticks_draw = 0, ticks_stretch = 0, ticks_bind = 0;
};
// Fault injection points of the fixture seam (verification/probe/
// motion_output_fixture.cpp, case 4 of the design's section 7): each kind
// fires `count` times. Production builds never fire one (fault() is constant
// false there); the enumeration exists in both so the call sites compile once.
enum class HdrFault : unsigned {
    None = 0, CapsTarget = 1, CapsBlending = 2, SelfTest = 3, Draw = 4, Lost = 5,
    Stretch = 6, Restore = 7, TargetCreate = 8, Bind = 9, Clear = 10 // Clear: the latching Clear reports failure (consumed by MotionOutput)
};
class HdrPass {
public:
    HdrPass() = default;
    ~HdrPass();
    HdrPass(const HdrPass&) = delete;
    HdrPass& operator=(const HdrPass&) = delete;
    // Device is BORROWED. `native` is the device's original method table; every
    // call goes through it. Runs the capability gate (section 5 of the design)
    // and the self test (with_depth: FP16 + RGBA32F + R32F, else FP16 + RGBA32F)
    // outside any application scene (own BeginScene/EndScene bracket); creates
    // the write-back shader (one device reference). On failure nothing is kept.
    void attach(IDirect3DDevice9* device, void* const* native, const D3DCAPS9& caps, D3DFORMAT main_format, bool with_depth) noexcept;
    bool enabled() const noexcept { return caps_.enabled; }
    const HdrCaps& caps() const noexcept { return caps_; }
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
    // Device references held (target surface, write-back shader).
    unsigned references() const noexcept { return (target_ ? 1u : 0u) + (shader_ ? 1u : 0u); }
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
    template<class Fn> Fn call(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    bool self_test(bool with_depth, bool scene_open, char* detail, std::size_t detail_size) noexcept;
    HRESULT save(SavedState& saved) noexcept;
    HRESULT restore(const SavedState& saved, IDirect3DSurface9* rt0) noexcept;
    HRESULT copy_draw(IDirect3DSurface9* source_target, IDirect3DTexture9* source_texture, IDirect3DSurface9* destination,
                      UINT width, UINT height, IDirect3DSurface9* final_rt0, HRESULT* restoration,
                      HRESULT injected_draw, bool injected_restore) noexcept;
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
    IDirect3DPixelShader9* shader_ = nullptr;   // embedded ps_3_0 identity copy
    IDirect3DSurface9* target_ = nullptr;       // level 0 of the owned FP16 texture (its container is obtained per use)
    UINT width_ = 0, height_ = 0;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    HdrFault fault_ = HdrFault::None;
    unsigned fault_count_ = 0;
#endif
};
} // namespace x3m::renderer
