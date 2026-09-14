// Depth-only prepass vertex programs the route jitters without routing.
//
// The engine draws every fogged asteroid twice: a depth-only prepass with a
// z_only vs_1_1 program (null PS, ZWRITEENABLE on, COLORWRITEENABLE 0, ZFUNC
// LESSEQUAL) and then the blended material draw (asteroid-fog-temporal.md,
// "Run 47"). The material draw's VS has a motion-profile row and is jittered
// with the rest of the scene; the prepass has no pair row, so without this
// table it goes out unjittered and the LESSEQUAL test of the jittered draw
// against the unjittered prepass depth drops whole facets per frame.
//
// Each row names one reviewed z_only program by whole-program FNV-1a/length
// identity (the rigid position table classifies the same programs as
// HomogeneousRowDots: r0 = v0.xyz * c4.x + c4.y, oPos = dp4(r0, c0..c3)) and
// the first register of its four clip rows. A matched program is jittered
// exactly as a motion-profile VS (rows 0/1 += jitter * row 3, restored
// bit-exactly after the draw) and nothing else: the draw keeps its programs,
// its z states and its colour mask, and it never routes (gate 3 requires a
// pair row). The route's shadow must hold the row's window
// (motion_output.cpp derives the windows from both tables).
#pragma once
#include <cstddef>
#include <cstdint>

namespace x3m::renderer {

struct DepthPrepassProfile {
    std::uint64_t vertex_fingerprint;  // FNV-1a 64 over the original VS bytes.
    std::uint32_t vertex_dword_count;  // Exact original length including END.
    std::uint32_t vertex_version;      // Version token (0xfffe0101: vs_1_1).
    std::uint16_t matrix_register;     // c<n>: first of four clip rows.
};

// z_only aliases (docs/reverse-engineering/shader-fingerprints.md): 356 and
// 380 bytes, the second adds a TEXCOORD0 pass-through.
inline constexpr DepthPrepassProfile depth_prepass_profiles[] = {
    {0x803ebfd17f79e413ull, 95, 0xfffe0101u, 0},
    {0xc78b4c68a87fce74ull, 89, 0xfffe0101u, 0},
};

// Registration-time lookup (never on the draw path): identity is the hash
// over the complete program plus its exact length and version token.
constexpr const DepthPrepassProfile* depth_prepass_vertex_row(std::uint64_t fingerprint, std::size_t words,
                                                              std::uint32_t version) noexcept {
    for (const auto& row : depth_prepass_profiles)
        if (row.vertex_fingerprint == fingerprint && row.vertex_dword_count == words && row.vertex_version == version)
            return &row;
    return nullptr;
}

} // namespace x3m::renderer
