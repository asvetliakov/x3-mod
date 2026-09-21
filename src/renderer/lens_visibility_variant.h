#pragma once
// Partial sun occlusion, step 1 (docs/architecture/sun-partial-occlusion.md, "Lens draws"):
// a creation-time wrap of an arbitrary ps_2_0 / ps_2_x / ps_3_0 program that multiplies its
// oC0 by the visibility fraction held in a 1x1 texture. The lens scene's programs are not
// known before the first diagnostic flight, so unlike the reviewed-pair transforms this one is
// structural, and refuses everything it cannot prove:
//
//   def  cK, 0.5, 0.5, 0, 1          ; K, S, T, O: the highest constant / sampler and the two
//   dcl_2d sS                        ;   highest temporaries the program never names
//   ...original, every oC0 destination redirected to rO (mask, _sat, _pp kept)...
//   mov  rT, cK
//   texld rT, rT, sS
//   mul  rO.<mask>, rO, rT.y         ; .y = the shaped fraction (sun_visibility_ps.hlsl)
//   mov  oC0, rO
//
// Refused (output untouched): ps_1_x and any non-pixel or unknown version, broken framing,
// relative addressing, predicated instructions, call / callnz / ret / label, an oC0 write
// inside flow control, oC0 not written in full, no free register. A program the device then
// rejects (ps_2_0 instruction or dependent-read limits) is the caller's refusal. Pure: no
// D3D, no per-draw work; one allocation in `output`. Input may alias output.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
enum class LensVisibilityScale : std::uint8_t { Rgb = 1, Alpha = 2, Both = 3 }; // sun_occlusion::core::Scale values
enum class LensVisibilityResult { Applied, InvalidInput, UnsupportedVersion, UnsupportedShader, NoOutput, ResourceLimit, AllocationFailure };
struct LensVisibilityLayout { unsigned sampler = 0, constant = 0, output_temporary = 0, fetch_temporary = 0; };
const char* lens_visibility_result_name(LensVisibilityResult) noexcept;
LensVisibilityResult lens_visibility_pixel_variant(const std::uint32_t* original, std::size_t words, LensVisibilityScale scale,
                                                   std::vector<std::uint32_t>& output, LensVisibilityLayout* layout) noexcept;
} // namespace x3m::renderer
