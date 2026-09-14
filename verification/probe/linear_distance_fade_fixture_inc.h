// Included only by the detached linear-material executable's explicit fade
// mode.
#include "../../src/renderer/linear_emission_pass.h"
#include "../../src/renderer/quad_vertex_program.h"
#include "../../src/proxy/fade_region_math.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace fade_fixture {
constexpr D3DFORMAT format = D3DFMT_A16B16G16R16F;
constexpr D3DRENDERSTATETYPE watched[] = {D3DRS_ZENABLE,
                                          D3DRS_ZWRITEENABLE,
                                          D3DRS_ALPHABLENDENABLE,
                                          D3DRS_SRCBLEND,
                                          D3DRS_DESTBLEND,
                                          D3DRS_BLENDOP,
                                          D3DRS_SEPARATEALPHABLENDENABLE,
                                          D3DRS_SRCBLENDALPHA,
                                          D3DRS_DESTBLENDALPHA,
                                          D3DRS_BLENDOPALPHA,
                                          D3DRS_COLORWRITEENABLE,
                                          D3DRS_COLORWRITEENABLE1,
                                          D3DRS_COLORWRITEENABLE2,
                                          D3DRS_SCISSORTESTENABLE,
                                          D3DRS_CULLMODE,
                                          D3DRS_ALPHATESTENABLE,
                                          D3DRS_FOGENABLE,
                                          D3DRS_DITHERENABLE,
                                          D3DRS_SRGBWRITEENABLE};
constexpr D3DSAMPLERSTATETYPE samplers[] = {
    D3DSAMP_ADDRESSU,  D3DSAMP_ADDRESSV,  D3DSAMP_MINFILTER,
    D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE};
