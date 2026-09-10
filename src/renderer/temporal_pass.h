#pragma once
#include <d3d9.h>
#include <cstdint>
#include "../temporal/resolve.h"

namespace x3m::renderer {
enum class MotionPolicy { Unavailable, KnownCameraOnly, PerPixel };
struct FrameInputs {
    IDirect3DTexture9* color = nullptr; // scene-linear FP16, native, complete local viewport
    IDirect3DTexture9* depth_snapshot = nullptr; // verified native D24X8 comparison snapshot
    IDirect3DTexture9* motion = nullptr; // RGBA32F if PerPixel; alpha ABI in temporal/README
    UINT width = 0, height = 0;
    std::uint64_t epoch = 0; // stable camera/scene/resource regime, not frame/clear count
    float clip_to_previous[16]{}; // unjittered, row-major, column-vector multiplication
    float current_jitter[2]{}, previous_jitter[2]{}; // raster pixels, positive Y down
    float weight = .9f;
    float rejection[4]{.0001f, 0.f, 65000.f, .000001f};
    MotionPolicy motion_policy = MotionPolicy::Unavailable;
    bool history_allowed = false, camera_cut = false;
    bool caller_scene_open = true;
    bool caller_stateblock_recording = false;
    bool caller_queries_idle = false; // positive knowledge: no active occlusion/statistics query
};
struct Output {
    // Borrowed native textures; no AddRef. Valid only until next run, invalidate,
    // before_reset, shutdown or destruction. Never retain across those boundaries.
    IDirect3DTexture9* color = nullptr;
    IDirect3DTexture9* depth = nullptr;
    std::uint64_t generation = 0;
    bool used_history = false;
};
struct Diagnostics {
    HRESULT operation = S_OK, restoration = S_OK;
    bool history_valid = false;
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
    void before_reset() noexcept; // releases all resources, including shaders; reinitialize afterwards
    void shutdown() noexcept;
    Diagnostics diagnostics() const noexcept { return diagnostics_; }
private:
    HRESULT allocate(UINT width, UINT height) noexcept;
    void release_history() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    IDirect3DPixelShader9 *decoder_ = nullptr, *resolve_ = nullptr;
    IDirect3DTexture9* colors_[2]{};
    IDirect3DTexture9* depths_[2]{};
    IDirect3DSurface9* color_surfaces_[2]{};
    IDirect3DSurface9* depth_surfaces_[2]{};
    UINT width_ = 0, height_ = 0, render_targets_ = 0, streams_ = 0, current_ = 0;
    std::uint64_t generation_ = 0;
    x3::temporal::HistoryState history_{};
    Diagnostics diagnostics_{};
};
} // namespace x3m::renderer
