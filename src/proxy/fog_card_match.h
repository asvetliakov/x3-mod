#pragma once
#include <cstdint>
namespace x3m {
// Captured run174/run180 card identity and D3D9 enum values. The production
// translation unit asserts the enums against the SDK. Unknown (-1) refuses.
constexpr bool fog_card_pair(std::uint64_t vs, std::uint64_t ps) noexcept {
    return vs == 0x7b6393fe2d3e1d85ull && ps == 0xf7e0b6647a3bfa62ull;
}
struct FogCardShape {
    bool indexed, user_memory, stream0, indices, stream0_only, frequency_known;
    unsigned topology, primitives, vertices, stride, position_offset, position_type, frequency;
    std::uint64_t declaration;
    bool matches() const noexcept {
        return indexed && !user_memory && stream0 && indices && stream0_only && frequency_known && frequency == 1 &&
            topology == 4 && primitives == 2 && vertices == 4 && stride == 24 && position_offset == 0 && position_type == 16 &&
            declaration == 0x0cdf6a8c884ad955ull;
    }
};
struct FogCardStates {
    long z, zwrite, alpha_test, blend, color_mask, cull, stencil, fill, source, destination, operation, separate_alpha;
    bool matches() const noexcept {
        return z == 0 && zwrite == 0 && alpha_test == 0 && blend == 1 && color_mask == 7 && cull == 1 && stencil == 0 && fill == 3 &&
            source == 2 && destination == 4 && operation == 1 && separate_alpha == 0;
    }
};
} // namespace x3m
