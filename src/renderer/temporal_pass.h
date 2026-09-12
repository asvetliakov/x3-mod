#pragma once
#include <d3d9.h>
#include <cstdint>
#include "../temporal/resolve.h"

namespace x3m::renderer {
enum class MotionPolicy { Unavailable, KnownCameraOnly, PerPixel };
// DerivedFromDepthSentinel needs no mask texture: a current pixel whose R32F
// depth is the -1 sentinel (no routed opaque draw wrote it) resolves
// current-only, and a sentinel previous tap contributes no energy. It requires
// the direct R32F depth input; the D24X8 decoder cannot carry the sentinel.
enum class ReactivePolicy { Unavailable, KnownNonReactive, RequiredMask, DerivedFromDepthSentinel };
struct FrameInputs {
    // Current color: exactly one of the two.
    // A16B16G16R16F texture at the frame size, native, complete local
    // viewport: the HDR route's FP16 scene target (stage 3 of the HDR scene
    // path) or a fixture's exact FP16 image. Sampled directly as s0: no copy,
    // no format conversion, no gate. The resolved output is Output::color (an
    // owned FP16 history texture); the caller decides what consumes it (the
    // HDR write-back samples it; the 8-bit route copies it back).
    IDirect3DTexture9* color = nullptr;
    // A8R8G8B8/X8R8G8B8 default-pool render-target surface (need not be a texture
    // level). Copied once into an owned FP16 scratch that becomes s0: by a
    // format-converting StretchRect (configure_copy(false), the default) or,
    // where the device does not grant that conversion (configure_copy(true)),
    // by a same-format StretchRect into an owned staging texture and one
    // identity draw; no gamma conversion is applied anywhere (values are
    // linear-encoded). In draw mode the pass also draws the new history back
    // into this surface after the resolve (Output::display_written), so the
    // caller's copy-back StretchRect is not needed.
    IDirect3DSurface9* color_surface = nullptr;
    // Current depth: exactly one of the two.
    IDirect3DTexture9* depth_snapshot = nullptr; // verified native D24X8 comparison snapshot; runs the decoder draw
    // R32F device depth z/w in [0,1]; -1 sentinel where nothing routed wrote.
    // Copied into the owned depth history; no decoder draw runs.
    IDirect3DTexture9* current_depth = nullptr;
    IDirect3DTexture9* motion = nullptr; // RGBA32F if PerPixel; alpha ABI in temporal/README
    IDirect3DTexture9* reactive = nullptr; // R32F if RequiredMask; exactly 0 safe, all else reactive
    UINT width = 0, height = 0;
    std::uint64_t epoch = 0; // stable camera/scene/resource regime, not frame/clear count
    float clip_to_previous[16]{}; // unjittered, row-major, column-vector multiplication
    // Raster pixels, positive Y down. The camera path subtracts the current
    // jitter before the inverse projection (the motion texture is rasterized on
    // the current jittered grid already, so the motion path uses it nowhere).
    // The previous jitter is uploaded for ABI stability only: the resolve never
    // adds it (history is on the unjittered grid; the producer's RG is already
    // the previous unjittered UV).
    float current_jitter[2]{}, previous_jitter[2]{};
    float weight = .9f;
    float rejection[4]{.0001f, .02f, 65000.f, .000001f}; // max(absolute, relative*depth) depth tolerance, HDR limit, minimum W
    // k of the resolve's reversible luminance weighting (resolve.h, c22.x):
    // 0 (the default, the 8-bit route) is the exact identity; the HDR route
    // uploads the exposure multiplier its write-back applies to the resolved
    // image, so the weighted domain is the display-relative luminance. Finite,
    // 0 <= k <= 65504; run refuses anything else.
    float luminance_k = 0.f;
    // Post-resolve sharpen of the display image (sharpen.h, rcas.hlsl;
    // docs/architecture/temporal-integration.md "Post-resolve sharpen"): 0
    // (the default) draws nothing and the run is bit-identical to a run
    // without the field; in (0, 1] the pass draws RCAS of its freshly written
    // FP16 history INTO color_surface (the game's 8-bit target) after the
    // resolve, replacing the caller's copy-back (Output::display_written).
    // The history itself is never sharpened. Requires color_surface (the FP16
    // input path has no 8-bit destination here: the HDR write-back sharpens)
    // and a pass initialised with the sharpen program; anything else refuses
    // the run. Finite, 0 <= sharpen <= 1.
    float sharpen = 0.f;
    MotionPolicy motion_policy = MotionPolicy::Unavailable;
    // Unknown coverage produces current-only output and cannot establish usable
    // history. RequiredMask demands complete conservative visible RGB coverage,
    // independent of alpha; missing/wrong input rejects the run.
    ReactivePolicy reactive_policy = ReactivePolicy::Unavailable;
    bool history_allowed = false, camera_cut = false;
    bool cut = false; // route cut detector verdict for this frame; rejects history like camera_cut
    // With DerivedFromDepthSentinel: reproject sentinel pixels through
    // clip_to_previous at the far plane instead of resolving them current-only.
    // Only correct when clip_to_previous is a real camera reprojection (the
    // route uploads identity today, so it leaves this false).
    bool sentinel_camera = false;
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
    bool caller_queries_idle = false; // positive knowledge: no active occlusion/statistics query
};
struct Output {
    // Borrowed native objects; no AddRef. Valid only until next run, invalidate,
    // before_reset, shutdown or destruction. Never retain across those boundaries.
    IDirect3DTexture9* color = nullptr;
    IDirect3DTexture9* depth = nullptr;
    std::uint64_t generation = 0;
    bool used_history = false;
    IDirect3DTexture9* reactive = nullptr; // owned snapshot if RequiredMask; same borrowing rules
    // Level 0 of color, for the caller's copy-back StretchRect into the 8-bit
    // main target. The caller owns state save/restore around the whole
    // copy / run / copy-back sequence; run restores only what it touched.
    IDirect3DSurface9* color_surface = nullptr;
    // FrameInputs::sharpen > 0, or the copy-by-draw mode with an 8-bit input:
    // the pass drew the display image (sharpened, or the identity copy of the
    // new history) into FrameInputs::color_surface itself; the caller must
    // not copy back.
    bool display_written = false;
    // The sharpened draw's result: S_FALSE when not requested, S_OK when it
    // drew (display_written), otherwise the failure that kept the resolve
    // (the history set is published regardless) and left the display to the
    // caller's copy-back; a lost device fails the run instead.
    HRESULT sharpen_result = S_FALSE;
    // The copy-by-draw write-back's result: S_FALSE when not requested (stretch
    // mode, an FP16 input, or the sharpen draw already wrote the display),
    // S_OK when it drew (display_written), otherwise the failure that left the
    // display to the caller's copy-back; a lost device fails the run instead.
    HRESULT copy_result = S_FALSE;
};
struct Diagnostics {
    HRESULT operation = S_OK, restoration = S_OK;
    bool history_valid = false;
    bool reset_pending = false; // before_reset seen, after_reset(SUCCEEDED) not yet
    std::uint64_t completed_frames = 0;
    // CPU-side QueryPerformanceCounter ticks of the last run's phases, taken
    // only with configure_timing(true); zero otherwise. Wall clock around the
    // device calls (submission cost, driver work, any blocking), never GPU
    // execution time. The five phases nest inside run and do not overlap.
    bool timed = false;
    std::uint64_t ticks_capture = 0;    // state block Capture plus the binding getters
    std::uint64_t ticks_copy_color = 0; // 8-bit color to FP16 scratch StretchRect, or the same-format staging copy in draw mode (allocations included)
    std::uint64_t ticks_copy_depth = 0; // R32F depth to depth history StretchRect
    std::uint64_t ticks_draw = 0;       // normalize, scene bracket, decoder/resolve/mask quads
    std::uint64_t ticks_apply = 0;      // binding restoration plus state block Apply
};
class TemporalPass {
public:
    TemporalPass() = default;
    ~TemporalPass();
    TemporalPass(const TemporalPass&) = delete;
    TemporalPass& operator=(const TemporalPass&) = delete;
    // Device is BORROWED, never AddRef'd. Bytecode consumed synchronously by D3D.
    // Caller serializes rendering/reset and invokes before_reset/shutdown before
    // native Reset or final device teardown. No wrapper refs, global maps or cycles.
    // `native_vtable`, when given, is the device's ORIGINAL method table: every
    // device call of the pass then goes through those slots instead of the
    // object's current vtable, so a caller that hooked the device (the proxy)
    // never observes the pass's own calls. Without it the object's vtable is
    // read at every call. `decoder` may be null; the D24X8 snapshot input is
    // then refused (the route supplies R32F depth and needs no decoder).
    // `sharpen` (ps_3_0 bytecode of src/temporal/taa_sharpen_ps.hlsl) may be
    // null; FrameInputs::sharpen > 0 is then refused. `copy` (the ps_3_0
    // identity program, src/temporal/hdr_writeback_ps.hlsl) may be null;
    // configure_copy(true) is then refused at run. Every quad the pass draws
    // binds the embedded vs_3_0 pass-through (quad_vertex_program.h) and its
    // declaration, both created here and surviving Reset.
    HRESULT initialize(IDirect3DDevice9* native_device, const DWORD* decoder, const DWORD* resolve,
                       void* const* native_vtable = nullptr, const DWORD* sharpen = nullptr, const DWORD* copy = nullptr) noexcept;
    // How an 8-bit color_surface input reaches the FP16 scratch and how the
    // resolved history reaches it again: false (default) by format-converting
    // StretchRect both ways (the caller copies back); true by a same-format
    // StretchRect into an owned staging texture plus identity draws both ways
    // (Output::display_written). The route decides per device from its
    // attach-time round-trip self test (docs/architecture/platform-portability.md).
    void configure_copy(bool by_draw) noexcept { copy_by_draw_ = by_draw; }
    bool copy_by_draw() const noexcept { return copy_by_draw_; }
    HRESULT run(const FrameInputs&, Output*) noexcept;
    void invalidate() noexcept;
    // Reset protocol, mirroring MotionOutput: before_reset releases every
    // default-pool object (histories, masks, scratch, the cached state block)
    // and keeps the compiled shaders and device; run is refused until
    // after_reset reports a successful Reset, after which resources are
    // re-created lazily on the next run.
    void before_reset() noexcept;
    void after_reset(HRESULT result) noexcept;
    void shutdown() noexcept; // full teardown, including shaders
    Diagnostics diagnostics() const noexcept { return diagnostics_; }
    // Phase timing of run (Diagnostics::ticks_*): off by default; costs five
    // QPC pairs per run when on and changes no device call.
    void configure_timing(bool enabled) noexcept { timing_ = enabled; }
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept {
        return reinterpret_cast<Fn>((vtable_ ? vtable_ : *reinterpret_cast<void* const* const*>(device_))[slot]);
    }
    HRESULT allocate(UINT width, UINT height, bool reactive) noexcept;
    HRESULT ensure_scratch() noexcept;
    HRESULT ensure_staging(D3DFORMAT format) noexcept;
    HRESULT ensure_block() noexcept;
    HRESULT normalize(UINT w, UINT h) noexcept;
    HRESULT quad(UINT w, UINT h) noexcept;
    void release_history() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr;
    // One D3DSBT_ALL block per device generation: captured before and applied
    // after every run instead of being created per frame. Default-pool-like:
    // released before Reset and re-created lazily afterwards.
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DPixelShader9 *decoder_ = nullptr, *resolve_ = nullptr, *sharpen_ = nullptr, *copy_ = nullptr;
    IDirect3DVertexShader9* quad_vs_ = nullptr;          // vs_3_0 pass-through of every quad (survives Reset)
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    IDirect3DTexture9* colors_[2]{};
    IDirect3DTexture9* depths_[2]{};
    IDirect3DTexture9* reactive_[2]{};
    IDirect3DTexture9* scratch_ = nullptr; // FP16 copy of an 8-bit color_surface input
    IDirect3DTexture9* staging_ = nullptr; // draw mode: same-format copy of the 8-bit input the identity draw samples
    IDirect3DSurface9* staging_surface_ = nullptr;
    D3DFORMAT staging_format_ = D3DFMT_UNKNOWN;
    IDirect3DSurface9* color_surfaces_[2]{};
    IDirect3DSurface9* depth_surfaces_[2]{};
    IDirect3DSurface9* reactive_surfaces_[2]{};
    IDirect3DSurface9* scratch_surface_ = nullptr;
    UINT width_ = 0, height_ = 0, render_targets_ = 0, streams_ = 0, current_ = 0;
    std::uint64_t generation_ = 0;
    x3::temporal::HistoryState history_{};
    ReactivePolicy reactive_policy_ = ReactivePolicy::Unavailable;
    Diagnostics diagnostics_{};
    bool timing_ = false;
    bool copy_by_draw_ = false;
    bool quad_fvf_ = false; // fixture-only XYZRHW twin (X3M_QUAD_FVF_SWITCH builds); always false in production
};
} // namespace x3m::renderer
