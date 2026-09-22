#pragma once
// Partial sun occlusion (docs/architecture/sun-partial-occlusion.md, "Lens draws" and "Step 2"):
// creation-time wraps of the lens scene's programs. The lens scene's programs were not known before
// the first diagnostic flight, so unlike the reviewed-pair transforms these are structural, and refuse
// everything they cannot prove. Pure: no D3D, no per-draw work; one allocation in `output`. Input may
// alias output.
//
// 1. Pixel wrap (step 1), any ps_2_0 / ps_2_x / ps_3_0 program: oC0 multiplied by the visibility
//    fraction held in a 1x1 texture.
//
//   def  cK, 0.5, 0.5, 0, 1          ; K, S, T, O: the highest constant / sampler and the two
//   dcl_2d sS                        ;   highest temporaries the program never names
//   ...original, every oC0 destination redirected to rO (mask, _sat, _pp kept)...
//   mov  rT, cK
//   texld rT, rT, sS
//   mul  rO.<mask>, rO, rT.y         ; .y = the shaped fraction (sun_visibility_ps.hlsl)
//   mov  oC0, rO
//
//   Refused (output untouched): ps_1_x and any non-pixel or unknown version, broken framing,
//   relative addressing, predicated instructions, call / callnz / ret / label, an oC0 write
//   inside flow control, oC0 not written in full, no free register, and (ps_2_0 / ps_2_x, counted
//   per the ps_2_0 slot table: lrp 2, pow / nrm 3, sincos 8, m3x3 3, m4x4 4, if / loop / rep 3 ...)
//   a program whose slots plus the wrap's (step 1: 3 arithmetic + 1 texture; clip: 32 + 10) would
//   exceed 64 arithmetic or 32 texture: wined3d does not enforce those limits, native D3D9 does,
//   and the same program must be treated alike on both. ps_2_x is held to the ps_2_0 floor (the
//   caps that raise it are not known here). A program the device still rejects is the caller's refusal.
//
// 2. Clip pair (step 2), vs_2_0 / vs_2_x with ps_2_0 / ps_2_x: the core bodies of the chain are clipped
//    per pixel against the route's scene depth (RT2, -1 sentinel = open), with a soft edge.
//
//   vertex: every oPos destination redirected to a free temporary rP; appended
//     mov oPos, rP / mov oTn, rP        ; n: a texcoord index free in both programs (lens_visibility_free_texcoord)
//   The oPos writes must be exactly four dp4 of one temporary with c[K], c[K+1], c[K+2], c[K+3] in lane
//   order (x, y, z, w), outside flow control: the body's local origin then lands at clip
//   (cK.w, cK+1.w, cK+2.w, cK+3.w), which the caller reads from its constant shadow to classify the body
//   (sun_occlusion::core::classify_body). `matrix_register` returns K.
//   pixel: the step-1 wrap plus, before the multiply,
//     dcl tn                            ; the clip position
//     def cC, 0.5, -0.5, 0.5 + dx/2, 0.5 + dy/2   ; dx, dy: one RT2 pixel in uv
//     def cD, 2dx, dy, dx, 2dy  /  def cW, -2dx, dy, -dx, 2dy   ; and cK.w = 1/9, the tap weight
//     uv = (tn.xy / tn.w) * (0.5, -0.5) + (0.5 + dx/2, 0.5 + dy/2)   ; the clip-derived uv of a fragment is a texel
//                                        edge under D3D9's pixel-centre rule; the half texel lands on the centre
//     nine taps of sD (RT2), every one on a texel centre and every offset one signed swizzle of cD / cW from the
//     fragment's uv: the fragment and the eight knight moves uv + (+-2 dx, +-dy), uv + (+-dx, +-2 dy), one ninth
//     each: open_px = ninths with .r < 0. The kernel is chosen so that a one-texel shift of a silhouette (RT2 is
//     on the jittered raster) moves any pixel's open_px by at most 2/9 = 0.222 along an axis (each column / row
//     of taps) and along a 45-degree diagonal (each dx + dy = const line), against 5/9 for the former +-1 / +-2
//     cross; across a vertical edge the profile is 1, 7/9, 5/9, 4/9, 2/9, 0 over five columns.
//     rO.<mask> *= open_px            (core_f: open_px * f)
//   Counts for a program like the lens scene's (8 arithmetic, 1 texture): 40 arithmetic, 11 texture
//   instructions, 9 temporaries, dependent-read depth 1: inside ps_2_0's limits (64 / 32 / 12 / 4), so
//   no promotion to ps_3_0 is needed and the program keeps its model.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
enum class LensVisibilityScale : std::uint8_t { Rgb = 1, Alpha = 2, Both = 3 }; // sun_occlusion::core::Scale values
enum class LensVisibilityResult { Applied, InvalidInput, UnsupportedVersion, UnsupportedShader, NoOutput, ResourceLimit, AllocationFailure };
// `constants`: every `def` register the wrap adds (K for the step-1 wrap; K, C, D, W for the clip pair; ~0u = unused). On
// native D3D9 a `def` writes the device's constant file when the program is set, so the caller records and restores them.
struct LensVisibilityLayout { unsigned sampler = 0, constant = 0, output_temporary = 0, fetch_temporary = 0, depth_sampler = 0; unsigned constants[4] = {~0u, ~0u, ~0u, ~0u}; };
const char* lens_visibility_result_name(LensVisibilityResult) noexcept;
LensVisibilityResult lens_visibility_pixel_variant(const std::uint32_t* original, std::size_t words, LensVisibilityScale scale,
                                                   std::vector<std::uint32_t>& output, LensVisibilityLayout* layout) noexcept;
// Step 2. `texcoord` 0..7 must be free in both programs; dx_u / dy_v are one RT2 pixel in uv; core_f multiplies the
// clipped body by the fraction as well (default off: a core body is clipped only, its ghosts carry f).
LensVisibilityResult lens_visibility_pixel_clip_variant(const std::uint32_t* original, std::size_t words, LensVisibilityScale scale, unsigned texcoord,
                                                        float dx_u, float dy_v, bool core_f, std::vector<std::uint32_t>& output, LensVisibilityLayout* layout) noexcept;
// origin_known: the dp4 source holds (position.xyz, 1), so the body's local origin is the rows' .w column (see the .cpp).
LensVisibilityResult lens_visibility_vertex_variant(const std::uint32_t* original, std::size_t words, unsigned texcoord,
                                                    std::vector<std::uint32_t>& output, unsigned* matrix_register, bool* origin_known) noexcept;
// The highest texcoord index the vertex program does not write and the pixel program does not declare;
// 8 when none, or when either program is not a vs_2_x / ps_2_x program.
unsigned lens_visibility_free_texcoord(const std::uint32_t* vertex, std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words) noexcept;
} // namespace x3m::renderer
