#pragma once
#include <d3d9.h>
#include <cstdint>

namespace x3m::ownership::portable_upload {

constexpr UINT maximum_buffer_bytes = 256u * 1024u * 1024u;

// Creation policy only. A caller that substitutes native_usage must first
// arrange allocation-owned immutable original-Usage metadata and rollback to
// the unmodified creation call if that metadata cannot be attached. Never
// discard unrelated usage bits or repair invalid MANAGED+DYNAMIC requests.
struct CreationPlan {
    DWORD requested_usage = 0;
    DWORD native_usage = 0;
    bool converted = false;
};
CreationPlan plan_creation(bool finite_capture, UINT bytes, DWORD usage,
                           D3DPOOL pool, const HANDLE* shared_handle) noexcept;

// Public descriptor snapshot from a caller-owned live native COM resource.
// No heap pointer, private backend fields, module identity or map-count claim.
// The caller retains the resource and owns immutable allocation identity.
struct BufferContract {
    UINT size = 0;
    DWORD usage = 0;
    DWORD fvf = 0;
    D3DRESOURCETYPE type = D3DRTYPE_FORCE_DWORD;
    D3DFORMAT format = D3DFMT_UNKNOWN;
};
bool inspect(IDirect3DVertexBuffer9* native, BufferContract* out) noexcept;
bool inspect(IDirect3DIndexBuffer9* native, BufferContract* out) noexcept;
bool same_description(IDirect3DResource9* current_native, const BufferContract&) noexcept;

struct Window { UINT offset = 0, size = 0; };
// Normalize ONLY the pointer/range from this buffer's already successful normal
// application Lock. The owner proves one observed pending lock, same thread,
// exact pointer identity, revision/generation and unchanged wrapper routing.
// This helper does not call Lock, inspect payload, or discover native bypasses.
// Only (0,0) is a whole-buffer zero-size form. Other ranges require nonzero size.
bool normalize_window(IDirect3DResource9* current_native, const BufferContract&, UINT offset, UINT size, DWORD flags,
                      const void* successful_lock_pointer, Window* out) noexcept;

// Computational x87/SSE environment and LastError are preserved. Ordinary COM
// semantics are trusted, including legitimate runtime/hook implementations.
// Standard D3D9 exposes no native map-count query: closure is separately proved
// from the wrapper's observed Lock/Unlock transaction, never inferred here.
}
