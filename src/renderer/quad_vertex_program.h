#pragma once
// The one vertex program and vertex layout of every full-screen quad the
// proxy draws (temporal resolve, sharpen and copy draws, HDR write-back,
// tonemap and meter chain, the route's self tests and sentinel fill). D3D9
// pairs ps_3_0 programs with vs_3_0 (the fixed-function XYZRHW path is not a
// documented partner for them; wined3d never checked), so every pass creates
// one vertex shader from these words and one declaration from
// quad_declaration, both surviving Reset, and binds them where it used to
// bind SetFVF(XYZRHW...) + SetVertexShader(nullptr). The quad is in clip
// space with the same -0.5 pixel shift the pre-transformed quads carried
// (D3D9 pixel centres are integers): x in {-1 - 1/W, 1 - 1/W}, y in
// {1 + 1/H, -1 + 1/H}, z 0, w 1, TEXCOORD0 (0,0)..(1,1). Every proxy quad
// draws with the viewport equal to the whole target, so no constant is
// uploaded; the program is a pure pass-through (src/temporal/quad_vs.hlsl).
#include <d3d9.h>
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/quad-vertex-program.json.
inline constexpr std::uint32_t quad_vertex_words[] = {
#include "quad_vertex_program_inc.h"
};
}
inline constexpr const auto& quad_vertex_program() noexcept { return detail::quad_vertex_words; }
// Stride 24: POSITION float4 (clip space) then TEXCOORD0 float2. The same
// layout the pre-transformed quads used ({x, y, z, rhw, u, v}), so the
// fixture-only XYZRHW twin (X3M_QUAD_FVF_SWITCH) draws the same bytes.
struct QuadVertex { float x, y, z, w, u, v; };
inline constexpr D3DVERTEXELEMENT9 quad_declaration[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()};
inline constexpr DWORD quad_fvf = D3DFVF_XYZRHW | D3DFVF_TEX1;
// The clip-space strip covering a width x height target (TRIANGLESTRIP, 2 primitives).
inline void quad_vertices(UINT width, UINT height, QuadVertex (&out)[4]) noexcept {
    const float dx = 1.f / float(width), dy = 1.f / float(height);
    const float x0 = -1.f - dx, x1 = 1.f - dx, y0 = 1.f + dy, y1 = -1.f + dy;
    out[0] = {x0, y0, 0.f, 1.f, 0.f, 0.f}; out[1] = {x1, y0, 0.f, 1.f, 1.f, 0.f};
    out[2] = {x0, y1, 0.f, 1.f, 0.f, 1.f}; out[3] = {x1, y1, 0.f, 1.f, 1.f, 1.f};
}
// The pre-transformed twin of quad_vertices: raster coordinates shifted by
// -0.5, rhw 1 (the path every pass drew before the vs_3_0 program). Fixture
// builds compiled with X3M_QUAD_FVF_SWITCH select it through the environment
// (X3M_FIXTURE_QUAD_FVF=1) to prove the two paths byte-identical on the
// Preview backend; production never compiles the switch.
inline void quad_vertices_xyzrhw(UINT width, UINT height, QuadVertex (&out)[4]) noexcept {
    const float w = float(width) - .5f, h = float(height) - .5f;
    out[0] = {-.5f, -.5f, 0.f, 1.f, 0.f, 0.f}; out[1] = {w, -.5f, 0.f, 1.f, 1.f, 0.f};
    out[2] = {-.5f, h, 0.f, 1.f, 0.f, 1.f}; out[3] = {w, h, 0.f, 1.f, 1.f, 1.f};
}
#ifdef X3M_QUAD_FVF_SWITCH
inline bool quad_fvf_requested() noexcept {
    char setting[8]{};
    return GetEnvironmentVariableA("X3M_FIXTURE_QUAD_FVF", setting, sizeof setting) == 1 && setting[0] == '1';
}
#else
inline constexpr bool quad_fvf_requested() noexcept { return false; }
#endif
} // namespace x3m::renderer
