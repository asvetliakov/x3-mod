#pragma once
#include <d3d9.h>
#include <cstdint>
#include <memory>
#include "../renderer/scene_boundary.h"

namespace x3m {
// Describes a surface binding for the selector: allocation identity (private
// data), texture container identity, dimensions, format and MSAA. Unknown on
// any failed query; a null surface is a known null binding.
renderer::Surface describe_surface(IDirect3DSurface9* surface) noexcept;
// Bounded runtime adapter for the scene-boundary selector. This is diagnostic
// depth preservation, not a visual temporal pass. It runs only in requested
// capture frames, holds no application COM references, and never changes the
// application's call arguments or HRESULT. Call under the capture mutex.
class SceneCapture {
public:
    SceneCapture() noexcept;
    // Explicit profile injection supports independently authored shader fixtures
    // and future separately verified builds. The game proxy uses only defaults.
    explicit SceneCapture(const renderer::SceneSignatures& signatures) noexcept;
    ~SceneCapture();
    SceneCapture(const SceneCapture&) = delete;
    SceneCapture& operator=(const SceneCapture&) = delete;

    void configure(bool requested) noexcept;
    void begin_frame(IDirect3DDevice9* device, std::uint64_t device_id, std::uint64_t frame, bool capturing) noexcept;
    bool end_frame(HRESULT present_result) noexcept;
    // True only during the main depth-writing scene, excluding background,
    // bloom and overlays. This is a collection boundary, not color coverage.
    bool collecting_scene() const noexcept;
    void invalidate() noexcept;
    void unsupported(const char* operation, HRESULT result) noexcept;

    void before_draw(IDirect3DDevice9* device, D3DPRIMITIVETYPE topology, UINT primitives) noexcept;
    void after_draw(HRESULT result) noexcept;
    renderer::Selection before_clear(IDirect3DDevice9* device, DWORD count, const D3DRECT* rects, DWORD flags,
                                     float depth) noexcept;
    bool after_clear(IDirect3DDevice9* device, HRESULT result) noexcept;
    void after_set_rt(IDirect3DDevice9* device, DWORD index, HRESULT result) noexcept;
    void after_set_depth(IDirect3DDevice9* device, HRESULT result) noexcept;
    void after_stretch(IDirect3DDevice9* device, IDirect3DSurface9* source, const RECT* source_rect,
                       IDirect3DSurface9* destination, const RECT* destination_rect, HRESULT result) noexcept;
    void after_color_fill(IDirect3DDevice9* device, IDirect3DSurface9* destination, const RECT* rect,
                          HRESULT result) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    renderer::SceneSignatures signatures_{};
    bool requested_ = false;
};
} // namespace x3m
