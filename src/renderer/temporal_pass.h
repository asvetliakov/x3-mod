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
    IDirect3DTexture9* color = nullptr; // scene-linear FP16 texture, native, complete local viewport
    // A8R8G8B8/X8R8G8B8 default-pool render-target surface (need not be a texture
    // level). Copied once by StretchRect into an owned FP16 scratch that becomes
    // s0; no gamma conversion is applied anywhere (values are linear-encoded).
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
    // Raster pixels, positive Y down. The camera path adds previous jitter after
    // reprojection; the motion path adds it to the producer's unjittered UV. The
    // current jitter is used by the camera path only: the motion texture is
    // rasterized on the current jittered grid already.
    float current_jitter[2]{}, previous_jitter[2]{};
    float weight = .9f;
    float rejection[4]{.0001f, 0.f, 65000.f, .000001f};
    MotionPolicy motion_policy = MotionPolicy::Unavailable;
    // Unknown coverage produces current-only output and cannot establish usable
    // history. RequiredMask demands complete conservative visible RGB coverage,
    // independent of alpha; missing/wrong input rejects the run.
    ReactivePolicy reactive_policy = ReactivePolicy::Unavailable;
    bool history_allowed = false, camera_cut = false;
    bool cut = false; // route cut detector verdict for this frame; rejects history like camera_cut
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
};
struct Diagnostics {
    HRESULT operation = S_OK, restoration = S_OK;
    bool history_valid = false;
    bool reset_pending = false; // before_reset seen, after_reset(SUCCEEDED) not yet
    std::uint64_t completed_frames = 0;
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
    HRESULT initialize(IDirect3DDevice9* native_device, const DWORD* decoder, const DWORD* resolve) noexcept;
    HRESULT run(const FrameInputs&, Output*) noexcept;
    void invalidate() noexcept;
    // Reset protocol, mirroring MotionOutput: before_reset releases every
    // default-pool object (histories, masks, scratch) and keeps the compiled
    // shaders and device; run is refused until after_reset reports a successful
    // Reset, after which resources are re-created lazily on the next run.
    void before_reset() noexcept;
    void after_reset(HRESULT result) noexcept;
    void shutdown() noexcept; // full teardown, including shaders
    Diagnostics diagnostics() const noexcept { return diagnostics_; }
private:
    HRESULT allocate(UINT width, UINT height, bool reactive) noexcept;
    HRESULT ensure_scratch() noexcept;
    void release_history() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    IDirect3DPixelShader9 *decoder_ = nullptr, *resolve_ = nullptr;
    IDirect3DTexture9* colors_[2]{};
    IDirect3DTexture9* depths_[2]{};
    IDirect3DTexture9* reactive_[2]{};
    IDirect3DTexture9* scratch_ = nullptr; // FP16 copy of an 8-bit color_surface input
    IDirect3DSurface9* color_surfaces_[2]{};
    IDirect3DSurface9* depth_surfaces_[2]{};
    IDirect3DSurface9* reactive_surfaces_[2]{};
    IDirect3DSurface9* scratch_surface_ = nullptr;
    UINT width_ = 0, height_ = 0, render_targets_ = 0, streams_ = 0, current_ = 0;
    std::uint64_t generation_ = 0;
    x3::temporal::HistoryState history_{};
    ReactivePolicy reactive_policy_ = ReactivePolicy::Unavailable;
    Diagnostics diagnostics_{};
};
} // namespace x3m::renderer
