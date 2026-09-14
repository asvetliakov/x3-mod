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
const Environment production{&read_memory, &scope, &content};
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

} // namespace x3m::fade_region
