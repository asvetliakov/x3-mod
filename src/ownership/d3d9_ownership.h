#pragma once
#include <d3d9.h>
#include <cstdint>

// Opt-in normal-D3D9 ownership boundary. Application COM references are separate from renderer-owned
// backend resources, so persistent history cannot keep its own owner alive.
namespace x3m::ownership {

struct Options {
    // Prepare a private snapshot of automatic, single-sample D24X8 through RESZ.
    // Copied into devices before their first application clear/draw. Default inert.
    bool capture_auto_depth = false;
};

// On success, consumes exactly the caller's owned native reference. On failure,
// the caller retains it. Native Ex factories are rejected before wrapper mode.
HRESULT wrap_factory(IDirect3D9* owned_native, IDirect3D9** out,
                     const Options& options = {}) noexcept;

struct CopyDepthView {
    // Borrowed native D24X8 snapshot. On verified Preview this texture uses
    // comparison sampling, not raw red-channel depth; GPU decode is separate.
    IDirect3DTexture9* texture = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t source_epoch = 0; // Successful clears of the original source.
    std::uint64_t copy_epoch = 0; // Source epoch of the last successful copy.
    D3DSURFACE_DESC source_desc{};
    HRESULT status = S_FALSE;
    bool requested = false;
    bool available = false;
    bool copy_valid = false;
    bool source_bound = false;
};

// Both calls require a live wrapper and rendering/reset serialization by caller.
// get_copy_depth_view returns S_OK for a recognized wrapper; inspect its fields.
// copy_auto_depth performs no app draw/clear/target substitution. The original
// source must currently be bound, and no state block may be recording. Texture 0
// and POINTSIZE are restored. A source clear does not invalidate a saved copy.
HRESULT get_copy_depth_view(IDirect3DDevice9* wrapped, CopyDepthView* out) noexcept;
HRESULT copy_auto_depth(IDirect3DDevice9* wrapped) noexcept;

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
