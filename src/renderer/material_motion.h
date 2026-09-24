#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "motion_output_profiles.h"

namespace x3m::renderer {
// Lane-only augmentation of an already validated/generated depth producer.
// Adds one DEF at c221 and one final MOV oC2.g = -1; original bytes remain
// ordered/verbatim and output is unchanged on refusal/allocation failure.
// Rejects any c221 use or absent depth export. Never use on extraction output.
bool material_motion_invalid_sun_share(std::vector<std::uint32_t>& program) noexcept;

struct MaterialMotionVariant {
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> pixel;
};
enum class MaterialMotionResult {
    Applied, InvalidInput, UnsupportedShader, ProfileMismatch, AllocationFailure
};
struct MaterialMotionAbi {
    static constexpr unsigned previous_vertex_constant = 252; // Four submitted rows.
    static constexpr unsigned pixel_coordinates_constant = 216; // Inverse size, prior jitter UV.
    static constexpr unsigned pixel_mode_constant = 217; // x=1 valid history request, x=0 invalid.
    // X3M_TAA_THIN_VOTE only (material_motion_configure_thin_vote): x = RT2 .a of the depth
    // fragment, 1 - thin on an opaque routed row, 1 otherwise; uploaded with c216-c217 in one call.
    // X3M_FADE_RT2_OWNER (material_motion_configure_fade_owner) uploads the same register:
    // .a = max(w * z + x, y), y = 1 on a fade-arm row (fade-rt2-ownership.md).
    static constexpr unsigned pixel_thin_constant = 218;
    static constexpr unsigned motion_render_target = 1;
    static constexpr unsigned depth_render_target = 2;  // R32F current device depth (z/w).
};

// The reviewed original pairs are the rows of the generated profile table. The
// live route substitutes variants only when the bound VS and PS fingerprints
// appear together in one row; a VS alias shared with other materials never
// qualifies on its own. Every row's constant bases equal the public ABI above
// (checked in material_motion.cpp), so the route uploads the same ranges for
// every pair.
inline constexpr const auto& material_motion_reviewed_pairs = motion_output_profiles;
// The row for an exact pair of original fingerprints, or nullptr. Binary
// search over a compile-time sorted index of the rows (O(log rows), no
// allocation), so the live route's per-draw pair gate does not scan the table.
const MotionOutputProfile* material_motion_profile(std::uint64_t vertex, std::uint64_t pixel) noexcept;
bool material_motion_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// The first row of a supported class hosting an original program of this exact
// fingerprint and DWORD count, or nullptr; the same lookups the per-stage
// transformers use, exposed so the route can record a program's row at
// registration time. Rows sharing a program agree on that program's side of
// the splice (static_assert in motion_output_profiles.h).
const MotionOutputProfile* material_motion_vertex_row(std::uint64_t vertex, std::size_t words) noexcept;
const MotionOutputProfile* material_motion_pixel_row(std::uint64_t pixel, std::size_t words) noexcept;
// FNV-1a 64 over the program bytes in little-endian DWORD order. Equal to the
// proxy's byte hash of the same bytecode, so create-time hashes identify pairs.
std::uint64_t material_motion_fingerprint(const std::uint32_t* words, std::size_t count) noexcept;

// Table-driven transformer for the reviewed SM3 material pairs (classes A, B,
// C and the two owned class D damage programs). All original position and color instructions remain unchanged. A
// previous-clip interpolator feeds our existing RGBA32F previous-UV/depth/
// validity program in the row's output register (oC1 for every row). With
// `current_depth` (the default) a second interpolator carries the current
// clip z/w of the rasterized position and the authored depth fragment writes
// z/w, the device depth, to oC2 (R32F) for rows with `depth_output`; a row
// without it, or a caller whose device cannot bind the third target, gets the
// motion-only variant. The vertex export is emitted whenever the row names a
// spare register and index, even for a motion-only pixel side.
//
// These pure, creation-time transformers perform no D3D calls or per-draw work.
// The vertex and pixel programs are created separately by the application, so
// each stage transforms independently: the program is matched to a table row
// by exact fingerprint, length and version, and the row's offsets and register
// choices are then revalidated against the actual words (instruction framing,
// header boundaries, the four position dots, class C static branches, the
// separate owned class D flow contract, and
// that the chosen registers are unused by the original, inside branches
// included). Success replaces the owned output; failure leaves
// it intact. Input may alias the output vector. The caller must separately
// establish opaque scene coverage, compatible MRT state, the row's light-loop
// bound, valid history, and ownership/restoration of the reserved constants
// and render target. The current pixel ABI requires a zero-origin viewport and
// the existing jitter convention. A matching shader pair alone establishes
// none of these contracts.
MaterialMotionResult material_motion_vertex_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, std::vector<std::uint32_t>& output, bool current_depth = true) noexcept;
MaterialMotionResult material_motion_pixel_variant(const std::uint32_t* pixel,
    std::size_t pixel_words, std::vector<std::uint32_t>& output, bool current_depth = true) noexcept;
