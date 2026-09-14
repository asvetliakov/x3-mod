#include "fade_region.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "../ownership/d3d9_ownership.h"
#include <windows.h>
#include <d3d9.h>

static_assert(sizeof(void*) == 4, "Verified x86 game layout only");

namespace x3m::fade_region {
namespace {
bool read_memory(std::uintptr_t address, void* out, std::size_t size) noexcept {
    return engine_memory::read(address, out, size);
}
bool scope(std::uintptr_t* descriptor, std::uint32_t* depth) noexcept {
    return object_trace::scope_descriptor(descriptor, depth);
}
// The wrapper pointer is a registry key only (a map find under the ownership
// mutex); the view must be tracked, unlocked and unambiguous.
bool content(std::uintptr_t wrapper, std::uint64_t* revision) noexcept {
    ownership::BufferContentView view{};
    const HRESULT hr = ownership::get_buffer_content_view(reinterpret_cast<IDirect3DResource9*>(wrapper), &view);
    if (FAILED(hr) || FAILED(view.status) || !view.requested || !view.known || view.ambiguous || view.pending_locks) return false;
    *revision = view.revision;
    return true;
}
// The wrapper pointer is again a registry key only; the view carries the
// published scan's box (prefix::Table::lookup) or its refusal.
bool prefix_bound(std::uintptr_t wrapper, std::uint32_t vertex_count, Box* box, std::uint64_t* revision, std::uint32_t* checkpoint, unsigned* refusal) noexcept {
    ownership::LockedPrefixView view{};
    // An admitted draw marks its buffer: the next DISCARD Unlock is scanned.
    const HRESULT hr = ownership::get_locked_prefix_view(reinterpret_cast<IDirect3DResource9*>(wrapper), vertex_count, true, &view);
    *refusal = view.reason; *revision = view.revision; *checkpoint = view.checkpoint;
    if (FAILED(hr) || !view.requested || !view.known) return false;
    for (unsigned a = 0; a < 3; ++a) { box->centre[a] = view.centre[a]; box->half[a] = view.half[a]; }
    return true;
}
const Environment production{&read_memory, &scope, &content, &prefix_bound};
}

const Environment& production_environment() noexcept { return production; }

Result resolve(BoundTable& table, const Query& query) noexcept {
    const DWORD error = GetLastError();
    const Result out = table.resolve(query, production);
    SetLastError(error);
    return out;
}
Result peek(const BoundTable& table, const Query& query) noexcept {
    const DWORD error = GetLastError();
    const Result out = table.peek(query, production);
    SetLastError(error);
    return out;
}
Result resolve_locked_prefix(const Query& query, std::uint32_t vertex_count) noexcept {
    const DWORD error = GetLastError();
    const Result out = fade_region::resolve_locked_prefix(query, vertex_count, production);
    SetLastError(error);
    return out;
}

} // namespace x3m::fade_region
