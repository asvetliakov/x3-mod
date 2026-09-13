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
    // C: as B, and the pixel program holds static `if b#`/`else`/`endif`
    // blocks (boolean constant conditions, balanced, nesting depth at most
    // one, depth zero at the append point). The transformer revalidates that
    // shape from the words and refuses every other control flow.
    RelocatedRegistersWithBranches = 2,
    // D: exactly the two damage BUMPMAP programs. Their owned token contract
    // proves one top-level NE IFC/MOV/join between static b0/b1 blocks, with
    // unconditional native color writes and END. Never a generic IFC class.
    BoundedDamageBranches = 3,
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
    // Current-depth interpolator for the R32F depth target (RT2): the VS
    // exports the current clip z (.x) and w (.y) of the rasterized position in
    // a second free output, the PS divides them into oC2. motion_output_depth_none
    // marks a register the program cannot spare; depth_output=false keeps the
    // row motion-only (its VS may still export an interpolator nobody reads).
    std::uint8_t vertex_depth_output_register;
    std::uint8_t depth_texcoord_index;     // Free in every program of the row's sharing component.
    std::uint8_t pixel_depth_input_register;
    bool depth_output;
    std::uint32_t observed_scene_draws;    // Metadata: Scene draws of this pair in one
                                           // captured session, zero when never observed.
                                           // Orders the rows and selects the fixtures'
                                           // exhaustive mutation sweep; no runtime meaning.
};

inline constexpr std::uint8_t motion_output_depth_none = 255;

// Derived numbers only; regenerate with --emit-header, never edit by hand.
inline constexpr MotionOutputProfile motion_output_profiles[] = {
#include "motion_output_profiles_inc.h"
};
inline constexpr std::size_t motion_output_profile_count =
    sizeof motion_output_profiles / sizeof motion_output_profiles[0];

// The vertex variant exports the current clip z/w when the row names both a
// spare output register and a spare TEXCOORD index; the pixel variant reads
// it only when depth_output is set (which requires the export).
constexpr bool motion_output_vertex_exports_depth(const MotionOutputProfile& row) noexcept {
    return row.vertex_depth_output_register != motion_output_depth_none &&
        row.depth_texcoord_index != motion_output_depth_none;
}
// Every row must name registers the shader model can address, and the rows
// that share one original program must agree on that program's side of the
// splice, because the live route creates one variant per original program
// (see docs/architecture/live-motion-route.md, "Pair keying"). Proven here at
// compile time so a regenerated table cannot silently break the scheme.
constexpr bool motion_output_depth_valid(const MotionOutputProfile& row) noexcept {
    const bool vertex_ok = row.vertex_depth_output_register == motion_output_depth_none ||
        (row.vertex_depth_output_register < 12 && row.vertex_depth_output_register != row.vertex_output_register);
    const bool index_ok = row.depth_texcoord_index == motion_output_depth_none ||
        (row.depth_texcoord_index < 16 && row.depth_texcoord_index != row.texcoord_index);
    const bool pixel_ok = row.pixel_depth_input_register == motion_output_depth_none ||
        (row.pixel_depth_input_register < 10 && row.pixel_depth_input_register != row.pixel_input_register);
    // A pixel program can only read an interpolator its vertex program writes.
    const bool output_ok = !row.depth_output ||
        (motion_output_vertex_exports_depth(row) && row.pixel_depth_input_register != motion_output_depth_none);
    return vertex_ok && index_ok && pixel_ok && output_ok;
}
constexpr bool motion_output_profile_valid(const MotionOutputProfile& row) noexcept {
    const bool class_valid = row.transformation_class == MotionOutputClass::ReferenceRegisters ||
        row.transformation_class == MotionOutputClass::RelocatedRegisters ||
        row.transformation_class == MotionOutputClass::RelocatedRegistersWithBranches ||
        row.transformation_class == MotionOutputClass::BoundedDamageBranches;
    bool ok = class_valid && row.vertex_version == 0xfffe0300u && row.pixel_version == 0xffff0300u &&
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
        row.position_dp4_dwords[3] + 4 == row.vertex_arithmetic_insert_dword &&
        motion_output_depth_valid(row);
    // The four dots are issued in XYZW order but need not be adjacent (other
    // work may sit between them); the arithmetic insert follows the last one.
    for (unsigned lane = 0; lane < 4; ++lane) {
        ok = ok && row.position_lane_masks[lane] == (1u << lane) &&
            row.position_dp4_dwords[lane] >= row.vertex_declaration_insert_dword &&
            (lane == 0 || row.position_dp4_dwords[lane] >= row.position_dp4_dwords[lane - 1] + 4);
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
        a.vertex_depth_output_register == b.vertex_depth_output_register &&
        a.depth_texcoord_index == b.depth_texcoord_index &&
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
        a.pixel_constant_base == b.pixel_constant_base &&
        // The one pixel variant either reads the depth interpolator from this
        // register under this index in every pair, or in none.
        a.pixel_depth_input_register == b.pixel_depth_input_register &&
        a.depth_texcoord_index == b.depth_texcoord_index && a.depth_output == b.depth_output;
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
