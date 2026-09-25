#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
// Process-start gains embedded in shader-local DEFs; changing a gain requires
// a new owned variant. Every gain must be finite and in [0,16].
struct LinearMaterialConfig {
    float direct_gain = 1.0f;
    float material_emissive_gain = 1.0f;
    float lightmap_emissive_gain = 1.0f;
    // Constant hemispherical fill inside the converted law, tinted by the
    // decoded sun register (docs/architecture/fill-light.md). Finite, 0..0.5.
    // Zero is off and emits no instruction, so the programs stay byte-identical
    // to a fill-less build. Pixel stage only; the vertex programs are unchanged.
    float fill = 0.0f;
    // Create-time opt-in only. Caller caches both stages separately and uploads
    // one immutable, validated frame packet before admitting a selective pair.
    // Existing default config produces byte-identical ordinary programs.
    bool selective_exposure = false;
};
enum class LinearMaterialResult {
    Applied, InvalidInput, InvalidConfig, UnsupportedShader, ProfileMismatch,
    ResourceLimit, AllocationFailure,
    FlowControl, // hull emissive widening: the light-map fetch sits inside loop/rep/if (dsx/dsy need depth 0)
    Subroutine   // hull emissive widening: a label/call/callnz/ret precedes the fetch (the static walk cannot place it)
};
struct MaterialExposureAbi {
    static constexpr unsigned vertex_constant = 250; // x = 1/e
    static constexpr unsigned pixel_constant = 222; // x=1/e, y=C/min(e,1), z=min(e,1)
    // Qualified e range is [1/8,2]; no shader-side factor-only clamping.
};
struct LinearMaterialAbi {
    static constexpr unsigned vertex_definition_base = 248;
    static constexpr unsigned pixel_definition_base = 212;
    static constexpr unsigned vertex_rgb_output = 8;
    static constexpr unsigned pixel_rgb_input = 7;
    static constexpr unsigned rgb_usage = 10; // D3DDECLUSAGE_COLOR
    static constexpr unsigned rgb_usage_index = 1;
};
// BUMPMAP keeps its original TEX0-4 basis; temporal TEX5/6 remain unchanged.
struct LinearBumpMaterialAbi {
    static constexpr unsigned vertex_definition_base = 248;
    static constexpr unsigned pixel_definition_base = 212;
    static constexpr unsigned vertex_rgb_output = 9;
    static constexpr unsigned pixel_rgb_input = 8;
    static constexpr unsigned rgb_usage = 10; // D3DDECLUSAGE_COLOR
    static constexpr unsigned rgb_usage_index = 1;
};
bool linear_material_config_valid(const LinearMaterialConfig& config) noexcept;
// Lobe-sum destination of an already validated converted pixel program: the
// unique MUL/MAD that multiplies the accumulated direct lobes by the decoded
// albedo register. Returns false, and the caller then emits no fill, when the
// program has zero or several such destinations (fail closed). Pure bounded
// scan of the passed words; no allocation, no D3D and no state.
bool linear_material_fill_sum(const std::uint32_t* code, std::size_t words,
    unsigned albedo_register, unsigned& sum_register, unsigned& instruction_dword) noexcept;
