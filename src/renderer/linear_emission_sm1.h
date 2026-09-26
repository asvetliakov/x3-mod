#pragma once
#include "linear_emission.h"

namespace x3m::renderer {
// Probe-only promotion contract; not linked to or admitted by the live SM2 route.
enum class LinearEmissionSm1Outputs {
    Native = 1,
    Emission = 2,
    Coverage = 3,
    // Separate mathematical prototype: M and three packed channel planes,
    // not native B/E/coverage outputs. Full precision only; no live admission.
    PackedScreen = 4,
    // Additive option (screen-emission-region.md, "Additive option"): the
    // native PS2 path with the colour lanes multiplied by config.gain before
    // the output MOV; alpha stays the original's. One output, no coverage.
    AdditiveGain = 5
};
struct LinearEmissionSm1Config {
    float gain = 1.0f;
    LinearEmissionSm1Outputs outputs = LinearEmissionSm1Outputs::Coverage;
    // Explicit alternative for qualification, not an SM1 precision emulator.
    // Applies to the native sample/arithmetic/alpha copy; output MOV and all
    // new decode/gain/coverage math remain full precision.
    bool native_partial_precision = false;
};
bool linear_emission_sm1_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// Six whole-original PS1.1 identities, nine exact pairs; native VS1 is unchanged.
// Authors a PS2.0 equivalent native path plus optional E/coverage, retaining
// opaque comments. This cannot promise historical SM1 precision equivalence:
// native B/alpha/interpolation/MRT qualification is mandatory before integration.
// Screen composition and projected-TSS handling are separate runtime contracts.
// PackedScreen writes M=(1,0,0,a), Pc=(q_c,q_c,q_c,q_c): no per-fragment
// decode (step E of screen-emission-region.md; the pass masks the green lane
// and decodes the accumulated native lane once at publication, config.gain is
// unused). It requires four targets, independent masks and ONE/INVSRCALPHA.
// Failure preserves output; original may alias output. No D3D/per-draw work.
LinearEmissionResult linear_emission_sm1_pixel_variant(const std::uint32_t* original, std::size_t count,
                                                       const LinearEmissionSm1Config& config,
                                                       std::vector<std::uint32_t>& output) noexcept;
} // namespace x3m::renderer
