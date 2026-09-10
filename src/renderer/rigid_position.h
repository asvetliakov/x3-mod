#pragma once
#include <cstddef>
#include <cstdint>

namespace x3m::renderer {
struct RigidPositionProfile {
    std::uint64_t hash;
    std::uint32_t word_count;
    std::uint16_t matrix_register;
    bool named_world_view_projection; // CTAB hint only, not an eligibility gate.
};
// Whole-program FNV-1a/length whitelist from independently reviewed shader bytes.
// A match establishes the reviewed shader's POSITION.xyz / forced W=1 row-dot
// path, not object lifetime, input declaration, opaque coverage or stable buffers.
// Caller supplies a readable complete shader DWORD span; no COM calls/allocations.
const RigidPositionProfile* find_rigid_position(const std::uint32_t* words,
                                              std::size_t word_count) noexcept;
} // namespace x3m::renderer
