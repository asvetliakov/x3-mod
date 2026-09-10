#include "rigid_position.h"

namespace x3m::renderer {
namespace {
constexpr RigidPositionProfile profiles[] = {
#include "rigid_position_profiles_inc.h"
};
}
const RigidPositionProfile* find_rigid_position(const std::uint32_t* words,
                                              std::size_t word_count) noexcept {
    if (!words) return nullptr;
    bool length_known = false;
    for (const auto& p : profiles) if (p.word_count == word_count) length_known = true;
    if (!length_known) return nullptr;
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < word_count; ++i) {
        // Explicit little-endian byte order agrees with the captured token stream
        // and keeps host-side verification independent of host representation.
        for (unsigned byte = 0; byte < 4; ++byte) {
            hash ^= (words[i] >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    }
    for (const auto& p : profiles)
        if (p.word_count == word_count && p.hash == hash) return &p;
    return nullptr;
}
} // namespace x3m::renderer
