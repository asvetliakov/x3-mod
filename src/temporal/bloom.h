#pragma once
// Bounded, allocation-free CPU ABI for the bloom kernels. See
// docs/architecture/hdr-bloom-filter.md; this file owns no GPU objects/state.
#include "agx.h"
#include <cstdint>

namespace x3::temporal {
constexpr unsigned kBloomMaxLevels = 6;
constexpr unsigned kBloomMaxDimension = 16384; // additionally enforce device caps
constexpr unsigned kBloomFirstRegister = 24;  // AgX c8..21, RCAS c23
constexpr unsigned kBloomRegisterCount = 5;
constexpr float kBloomMaxStrength = 1.f;
constexpr float kBloomMaxAuthoredGlowGain = 4.f;

struct BloomParams {
    unsigned levels = 5;
    float strength = 0.05f;
    float threshold = 1.f; // exposed-linear Rec.709 luminance; zero disables cut
    float knee = 0.5f;     // fraction of threshold, [0,1]
    float scatter = 0.7f;  // convex contribution from the next coarser level
    // Zero keeps the legacy RGB-only source arithmetic. Positive values opt
    // into alpha-authored colored glow plus complementary HDR highlights.
    float authored_glow_gain = 0.f;
    float highlight_gain = 0.05f; // only used in the authored-glow mode
    // Bloom-only decoded-space ceiling on the extraction source, applied by
    // bloomExposed() through the existing c27.y min before exposure. It never
    // touches the displayed scene. kAgxClampOff is the unbounded feed.
    // docs/architecture/bloom-falloff.md.
    float source_clamp = kAgxClampOff;
};
struct BloomSize { unsigned width = 0, height = 0; };
struct BloomLayout {
    BloomSize level[kBloomMaxLevels]{};
    unsigned count = 0;
    std::uint64_t pixels = 0; // sum of downsample levels, not bytes
};
struct BloomConstants {
    float source[4]{};      // c24: source/coarse width,height,1/width,1/height
    float destination[4]{}; // c25: output/fine width,height,1/width,1/height
    float filter[4]{1.f, 0.5f, 0.7f, 0.05f}; // c26: threshold,knee,scatter,strength
    float radiance[4]{1.f, kAgxClampOff, kAgxClampOff, 0.f}; // c27: exposure,decoded clamp,FP16 bound,authored-glow gain
    float decode[4]{kAgxDecodeGamma, 0.f, 0.f, 0.f}; // c28: AgX decode xyz; authored-mode highlight gain in w
};
static_assert(sizeof(BloomConstants) == kBloomRegisterCount * 4 * sizeof(float));

inline bool valid_bloom_params(const BloomParams& p) noexcept {
    return p.levels >= 1 && p.levels <= kBloomMaxLevels
        && std::isfinite(p.strength) && p.strength >= 0.f && p.strength <= kBloomMaxStrength
        && std::isfinite(p.threshold) && p.threshold >= 0.f && p.threshold <= kAgxClampOff
        && std::isfinite(p.knee) && p.knee >= 0.f && p.knee <= 1.f
        && std::isfinite(p.scatter) && p.scatter >= 0.f && p.scatter <= 1.f
        && std::isfinite(p.authored_glow_gain) && p.authored_glow_gain >= 0.f
        && p.authored_glow_gain <= kBloomMaxAuthoredGlowGain
        && std::isfinite(p.highlight_gain) && p.highlight_gain >= 0.f && p.highlight_gain <= 1.f
        && std::isfinite(p.source_clamp) && p.source_clamp > 0.f && p.source_clamp <= kAgxClampOff;
}
inline bool valid_bloom_size(BloomSize s) noexcept {
    return s.width && s.height && s.width <= kBloomMaxDimension && s.height <= kBloomMaxDimension;
}
// The four-tap extraction shader is legal only for this dimension class.
// The one-pixel axes and mixed parity cases use generic area extraction.
inline bool bloom_even_extraction(BloomSize s) noexcept {
    return valid_bloom_size(s) && (s.width % 2 == 0) && (s.height % 2 == 0);
}
// Six small authored programs avoid evaluating unselected transfer functions
// and keep the generic nine-source kernel within the portable SM3 slot limit.
// Order matches gamma/sRGB/none, then their even-dimension variants.
enum class BloomExtractShader { gamma22, srgb, none, even_gamma22, even_srgb, even_none };
inline bool select_bloom_extract(BloomExtractShader& out, BloomSize size, AgxDecode decode) noexcept {
    if (!valid_bloom_size(size)
        || (decode != AgxDecode::gamma22 && decode != AgxDecode::srgb && decode != AgxDecode::none)) return false;
    const unsigned mode = decode == AgxDecode::gamma22 ? 0 : decode == AgxDecode::srgb ? 1 : 2;
    out = static_cast<BloomExtractShader>(mode + (bloom_even_extraction(size) ? 3 : 0));
    return true;
}
// A 1x1 input still gets one 1x1 extraction level; do not repeat terminal levels.
// Failure leaves the output unchanged so a partial layout cannot be published.
inline bool prepare_bloom_layout(BloomLayout& out, BloomSize source, unsigned levels) noexcept {
    if (!valid_bloom_size(source) || !levels || levels > kBloomMaxLevels) return false;
    BloomLayout next{};
    do {
        source = {(source.width + 1) / 2, (source.height + 1) / 2};
        next.level[next.count++] = source;
        next.pixels += std::uint64_t(source.width) * source.height;
    } while (next.count < levels && (source.width > 1 || source.height > 1));
    out = next;
    return true;
}
// Dimensions describe the selected kernel's actual sampled/output textures.
// Downsample requires destination==ceil(source/2); upsample requires the
// source/coarse size==ceil(destination/fine/2). The renderer owns that check.
inline bool prepare_bloom(BloomConstants& out, BloomSize source, BloomSize destination,
                          const BloomParams& p, float exposure, float clamp_max,
                          AgxDecode decode) noexcept {
    if (!valid_bloom_params(p) || !valid_bloom_size(source) || !valid_bloom_size(destination)
        || (decode != AgxDecode::gamma22 && decode != AgxDecode::srgb && decode != AgxDecode::none)) return false;
    AgxConstants agx{};
    if (!prepare(agx, exposure, clamp_max, decode, AgxLook::none)) return false;
    BloomConstants next{};
    next.source[0] = float(source.width); next.source[1] = float(source.height);
    next.source[2] = 1.f / source.width; next.source[3] = 1.f / source.height;
    next.destination[0] = float(destination.width); next.destination[1] = float(destination.height);
    next.destination[2] = 1.f / destination.width; next.destination[3] = 1.f / destination.height;
    next.filter[0] = p.threshold; next.filter[1] = p.knee;
    next.filter[2] = p.scatter; next.filter[3] = p.strength;
    next.radiance[0] = agx.exposure[0];
    // The bloom source ceiling only ever tightens the display firefly clamp.
    next.radiance[1] = agx.exposure[1] < p.source_clamp ? agx.exposure[1] : p.source_clamp;
    for (unsigned i = 0; i < 4; ++i) next.decode[i] = agx.decode[i];
    next.radiance[3] = p.authored_glow_gain;
    // c28.w was unused; retain its old zero value in the legacy mode. xyz
    // remain the decode ABI, and c8..21 still carry the exact display block.
    if (p.authored_glow_gain > 0.f) next.decode[3] = p.highlight_gain;
    out = next;
    return true;
}
} // namespace x3::temporal