// A native scalar changes only its semantic/component carrier. The live
// transaction must copy the original wrap bit to the destination component,
// preserving other bits and restoring application state after the draw.
struct LinearMaterialScalarTransport {
    std::uint8_t source_texcoord = 0, source_component = 0;
    std::uint8_t destination_texcoord = 0, destination_component = 0;
};
struct LinearMaterialPairContract {
    std::uint32_t sampler_mask = 0;
    bool bump = false; // Includes BUMPMAP_LOW; independent of sampler count.
    std::array<LinearMaterialScalarTransport,2> scalar_transport{};
    std::uint8_t scalar_transport_count = 0;
};
// One exact-pair lookup supplies both cached sampler admission and technique
// telemetry. A zero mask is unsupported; technique alone never admits a draw.
LinearMaterialPairContract linear_material_pair_contract(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// Exact 168 DEFAULT/BUMPMAP/BUMPMAP_LOW pairs, independent of the temporal registry. The
// live caller must also establish both combined objects, opaque scene coverage,
// gamma-2.2 composition, sampler decode state, no MSAA and the existing temporal
// gates. This helper establishes none of those draw-time conditions.
// Hull DEFAULT returns 0x0f, hull BUMPMAP/LOW 0x1f; Asteroid DEFAULT/BUMP
// return 0x07/0x0f; glass DEFAULT returns 0x07; XT DEFAULT/BUMP return 0x1d/0x39, and unsupported pairs zero. The mask only
// identifies required disabled-sRGB samplers; it establishes no dynamic gates.
std::uint32_t linear_material_sampler_mask(std::uint64_t vertex, std::uint64_t pixel) noexcept;
bool linear_material_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// True for the Asteroid-family pairs of the same tables: the reviewed pairs
// whose pixel row carries an asteroid base/detail layout, which includes the
// six distance-fade pairs of linear_distance_fade.h. Identity only: it admits
// nothing and reads no draw state. Diagnostic classification: no production caller since the shimmer trace was removed
// (2026-09-25); verification/probe/linear_distance_fade_structure.cpp still reads it.
bool linear_material_asteroid_pair(std::uint64_t vertex, std::uint64_t pixel) noexcept;

// Pure create-time combined material + same-draw motion/depth transformations.
// Inputs are complete ORIGINAL programs. Original fingerprint/count, material
// sites, resource reservations and the ordinary motion row must all validate.
// Motion's existing row-explicit API only receives the immutable original;
// original-offset merging then preserves its inserted temporal bytes exactly.
// New RGB uses full precision and a distinct varying; original sampled alpha,
// position, angular math and temporal outputs retain their existing operations.
// Failure leaves output intact; input may alias output. No D3D calls, constant
// uploads or per-draw work occur here. GPU and native-Windows behavior require
// separate qualification, including the documented DX9 exceptional-value rules.
LinearMaterialResult linear_material_vertex_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true) noexcept;
LinearMaterialResult linear_material_pixel_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true) noexcept;
// The same transformation, additionally reporting whether the constant fill
// entered this program: false for a zero configured fill and for a refused
// (non-unique) lobe sum. Written on every return, including failures.
LinearMaterialResult linear_material_pixel_variant_fill(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth, bool& fill_applied) noexcept;
// Explicit opt-in ordinary material extraction. Existing APIs emit identical
// shaders. Writes the sun luminance fraction to oC2.g AFTER motion/depth writes;
// leaves oC2.r, color, alpha and discard unchanged. No fade producer support.
// extraction_applied reports the program proof (not per-pixel radiance validity).
// Invalid radiance or a refused extraction writes -1; proved zero sun writes 0.
// Unknown/malformed inputs and resource collisions retain output unchanged.
// With current_depth=false this does not establish any valid depth in oC2.r;
// receiver consumers require ordinary same-draw depth independently.
// A selective_exposure config emits the same selective RGB as the base API,
// writes the unqualified -1 sentinel, and reports extraction_applied=false.
// Requires separate G32R32F MRT/state qualification before any live use.
LinearMaterialResult linear_material_pixel_variant_sun_share(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth, bool& extraction_applied) noexcept;
// Fill in linear light inside the ORIGINAL pixel programs (option C,
// docs/architecture/original-shading-critique.md 1a): the ordinary motion/depth
// variant of a reviewed original PS plus, immediately before its located
// lobe-sum site, sum.xyz = encode(decode(max(sum,0)) + K*decode(LightDir_Color0))
// with the exact 2.2 power law (32 weighted slots, r12/r13 scratch, one
// shader-local `def c215 = (K, 1e-22, 2.2, 1/2.2)`). Nothing else in the
// program changes: no linear materials, gains, decoded albedo or relocated
// varyings. K must be finite in [0, 0.5]; K = 0 writes the plain motion variant
// byte for byte and reports fill_applied = false. A program without a unique
// lobe sum keeps the plain motion variant and reports fill_applied = false
// (fail closed). Vertex programs and unreviewed programs are UnsupportedShader.
// Pure, allocation-bounded, no D3D; input may alias output; failure leaves
// output intact.
LinearMaterialResult linear_material_original_fill_pixel_variant(const std::uint32_t* original,
    std::size_t words, float fill, std::vector<std::uint32_t>& output, bool current_depth,
    bool& fill_applied) noexcept;
// Hull emissive widening parameters (K, B; see the widen paragraph below).
struct HullLightmapWiden { float k = 3.f; float b = 3.f; };
// Static flow-control depth of the instruction at DWORD offset `site` of a
// ps_3_0 program: loop/rep/if/ifc open a level, endloop/endrep/endif close one
// (else keeps it). -1 when the walk does not land on an instruction boundary
// at `site` or the program is malformed; -2 when a label, call, callnz or ret
// precedes the site (a subroutine the static walk cannot place). The widening
// refuses a fetch at depth > 0 (LinearMaterialResult::FlowControl) or after a
// subroutine construct (Subroutine): dsx/dsy are undefined inside dynamic flow
// control. Pure, no D3D.
int linear_material_flow_control_depth(const std::uint32_t* code, std::size_t words, std::size_t site) noexcept;
// Original-shading share producer (docs/architecture/legacy-sun-application.md
// 1): the fill variant above (K = fill, K = 0 is the plain motion variant)
// plus the sun's code-value contribution S_c propagated on the original
// operands (seed MULs before each sun MAD, one parallel op per sun-dependent
// RGB op, the fill twin fill(sum) - fill(sum - S_sum) when K > 0), the final
// RGB instruction redirected through r11 keeping its _pp, and the converted
// producer's reduction s = Y(S_c)/Y(C) (c221 luma weights, 2^-20 epsilon,
// -1 for an invalid domain) written to oC2.g after the motion/depth writes.
// Colour, alpha, motion and depth are the fill/motion variant's. A program
// without the contract's structure keeps the fill/motion variant byte for
// byte and reports share_applied = false; vertex and unreviewed programs are
// UnsupportedShader. Pure, allocation-bounded, no D3D; input may alias output;
// failure leaves output intact. Depth-off generation establishes no oC2.r.
// lightmap_gain (default 1 = none) composes the hull self-illumination gain
// below with the share producer (its c212/c221 DEFs and r11-r23 are disjoint
// from c223 and rL): the same DEF and MUL, the share reduction unchanged;
// lightmap_gain_applied reports it as the dedicated entry point does.
LinearMaterialResult linear_material_original_sun_share_pixel_variant(const std::uint32_t* original,
    std::size_t words, float fill, std::vector<std::uint32_t>& output, bool current_depth,
    bool& share_applied, float lightmap_gain = 1.0f, bool* lightmap_gain_applied = nullptr,
    bool lightmap_dynamic = false, const HullLightmapWiden* widen = nullptr, bool* widen_applied = nullptr) noexcept;
