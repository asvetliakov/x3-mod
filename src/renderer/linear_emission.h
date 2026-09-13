#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
// Embedded in shader-local DEFs at creation. Changing gain needs a new variant.
struct LinearEmissionConfig { float gain = 1.0f; };
enum class LinearEmissionResult {
    Applied, InvalidInput, InvalidConfig, UnsupportedShader, ProfileMismatch,
    ResourceLimit, AllocationFailure
};
struct LinearEmissionAbi {
    static constexpr unsigned definition_base = 30;
    static constexpr unsigned emission_temporary = 2;
    static constexpr unsigned transfer_temporary = 3;
    static constexpr unsigned emission_output = 1;
};
bool linear_emission_config_valid(const LinearEmissionConfig& config) noexcept;
// Only the five shared engine/effects DEFAULT VS2/PS2 pairs. This establishes
// no ownership, blend, sampler, alpha, MRT, query or other live admission gate.
// INSTANCE aliases remain excluded despite sharing these pixel programs.
bool linear_emission_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;

// Pure creation-time PS2 augmentation; the original VS2 is never transformed.
// Preserve every original word, including comments, native PP oC0 and raw alpha.
// Copy pre-fade RGB, then append full-precision capped decode, preserved fade,
// gain and final finite sanitation into oC1 with exact +0 alpha. Native oC0 is
// never read. Failure leaves output intact; input may alias output.
// Original PP output is retained despite the literal SM2 output restriction:
// actual shader creation/native-output parity and native Windows execution are
// separate qualification gates. No D3D calls, uploads or per-draw work here.
LinearEmissionResult linear_emission_pixel_variant(const std::uint32_t* original,
    std::size_t words, const LinearEmissionConfig& config,
    std::vector<std::uint32_t>& output) noexcept;
} // namespace x3m::renderer
