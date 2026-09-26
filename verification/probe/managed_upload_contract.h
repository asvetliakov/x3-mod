#pragma once
#include <d3d9.h>
#include <cstdint>

namespace x3m::ownership::managed_upload {

// Borrowed inspection token, not ownership or finite-content evidence. The
// caller retains the exact native resource and must not modify these fields.
// Reset/allocation generation, wrapper forwarding, thread, revision and pending
// count are separately enforced by the ownership observer.
struct BufferContract {
    std::uint64_t generation = 0;
    UINT size = 0;
    D3DRESOURCETYPE type = D3DRTYPE_FORCE_DWORD;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    IUnknown* borrowed_native = nullptr;
    const void* backend_resource = nullptr;
    const void* heap_data = nullptr;
};
struct Window {
    std::uint64_t generation = 0;
    UINT offset = 0;
    UINT size = 0;
};
constexpr UINT maximum_buffer_bytes = 256u * 1024u * 1024u;

// Exact Preview PE32 qualification. Pins verified D3D9/WineD3D modules for the
// process lifetime; checks live native Set/Get/FreePrivateData and
// Lock/Unlock/GetDesc endpoints/imports,
// actual MANAGED+WRITEONLY descriptor and pinned heap layout. No QI/AddRef or
// extra Lock/Unlock, no mapped payload reads. Caller supplies a live native
// interface and serializes destruction, mappings, reset and dispatch mutation.
bool inspect(IDirect3DVertexBuffer9* native, BufferContract* out) noexcept;
bool inspect(IDirect3DIndexBuffer9* native, BufferContract* out) noexcept;

// Requires the pointer from this caller's successful ordinary write Lock,
// flags 0/NOSYSLOCK, and exactly one native mapping. Only (0,0) is a whole-buffer
// zero-size form; otherwise size must be nonzero and entirely within allocation.
// Revalidates the SAME token/resource/heap and actual pointer == heap+offset.
// Caller proves same-thread, single observed mapping and unchanged wrapper
// forwarding; executes MFENCE/compiler barrier before any subsequent scan.
bool validate_window(const BufferContract&, UINT offset, UINT size, DWORD flags, const void* successful_lock_pointer,
                     Window* out) noexcept;

// After the normal Unlock, revalidates the same token/heap and native map count
// zero. This alone does not validate the observer's revision or reset generation.
bool validate_closed(const BufferContract&) noexcept;

// Every public call preserves incoming LastError and the computational x87/SSE
// environment (control/status/tag and MXCSR, including sticky exception flags).
// The normal calling ABI still permits volatile XMM registers to change.
// Runtime code patching, corrupted objects and foreign concurrent mutations are
// outside this backend proof; replacements of checked slots/imports fail closed.
}