// Pair form retained for the detached fixtures: both stages are qualified
// against one row before either transforms, and success publishes both
// programs atomically. Input may alias either output vector.
MaterialMotionResult material_motion_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words,
    MaterialMotionVariant& output, bool current_depth = true) noexcept;

// Row-explicit forms. The program must still carry the row's exact fingerprint,
// length and version (UnsupportedShader otherwise); the row's structural fields
// are then revalidated against the words (ProfileMismatch on any inconsistency).
// The table lookups above call these; the structural fixture uses them with
// deliberately perturbed rows to prove the revalidation refuses, which the
// fingerprint gate makes unreachable through the lookup forms.
MaterialMotionResult material_motion_vertex_variant_for(const MotionOutputProfile& row,
    const std::uint32_t* vertex, std::size_t vertex_words, std::vector<std::uint32_t>& output,
    bool current_depth = true) noexcept;
MaterialMotionResult material_motion_pixel_variant_for(const MotionOutputProfile& row,
    const std::uint32_t* pixel, std::size_t pixel_words, std::vector<std::uint32_t>& output,
    bool current_depth = true) noexcept;
// Whether the variants of this row carry the depth export/output under the
// given option (the words the transformers add depend on it).
bool material_motion_vertex_exports_depth(const MotionOutputProfile& row, bool current_depth) noexcept;
bool material_motion_pixel_writes_depth(const MotionOutputProfile& row, bool current_depth) noexcept;

// Thin vote (X3M_TAA_THIN_VOTE; docs/architecture/taa-thin-geometry-alternatives.md section 3.2):
// process-wide, off by default, set once before any variant is built. On, every depth-writing
// pixel variant appends the thin-vote depth fragment (RT2 .a = c218.x instead of w) and carries
// the motion fragment's literals in two DEFs at c219/c220 instead of three at c218-c220 (same
// values, same instructions); motion-only variants and every vertex variant are unchanged. Off,
// every output is the earlier one byte for byte.
void material_motion_configure_thin_vote(bool on) noexcept;
bool material_motion_thin_vote() noexcept;
// Fade owner (X3M_FADE_RT2_OWNER; docs/architecture/fade-rt2-ownership.md): process-wide, off by
// default, set once before any variant is built. On, every depth-writing pixel variant appends the
// fade-owner depth fragment (RT2 .a = max(w * c218.z + c218.x, c218.y); it replaces the plain and the
// thin-vote fragment, which it reproduces with the route's per-row c218) and carries the motion
// fragment's literals in two DEFs at c219/c220 exactly as the thin vote does. Off, every output is
// the earlier one byte for byte.
void material_motion_configure_fade_owner(bool on) noexcept;
bool material_motion_fade_owner() noexcept;
// Words the pixel transformer inserts at the row's definition insert: 18 (three DEFs), or 12 for
// a depth-writing variant with the thin vote or the fade owner on.
std::size_t material_motion_pixel_definition_words(bool depth) noexcept;
} // namespace x3m::renderer
