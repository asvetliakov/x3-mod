#pragma once
#include <cstdint>

// Screen-emission admission (docs/architecture/screen-emission-region.md):
// one table for step B's locked-prefix scan allowlist and step C's packed
// admission. Keyed by the proxy's FNV-1a 64 fingerprint of the original
// shader bytes (the sweep's vs_<hash>.bin / ps_<hash>.bin names).
//
// The nine exact SM1 pairs of docs/architecture/linear-emission-sm1.md
// (rows 16-24 of the effects ledger) with their promoted PackedScreen
// producers from linear_emission_sm1.cpp (the isolated pure-promotion
// checkpoint, all nine qualified detached). Step C admits any of them in the
// exact native screen state (ADD, ONE/INVSRCCOLOR, mask 15) once a locked-
// prefix bound exists. The bound itself is only derived for the three
// INSTANCE_BULLETS vertex shaders (`bullet`): one body class per the SM1
// note (101 DWORDs, 7 slots; g_mViewProjection at c0-3 over the position
// input), of which only 5e484a06672e28fb has live and disassembly evidence
// (effects-engine-remaining-emission.md, "Bullet vertex buffer writer"). The
// bound is data-driven per buffer (the Unlock scan of the draw's own
// stride-24 FLOAT3 stream); the VS identity only selects the c0-3 window.
// The DEFAULT/INSTANCE scalar-fade bodies transform through their own
// matrices and have no bound source yet: their draws refuse to native.
namespace x3m::screen_emission {

struct Pair {
    std::uint64_t vertex, pixel;
    bool bullet; // bound-capable: the locked-prefix scan allowlist (step B)
};

// Row 19 (the only observed screen population) first.
constexpr Pair pairs[] = {
    {0x5e484a06672e28fbull, 0xec1f5c4a2f4e1445ull, true},
    {0x1b6863a088a177afull, 0x84d3de8887c963c5ull, true},
    {0x21a2c13be7f989c3ull, 0xd4a26efb7c603931ull, true},
    {0x0d44b36d48d24f7aull, 0x078494828322bccaull, false},
    {0x637dadcb5efa3288ull, 0x078494828322bccaull, false},
    {0x6da1b1b6ed63ec82ull, 0x2ea025492d370c8eull, false},
    {0xed42e0742e47dca4ull, 0x2ea025492d370c8eull, false},
    {0x88620f88d6e0a00eull, 0xa5c3495e27270b4aull, false},
    {0xf9755e1154244f58ull, 0xa5c3495e27270b4aull, false},
};
constexpr unsigned pair_count = sizeof pairs / sizeof pairs[0];
static_assert(pair_count == 9, "nine exact SM1 pairs");

// Step B: the vertex shaders whose non-indexed stride-24 draws mark their
// stream-0 buffer for the DISCARD-Unlock scan and get the locked-prefix
// rectangle. Only the bound-capable (bullet) bodies.
inline bool admitted_vertex_shader(std::uint64_t hash) noexcept {
    if (!hash) return false;
    for (const auto& p : pairs) if (p.bullet && p.vertex == hash) return true;
    return false;
}

// Step C: the exact pair identity of a packed-screen candidate draw.
inline bool admitted_pair(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    if (!vertex || !pixel) return false;
    for (const auto& p : pairs) if (p.vertex == vertex && p.pixel == pixel) return true;
    return false;
}

// Table index of an exact pair, or pair_count when it is not one of the nine.
// The additive option's per-frame telemetry names the pairs it admitted with
// a bit mask over these indices; no draw-path work depends on it.
inline unsigned admitted_pair_index(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    if (!vertex || !pixel) return pair_count;
    for (unsigned i = 0; i < pair_count; ++i)
        if (pairs[i].vertex == vertex && pairs[i].pixel == pixel) return i;
    return pair_count;
}

// The six original PS1.1 identities whose promoted producer is created at
// registration (each serves one or two pairs).
inline bool admitted_pixel_shader(std::uint64_t pixel) noexcept {
    if (!pixel) return false;
    for (const auto& p : pairs) if (p.pixel == pixel) return true;
    return false;
}

} // namespace x3m::screen_emission
