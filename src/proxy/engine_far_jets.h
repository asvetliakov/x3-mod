#pragma once
#include <cstdint>
#include "engine_far_jets_core.h"

// Far engine jets (engine_far_jets_core.h): the per-frame buffer the small-parts cull stub fills through
// x3m_engine_far_jet on its cull path, for JET nodes (+0x130 & 0x4000001) it culls while a device requests the plume
// stage. Written and read on the engine's render thread only (the pass and the motion route's Present/resolve hooks run
// there), so no synchronisation. Compiled without SSE/MMX and exceptions (CMakeLists.txt): the handler runs inside the
// engine's cull/LOD pass with no CPU-state boundary; it makes no Win32 call, so LastError is untouched.
namespace x3m::engine_far_jets {
// The motion route: a device's plume-stage request (core::Requests; `counted` is the device's own flag, `device` its
// identity). The handler copies while at least one device requests; nothing while none does.
void request(bool* counted, bool requested, const void* device) noexcept;
// The requesting device whose resolve takes the stage's arming decision owns the buffer's frame (core::Requests).
void claim(const void* device) noexcept;
// Whether `device` empties the buffer at its frame begin: it owns the resolve, or no device does.
bool clears(const void* device) noexcept;
// The owning device's frame begin (Present): empties the buffer and the counts.
void begin_frame() noexcept;
unsigned count() noexcept;
const core::Raw* entries() noexcept;
core::Stats stats() noexcept;
// The frame's engine-culled (node handle, view handle) pairs (core::CulledPair; the node-sourced walk's exclusion
// list, engine_nozzle_walk_core.h), emptied with the buffer.
unsigned culled_count() noexcept;
const core::CulledPair* culled() noexcept;
} // namespace x3m::engine_far_jets
// The stub's call: cdecl, node = EDI, measure = ESI, view = the pass's site [ESP+0x28]; EAX/ECX/EDX saved by the stub.
extern "C" void x3m_engine_far_jet(std::uint32_t node, std::int32_t measure, std::uint32_t view);
extern "C" volatile std::uint32_t x3m_engine_far_armed;