struct Snapshot {
  Com<IDirect3DVertexShader9> vs;
  Com<IDirect3DPixelShader9> ps;
  Com<IDirect3DVertexBuffer9> vb;
  Com<IDirect3DIndexBuffer9> ib;
  Com<IDirect3DVertexDeclaration9> declaration;
  Com<IDirect3DSurface9> depth, extra[2];
  Com<IDirect3DBaseTexture9> textures[5]; // s0-s3 Asteroid, s0-s4 station BUMPMAP
  DWORD rs[std::size(watched)]{}, ss[5][std::size(samplers)]{};
  UINT offset = 0, stride = 0, frequency = 0;
  D3DVIEWPORT9 viewport{};
  RECT scissor{};
  explicit Snapshot(IDirect3DDevice9 *d) {
    api(d->GetVertexShader(&vs.p));
    api(d->GetPixelShader(&ps.p));
    api(d->GetVertexDeclaration(&declaration.p));
    api(d->GetDepthStencilSurface(&depth.p));
    api(d->GetStreamSource(0, &vb.p, &offset, &stride));
    api(d->GetStreamSourceFreq(0, &frequency));
    api(d->GetIndices(&ib.p));
    for (unsigned i = 0; i < 2; ++i) {
      HRESULT h = d->GetRenderTarget(i + 1, &extra[i].p);
      require(h == D3DERR_NOTFOUND || SUCCEEDED(h), "snapshot RT");
    }
    api(d->GetViewport(&viewport));
    api(d->GetScissorRect(&scissor));
    for (unsigned i = 0; i < std::size(watched); ++i)
      api(d->GetRenderState(watched[i], &rs[i]));
    for (unsigned i = 0; i < 5; ++i) {
      api(d->GetTexture(i, &textures[i].p));
      for (unsigned j = 0; j < std::size(samplers); ++j)
        api(d->GetSamplerState(i, samplers[j], &ss[i][j]));
    }
  }
  void check(IDirect3DDevice9 *d, IDirect3DSurface9 *expected_rt) const {
    Snapshot now(d);
    Com<IDirect3DSurface9> rt;
    api(d->GetRenderTarget(0, &rt.p));
    require(rt.p == expected_rt && vs.p == now.vs.p && ps.p == now.ps.p &&
                vb.p == now.vb.p && ib.p == now.ib.p &&
                declaration.p == now.declaration.p && depth.p == now.depth.p &&
                offset == now.offset && stride == now.stride &&
                frequency == now.frequency,
            "fade caller bindings restored");
    require(!std::memcmp(rs, now.rs, sizeof rs) &&
                !std::memcmp(ss, now.ss, sizeof ss) &&
                !std::memcmp(&viewport, &now.viewport, sizeof viewport) &&
                !std::memcmp(&scissor, &now.scissor, sizeof scissor),
            "fade caller state restored");
    for (unsigned i = 0; i < 2; ++i)
      require(extra[i].p == now.extra[i].p, "fade extra RT restored");
    for (unsigned i = 0; i < 5; ++i)
      require(textures[i].p == now.textures[i].p, "fade samplers restored");
  }
};
void dump(const char *label, unsigned id, unsigned step,
          const std::vector<Pixel> &pixels) {
  char name[100];
  std::snprintf(name, sizeof name, "fade_%u_%u_%s.rgba32f", id, step, label);
  std::ofstream file(name, std::ios::binary);
  file.write(reinterpret_cast<const char *>(pixels.data()),
             pixels.size() * sizeof(Pixel));
  require(bool(file), "fade readback write");
}
struct Target {
  Com<IDirect3DTexture9> texture;
  Com<IDirect3DSurface9> surface;
  Target(IDirect3DDevice9 *d, unsigned width, unsigned height) {
    api(d->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format,
                         D3DPOOL_DEFAULT, &texture.p, nullptr));
    api(texture->GetSurfaceLevel(0, &surface.p));
  }
  Target(IDirect3DDevice9 *d, unsigned size) : Target(d, size, size) {}
};
// Inject one real setter mutation followed by failure, not merely a skipped
// call.
using SetVs = HRESULT(WINAPI *)(IDirect3DDevice9 *, IDirect3DVertexShader9 *);
SetVs actual_set_vs = nullptr;
IDirect3DVertexShader9 *fail_vs = nullptr;
unsigned failed_vs_calls = 0;
HRESULT WINAPI set_vs(IDirect3DDevice9 *d, IDirect3DVertexShader9 *v) {
  HRESULT result = actual_set_vs(d, v);
  if (v && v == fail_vs && SUCCEEDED(result)) {
    fail_vs = nullptr;
    ++failed_vs_calls;
    return E_FAIL;
  }
  return result;
}
struct Draw {
  IDirect3DDevice9 *d;
  Com<IDirect3DVertexBuffer9> vb;
  Com<IDirect3DIndexBuffer9> ib;
  unsigned primitives = 0;
  // Ordinary source, two truly overlapping quads in one DIP, or two ordered
  // DIPs with different rectangles. Integer pixel bounds avoid edge
  // ambiguity.
  Draw(IDirect3DDevice9 *device, const Case &c, unsigned step, unsigned width)
      : Draw(device, c, RECT{LONG(step ? 6 : 2), LONG(step ? 6 : 2), LONG(step ? 14 : 10), LONG(step ? 14 : 10)},
             width, width, c.reverse == 1 ? 2 : 1) {}
  // Quad covering exactly the target pixels [left, right) x [top, bottom).
  Draw(IDirect3DDevice9 *device, const Case &c, const RECT &px, unsigned width, unsigned height, unsigned copies)
      : d(device) {
    primitives = 2 * copies;
    float verts[8][14]{};
    unsigned short indices[12]{};
    for (unsigned copy = 0; copy < copies; ++copy) {
      unsigned base = 4 * copy;
      const LONG xy[4][2] = {{px.left, px.top}, {px.right, px.top}, {px.left, px.bottom}, {px.right, px.bottom}};
      for (unsigned n = 0; n < 4; ++n) {
        auto &v = verts[base + n];
        v[0] = 2.f * (xy[n][0] - .25f) / width - 1;
        v[1] = 1 - 2.f * (xy[n][1] - .25f) / height;
        v[2] = .5f;
        std::memcpy(v + 5, c.f + 28, 12);
        std::memcpy(v + 8, c.f + 36, 12);
        std::memcpy(v + 11, c.f + 39, 12);
      }
      const unsigned short local[] = {0, 1, 2, 1, 3, 2};
      for (unsigned n = 0; n < 6; ++n)
        indices[copy * 6 + n] = static_cast<unsigned short>(base + local[n]);
    }
    api(d->CreateVertexBuffer(copies * 4 * 56, 0, 0, D3DPOOL_MANAGED, &vb.p,
                              nullptr));
    api(d->CreateIndexBuffer(copies * 6 * 2, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
                             &ib.p, nullptr));
    void *data = nullptr;
    api(vb->Lock(0, 0, &data, 0));
    std::memcpy(data, verts, copies * 4 * 56);
    api(vb->Unlock());
    api(ib->Lock(0, 0, &data, 0));
    std::memcpy(data, indices, copies * 6 * 2);
    api(ib->Unlock());
  }
  void bind() {
    api(d->SetStreamSource(0, vb.p, 0, 56));
    api(d->SetIndices(ib.p));
  }
  HRESULT issue(bool invalid = false) {
    if (invalid)
      api(d->SetIndices(nullptr));
    HRESULT hr = d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
                                         primitives * 2, 0, primitives);
    if (invalid)
      api(d->SetIndices(ib.p));
    return hr;
  }
};
struct Upload {
  IDirect3DDevice9 *d;
  unsigned width;
  Com<IDirect3DTexture9> texture;
  Com<IDirect3DVertexShader9> vs;
  Com<IDirect3DPixelShader9> ps;
  Com<IDirect3DVertexDeclaration9> declaration;
  Upload(IDirect3DDevice9 *device, unsigned size) : d(device), width(size) {
    api(d->CreateTexture(size, size, 1, 0, format, D3DPOOL_MANAGED, &texture.p,
                         nullptr));
    api(d->CreateVertexShader(
        reinterpret_cast<const DWORD *>(quad_vertex_program()), &vs.p));
    api(d->CreateVertexDeclaration(quad_declaration, &declaration.p));
    const DWORD copy[] = {0xffff0300, 0x0200001f, 0x80000005, 0x90030000,
                          0x0200001f, 0x90000000, 0xa00f0800, 0x03000042,
                          0x800f0000, 0x90e40000, 0xa0e40800, 0x02000001,
                          0x800f0800, 0x80e40000, 0xffff};
    api(d->CreatePixelShader(copy, &ps.p));
  }
  void background(IDirect3DSurface9 *target, bool hostile) {
    D3DLOCKED_RECT lock{};
    api(texture->LockRect(0, &lock, nullptr, 0));
    const unsigned short values[] = {0x0000, 0x1001, 0x3401, 0x3801,
                                     0x3c01, 0x4800, 0x5c00, 0x7bff};
    for (unsigned y = 0; y < width; ++y)
      for (unsigned x = 0; x < width; ++x) {
        auto *pixel = reinterpret_cast<unsigned short *>(
                          static_cast<char *>(lock.pBits) + y * lock.Pitch) +
                      4 * x;
        for (unsigned k = 0; k < 3; ++k)
          pixel[k] = hostile && x >= 2 && x < 10 && y >= 2 && y < 10
                         ? (k == 0   ? 0x7c00
                            : k == 1 ? 0xfc00
                                     : 0x7e00)
                         : values[(x + 3 * y + k) % 8];
        pixel[3] = 0x3555;
      }
    api(texture->UnlockRect(0));
    api(d->SetRenderTarget(2, nullptr));
    api(d->SetRenderTarget(1, nullptr));
    api(d->SetDepthStencilSurface(nullptr));
    api(d->SetRenderTarget(0, target));
    D3DVIEWPORT9 viewport{0, 0, width, width, 0, 1};
    api(d->SetViewport(&viewport));
    for (auto rs : {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE,
                    D3DRS_ALPHATESTENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
                    D3DRS_FOGENABLE, D3DRS_DITHERENABLE, D3DRS_SRGBWRITEENABLE,
                    D3DRS_SCISSORTESTENABLE})
      api(d->SetRenderState(rs, FALSE));
    api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15));
    api(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    api(d->SetVertexShader(vs.p));
    api(d->SetPixelShader(ps.p));
    api(d->SetVertexDeclaration(declaration.p));
    api(d->SetTexture(0, texture.p));
    for (auto ss : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER})
      api(d->SetSamplerState(0, ss, D3DTEXF_POINT));
    api(d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
    api(d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE));
    api(d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
    api(d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
    QuadVertex vertices[4];
    quad_vertices(width, width, vertices);
    api(d->BeginScene());
    api(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices,
                           sizeof(QuadVertex)));
    api(d->EndScene());
    api(d->SetTexture(0, nullptr));
  }
};
// Station producer AlphaValue by case flags (run_linear_distance_fade.py
// STATION_ALPHA_ZERO / STATION_ALPHA_ONE); otherwise the ordinary .625.
constexpr unsigned station_alpha_zero_flag = 0x400000, station_alpha_one_flag = 0x800000;
float station_alpha_value(const Case &c) {
  return (c.flags & station_alpha_zero_flag) ? 0.f : (c.flags & station_alpha_one_flag) ? 1.f : .625f;
}
void source_state(Gpu &gpu, Case c, IDirect3DSurface9 *target,
                  IDirect3DSurface9 *depth, Draw &draw) {
  gpu.state(c, 0);
  auto *d = gpu.d;
  api(d->SetRenderTarget(0, target));
  api(d->SetDepthStencilSurface(depth));
  api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1, 0));
  if (c.flags & 0x200000) {
    const D3DRECT occluder{0, 0, 6, 16};
    api(d->Clear(1, &occluder, D3DCLEAR_ZBUFFER, 0, .25f, 0));
  }
  api(d->SetRenderState(D3DRS_ZENABLE, TRUE));
  api(d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE));
  api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
  api(d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
  api(d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
  api(d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD));
  api(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE));
  api(d->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_DESTALPHA));
  api(d->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVDESTALPHA));
  api(d->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_REVSUBTRACT));
  api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 7));
  api(d->SetRenderState(D3DRS_COLORWRITEENABLE1, 3));
  api(d->SetRenderState(D3DRS_COLORWRITEENABLE2, 5));
  // Asteroid producers overwrite the material alpha with one so the texture
  // alpha alone drives the source; the station pair keeps its AlphaValue
  // (c39.x: .625 as the ordinary fixture, 0 or 1 by the case flags).
  const float alpha[4] = {c.pair == station_fade_pair ? station_alpha_value(c) : 1.f, 0, 0, 0};
  const unsigned vi = pair_v[c.pair];
  api(d->SetVertexShaderConstantF((vi == 9 || vi == 12) ? 18 : 39, alpha, 1));
  draw.bind();
}
// Region case group (docs/architecture/linear-distance-fade-region.md, step
// 1): asteroid-pair geometry sampled inside a synthetic object-space AABB,
// drawn through the unchanged prototype-1 bracket with the rows the
// production projection sees; every M pixel the source rasterised must lie in
// the rectangle x3m::fade_region::derive computes. Labels and order are
// mirrored by run_linear_distance_fade.py (REGION_CASES).
struct RegionCase {
  const char *label;
  float centre[3], half[3];
  float rows[16];        // rows as the application submits them (c24..c27)
  unsigned jitter;       // 0: none; else the 1-based Halton index (production sequence)
  unsigned viewport[4];  // application viewport; {0,0,0,0} = whole 16x16 target
  unsigned scissor;      // SCISSORTESTENABLE with rect (0,0,8,8)
  unsigned rows_known, bound_known, fill_solid;
};
constexpr float region_nan = std::numeric_limits<float>::quiet_NaN();
constexpr float region_inf = std::numeric_limits<float>::infinity();
// Perspective-like rows: x' = s x, y' = s y, w = z + d; z' = w / 2 unless
// near_plane, where z' = z crosses the near plane while every w stays > 0.
#define REGION_ROWS(s, d) {s, 0, 0, 0, 0, s, 0, 0, 0, 0, .5f, .5f * (d), 0, 0, 1, d}
#define REGION_NEAR(s, d) {s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1, 0, 0, 0, 1, d}
constexpr RegionCase region_cases[] = {
    {"interior", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"edge_left", {-1.5f, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"edge_right", {1.5f, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"edge_top", {0, 1.5f, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"edge_bottom", {0, -1.5f, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"corner", {1.5f, 1.5f, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"offscreen", {6, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"w_zero", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, .5f), 0, {}, 0, 1, 1, 1},
    {"w_negative", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 0), 0, {}, 0, 1, 1, 1},
    {"w_tiny", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, .5f + 1.f / 1024 + 1e-6f), 0, {}, 0, 1, 1, 1},
    {"near_plane", {0, 0, 0}, {.5f, .5f, .5f}, REGION_NEAR(1, 2), 0, {}, 0, 1, 1, 1},
    {"nan_rows", {0, 0, 0}, {.5f, .5f, .5f}, {region_nan, 0, 0, 0, 0, 1, 0, 0, 0, 0, .5f, 1, 0, 0, 1, 2}, 0, {}, 0, 1, 1, 1},
    {"inf_rows", {0, 0, 0}, {.5f, .5f, .5f}, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, .5f, 1, 0, 0, 1, region_inf}, 0, {}, 0, 1, 1, 1},
    {"rows_unknown", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 0, 1, 1},
    {"bound_unknown", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 0, 1},
    {"negative_extent", {0, 0, 0}, {.5f, -.5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"fill_wireframe", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 0},
    {"tiny", {0, 0, 0}, {.005f, .005f, .005f}, REGION_ROWS(1, 2), 0, {}, 0, 1, 1, 1},
    {"huge", {0, 0, 0}, {2, 2, 2}, REGION_ROWS(4, 5), 0, {}, 0, 1, 1, 1},
    {"viewport_offset", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {4, 4, 8, 8}, 0, 1, 1, 1},
    {"scissor", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 0, {}, 1, 1, 1, 1},
    {"jitter_1", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 1, {}, 0, 1, 1, 1},
    {"jitter_2", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 2, {}, 0, 1, 1, 1},
    {"jitter_3", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 3, {}, 0, 1, 1, 1},
    {"jitter_4", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 4, {}, 0, 1, 1, 1},
    {"jitter_5", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 5, {}, 0, 1, 1, 1},
    {"jitter_6", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 6, {}, 0, 1, 1, 1},
    {"jitter_7", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 7, {}, 0, 1, 1, 1},
    {"jitter_8", {0, 0, 0}, {.5f, .5f, .5f}, REGION_ROWS(1, 2), 8, {}, 0, 1, 1, 1},
};
#undef REGION_ROWS
#undef REGION_NEAR
// motion_output.cpp's jitter sequence: Halton bases 2 and 3, 1-based index,
// centred on the raster centre.
float region_halton(unsigned index, unsigned base) {
  float fraction = 1.f, result = 0.f;
  while (index) {
    fraction /= float(base);
    result += fraction * float(index % base);
    index /= base;
  }
  return result;
}
// Geometry sampled inside the box: the eight corners plus 24 deterministic
// interior points; the twelve face triangles plus 20 interior triangles.
struct BoxDraw {
  IDirect3DDevice9 *d;
  Com<IDirect3DVertexBuffer9> vb;
  Com<IDirect3DIndexBuffer9> ib;
  static constexpr unsigned vertices = 32, triangles = 32;
  BoxDraw(IDirect3DDevice9 *device, const Case &c, const RegionCase &r)
      : d(device) {
    float verts[vertices][14]{};
    unsigned short indices[triangles * 3]{};
    unsigned seed = 0x9e3779b9u;
    auto next = [&seed]() {
      seed = seed * 1664525u + 1013904223u;
      return float(seed >> 8) / float(1u << 24); // [0, 1)
    };
    for (unsigned n = 0; n < vertices; ++n) {
      auto &v = verts[n];
      for (unsigned a = 0; a < 3; ++a) {
        const float t = n < 8 ? float((n >> a) & 1u) : next();
        v[a] = r.centre[a] + (2 * t - 1) * r.half[a];
      }
      std::memcpy(v + 5, c.f + 28, 12);
      std::memcpy(v + 8, c.f + 36, 12);
      std::memcpy(v + 11, c.f + 39, 12);
    }
    const unsigned short faces[12][3] = {{0, 1, 2}, {1, 3, 2}, {4, 6, 5}, {5, 6, 7},
                                         {0, 4, 1}, {1, 4, 5}, {2, 3, 6}, {3, 7, 6},
                                         {0, 2, 4}, {2, 6, 4}, {1, 5, 3}, {3, 5, 7}};
    unsigned cursor = 0;
    for (const auto &f : faces)
      for (unsigned k = 0; k < 3; ++k)
        indices[cursor++] = f[k];
    while (cursor < triangles * 3)
      indices[cursor++] = static_cast<unsigned short>(unsigned(next() * vertices) % vertices);
    api(d->CreateVertexBuffer(vertices * 56, 0, 0, D3DPOOL_MANAGED, &vb.p, nullptr));
    api(d->CreateIndexBuffer(triangles * 6, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib.p, nullptr));
    void *data = nullptr;
    api(vb->Lock(0, 0, &data, 0));
    std::memcpy(data, verts, sizeof verts);
    api(vb->Unlock());
    api(ib->Lock(0, 0, &data, 0));
    std::memcpy(data, indices, sizeof indices);
    api(ib->Unlock());
  }
  void bind() {
    api(d->SetStreamSource(0, vb.p, 0, 56));
    api(d->SetIndices(ib.p));
  }
  HRESULT issue() {
    return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertices, 0, triangles);
  }
};
void equal(const std::vector<Pixel> &a, const std::vector<Pixel> &b,
           const char *message) {
  require(a.size() == b.size() &&
              !std::memcmp(a.data(), b.data(), a.size() * sizeof(Pixel)),
          message);
}
// Bit-exact comparison with a one-line witness of the first differing pixel.
void exact(const std::vector<Pixel> &actual, const std::vector<Pixel> &expected,
           const char *label, const char *message) {
  require(actual.size() == expected.size(), message);
  for (unsigned n = 0; n < actual.size(); ++n)
    if (std::memcmp(&actual[n], &expected[n], sizeof(Pixel))) {
      std::printf("FADE_INPLACE_DIFF label=%s pixel=%u x=%u y=%u actual=%g,%g,%g,%g expected=%g,%g,%g,%g\n",
                  label, n, n % 16, n / 16, actual[n].f[0], actual[n].f[1], actual[n].f[2], actual[n].f[3],
                  expected[n].f[0], expected[n].f[1], expected[n].f[2], expected[n].f[3]);
      require(false, message);
    }
}
constexpr auto in_place = LinearCompositionPolicy::DistanceFadeInPlace;
// Rectangle case group of the in-place policy (docs/architecture/
// linear-distance-fade-region.md, step 2). Every bracket runs twice: through
// the prototype-1 exchange on A and in place on a bit-exact copy of A with
// the rectangle; the in-place A must equal the exchanged C and the two M
// targets must agree bit-exactly. Labels, order and bracket counts are
// mirrored by run_linear_distance_fade.py (INPLACE_CASES).
struct InPlaceCase {
  const char *label;
  unsigned brackets;
  RECT draw[2];     // source quad pixels per bracket
  int known[2];     // 0: unknown rectangle (whole target)
  RECT region[2];   // the rectangle handed to the pass when known
  unsigned scissor; // application scissor (12,12,16,16) enabled: zero coverage
};
constexpr InPlaceCase inplace_cases[] = {
    {"full_unknown", 1, {{2, 2, 10, 10}, {}}, {0, 0}, {{}, {}}, 0},
    {"exact", 1, {{2, 2, 10, 10}, {}}, {1, 0}, {{2, 2, 10, 10}, {}}, 0},
    {"partial", 1, {{2, 2, 10, 10}, {}}, {1, 0}, {{0, 0, 12, 12}, {}}, 0},
    {"zero_scissor", 1, {{2, 2, 10, 10}, {}}, {1, 0}, {{0, 0, 16, 16}, {}}, 1},
    {"tiny", 1, {{5, 5, 6, 6}, {}}, {1, 0}, {{5, 5, 6, 6}, {}}, 0},
    {"disjoint", 2, {{0, 0, 6, 6}, {8, 8, 14, 14}}, {1, 1}, {{0, 0, 6, 6}, {8, 8, 14, 14}}, 0},
    {"overlapping", 2, {{2, 2, 10, 10}, {6, 6, 14, 14}}, {1, 1}, {{2, 2, 10, 10}, {6, 6, 14, 14}}, 0},
    {"unknown_then_known", 2, {{2, 2, 10, 10}, {6, 6, 14, 14}}, {0, 1}, {{}, {6, 6, 14, 14}}, 0},
};
void run(IDirect3DDevice9 *d, Shaders &shaders, const std::vector<Case> &cases,
         const Words &composition, bool after_reset) {
  Gpu gpu(d, shaders, 16);
  // Finish all immutable shader-cache creation before reference-lifetime proof.
  for (const auto &c : cases) {
    shaders.bind(c, 0);
    shaders.bind(c, 3);
  }
  api(d->SetVertexShader(nullptr));
  api(d->SetPixelShader(nullptr));
  const ULONG start_refs = d->AddRef();
  d->Release();
  unsigned checked = 0, brackets = 0, native_calls = 0, restored = 0;
  {
    Target scene(d, 16), native(d, 16);
    // Observe native/E alpha before FP16 output quantization. Actual composition
    // targets remain FP16; these unblended MRTs are diagnostic references only.
    Com<IDirect3DSurface9> alpha_m;
    api(d->CreateRenderTarget(16, 16, D3DFMT_A32B32G32R32F,
                             D3DMULTISAMPLE_NONE, 0, FALSE, &alpha_m.p, nullptr));
    Upload upload(d, 16);
    Com<IDirect3DSurface9> depth;
    api(d->CreateDepthStencilSurface(16, 16, D3DFMT_D24S8, D3DMULTISAMPLE_NONE,
                                     0, TRUE, &depth.p, nullptr));
    std::array<void *, 119> slots{};
    std::memcpy(slots.data(), *reinterpret_cast<void ***>(d), sizeof slots);
    std::memcpy(&actual_set_vs, &slots[92], sizeof actual_set_vs);
    slots[92] = reinterpret_cast<void *>(&set_vs);
    // The adapter argument is the current display format, not the color/RT
    // format. In windowed mode these need not match. Re-query after Reset.
    D3DDISPLAYMODE display{};
    api(d->GetDisplayMode(0, &display));
    if (!after_reset) {
      for (unsigned which = 0; which < 4; ++which) {
        auto caps = shaders.caps;
        if (!which)
          caps.NumSimultaneousRTs = 2;
        else
          caps.PrimitiveMiscCaps &=
              ~(which == 1   ? D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING
                : which == 2 ? D3DPMISCCAPS_SEPARATEALPHABLEND
                             : D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
        LinearEmissionPass refused;
        refused.fixture_source_over(
            reinterpret_cast<const DWORD *>(composition.data()));
        require(refused.attach(d, slots.data(), caps, display.Format,
                               D3DFMT_D24S8) == D3DERR_NOTAVAILABLE &&
                    !refused.caps().enabled && refused.references() == 0,
                "source-over capability refusal before allocation");
      }
      std::printf("FADE_CAPS refused=4\n");
    }
    LinearEmissionPass pass;
    pass.fixture_source_over(
        reinterpret_cast<const DWORD *>(composition.data()));
    // Step 2 twin: a second component instance (own pool, own M) runs the
    // in-place policy on a bit-exact copy of every pre-draw A.
    LinearEmissionPass inplace;
    inplace.fixture_source_over(reinterpret_cast<const DWORD *>(composition.data()));
    Target twin(d, 16);
    unsigned inplace_cases_run = 0, inplace_brackets = 0, inplace_native = 0, inplace_exact_a = 0, inplace_exact_m = 0;
    const HRESULT attached = pass.attach(d, slots.data(), shaders.caps,
                                         display.Format, D3DFMT_D24S8);
    std::printf("FADE_ATTACH reset=%u adapter=%u misc=%08lx hr=%08lx "
                "reason=%s formats=%08lx programs=%08lx\n",
                unsigned(after_reset), unsigned(display.Format),
                shaders.caps.PrimitiveMiscCaps, attached, pass.caps().reason,
                pass.caps().formats, pass.caps().programs);
    api(attached);
    require(pass.caps().enabled, "source-over component caps");
    api(pass.ensure_targets(16, 16));
    const auto allocations = pass.allocations();
    require(allocations == 4, "one B/E/C/M pool");
    require(pass.caps().supported_policies == 6 && pass.caps().available_policies == 6 &&
                pass.caps().supports(in_place) && pass.caps().supports(LinearCompositionPolicy::DistanceFade),
            "in-place policy available beside the exchange fade");
    api(inplace.attach(d, slots.data(), shaders.caps, display.Format, D3DFMT_D24S8));
    api(inplace.ensure_targets(16, 16));
    require(inplace.allocations() == 4 && inplace.caps().supports(in_place), "in-place twin pool");
    // One in-place bracket on the twin: arm() binds the identical source
    // state on the twin target, issue() runs the identical source once. The
    // result must equal the exchanged C and M of the primary bit-exactly.
    auto twin_bracket = [&](std::uint64_t frame, bool first, IDirect3DPixelShader9 *ps, IDirect3DVertexShader9 *vs,
                            bool known, RECT rect, auto &&arm, auto &&issue, const std::vector<Pixel> &expected_a,
                            const std::vector<Pixel> &expected_m, const char *label) {
      arm(twin.surface.p);
      Snapshot caller(d);
      if (first)
        require(inplace.begin_frame(frame).ready, "in-place M frame clear");
      LinearEmissionBoundary boundary{twin.surface.p, ps, frame, true, vs};
      boundary.policy = in_place;
      boundary.region = rect;
      boundary.region_known = known;
      api(d->BeginScene());
      const auto prep = inplace.prepare(boundary);
      if (!prep.ready)
        std::printf("FADE_INPLACE_REFUSAL label=%s saved=%08lx operation=%08lx restore=%08lx\n", label, prep.saved,
                    prep.operation, prep.restore);
      require(prep.ready, "in-place bracket prepared");
      const HRESULT source = issue();
      ++inplace_native;
      const auto done = inplace.finish(source);
      api(d->EndScene());
      if (FAILED(source) || done.image != LinearEmissionImage::Linear)
        std::printf("FADE_INPLACE_INCOMPLETE label=%s source=%08lx image=%u composition=%08lx restore=%08lx recovery=%08lx\n",
                    label, source, unsigned(done.image), done.composition, done.restore, done.recovery);
      require(SUCCEEDED(source) && done.image == LinearEmissionImage::Linear && !done.candidate_bound &&
                  done.recovery == S_FALSE && SUCCEEDED(done.restore) && SUCCEEDED(done.composition),
              "in-place completion");
      require(!inplace.owning_candidate() && inplace.acknowledge_exchange(true) == D3DERR_INVALIDCALL &&
                  inplace.recover_native().image == LinearEmissionImage::None && !inplace.reference_accounting_busy(),
              "in-place bracket reached the exchange path");
      caller.check(d, twin.surface.p);
      require(inplace.coverage_valid(), "in-place coverage complete");
      exact(gpu.read(twin.surface.p, format), expected_a, label, "in-place A differs from the exchanged C");
      ++inplace_exact_a;
      exact(gpu.read(inplace.coverage_target(), format), expected_m, label, "in-place M differs from the exchanged M");
      ++inplace_exact_m;
      ++inplace_brackets;
      require(inplace.allocations() == 4, "in-place per-draw pool allocation");
    };
    for (auto c : cases) {
      if (after_reset && (c.id % 10) != 0)
        continue;
      require(((c.pair >= 110 && c.pair < 116) || c.pair == station_fade_pair) && c.reverse <= 3 && c.affine <= 5,
              "fade case contract");
      if (after_reset)
        c.id += 1000;
      upload.background(scene.surface.p, (c.flags & 0x100000) != 0);
      const auto initial = gpu.read(scene.surface.p, format);
      dump("initial", c.id, 0, initial);
      unsigned steps = c.reverse == 2 ? 2 : c.reverse == 3 ? 16 : 1;
      unsigned case_native = 0, case_brackets = 0;
      for (unsigned step = 0; step < steps; ++step) {
        Case current = c;
        if (c.reverse == 2 && step) {
          std::swap(current.f[3], current.f[5]);
          current.f[6] = .25f;
        }
        Draw draw(d, current, c.reverse == 2 ? step : 0, 16);
        source_state(gpu, current, scene.surface.p, depth.p, draw);
        // Cold alpha transport witness: direct native/linear MRT outputs have
        // identical alpha bits before blending, including original MUL_pp
        // lowering.
        shaders.bind(current, 3);
        IDirect3DVertexShader9 *augmented_vs =
            shaders.vertices.at(shaders.key(current, 3, false));
        IDirect3DPixelShader9 *augmented_ps =
            shaders.pixels.at(shaders.key(current, 3, true));
        api(d->SetRenderTarget(0, gpu.color[0].p));
        api(d->SetRenderTarget(1, gpu.motion.p));
        api(d->SetRenderTarget(2, alpha_m.p));
        api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
        for (auto rs : {D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1,
                        D3DRS_COLORWRITEENABLE2})
          api(d->SetRenderState(rs, 15));
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
        api(d->BeginScene());
        api(draw.issue());
        api(d->EndScene());
        const auto raw_native = gpu.read(gpu.color[0].p, D3DFMT_A32B32G32R32F),
                   raw_linear = gpu.read(gpu.motion.p, D3DFMT_A32B32G32R32F);
        for (unsigned n = 0; n < raw_native.size(); ++n)
          require(!std::memcmp(&raw_native[n].f[3], &raw_linear[n].f[3], 4),
                  "actual native/E alpha identity");
        dump("source", c.id, step, raw_linear);
        // Compare against the separately created ORIGINAL program as well:
        // native B under blend alone cannot prove raw source-alpha precision.
        source_state(gpu, current, gpu.color[0].p, depth.p, draw);
        api(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15));
        api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
        api(d->BeginScene());
        api(draw.issue());
        api(d->EndScene());
        equal(raw_native, gpu.read(gpu.color[0].p, D3DFMT_A32B32G32R32F),
              "original and dual native RGBA exact");
        // Native B reference starts with exactly the same current A, not the
        // initial frame after an earlier ordered bracket has already composed a
        // new A.
        api(d->SetRenderTarget(2, nullptr));
        api(d->SetRenderTarget(1, nullptr));
        api(d->SetDepthStencilSurface(nullptr));
        api(d->SetRenderTarget(0, gpu.color[1].p));
        api(d->StretchRect(scene.surface.p, nullptr, native.surface.p, nullptr,
                           D3DTEXF_NONE));
        if (!c.affine)
          api(d->StretchRect(scene.surface.p, nullptr, twin.surface.p, nullptr, D3DTEXF_NONE));
        source_state(gpu, current, native.surface.p, depth.p, draw);
        api(d->BeginScene());
        api(draw.issue());
        api(d->EndScene());
        const auto expected_native = gpu.read(native.surface.p, format);
        source_state(gpu, current, scene.surface.p, depth.p, draw);
        Snapshot caller(d);
        if (!step) {
          auto begin = pass.begin_frame(c.id + 1);
          require(begin.ready && begin.state_preserved, "fade M frame clear");
          caller.check(d, scene.surface.p);
        }
        // A repeated begin_frame must not erase earlier producer coverage.
        const auto prior_mask = gpu.read(pass.coverage_target(), format);
        const bool prior_valid = pass.coverage_valid();
        auto same = pass.begin_frame(c.id + 1);
        require(!same.ready && pass.coverage_valid() == prior_valid,
                "same frame refuses re-clear");
        equal(gpu.read(pass.coverage_target(), format), prior_mask,
              "same frame retains earlier M bytes");
        LinearEmissionBoundary boundary{scene.surface.p, augmented_ps, c.id + 1,
                                        true, augmented_vs};
        if (!after_reset && !c.id && !step) {
          for (auto state : {D3DRS_SEPARATEALPHABLENDENABLE,
                             D3DRS_COLORWRITEENABLE, D3DRS_SRCBLEND}) {
            DWORD original = 0;
            api(d->GetRenderState(state, &original));
            api(d->SetRenderState(
                state, state == D3DRS_SEPARATEALPHABLENDENABLE ? TRUE
                       : state == D3DRS_COLORWRITEENABLE       ? 15
                                                               : D3DBLEND_ONE));
            Snapshot rejected_caller(d);
            api(d->BeginScene());
            const auto refusal = pass.prepare(boundary);
            api(d->EndScene());
            require(!refusal.ready && refusal.operation == D3DERR_INVALIDCALL &&
                        refusal.state_preserved && refusal.restore == S_FALSE,
                    "source-over exact state refusal before transfer");
            rejected_caller.check(d, scene.surface.p);
            equal(gpu.read(pass.coverage_target(), format), prior_mask,
                  "state refusal preserves M");
            api(d->SetRenderState(state, original));
          }
          caller.check(d, scene.surface.p);
          std::printf("FADE_STATE refused=3\n");
        }
        if (c.affine == 1)
          fail_vs = augmented_vs;
        if (c.affine == 2)
          pass.inject(LinearEmissionPassFault::Copy);
        if (c.affine == 4)
          pass.inject(LinearEmissionPassFault::Composite);
        api(d->BeginScene());
        const auto prep = pass.prepare(boundary);
        if (!prep.ready) {
          require(c.affine == 1 || c.affine == 2,
                  "unexpected preparation refusal");
          require(prep.operation == E_FAIL && prep.state_preserved,
                  "pre-source first failure and rollback");
          caller.check(d, scene.surface.p);
          ++restored;
          api(draw.issue());
          ++native_calls;
          ++case_native;
          api(d->EndScene());
          equal(gpu.read(scene.surface.p, format), expected_native,
                "clean refusal native source exactly once");
          equal(gpu.read(pass.coverage_target(), format), prior_mask,
                "clean refusal preserves earlier M");
          std::printf("FADE_FAILURE id=%u stage=%u native=1 prepared=0 "
                      "first=%08lx coverage=%u\n",
                      c.id, c.affine, prep.operation,
                      unsigned(pass.coverage_valid()));
          break;
        }
        ++brackets;
        ++case_brackets;
        if (c.affine == 5)
          pass.inject(LinearEmissionPassFault::Restore);
        const HRESULT source = draw.issue(c.affine == 3);
        ++native_calls;
        ++case_native;
        const auto finish = pass.finish(source);
        api(d->EndScene());
        require(finish.source == source, "original source HRESULT retained");
        if (c.affine == 3)
          require(FAILED(source) &&
                      finish.image == LinearEmissionImage::Incomplete &&
                      !pass.coverage_valid(),
                  "failed native source remains incomplete");
        else
          require(SUCCEEDED(source), "native source call failed");
        if (c.affine == 5)
          require(finish.restore == E_FAIL && !finish.candidate_bound &&
                      !pass.coverage_valid(),
                  "post-source restore failure remains incomplete");
        auto *slot = pass.owning_candidate();
        if (!slot && c.affine == 5) {
          const auto recovery = pass.recover_native();
          require(recovery.candidate_bound,
                  "native recovery binds and restores");
          slot = pass.owning_candidate();
        }
        const auto completed_native = gpu.read(pass.fixture_native(), format);
        require(slot && *slot, "owning candidate available");
        std::swap(scene.surface.p, *slot);
        api(pass.acknowledge_exchange(true));
        caller.check(d, scene.surface.p);
        ++restored;
        if (c.affine == 3 || c.affine == 5) {
          require(!pass.coverage_valid(),
                  "incomplete coverage survives B adoption");
          std::printf("FADE_FAILURE id=%u stage=%u native=1 prepared=1 "
                      "first=%08lx coverage=0\n",
                      c.id, c.affine, c.affine == 3 ? source : finish.restore);
          break;
        }
        equal(completed_native, expected_native,
              "native B exact under source-over MRT");
        if (c.affine == 4) {
          equal(gpu.read(scene.surface.p, format), expected_native,
                "composite failure adopts certified B");
          require(pass.coverage_valid(),
                  "successful source complete M after native B recovery");
          std::printf("FADE_FAILURE id=%u stage=4 native=1 prepared=1 "
                      "first=%08lx coverage=1\n",
                      c.id, finish.composition);
          break;
        }
        require(finish.image == LinearEmissionImage::Linear &&
                    pass.coverage_valid(),
                "linear candidate and complete mask");
        dump("E", c.id, step, gpu.read(pass.fixture_energy(), format));
        const auto composed_mask = gpu.read(pass.coverage_target(), format);
        dump("M", c.id, step, composed_mask);
        const auto composed = gpu.read(scene.surface.p, format);
        dump("C", c.id, step, composed);
        if (current.f[6] == 0)
          equal(composed, initial, "zero-alpha repeated bracket is exact raw A");
        // In-place twin of this step: known conservative rectangle for even
        // case rows, unknown (whole target) for odd rows.
        {
          const bool known = ((c.id / 10) % 2) == 0;
          const LONG lo = (c.reverse == 2 && step) ? 6 : 2, hi = (c.reverse == 2 && step) ? 14 : 10;
          twin_bracket(c.id + 1, step == 0, augmented_ps, augmented_vs, known, RECT{lo, lo, hi, hi},
                       [&](IDirect3DSurface9 *target) { source_state(gpu, current, target, depth.p, draw); },
                       [&]() { return draw.issue(); }, composed, composed_mask, "case");
        }
      }
      if (!c.affine)
        ++inplace_cases_run;
      require(pass.allocations() == allocations, "no per-draw pool allocation");
      std::printf("FADE_CASE id=%u pair=%u steps=%u native=%u brackets=%u "
                  "fault=%u reset=%u\n",
                  c.id, c.pair, steps, case_native, case_brackets, c.affine,
                  unsigned(after_reset));
      ++checked;
    }
    if (!after_reset) {
      // Region group: the 'full' case of pair 110 (alpha 1) supplies the
      // material state; the geometry, rows, viewport and scissor are the
      // region case's. The bracket is prototype 1, unchanged.
      const Case *base = nullptr;
      for (const auto &c : cases)
        if (c.id == 2) base = &c;
      require(base && base->pair == 110 && base->f[6] == 1, "region base case");
      unsigned region_violations = 0, region_bound_cases = 0;
      for (unsigned i = 0; i < std::size(region_cases); ++i) {
        const auto &r = region_cases[i];
        const unsigned id = 5000 + i;
        upload.background(scene.surface.p, false);
        api(d->StretchRect(scene.surface.p, nullptr, twin.surface.p, nullptr, D3DTEXF_NONE));
        BoxDraw draw(d, *base, r);
        Draw unused(d, *base, 0, 16);
        shaders.bind(*base, 3);
        IDirect3DVertexShader9 *augmented_vs = shaders.vertices.at(shaders.key(*base, 3, false));
        IDirect3DPixelShader9 *augmented_ps = shaders.pixels.at(shaders.key(*base, 3, true));
        // The rows the draw uses: the application's rows, jittered exactly as
        // MotionOutput::apply_jitter does (shared helper), on the device and
        // in the projection.
        float rows[16];
        std::memcpy(rows, r.rows, sizeof rows);
        if (r.jitter)
          x3m::fade_region::jitter_rows(rows, region_halton(r.jitter, 2) - .5f, region_halton(r.jitter, 3) - .5f, 16, 16);
        x3m::fade_region::Viewport viewport{0, 0, 16, 16};
        if (r.viewport[2])
          viewport = {r.viewport[0], r.viewport[1], r.viewport[2], r.viewport[3]};
        auto arm = [&](IDirect3DSurface9 *target) {
          source_state(gpu, *base, target, depth.p, unused);
          draw.bind();
          api(d->SetVertexShaderConstantF(24, rows, 4));
          if (r.viewport[2]) {
            D3DVIEWPORT9 vp{r.viewport[0], r.viewport[1], r.viewport[2], r.viewport[3], 0, 1};
            api(d->SetViewport(&vp));
          }
          if (r.scissor) {
            const RECT scissor{0, 0, 8, 8};
            api(d->SetScissorRect(&scissor));
            api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE));
          }
          api(d->SetRenderState(D3DRS_FILLMODE, r.fill_solid ? D3DFILL_SOLID : D3DFILL_WIREFRAME));
        };
        arm(scene.surface.p);
        x3m::fade_region::Box box{};
        for (unsigned a = 0; a < 3; ++a) { box.centre[a] = r.centre[a]; box.half[a] = r.half[a]; }
        const auto region = x3m::fade_region::derive(r.rows_known ? rows : nullptr, r.bound_known != 0, box, viewport, r.fill_solid != 0, x3m::fade_region::Rect{0, 0, 16, 16});
        const auto rect = x3m::fade_region::intersect(region.rect, x3m::fade_region::Rect{0, 0, 16, 16});
        Snapshot caller(d);
        auto begin = pass.begin_frame(id + 1);
        require(begin.ready && begin.state_preserved, "region M frame clear");
        LinearEmissionBoundary boundary{scene.surface.p, augmented_ps, id + 1, true, augmented_vs};
        api(d->BeginScene());
        const auto prep = pass.prepare(boundary);
        require(prep.ready, "region bracket prepared");
        const HRESULT source = draw.issue();
        const auto finish = pass.finish(source);
        api(d->EndScene());
        if (FAILED(source) || finish.image != LinearEmissionImage::Linear)
          std::printf("FADE_REGION_FAILURE label=%s source=%08lx image=%u composition=%08lx restore=%08lx\n",
                      r.label, source, unsigned(finish.image), finish.composition, finish.restore);
        require(SUCCEEDED(source) && finish.image == LinearEmissionImage::Linear, "region bracket completed");
        auto *slot = pass.owning_candidate();
        require(slot && *slot, "region owning candidate");
        std::swap(scene.surface.p, *slot);
        api(pass.acknowledge_exchange(true));
        caller.check(d, scene.surface.p);
        require(pass.coverage_valid(), "region coverage complete");
        const auto mask = gpu.read(pass.coverage_target(), format);
        dump("M", id, 0, mask);
        // In-place twin with the production rectangle: the conservativeness
        // witness of step 1 is exactly what makes the in-place result equal.
        twin_bracket(id + 1, true, augmented_ps, augmented_vs, true,
                     RECT{rect.left, rect.top, rect.right, rect.bottom}, arm, [&]() { return draw.issue(); },
                     gpu.read(scene.surface.p, format), mask, r.label);
        ++inplace_cases_run;
        std::printf("FADE_INPLACE_REGION id=%u label=%s rect=%d,%d,%d,%d exact=1\n", id, r.label, rect.left, rect.top,
                    rect.right, rect.bottom);
        unsigned covered = 0, violations = 0;
        for (unsigned n = 0; n < mask.size(); ++n) {
          if (mask[n].f[0] == 0)
            continue;
          ++covered;
          if (!x3m::fade_region::contains(rect, int(n % 16), int(n / 16)))
            ++violations;
        }
        region_violations += violations;
        region_bound_cases += region.bound;
        api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE));
        api(d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
        std::printf("FADE_REGION id=%u label=%s bound=%u reason=%u rect=%d,%d,%d,%d viewport=%u,%u,%u,%u covered=%u violations=%u area=%llu\n",
                    id, r.label, region.bound, unsigned(region.reason), rect.left, rect.top, rect.right, rect.bottom,
                    viewport.x, viewport.y, viewport.width, viewport.height, covered, violations,
                    static_cast<unsigned long long>(x3m::fade_region::area(rect)));
      }
      require(pass.allocations() == allocations, "no region pool allocation");
      require(region_violations == 0, "M pixel outside its region rectangle");
      std::printf("FADE_REGION_RESULT cases=%u bound=%u violations=%u\n",
                  unsigned(std::size(region_cases)), region_bound_cases, region_violations);
      // In-place rectangle group: alpha 0.5 so every covered pixel composes.
      Case blend = *base;
      blend.f[6] = .5f;
      shaders.bind(blend, 3);
      IDirect3DVertexShader9 *blend_vs = shaders.vertices.at(shaders.key(blend, 3, false));
      IDirect3DPixelShader9 *blend_ps = shaders.pixels.at(shaders.key(blend, 3, true));
      // Prototype-1 exchange bracket on the primary: returns exchanged C.
      auto exchange_bracket = [&](std::uint64_t frame, bool first, unsigned scissor, Draw &draw) {
        source_state(gpu, blend, scene.surface.p, depth.p, draw);
        if (scissor) {
          const RECT app{12, 12, 16, 16};
          api(d->SetScissorRect(&app));
          api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE));
        }
        Snapshot caller(d);
        if (first)
          require(pass.begin_frame(frame).ready, "rectangle M frame clear");
        LinearEmissionBoundary boundary{scene.surface.p, blend_ps, frame, true, blend_vs};
        api(d->BeginScene());
        require(pass.prepare(boundary).ready, "rectangle exchange bracket prepared");
        const HRESULT source = draw.issue();
        const auto finish = pass.finish(source);
        api(d->EndScene());
        require(SUCCEEDED(source) && finish.image == LinearEmissionImage::Linear, "rectangle exchange bracket completed");
        auto *slot = pass.owning_candidate();
        require(slot && *slot, "rectangle owning candidate");
        std::swap(scene.surface.p, *slot);
        api(pass.acknowledge_exchange(true));
        caller.check(d, scene.surface.p);
        require(pass.coverage_valid(), "rectangle coverage complete");
      };
      for (unsigned i = 0; i < std::size(inplace_cases); ++i) {
        const auto &r = inplace_cases[i];
        const unsigned id = 6000 + i;
        upload.background(scene.surface.p, false);
        api(d->StretchRect(scene.surface.p, nullptr, twin.surface.p, nullptr, D3DTEXF_NONE));
        const auto initial = gpu.read(scene.surface.p, format);
        for (unsigned k = 0; k < r.brackets; ++k) {
          Draw draw(d, blend, r.draw[k], 16, 16, 1);
          exchange_bracket(id + 1, k == 0, r.scissor, draw);
          const auto composed = gpu.read(scene.surface.p, format), mask = gpu.read(pass.coverage_target(), format);
          if (r.scissor) {
            equal(composed, initial, "zero coverage changed A");
            for (const auto &m : mask)
              require(m.f[0] == 0, "zero coverage marked M");
          }
          twin_bracket(id + 1, k == 0, blend_ps, blend_vs, r.known[k] != 0, r.region[k],
                       [&](IDirect3DSurface9 *target) {
                         source_state(gpu, blend, target, depth.p, draw);
                         if (r.scissor) {
                           const RECT app{12, 12, 16, 16};
                           api(d->SetScissorRect(&app));
                           api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE));
                         }
                       },
                       [&]() { return draw.issue(); }, composed, mask, r.label);
        }
        api(d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE));
        ++inplace_cases_run;
        std::printf("FADE_INPLACE id=%u label=%s brackets=%u exact=1\n", id, r.label, r.brackets);
      }
      // Failure ladder of the in-place bracket on the twin. Every stage issues
      // the source at most once; nothing is exchanged; the first HRESULT is
      // the chronologically first failure; a failure after the source leaves
      // the rectangle of A exactly as before the draw (recovered from B).
      Draw ladder_draw(d, blend, RECT{2, 2, 10, 10}, 16, 16, 1);
      const RECT ladder_rect{2, 2, 10, 10};
      const char *const ladder_labels[] = {"partial_vs", "copy", "region_scissor", "source", "composite",
                                           "composite_scissor", "restore", "recovery", "restore_recovery"};
      for (unsigned stage = 1; stage <= 9; ++stage) {
        const unsigned id = 7000 + stage;
        upload.background(twin.surface.p, false);
        const auto initial = gpu.read(twin.surface.p, format);
        // Exchange reference for the restore stage: the linear result stays.
        std::vector<Pixel> linear;
        if (stage == 7) {
          upload.background(scene.surface.p, false);
          equal(gpu.read(scene.surface.p, format), initial, "ladder reference background");
          exchange_bracket(id + 1, true, 0, ladder_draw);
          linear = gpu.read(scene.surface.p, format);
        }
        source_state(gpu, blend, twin.surface.p, depth.p, ladder_draw);
        Snapshot caller(d);
        require(inplace.begin_frame(id + 1).ready, "ladder M frame clear");
        const auto prior_mask = gpu.read(inplace.coverage_target(), format);
        LinearEmissionBoundary boundary{twin.surface.p, blend_ps, id + 1, true, blend_vs};
        boundary.policy = in_place;
        boundary.region = ladder_rect;
        boundary.region_known = true;
        if (stage == 1) fail_vs = blend_vs;
        if (stage == 2) inplace.inject(LinearEmissionPassFault::Copy);
        if (stage == 3) inplace.inject(LinearEmissionPassFault::RegionScissor);
        if (stage == 5) inplace.inject(LinearEmissionPassFault::Composite);
        if (stage == 6) inplace.inject(LinearEmissionPassFault::CompositeScissor);
        if (stage == 7 || stage == 9) inplace.inject(LinearEmissionPassFault::Restore);
        if (stage == 8) inplace.inject(LinearEmissionPassFault::RegionRecovery);
        api(d->BeginScene());
        const auto prep = inplace.prepare(boundary);
        HRESULT first = S_OK;
        unsigned prepared = 0, coverage = 0, blocked = 0;
        HRESULT recovery = S_FALSE;
        if (stage <= 3) {
          require(!prep.ready && prep.operation == E_FAIL && prep.state_preserved && SUCCEEDED(prep.saved),
                  "in-place pre-source first failure and rollback");
          caller.check(d, twin.surface.p);
          equal(gpu.read(twin.surface.p, format), initial, "in-place clean refusal touched A");
          equal(gpu.read(inplace.coverage_target(), format), prior_mask, "in-place clean refusal touched M");
          api(ladder_draw.issue());
          api(d->EndScene());
          first = prep.operation;
          coverage = inplace.coverage_valid();
          require(coverage == 1, "in-place clean refusal keeps coverage");
          // Not blocked: the same frame accepts the next bracket.
          api(d->BeginScene());
          require(inplace.prepare(boundary).ready, "clean refusal blocked the frame");
          const auto again = inplace.finish(ladder_draw.issue());
          api(d->EndScene());
          require(again.image == LinearEmissionImage::Linear, "post-refusal in-place bracket");
          caller.check(d, twin.surface.p);
        } else {
          require(prep.ready, "in-place ladder prepared");
          prepared = 1;
          const HRESULT source = ladder_draw.issue(stage == 4 || stage == 8 || stage == 9);
          const auto done = inplace.finish(source);
          api(d->EndScene());
          require(done.source == source, "in-place original source HRESULT retained");
          require(done.image == LinearEmissionImage::Incomplete && !done.candidate_bound, "in-place failure image");
          require(!inplace.owning_candidate() && inplace.acknowledge_exchange(true) == D3DERR_INVALIDCALL &&
                      inplace.recover_native().image == LinearEmissionImage::None && !inplace.reference_accounting_busy(),
                  "in-place failure reached the exchange path");
          if (stage == 9) {
            // Failed restore plus recovery: the recovery detached the three
            // sampler stages before the copy; everything else was restored
            // (the fault only flips the reported result).
            Com<IDirect3DSurface9> rt0;
            api(d->GetRenderTarget(0, &rt0.p));
            require(rt0.p == twin.surface.p, "restore/recovery stage lost the caller's A");
            for (unsigned i = 0; i < 3; ++i) {
              Com<IDirect3DBaseTexture9> texture;
              api(d->GetTexture(i, &texture.p));
              require(!texture.p, "recovery after failed restore left a sampler stage bound");
            }
          } else
            caller.check(d, twin.surface.p);
          recovery = done.recovery;
          if (stage == 9) {
            require(FAILED(source) && done.composition == S_FALSE && done.restore == E_FAIL && done.recovery == S_OK,
                    "failed restore must still recover the rectangle");
            first = source;
          } else if (stage == 4 || stage == 8) {
            require(FAILED(source) && done.composition == S_FALSE, "invalid source must fail before composition");
            first = source;
            require(done.recovery == (stage == 8 ? E_FAIL : S_OK), "source failure recovery");
          } else if (stage == 5 || stage == 6) {
            require(SUCCEEDED(source) && done.composition == E_FAIL && SUCCEEDED(done.restore) && done.recovery == S_OK,
                    "composite failure recovery");
            first = done.composition;
          } else {
            require(SUCCEEDED(source) && SUCCEEDED(done.composition) && done.restore == E_FAIL && done.recovery == S_FALSE,
                    "restore failure keeps the composed rectangle");
            first = done.restore;
            exact(gpu.read(twin.surface.p, format), linear, "restore", "restore failure lost the linear rectangle");
          }
          if (stage != 7 && stage != 8)
            exact(gpu.read(twin.surface.p, format), initial, ladder_labels[stage - 1],
                  "in-place recovery is not the exact pre-draw rectangle");
          coverage = inplace.coverage_valid();
          require(!coverage, "in-place failure left coverage valid");
          // Suppression: the frame stays blocked until the next begin_frame.
          const auto refused = inplace.prepare(boundary);
          blocked = !refused.ready && refused.saved == S_FALSE && refused.operation == S_FALSE;
          require(blocked, "failed in-place frame accepted another bracket");
        }
        std::printf("FADE_INPLACE_FAILURE id=%u stage=%u label=%s native=1 prepared=%u first=%08lx recovery=%08lx "
                    "coverage=%u blocked=%u exchange=0\n",
                    id, stage, ladder_labels[stage - 1], prepared, first, recovery, coverage, blocked);
      }
      require(inplace.allocations() == 4, "ladder allocated");
      // Capability refusal: without D3DPRASTERCAPS_SCISSORTEST the in-place
      // policy is unsupported and the same boundary falls back to the
      // exchange-based fade, whose result equals the in-place result.
      {
        auto caps = shaders.caps;
        caps.RasterCaps &= ~DWORD(D3DPRASTERCAPS_SCISSORTEST);
        LinearEmissionPass noscissor;
        noscissor.fixture_source_over(reinterpret_cast<const DWORD *>(composition.data()));
        require(noscissor.attach(d, slots.data(), caps, display.Format, D3DFMT_D24S8) == S_OK &&
                    noscissor.caps().enabled && noscissor.caps().supported_policies == 2 &&
                    noscissor.caps().available_policies == 2 && !noscissor.caps().supports(in_place) &&
                    noscissor.caps().supports(LinearCompositionPolicy::DistanceFade),
                "scissor capability gates only the in-place policy");
        api(noscissor.ensure_targets(16, 16));
        const unsigned id = 8000;
        upload.background(scene.surface.p, false);
        api(d->StretchRect(scene.surface.p, nullptr, twin.surface.p, nullptr, D3DTEXF_NONE));
        const auto initial = gpu.read(scene.surface.p, format);
        source_state(gpu, blend, scene.surface.p, depth.p, ladder_draw);
        Snapshot caller(d);
        require(noscissor.begin_frame(id + 1).ready, "no-scissor M frame clear");
        LinearEmissionBoundary boundary{scene.surface.p, blend_ps, id + 1, true, blend_vs};
        boundary.policy = in_place;
        boundary.region = ladder_rect;
        boundary.region_known = true;
        api(d->BeginScene());
        const auto refusal = noscissor.prepare(boundary);
        require(!refusal.ready && refusal.operation == D3DERR_NOTAVAILABLE && refusal.saved == S_FALSE &&
                    refusal.state_preserved && refusal.restore == S_FALSE && noscissor.coverage_valid(),
                "in-place refusal without scissor caps");
        caller.check(d, scene.surface.p);
        equal(gpu.read(scene.surface.p, format), initial, "capability refusal touched A");
        // Fallback: the exchange-based fade on the same pass and boundary.
        boundary.policy = LinearCompositionPolicy::DistanceFade;
        require(noscissor.prepare(boundary).ready, "exchange fallback prepared");
        const HRESULT source = ladder_draw.issue();
        const auto finish = noscissor.finish(source);
        api(d->EndScene());
        require(SUCCEEDED(source) && finish.image == LinearEmissionImage::Linear, "exchange fallback completed");
        auto *slot = noscissor.owning_candidate();
        require(slot && *slot, "fallback owning candidate");
        std::swap(scene.surface.p, *slot);
        api(noscissor.acknowledge_exchange(true));
        caller.check(d, scene.surface.p);
        const auto composed = gpu.read(scene.surface.p, format), mask = gpu.read(noscissor.coverage_target(), format);
        twin_bracket(id + 1, true, blend_ps, blend_vs, true, ladder_rect,
                     [&](IDirect3DSurface9 *target) { source_state(gpu, blend, target, depth.p, ladder_draw); },
                     [&]() { return ladder_draw.issue(); }, composed, mask, "no_scissor_fallback");
        ++inplace_cases_run;
        api(d->SetRenderTarget(2, nullptr));
        api(d->SetRenderTarget(1, nullptr));
        api(d->SetRenderTarget(0, gpu.back.p));
        api(d->SetDepthStencilSurface(nullptr));
        noscissor.before_reset();
        require(noscissor.references() == 4, "no-scissor pool retired");
        noscissor.detach();
        require(noscissor.references() == 0, "no-scissor references retired");
        std::printf("FADE_INPLACE_CAPS refused=1 fallback=1 exact=1\n");
      }
      // Reset with an in-place bracket interrupted after prepare: the owned
      // MRT attachments (E, M) are detached, the caller's A stays bound.
      {
        const unsigned id = 8100;
        upload.background(twin.surface.p, false);
        source_state(gpu, blend, twin.surface.p, depth.p, ladder_draw);
        require(inplace.begin_frame(id + 1).ready, "interrupted M frame clear");
        LinearEmissionBoundary boundary{twin.surface.p, blend_ps, id + 1, true, blend_vs};
        boundary.policy = in_place;
        boundary.region = ladder_rect;
        boundary.region_known = true;
        api(d->BeginScene());
        require(inplace.prepare(boundary).ready, "interrupted in-place bracket prepared");
        api(d->EndScene());
        require(inplace.reference_accounting_busy(), "interrupted bracket retains getters");
        inplace.before_reset();
        Com<IDirect3DSurface9> rt0, rt1, rt2;
        api(d->GetRenderTarget(0, &rt0.p));
        require(rt0.p == twin.surface.p, "interrupted in-place Reset replaced the caller's A");
        const HRESULT h1 = d->GetRenderTarget(1, &rt1.p), h2 = d->GetRenderTarget(2, &rt2.p);
        require((h1 == D3DERR_NOTFOUND || SUCCEEDED(h1)) && !rt1.p && (h2 == D3DERR_NOTFOUND || SUCCEEDED(h2)) && !rt2.p,
                "interrupted in-place Reset retained E/M attachments");
        require(inplace.references() == 4 && !inplace.reference_accounting_busy() && !inplace.coverage_valid() &&
                    !inplace.owning_candidate(),
                "interrupted in-place Reset retained bracket state");
        api(inplace.ensure_targets(16, 16));
        require(inplace.allocations() == 8, "in-place pool recreated after Reset");
        std::printf("FADE_INPLACE_RESET interrupted=1 detached=1\n");
      }
    }
    api(d->SetTexture(0, nullptr));
    api(d->SetTexture(1, nullptr));
    api(d->SetTexture(2, nullptr));
    api(d->SetTexture(3, nullptr));
    api(d->SetRenderTarget(2, nullptr));
    api(d->SetRenderTarget(1, nullptr));
    api(d->SetRenderTarget(0, gpu.back.p));
    api(d->SetDepthStencilSurface(nullptr));
    api(d->SetStreamSource(0, nullptr, 0, 0));
    api(d->SetIndices(nullptr));
    api(d->SetVertexDeclaration(nullptr));
    api(d->SetVertexShader(nullptr));
    api(d->SetPixelShader(nullptr));
    pass.before_reset();
    require(pass.references() == 4,
            "only fixed programs remain after pool retirement");
    pass.detach();
    require(pass.references() == 0, "pass owned references retired");
    inplace.before_reset();
    require(inplace.references() == 4, "in-place fixed programs remain after pool retirement");
    inplace.detach();
    require(inplace.references() == 0, "in-place owned references retired");
    std::printf("FADE_INPLACE_BATCH reset=%u cases=%u brackets=%u native=%u exact_a=%u exact_m=%u\n",
                unsigned(after_reset), inplace_cases_run, inplace_brackets, inplace_native, inplace_exact_a,
                inplace_exact_m);
  }
  const ULONG final_refs = d->AddRef();
  d->Release();
  require(final_refs == start_refs, "all experiment resources retired");
  std::printf("FADE_BATCH reset=%u cases=%u brackets=%u native=%u restored=%u "
              "refs_before=%lu refs_after=%lu\n",
              unsigned(after_reset), checked, brackets, native_calls, restored,
              start_refs, final_refs);
}
// Paired EVENT-fenced completion windows of 1/4/16 brackets per frame for the
// exchange fade and the in-place fade at whole-target, ~0.06 and ~0.01 area
// fractions. The window covers prepare/source/finish (and the exchange) of
// every bracket; the per-frame M clear runs before the fence. Diagnostic
// timings on a detached device, not game FPS.
void timing(IDirect3DDevice9 *d, Shaders &shaders, const std::vector<Case> &cases, const Words &composition) {
  Gpu gpu(d, shaders, 16);
  const Case *base = nullptr;
  for (const auto &c : cases)
    if (c.id == 2) base = &c;
  require(base && base->pair == 110, "timing base case");
  Case blend = *base;
  blend.f[6] = .5f;
  shaders.bind(blend, 3);
  IDirect3DVertexShader9 *vs = shaders.vertices.at(shaders.key(blend, 3, false));
  IDirect3DPixelShader9 *ps = shaders.pixels.at(shaders.key(blend, 3, true));
  std::array<void *, 119> slots{};
  std::memcpy(slots.data(), *reinterpret_cast<void ***>(d), sizeof slots);
  D3DDISPLAYMODE display{};
  api(d->GetDisplayMode(0, &display));
  Com<IDirect3DQuery9> event;
  api(d->CreateQuery(D3DQUERYTYPE_EVENT, &event.p));
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  std::uint64_t frame = 1;
  for (const auto &size : {std::make_pair(1280u, 768u), std::make_pair(1920u, 1080u)}) {
    const unsigned w = size.first, h = size.second;
    Target scene(d, w, h);
    Com<IDirect3DSurface9> depth;
    api(d->CreateDepthStencilSurface(w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth.p, nullptr));
    LinearEmissionPass pass;
    pass.fixture_source_over(reinterpret_cast<const DWORD *>(composition.data()));
    api(pass.attach(d, slots.data(), shaders.caps, display.Format, D3DFMT_D24S8));
    api(pass.ensure_targets(w, h));
    require(pass.caps().supports(in_place), "timing in-place caps");
    for (const double fraction : {1.0, .06, .01}) {
      RECT rect{0, 0, LONG(w), LONG(h)};
      if (fraction < 1) {
        const LONG side = LONG(std::lround(std::sqrt(fraction * w * h)));
        rect = {LONG(w / 2) - side / 2, LONG(h / 2) - side / 2, LONG(w / 2) - side / 2 + side, LONG(h / 2) - side / 2 + side};
      }
      const double actual = double(rect.right - rect.left) * double(rect.bottom - rect.top) / (double(w) * h);
      Draw draw(d, blend, rect, w, h, 1);
      for (const bool inplace : {false, true})
        for (const unsigned dips : {1u, 4u, 16u})
          for (unsigned iteration = 0; iteration < 10; ++iteration) {
            api(d->SetRenderTarget(2, nullptr));
            api(d->SetRenderTarget(1, nullptr));
            api(d->SetDepthStencilSurface(nullptr));
            api(d->SetRenderTarget(0, scene.surface.p));
            api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0x40404040, 1, 0));
            source_state(gpu, blend, scene.surface.p, depth.p, draw);
            require(pass.begin_frame(frame).ready, "timing M frame clear");
            api(event->Issue(D3DISSUE_END));
            gpu.wait(event.p);
            LARGE_INTEGER begin, end;
            QueryPerformanceCounter(&begin);
            api(d->BeginScene());
            for (unsigned k = 0; k < dips; ++k) {
              LinearEmissionBoundary boundary{scene.surface.p, ps, frame, true, vs};
              boundary.policy = inplace ? in_place : LinearCompositionPolicy::DistanceFade;
              boundary.region = rect;
              boundary.region_known = fraction < 1;
              require(pass.prepare(boundary).ready, "timing bracket prepared");
              const auto done = pass.finish(draw.issue());
              require(done.image == LinearEmissionImage::Linear, "timing bracket completed");
              if (!inplace) {
                auto *slot = pass.owning_candidate();
                require(slot && *slot, "timing owning candidate");
                std::swap(scene.surface.p, *slot);
                api(pass.acknowledge_exchange(true));
              }
            }
            api(d->EndScene());
            api(event->Issue(D3DISSUE_END));
            gpu.wait(event.p);
            QueryPerformanceCounter(&end);
            ++frame;
            if (iteration >= 2)
              std::printf("FADE_TIMING width=%u height=%u policy=%s f=%.4f rect=%ld,%ld,%ld,%ld dips=%u iteration=%u "
                          "completed_ms=%.6f\n",
                          w, h, inplace ? "inplace" : "exchange", actual, rect.left, rect.top, rect.right, rect.bottom,
                          dips, iteration - 2, 1000. * double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart));
          }
    }
    api(d->SetRenderTarget(2, nullptr));
    api(d->SetRenderTarget(1, nullptr));
    api(d->SetRenderTarget(0, gpu.back.p));
    api(d->SetDepthStencilSurface(nullptr));
    api(d->SetStreamSource(0, nullptr, 0, 0));
    api(d->SetIndices(nullptr));
    pass.before_reset();
    pass.detach();
    require(pass.references() == 0, "timing pass retired");
  }
  std::printf("FADE_TIMING_RESULT sizes=2 fractions=3 policies=2 dips=3 iterations=8\n");
}
} // namespace fade_fixture
void distance_fade_fixture(IDirect3DDevice9 *d, Shaders &shaders,
                           const std::vector<Case> &cases,
                           const char *composition_path) {
  const auto composition = load(composition_path);
  require(!composition.empty() && composition[0] == 0xffff0300,
          "frozen composite input");
  fade_fixture::run(d, shaders, cases, composition, false);
  Com<IDirect3DSwapChain9> chain;
  api(d->GetSwapChain(0, &chain.p));
  D3DPRESENT_PARAMETERS pp{};
  api(chain->GetPresentParameters(&pp));
  chain.p->Release();
  chain.p = nullptr;
  api(d->Reset(&pp));
  fade_fixture::run(d, shaders, cases, composition, true);
  fade_fixture::timing(d, shaders, cases, composition);
  std::printf("FADE_RESULT PASS reset=1 partial_vs_failures=%u\n",
              fade_fixture::failed_vs_calls);
}
