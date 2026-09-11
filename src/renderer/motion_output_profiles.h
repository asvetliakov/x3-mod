#pragma once
// Table of transformable SM3 material pairs for the same-draw motion route.
// The rows are generated derived metadata (offsets, register numbers, counts;
// never shader words) by tools/analysis/inspect_motion_output_profiles.py, see
// docs/reverse-engineering/motion-output-profiles.md ("Generated table"). A row
// is transformer input: material_motion.cpp revalidates every structural
// assumption from the program it is handed before splicing. A row is not an
// eligibility decision; the live route still gates on draw state and history.
#include <cstddef>
#include <cstdint>

namespace x3m::renderer {
enum class MotionOutputClass : std::uint8_t {
    // A: the reviewed Argon registers (o6/TEXCOORD4, v5, r5-7, c216-220, oC1)
    // are free in both programs; only offsets and position registers differ.
    ReferenceRegisters = 0,
    // B: same shape, but one or more reference registers are occupied and the
    // row names free substitutes.
    RelocatedRegisters = 1,
    // C: as B with balanced static branches in the pixel program. Reserved;
    // the transformer refuses it until control-flow depth is validated.
    RelocatedRegistersWithBranches = 2,
};

struct MotionOutputProfile {
    std::uint64_t vertex_fingerprint;      // FNV-1a 64 over the original VS bytes.
    std::uint32_t vertex_dword_count;      // Exact original length including END.
    std::uint32_t vertex_version;          // 0xfffe0300.
    std::uint64_t pixel_fingerprint;
    std::uint32_t pixel_dword_count;
    std::uint32_t pixel_version;           // 0xffff0300.
    MotionOutputClass transformation_class;
    std::uint16_t matrix_register;         // c<n>: first of four clip rows.
    std::uint16_t position_temporary;      // r<n> the position dots read.
    std::uint16_t position_dp4_dwords[4];  // Original DP4 offsets in XYZW order.
    std::uint8_t position_lane_masks[4];   // Their single-lane o0 write masks.
    std::uint16_t vertex_declaration_insert_dword; // Original VS header end.
    std::uint16_t vertex_arithmetic_insert_dword;  // One past the last position DP4.
    std::uint16_t pixel_definition_insert_dword;   // One past the original PS DEFs.
    std::uint16_t pixel_declaration_insert_dword;  // Original PS header end.
    std::uint16_t pixel_append_dword;              // Original END index.
    std::uint8_t vertex_output_register;   // Free o<n> for previous clip.
    std::uint8_t texcoord_index;           // Its TEXCOORD index, free in both stages.
    std::uint8_t pixel_input_register;     // Free v<n> receiving it.
    std::uint8_t pixel_temporary_base;     // First of three free r<n>.
    std::uint8_t pixel_output_register;    // oC<n> for the motion target.
    std::uint16_t vertex_constant_base;    // Four previous rows.
    std::uint16_t pixel_constant_base;     // Five pixel ABI constants.
    bool light_loop_bound_required;        // VS reads constants relatively.
    std::uint8_t light_loop_max_count;     // Draw-time bound on integer i0.x.
};

// Derived numbers only; regenerate with --emit-header, never edit by hand.
inline constexpr MotionOutputProfile motion_output_profiles[] = {
#include "motion_output_profiles_inc.h"
};
inline constexpr std::size_t motion_output_profile_count =
    sizeof motion_output_profiles / sizeof motion_output_profiles[0];

// Every row must name registers the shader model can address, and the rows
// that share one original program must agree on that program's side of the
// splice, because the live route creates one variant per original program
// (see docs/architecture/live-motion-route.md, "Pair keying"). Proven here at
// compile time so a regenerated table cannot silently break the scheme.
constexpr bool motion_output_profile_valid(const MotionOutputProfile& row) noexcept {
    bool ok = row.vertex_version == 0xfffe0300u && row.pixel_version == 0xffff0300u &&
        row.vertex_dword_count > 2 && row.pixel_dword_count > 2 &&
        row.vertex_output_register < 12 && row.texcoord_index < 16 &&
        row.pixel_input_register < 10 && row.pixel_temporary_base + 3 <= 32 &&
        row.pixel_output_register < 4 && row.pixel_output_register != 0 &&
        row.vertex_constant_base + 4 <= 256 && row.pixel_constant_base + 5 <= 224 &&
        row.matrix_register + 4 <= row.vertex_constant_base && row.position_temporary < 32 &&
        row.vertex_declaration_insert_dword < row.vertex_arithmetic_insert_dword &&
        row.vertex_arithmetic_insert_dword < row.vertex_dword_count &&
        row.pixel_definition_insert_dword <= row.pixel_declaration_insert_dword &&
        row.pixel_declaration_insert_dword < row.pixel_append_dword &&
        row.pixel_append_dword + 1u == row.pixel_dword_count &&
        row.position_dp4_dwords[3] + 4 == row.vertex_arithmetic_insert_dword;
    for (unsigned lane = 0; lane < 4; ++lane) {
        ok = ok && row.position_lane_masks[lane] == (1u << lane) &&
            row.position_dp4_dwords[lane] >= row.vertex_declaration_insert_dword &&
            (lane == 0 || row.position_dp4_dwords[lane] == row.position_dp4_dwords[lane - 1] + 4);
    }
    return ok;
}
constexpr bool motion_output_vertex_sides_agree(const MotionOutputProfile& a,
                                                const MotionOutputProfile& b) noexcept {
    bool ok = a.vertex_dword_count == b.vertex_dword_count && a.vertex_version == b.vertex_version &&
        a.matrix_register == b.matrix_register && a.position_temporary == b.position_temporary &&
        a.vertex_declaration_insert_dword == b.vertex_declaration_insert_dword &&
        a.vertex_arithmetic_insert_dword == b.vertex_arithmetic_insert_dword &&
        a.vertex_output_register == b.vertex_output_register && a.texcoord_index == b.texcoord_index &&
        a.vertex_constant_base == b.vertex_constant_base &&
        a.light_loop_bound_required == b.light_loop_bound_required &&
        a.light_loop_max_count == b.light_loop_max_count;
    for (unsigned lane = 0; lane < 4; ++lane)
        ok = ok && a.position_dp4_dwords[lane] == b.position_dp4_dwords[lane] &&
            a.position_lane_masks[lane] == b.position_lane_masks[lane];
    return ok;
}
constexpr bool motion_output_pixel_sides_agree(const MotionOutputProfile& a,
                                               const MotionOutputProfile& b) noexcept {
    return a.pixel_dword_count == b.pixel_dword_count && a.pixel_version == b.pixel_version &&
        a.pixel_definition_insert_dword == b.pixel_definition_insert_dword &&
        a.pixel_declaration_insert_dword == b.pixel_declaration_insert_dword &&
        a.pixel_append_dword == b.pixel_append_dword && a.texcoord_index == b.texcoord_index &&
        a.pixel_input_register == b.pixel_input_register &&
        a.pixel_temporary_base == b.pixel_temporary_base &&
        a.pixel_output_register == b.pixel_output_register &&
        a.pixel_constant_base == b.pixel_constant_base;
}
constexpr bool motion_output_profiles_consistent() noexcept {
    for (std::size_t i = 0; i < motion_output_profile_count; ++i) {
        const auto& a = motion_output_profiles[i];
        if (!motion_output_profile_valid(a)) return false;
        for (std::size_t j = 0; j < i; ++j) {
            const auto& b = motion_output_profiles[j];
            if (a.vertex_fingerprint == b.vertex_fingerprint && a.pixel_fingerprint == b.pixel_fingerprint)
                return false; // Duplicate pair row.
            if (a.vertex_fingerprint == b.vertex_fingerprint && !motion_output_vertex_sides_agree(a, b))
                return false;
            if (a.pixel_fingerprint == b.pixel_fingerprint && !motion_output_pixel_sides_agree(a, b))
                return false;
        }
    }
    return motion_output_profile_count > 0;
}
static_assert(motion_output_profiles_consistent(),
              "motion_output_profiles_inc.h: rows sharing an original program must agree on "
              "that program's side of the splice, and every row must be well formed");
} // namespace x3m::renderer
