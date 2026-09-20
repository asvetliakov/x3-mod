#pragma once
#include <d3d9.h>
#include <cstdint>

namespace x3m {
// Capture-only helpers. Call while holding the proxy's capture mutex. Resource
// IDs are process-local allocation identities, not scene-object or content IDs.
// They live as POD private data on the resource, without retaining COM references.
uint64_t resource_id(IDirect3DResource9* resource);
// Read only: never assigns private data. S_FALSE means no existing capture ID.
HRESULT query_resource_id(IDirect3DResource9* resource, uint64_t* id) noexcept;
void capture_constants(IDirect3DDevice9* device, bool vertex, const D3DCAPS9& caps);
void capture_geometry(IDirect3DDevice9* device, bool user_memory, const D3DCAPS9& caps);
}
