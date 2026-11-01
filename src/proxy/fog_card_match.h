#pragma once
#include <cstdint>
namespace x3m {
// Captured run174/run180 card identity and D3D9 enum values. The production
// translation unit asserts the enums against the SDK. Unknown (-1) refuses.
// FL edition: X3FL.exe's effects nebulafog_0000/_0001 select a second exact pair, ececc28e00592d35 /
// 214fc10d3672ab92 (fl/fog-cards.md sections 1 and 3.2; the offline inventory,
// verification/results/fl-port/sector-background/fog_card_pairs.py): same shading tokens and bindings, same draw
// and state gates. Both pixel programs are card sources and both witnessed pairings are admitted, never a mixed
// one. AP's resolved set holds the same programs, but AP keeps its captured single pair (AP behaviour unchanged).
constexpr bool fog_card_pair(std::uint64_t vs, std::uint64_t ps) noexcept {
#if X3M_EDITION_FL
    return (vs == 0x7b6393fe2d3e1d85ull && ps == 0xf7e0b6647a3bfa62ull) ||
           (vs == 0xececc28e00592d35ull && ps == 0x214fc10d3672ab92ull);
#else
    return vs == 0x7b6393fe2d3e1d85ull && ps == 0xf7e0b6647a3bfa62ull;
#endif
}
// The card pixel programs: the source the card path observes (and refuses when its pair is not admitted) and the
// fog latch's card signal.
constexpr bool fog_card_pixel(std::uint64_t ps) noexcept {
#if X3M_EDITION_FL
    return ps == 0xf7e0b6647a3bfa62ull || ps == 0x214fc10d3672ab92ull;
#else
    return ps == 0xf7e0b6647a3bfa62ull;
#endif
}
struct FogCardShape {
    bool indexed, user_memory, stream0, indices, stream0_only, frequency_known;
    unsigned topology, primitives, vertices, stride, position_offset, position_type, frequency;
    std::uint64_t declaration;
    bool static_matches() const noexcept {
        return indexed && !user_memory && stream0 && indices && stream0_only && topology == 4 && primitives == 2 &&
               vertices == 4 && stride == 24 && position_offset == 0 && position_type == 16 &&
               declaration == 0x0cdf6a8c884ad955ull;
    }
    bool matches() const noexcept { return static_matches() && frequency_known && frequency == 1; }
};
// The write-safety states (zwrite, stencil, the colour mask the replacement
// saves and restores) and the screen-blend identity are exact. ZENABLE and
// CULLMODE take either value the engine gives this material: the dust pass's
// override (0, NONE: run174, all 422 in-flight cards) or the material's own
// text (g_ZEnable 1, g_CullMode 2 = CW; sector-fog.md section 4). A masked
// card writes nothing under either (depth write off, stencil off), so the
// docked-at-load variant (run278, fog-handover.md case C) is the same draw.
// ALPHATESTENABLE takes 0 or 1: the docked-at-load card carries alpha test 1
// with z 0 and cull NONE (run283, the only differing state). The replacement
// forces COLORWRITEENABLE to 0 on a masked card and zwrite 0 and stencil 0
// stay exact, so alpha test can only discard fragments of a draw that writes
// nothing; the composite sets its own states in a state block.
// SEPARATEALPHABLENDENABLE takes 0 or 1 on the same reasoning: in cockpit view
// the engine leaves it 1 from an earlier ONE/ONE/ADD draw (Run143 native,
// 19 refusal rows, the only differing state), and a separate alpha blend
// cannot affect a draw that writes nothing. Any other value (2, unknown -1)
// refuses; pair, declaration, shape, scene and frequency gates stay strict.
struct FogCardStates {
    long z, zwrite, alpha_test, blend, color_mask, cull, stencil, fill, source, destination, operation, separate_alpha;
    bool matches() const noexcept {
        return (z == 0 || z == 1) && zwrite == 0 && (alpha_test == 0 || alpha_test == 1) && blend == 1 &&
               color_mask == 7 && (cull == 1 || cull == 2) && stencil == 0 && fill == 3 && source == 2 &&
               destination == 4 && operation == 1 && (separate_alpha == 0 || separate_alpha == 1);
    }
};
} // namespace x3m
