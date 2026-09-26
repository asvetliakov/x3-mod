#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
// Embedded at shader creation. Changing gain or coverage needs a new variant.
struct LinearEmissionConfig {
    float gain = 1.0f;
    bool coverage = false;
};
enum class LinearEmissionResult {
    Applied,
    InvalidInput,
    InvalidConfig,
    UnsupportedShader,
    ProfileMismatch,
    ResourceLimit,
    AllocationFailure
};
struct LinearEmissionAbi {
    static constexpr unsigned definition_base = 30;
    static constexpr unsigned emission_temporary = 2;
    static constexpr unsigned transfer_temporary = 3;
    static constexpr unsigned emission_output = 1;
    static constexpr unsigned coverage_output = 2;
};
bool linear_emission_config_valid(const LinearEmissionConfig& config) noexcept;
// Only the twenty exact engine/effects DEFAULT and INSTANCE VS2/PS2/PS2.x
// pairs. This establishes no ownership, blend, sampler, alpha, MRT, query or
// other live admission gate; technique names and shared bodies never admit a pair.
bool linear_emission_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// Registry index of a reviewed pair (0..19), linear_emission_pair_count for
// any other pair. One gain covers all twenty (the engine/effect family split
// is undone: docs/reverse-engineering/effect-shader-users.md).
constexpr unsigned linear_emission_pair_count = 20;
unsigned linear_emission_pair_index(std::uint64_t vertex, std::uint64_t pixel) noexcept;

// Pure creation-time PS2 augmentation; the original VS2 is never transformed.
// Preserve every original word, including comments, native PP oC0 and raw alpha.
// Copy pre-fade RGB, then append full-precision capped decode, preserved fade,
// gain and final finite sanitation into oC1 with exact +0 alpha. Native oC0 is
// never read. Optional coverage writes constant one to all oC2 lanes, independent
// of source/fade/alpha/gain; coverage alpha is not a payload contract. Coverage
// defaults off and retains the established two-output bytes. Failure leaves
// output intact; input may alias output.
// Original PP output is retained despite the literal SM2 output restriction:
// actual shader creation/native-output parity and native Windows execution are
// separate qualification gates. No D3D calls, uploads or per-draw work here.
LinearEmissionResult linear_emission_pixel_variant(const std::uint32_t* original, std::size_t words,
                                                   const LinearEmissionConfig& config,
                                                   std::vector<std::uint32_t>& output) noexcept;

// Source-only encoded gain (docs/architecture/linear-emission-cost.md,
// section 4, "Implemented"): the same ten exact PS2/PS2.x programs with one
// `def c31 = (gain, 0, 0, 0)` before the declarations and one
// `mul r0.xyz, r0, c31.x` immediately before the native `mov oC0, r0`. Every
// original word, the native output MOV and raw alpha stay untouched; no
// bracket, no extra output, no decode. Gain 1 returns the original words
// (byte identity with no option). Gain is finite 1..8. Failure leaves output
// intact; input may alias output. No D3D calls or per-draw work here.
bool linear_emission_source_gain_valid(float gain) noexcept;
LinearEmissionResult linear_emission_source_gain_variant(const std::uint32_t* original, std::size_t words, float gain,
                                                         std::vector<std::uint32_t>& output) noexcept;
// Colour blend admission of the source-gain draw, shared by the proxy and the
// GPU fixture. Inputs are the D3DRS_ALPHABLENDENABLE / SRGBWRITEENABLE values
// and the colour triple SRCBLEND / DESTBLEND / BLENDOP (raw D3DBLEND and
// D3DBLENDOP codes: ONE = 2, INVSRCCOLOR = 4, ADD = 1). The separate alpha
// factors are not inputs: the variant multiplies only rgb, so the ONE/ONE/ADD
// colour law holds whatever SEPARATEALPHABLENDENABLE and the alpha triple are.
// Screen (ONE/INVSRCCOLOR/ADD) cannot carry the gain as is (INVSRCCOLOR reads
// the gained output, so `G*s + bg*(1-G*s)` goes negative for G*s > 1): the
// proxy admits it with DESTBLEND ONE substituted for the draw (additive
// `G*s + bg`, docs/architecture/linear-emission-cost.md, "Screen
// substitution"), so Screen is a distinct admission, not a refusal; blend
// off, sRGB write (no linear FP16 target), other factors or ops are the
// generic blend refusal.
enum class SourceGainBlend : std::uint8_t { Admit = 0, Blend = 1, Screen = 2 };
SourceGainBlend linear_emission_source_gain_blend(std::uint32_t blend_enable, std::uint32_t srgb_write,
                                                  std::uint32_t src, std::uint32_t dst, std::uint32_t op) noexcept;

// Phase 3 of docs/architecture/emitter-plan.md: the emitters the effects gain
// cannot reach, drawn by hull programs (position lights, deco flares, warning
// signs, warp tunnels; the ADD ONE/ONE material census in
// docs/reverse-engineering/effect-shader-users.md, "Additive emitters drawn by
// material programs"). Twelve exact ps_3_0 originals: six
// `XT_standard_lighting.fx` and six `standard_lighting.fx` material programs.
// Identity only; this admits no draw and reads no state.
constexpr unsigned linear_emission_hull_program_count = 12;
unsigned linear_emission_hull_program_index(std::uint64_t pixel) noexcept;
bool linear_emission_hull_program_reviewed(std::uint64_t pixel) noexcept;
// Colour blend admission of a hull-program emitter draw: only ADD ONE/ONE with
// blending on and sRGB write off. Screen (ONE/INVSRCCOLOR) is NOT substituted
// here (the plan keys phase 3 on the ONE/ONE materials), so it is an ordinary
// refusal; the returned verdict is never Screen.
SourceGainBlend linear_emission_hull_source_gain_blend(std::uint32_t blend_enable, std::uint32_t srgb_write,
                                                       std::uint32_t src, std::uint32_t dst, std::uint32_t op) noexcept;
// Pure creation-time gain of the ORIGINAL hull program's whole colour output
// (the emitter art of the ONE/ONE materials is diffuse-authored with the
// lightmap slot black, so the r0 sample alone carries nothing: archive check
// 2026-09-17, docs/architecture/emitter-plan.md phase 3): one
// `def c223 = (gain, 0, 0, 0)` before the first declaration, the final colour
// instruction (`add oC0.xyz, r1, r0` in standard_lighting, `mad oC0.xyz, r1,
// r2.z, r0` in XT_standard_lighting) redirected to write r0.xyz with its
// opcode, _pp, mask and operands kept (r0 is dead after it), and one
// `mul oC0.xyz, r0, c223.x` with the original destination token in its place.
// No linear material, no decode, no extra output: every instruction before
// the colour site and the native alpha (`mul oC0.w, r2.w, v0.w`) are
// verbatim, so the alpha lane and a black pixel are bit-identical. Gain 1
// returns the original words. Gain is finite 1..8. Any other program, any
// other tail, any use of c223 and any relative addressing fail closed.
// Failure leaves output intact; input may alias output. No D3D calls or
// per-draw work here.
LinearEmissionResult linear_emission_hull_source_gain_variant(const std::uint32_t* original, std::size_t words,
                                                              float gain, std::vector<std::uint32_t>& output) noexcept;
} // namespace x3m::renderer