// Hull self-illumination gain (docs/reverse-engineering/hull-self-illumination.md
// 5, --hull-lightmap-gain): the fill variant above (K = fill, K = 0 the plain
// motion variant) plus, in the 100 reviewed programs that add a light-map
// term, one shader-local `def c223 = (G, 0, 0, 0)` and one
// `mul rL.xyz, rL, c223.x` immediately after the pinned light-map fetch
// (+2 instructions, +1 slot); rL.w and every other word stay as they are, so
// the alpha LRP, the lit colour, motion and depth are the fill variant's. G
// must be finite in [1, 8]; G = 1 writes the fill variant byte for byte and
// reports gain_applied = false. A program without the term (the four glass
// and four asteroid originals) keeps the fill variant byte for byte and
// reports gain_applied = false (fail closed). Pure, allocation-bounded, no
// D3D; input may alias output; failure leaves output intact.
// dynamic (--light-map-far-fade, both entry points): no DEF; the MUL reads
// c217.w, the free lane of the motion ABI's per-draw mode vector
// (MaterialMotionAbi::pixel_mode_constant), so the caller MUST upload the
// effective gain there on every draw that binds the variant. dynamic = false
// is byte for byte the program above.
// widen (docs/architecture/hull-emissive-widening.md 8.3 R1/R2,
// --hull-emissive-widening K[,B], both entry points): the gained variant's
// light-map `texld rL, v1, s` is replaced by a 28-instruction block (117 DWORDs,
// +35 weighted slots, three temporaries rG/rT/rU above the program's highest):
// dsx/dsy of v1 into rG; the light map's own texel footprint per pixel
// rho^2 = max(|dUV/dx|^2, |dUV/dy|^2) . (W c, H c)^2 from the per-draw lanes
// c217.yz (the caller MUST upload ((W c)^2, (H c)^2) of the bound light map
// on every draw that binds the variant; 0 gives k = 1); k = clamp(rho, 1, K)
// (rsq, max_sat against 1/K, rcp); `texldd rL, v1, s, k rG` (the sampler
// filters the light map over a k x k pixel footprint); two coarse texldd with
// one axis of the gradients doubled each (2 k on x, then on y);
// r = luma(rL) / max(min(luma_x, luma_y), 2^-8) (the larger axis ratio: a
// strip 2.0, a 1-D edge and a convex corner 1.33), t = saturate((r - 1.35)
// / 0.15), b = saturate(32 luma(rL) - 1) (no boost below luma 1/32, full from
// 1/16), g = saturate(rho^2 / K - 1) (no boost while the widened fetch is still
// magnified), rL.xyz *= 1 + (B - 1) t b g (the guarded thin-emitter boost). Three
// shader-local DEFs: c210 = (1/K, 2^-8, B - 1, 1/0.15), c211 = (Rec.709 luma,
// 9), c203 = (32, 1, 0, 0); all proven free of the original. The fetch must
// sit at flow-control depth 0 (FlowControl otherwise). No intensity rescale otherwise. K
// finite in (1, 8], B finite in [1, K]; widen with G = 1 or out-of-range
// K/B is InvalidConfig. widen_applied reports it (equal to gain_applied).
LinearMaterialResult linear_material_hull_lightmap_gain_pixel_variant(const std::uint32_t* original,
    std::size_t words, float fill, float gain, std::vector<std::uint32_t>& output, bool current_depth,
    bool& fill_applied, bool& gain_applied, bool dynamic = false, const HullLightmapWiden* widen = nullptr, bool* widen_applied = nullptr) noexcept;
// The light-map sampler stage of a reviewed hull/palette/XT pixel program (2 for
// DEFAULT layouts, 3 for BUMPMAP), 0 for a program without the term (glass,
// asteroid) or an unreviewed one. Table lookup only, no bytecode.
unsigned linear_material_hull_lightmap_stage(std::uint64_t pixel, std::size_t words) noexcept;
// Four XT DEFAULT programs require an explicitly authored producer repair.
// Ordinary and linear repaired pairs must be published together by the caller;
// these APIs never make the shared original VS a stage-global replacement.
bool linear_material_xt_default_pair(std::uint64_t vertex, std::uint64_t pixel) noexcept;
LinearMaterialResult linear_material_xt_default_vertex_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true, bool linear = true) noexcept;
LinearMaterialResult linear_material_xt_default_pixel_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true, bool linear = true) noexcept;
} // namespace x3m::renderer
