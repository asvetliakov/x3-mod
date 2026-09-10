#pragma once
#include <d3d9.h>
#include <cstdint>

// Experimental normal-D3D9 ownership boundary. Not connected to the installed
// capture proxy. Application COM references are separate from renderer-owned
// backend resources, so persistent history cannot keep its own owner alive.
namespace x3m::ownership {

struct Options {
    // Initial automatic D24X8, single-sample surfaces only. Default is inert.
    // Copied into each device before its first application clear or draw.
    bool sampleable_auto_depth = false;
};

// On success, consumes exactly the caller's owned native reference. On failure,
// the caller retains it. Native Ex factories are rejected before wrapper mode.
HRESULT wrap_factory(IDirect3D9* owned_native, IDirect3D9** out,
                     const Options& options = {}) noexcept;

struct DepthView {
    IDirect3DTexture9* texture = nullptr; // Borrowed native INTZ, never app-visible.
    std::uint64_t generation = 0; // Changes whenever active storage is created/retired.
    std::uint64_t clear_epoch = 0; // Successful Z clears of this active allocation.
    D3DSURFACE_DESC logical_desc{};
    HRESULT status = S_FALSE; // S_OK active; S_FALSE off/ineligible; failure reason.
    bool requested = false;
    bool available = false;
    bool bound = false; // Physical INTZ is the current depth target.
};

// Renderer-only snapshot: valid wrapper returns S_OK even when unavailable;
// inspect status/available. No AddRef. Hold the wrapper while using the borrowed
// texture, and stop using it before Reset, device loss or final wrapper Release.
// Unbind the native depth target before sampling and restore it afterwards.
HRESULT get_depth_view(IDirect3DDevice9* wrapped, DepthView* out) noexcept;

// Renderer-only borrowed pointer. No AddRef; caller must hold a live application
// wrapper reference throughout use. Never return this pointer to application code.
// Returns null for an unrecognized pointer without dereferencing that pointer.
IDirect3DDevice9* borrowed_native_device(IDirect3DDevice9* wrapped) noexcept;

// Adopt a native renderer resource created through borrowed_native_device.
// Success consumes one owned reference; failure leaves ownership with caller.
// Application wrappers are rejected to avoid logical reference cycles. The
// renderer must restore app-visible bindings before returning from injected work.
// All adopted references are released before Reset and before backend teardown.
HRESULT retain_renderer_resource(IDirect3DDevice9* wrapped, IUnknown* owned_resource) noexcept;

} // namespace x3m::ownership
