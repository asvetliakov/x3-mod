// Detached PS1.1 promotion qualification. Only authored fixture data is stored
// here; all nine VS and six PS originals are loaded from local fingerprinted
// files.
#define WIN32_LEAN_AND_MEAN
#include "../../src/renderer/linear_emission_sm1.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
namespace {
using Words = std::vector<std::uint32_t>;
constexpr unsigned W = 32, H = 32, CASES = 31;
void need(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
void api(HRESULT hr, const char *why) {
  if (FAILED(hr)) {
    std::printf("SM1_API hr=%08lx label=%s\n", hr, why);
    throw std::runtime_error(why);
  }
}
template <class T> struct Com {
  T *p = nullptr;
  Com() = default;
  Com(const Com &) = delete;
  Com &operator=(const Com &) = delete;
  ~Com() { reset(); }
  void reset() {
    if (p) {
      p->Release();
      p = nullptr;
    }
  }
  T *operator->() const { return p; }
};
struct Pair {
  const char *vs, *ps;
  unsigned layout;
}; // 0 DEFAULT, 1 INSTANCE, 2 bullet.
constexpr Pair pairs[] = {{"0d44b36d48d24f7a", "078494828322bcca", 0},
                          {"1b6863a088a177af", "84d3de8887c963c5", 2},
                          {"21a2c13be7f989c3", "d4a26efb7c603931", 2},
                          {"5e484a06672e28fb", "ec1f5c4a2f4e1445", 2},
                          {"637dadcb5efa3288", "078494828322bcca", 1},
                          {"6da1b1b6ed63ec82", "2ea025492d370c8e", 0},
                          {"88620f88d6e0a00e", "a5c3495e27270b4a", 0},
                          {"ed42e0742e47dca4", "2ea025492d370c8e", 1},
                          {"f9755e1154244f58", "a5c3495e27270b4a", 1}};
constexpr const char *names[] = {"unorm_point",
                                 "unorm_linear",
                                 "dxt_point",
                                 "dxt_linear",
                                 "fp16_point",
                                 "fp16_linear",
                                 "mip_bias_point",
                                 "mip_bias_linear",
                                 "address_wrap",
                                 "address_mirror",
                                 "fog_on",
                                 "fog_zero",
                                 "perspective",
                                 "clip",
                                 "flat",
                                 "wrap0",
                                 "alpha_ge128",
                                 "alpha_gt128",
                                 "rgb_zero",
                                 "gain_zero",
                                 "screen_native",
                                 "depth",
                                 "hdr_boundary",
                                 "fade_zero",
                                 "alpha_zero",
                                 "gain_2_5",
                                 "fog_fractional",
                                 "fog_lit",
                                 "ps1_range",
                                 "screen_overlap",
                                 "cap_before_fade_gain"};
std::uint64_t hash(const Words &w) {
  std::uint64_t h = 14695981039346656037ull;
  for (auto v : w)
    for (unsigned k = 0; k < 4; ++k) {
      h ^= (v >> (k * 8)) & 255;
      h *= 1099511628211ull;
    }
  return h;
}
Words load(const std::string &folder, const char *kind, const char *id) {
  std::ifstream in(folder + "/" + kind + "_" + id + ".bin",
                   std::ios::binary | std::ios::ate);
  need(bool(in), "local original missing");
  auto n = in.tellg();
  need(n > 0 && n % 4 == 0, "original word framing");
  Words w(std::size_t(n) / 4);
  in.seekg(0);
  in.read(reinterpret_cast<char *>(w.data()), n);
  need(bool(in), "original read");
  need(hash(w) == std::stoull(id, nullptr, 16), "whole original fingerprint");
  return w;
}
float fp16(unsigned short h) {
  unsigned s = unsigned(h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 1023, b;
  if (!e) {
    if (!m)
      b = s;
    else {
      int shift = 0;
      while (!(m & 1024)) {
        m <<= 1;
        ++shift;
      }
      b = s | unsigned(127 - 14 - shift) << 23 | (m & 1023) << 13;
    }
  } else
    b = s | ((e == 31 ? 255 : e + 112) << 23) | (m << 13);
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}
unsigned short half(float f) {
  unsigned b;
  std::memcpy(&b, &f, 4);
  unsigned s = (b >> 16) & 0x8000, m = b & 0x7fffff;
  int e = int((b >> 23) & 255) - 127 + 15;
  if (e >= 31)
    return static_cast<unsigned short>(s | 0x7c00);
  if (e <= 0) {
    if (e < -10)
      return static_cast<unsigned short>(s);
    m |= 0x800000;
    unsigned sh = unsigned(14 - e), v = m >> sh;
    unsigned rem = m & ((1u << sh) - 1);
    v += rem > (1u << (sh - 1)) || (rem == (1u << (sh - 1)) && (v & 1));
    return static_cast<unsigned short>(s | v);
  }
  unsigned v = m >> 13, rem = m & 8191;
  v += rem > 4096 || (rem == 4096 && (v & 1));
  return static_cast<unsigned short>(s + (unsigned(e) << 10) + v);
}
DWORD reg(unsigned t, unsigned i) {
  return 0x80000000u | ((t & 7) << 28) | ((t & 24) << 8) | i;
}
DWORD dst(unsigned t, unsigned i, unsigned mask = 15, bool pp = false) {
  return reg(t, i) | (mask << 16) | (pp ? 0x200000u : 0);
}
DWORD src(unsigned t, unsigned i, unsigned sw = 0xe4) {
  return reg(t, i) | (sw << 16);
}
void ins(Words &w, unsigned op, std::initializer_list<DWORD> a) {
  w.push_back(op | (unsigned(a.size()) << 24));
  w.insert(w.end(), a.begin(), a.end());
}
// Independently authored measurement shader: sample, raw COLOR multiplier and
// survival mask only. No gamma, gain, sanitization or native source
// multiplication.
Words witness(bool bullet, bool pp) {
  Words w{0xffff0200u};
  ins(w, 31, {0x80000000u, dst(3, 0, 3)});
  ins(w, 31, {0x80000000u, dst(1, 0, bullet ? 8 : 7)});
  ins(w, 31, {0x90000000u, dst(10, 0)});
  ins(w, 81, {dst(2, 0), 0x3f800000u, 0, 0, 0});
  ins(w, 66, {dst(0, 0, 15, pp), src(3, 0), src(10, 0)});
  ins(w, 1, {dst(8, 0), src(0, 0)});
  ins(w, 1, {dst(0, 1, 7), src(1, 0, bullet ? 0xff : 0)});
  ins(w, 1, {dst(0, 1, 8), src(0, 0, 0xff)});
  ins(w, 1, {dst(8, 1), src(0, 1)});
  ins(w, 1, {dst(0, 2), src(2, 0, 0)});
  ins(w, 1, {dst(8, 2), src(0, 2)});
  w.push_back(0xffffu);
  return w;
}
using Image = std::vector<unsigned short>;
struct Vertex {
  float x, y, z, u, v;
  DWORD color;
};
struct Fixture {
  Com<IDirect3D9> d3d;
  Com<IDirect3DDevice9> d;
  Com<IDirect3DVertexDeclaration9> decl;
  Com<IDirect3DVertexBuffer9> vb;
  Com<IDirect3DIndexBuffer9> ib;
  Com<IDirect3DSurface9> rt[3], readback, depth;
  Com<IDirect3DTexture9> tex;
  D3DCAPS9 caps{};
  D3DPRESENT_PARAMETERS pp{};
  HWND window = nullptr;
  unsigned tex_case = ~0u;
  Fixture() {
    window = CreateWindowA("STATIC", "Detached SM1 parity", WS_OVERLAPPEDWINDOW,
                           0, 0, 80, 80, nullptr, nullptr,
                           GetModuleHandleA(nullptr), nullptr);
    need(window != nullptr, "window");
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    need(d3d.p != nullptr, "D3D9");
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.Windowed = TRUE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    api(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                          D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &d.p),
        "device");
    api(d->GetDeviceCaps(&caps), "caps");
    std::printf("SM1_CAPS ps=%08lx vs=%08lx mrt=%lu ps1_max=%.9g "
                "independent_masks=%u post_blend=%u\n",
                caps.PixelShaderVersion, caps.VertexShaderVersion,
                caps.NumSimultaneousRTs, double(caps.PixelShader1xMaxValue),
                unsigned(bool(caps.PrimitiveMiscCaps &
                              D3DPMISCCAPS_INDEPENDENTWRITEMASKS)),
                unsigned(bool(caps.PrimitiveMiscCaps &
                              D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)));
    need(caps.PixelShaderVersion >= D3DPS_VERSION(2, 0) &&
             caps.NumSimultaneousRTs >= 3 && caps.PixelShader1xMaxValue > 0,
         "required SM2/3MRT/SM1 caps");
    need(bool(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING),
         "MRT post-pixel blend capability");
    D3DDISPLAYMODE display{};
    api(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &display),
        "adapter format");
    api(d3d->CheckDeviceFormat(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, display.Format,
            D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
            D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F),
        "FP16 render/blend format capability");
    const D3DVERTEXELEMENT9 el[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,
         0},
        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_TEXCOORD, 0},
        {0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,
         0},
        D3DDECL_END()};
    api(d->CreateVertexDeclaration(el, &decl.p), "declaration");
    api(d->CreateVertexBuffer(4 * sizeof(Vertex), 0, 0, D3DPOOL_MANAGED, &vb.p,
                              nullptr),
        "vertices");
    api(d->CreateIndexBuffer(24, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib.p,
                             nullptr),
        "indices");
    void *data = nullptr;
    api(ib->Lock(0, 0, &data, 0), "index lock");
    const unsigned short order[] = {0, 1, 2, 2, 1, 3, 0, 1, 2, 2, 1, 3};
    std::memcpy(data, order, sizeof order);
    api(ib->Unlock(), "index unlock");
    targets();
  }
  ~Fixture() {
    if (window)
      DestroyWindow(window);
  }
  void targets() {
    for (auto &r : rt)
      api(d->CreateRenderTarget(W, H, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE,
                                0, FALSE, &r.p, nullptr),
          "FP16 target");
    api(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A16B16G16R16F,
                                       D3DPOOL_SYSTEMMEM, &readback.p, nullptr),
        "readback");
    api(d->CreateDepthStencilSurface(W, H, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                                     TRUE, &depth.p, nullptr),
        "depth");
  }
  void reset() {
    api(d->SetTexture(0, nullptr), "reset texture");
    for (unsigned i = 1; i < 3; ++i)
      api(d->SetRenderTarget(i, nullptr), "reset MRT");
    Com<IDirect3DSurface9> back;
    api(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p),
        "reset backbuffer");
    api(d->SetRenderTarget(0, back.p), "reset RT0");
    api(d->SetDepthStencilSurface(nullptr), "reset depth");
    back.reset();
    for (auto &r : rt)
      r.reset();
    readback.reset();
    depth.reset();
    api(d->Reset(&pp), "Reset");
    targets();
    std::puts("SM1_RESET passed=1");
  }
  void texture(unsigned c) {
    if (tex_case == c)
      return;
    tex.reset();
    tex_case = c;
    const bool dxt = c == 2 || c == 3;
    const bool floating = c == 4 || c == 5 || c == 22 || c == 28 || c == 30;
    const auto fmt = dxt        ? D3DFMT_DXT5
                     : floating ? D3DFMT_A16B16G16R16F
                                : D3DFMT_A8R8G8B8;
    api(d->CreateTexture(8, 8, 4, 0, fmt, D3DPOOL_MANAGED, &tex.p, nullptr),
        "source texture");
    for (unsigned level = 0; level < 4; ++level) {
      D3DLOCKED_RECT lock{};
      api(tex->LockRect(level, &lock, nullptr, 0), "texture lock");
      unsigned n = std::max(1u, 8u >> level);
      if (dxt) {
        for (unsigned y = 0; y < (n + 3) / 4; ++y)
          for (unsigned x = 0; x < (n + 3) / 4; ++x) {
            unsigned char block[16] = {255,  0,    0,    0,    0,    0,
                                       0,    0,    0x1f, 0xf8, 0xe0, 0x07,
                                       0x44, 0xee, 0x44, 0xee};
            std::uint64_t ab = 0;
            for (unsigned k = 0; k < 16; ++k)
              ab |= std::uint64_t((k + x + y + level) % 8) << (3 * k);
            for (unsigned k = 0; k < 6; ++k)
              block[k + 2] = static_cast<unsigned char>(ab >> (8 * k));
            std::memcpy(static_cast<unsigned char *>(lock.pBits) +
                            y * lock.Pitch + x * 16,
                        block, 16);
          }
      } else
        for (unsigned y = 0; y < n; ++y)
          for (unsigned x = 0; x < n; ++x) {
            unsigned alpha = (c == 24 ? 0 : (x + y) % 3 + 127);
            float rgba[] = {
                float((x * 31 + y * 17 + level * 53) % 224 + 16) / 255,
                float((x * 13 + y * 29 + level * 71) % 224 + 16) / 255,
                float((x * 41 + y * 7 + level * 97) % 224 + 16) / 255,
                float(alpha) / 255};
            if (c == 29) {
              rgba[0] = 128.f / 255;
              rgba[1] = 64.f / 255;
              rgba[2] = 32.f / 255;
              rgba[3] = 128.f / 255;
              alpha = 128;
            }
            if (c == 30) {
              rgba[0] = rgba[1] = rgba[2] = 256.f;
              rgba[3] = .5f;
            }
            if (c == 18)
              rgba[0] = rgba[1] = rgba[2] = 0;
            if (c == 22 || c == 28) {
              const float limit =
                  c == 22
                      ? std::min(32752.f,
                                 std::max(4.f, 2 * caps.PixelShader1xMaxValue))
                      : std::min(8.f, caps.PixelShader1xMaxValue);
              for (unsigned k = 0; k < 3; ++k)
                rgba[k] *= limit;
            }
            if (floating) {
              auto *row = reinterpret_cast<unsigned short *>(
                  static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch);
              for (unsigned k = 0; k < 4; ++k)
                row[x * 4 + k] = half(rgba[k]);
            } else {
              DWORD color = (alpha << 24) |
                            (DWORD(std::lround(rgba[0] * 255)) << 16) |
                            (DWORD(std::lround(rgba[1] * 255)) << 8) |
                            DWORD(std::lround(rgba[2] * 255));
              std::memcpy(static_cast<unsigned char *>(lock.pBits) +
                              y * lock.Pitch + x * 4,
                          &color, 4);
            }
          }
      api(tex->UnlockRect(level), "texture unlock");
    }
  }
  void inputs(unsigned c, unsigned layout, IDirect3DVertexShader9 *vs) {
    texture(c);
    const float edge = c == 13 ? 1.6f : .92f;
    Vertex v[] = {{-edge, edge, .25f, -.15f, .1f, 0x2070d030u},
                  {edge, edge, .25f, 1.2f, .1f, 0xdf309070u},
                  {-edge, -edge, .25f, -.15f, 1.1f, 0x807030d0u},
                  {edge, -edge, .25f, 1.2f, 1.1f, 0xf0d07030u}};
    if (c == 15) {
      v[0].u = v[2].u = .9f;
      v[1].u = v[3].u = .1f;
    }
    if (c == 21)
      v[1].z = v[3].z = .9f;
    if (c == 29)
      for (auto &a : v)
        a.color = 0x803070d0u;
    if (c == 30)
      for (auto &a : v)
        a.color = 0x203070d0u;
    if (c == 11 || c == 23)
      for (auto &a : v)
        a.color &= 0xffffff;
    void *data = nullptr;
    api(vb->Lock(0, 0, &data, 0), "vertex lock");
    std::memcpy(data, v, sizeof v);
    api(vb->Unlock(), "vertex unlock");
    api(d->SetVertexShader(vs), "native VS");
    api(d->SetVertexDeclaration(decl.p), "input declaration");
    api(d->SetStreamSource(0, vb.p, 0, sizeof(Vertex)), "stream");
    api(d->SetStreamSourceFreq(0, 1), "stream frequency");
    api(d->SetIndices(ib.p), "index bind");
    const float matrix[] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, c == 12 ? .35f : 0, 0, 0, 1};
    api(d->SetVertexShaderConstantF(0, matrix, 4), "VP/WVP");
    if (layout != 2) {
      const float world[] = {.5f, 0, 0, -.2f, 0, .5f, 0, .1f, 0, 0, 0, 1},
                  camera[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 3};
      const float uv[] = {.75f, 0, .13f, 0, 0, -.8f, .9f, 0};
      const float flag[] = {
          c == 26 ? .5f : float(c == 10 || c == 11 || c == 27), 0, 0, 0};
      const float fade[] = {c == 23 ? 0.f : c == 30 ? .125f : .63f, 0, 0, 0};
      const float fog[] = {c == 11 ? -.5f : c == 27 ? 3.f : 1.4f, .35f, 0, 0};
      api(d->SetVertexShaderConstantF(4, world, 3), "world");
      api(d->SetVertexShaderConstantF(7, camera, 3), "camera");
      if (layout == 0)
        api(d->SetVertexShaderConstantF(10, uv, 2), "DEFAULT UV");
      unsigned base = layout == 0 ? 12 : 10;
      api(d->SetVertexShaderConstantF(base, flag, 1), "native float fog flag");
      api(d->SetVertexShaderConstantF(base + 1, fade, 1), "native alpha");
      api(d->SetVertexShaderConstantF(base + 2, fog, 1), "native fog clip");
    }
    api(d->SetTexture(0, tex.p), "source bind");
    for (unsigned stage = 1; stage < 4; ++stage)
      api(d->SetTexture(stage, nullptr), "unused texture");
    const bool linear = c == 1 || c == 3 || c == 5 || c == 7;
    api(d->SetSamplerState(0, D3DSAMP_MINFILTER,
                           linear ? D3DTEXF_LINEAR : D3DTEXF_POINT),
        "minfilter");
    api(d->SetSamplerState(0, D3DSAMP_MAGFILTER,
                           linear ? D3DTEXF_LINEAR : D3DTEXF_POINT),
        "magfilter");
    api(d->SetSamplerState(0, D3DSAMP_MIPFILTER,
                           c == 7   ? D3DTEXF_LINEAR
                           : c == 6 ? D3DTEXF_POINT
                                    : D3DTEXF_NONE),
        "mipfilter");
    // Texture is magnified before bias; these offsets positively select the
    // differently authored higher mips (and a fractional mip mix for case 7).
    float bias = c == 6 ? 3.f : c == 7 ? 2.65f : 0;
    DWORD bias_bits;
    std::memcpy(&bias_bits, &bias, 4);
    api(d->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, bias_bits), "mip bias");
    for (auto state : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV})
      api(d->SetSamplerState(0, state,
                             c == 8   ? D3DTADDRESS_WRAP
                             : c == 9 ? D3DTADDRESS_MIRROR
                                      : D3DTADDRESS_CLAMP),
          "address");
    api(d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE), "no sRGB sample");
    api(d->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0), "max mip");
    api(d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
                                D3DTTFF_DISABLE),
        "projection off");
  }
  std::array<Image, 3> render(IDirect3DPixelShader9 *ps, unsigned outputs,
                              unsigned c, bool measure = false) {
    for (unsigned i = 1; i < 3; ++i)
      api(d->SetRenderTarget(i, nullptr), "detach MRT");
    for (unsigned i = 0; i < 3; ++i)
      api(d->ColorFill(rt[i].p, nullptr,
                       i == 0 && !measure ? D3DCOLOR_ARGB(64, 32, 64, 96) : 0),
          "clear target");
    api(d->SetRenderTarget(0, rt[0].p), "RT0");
    for (unsigned i = 1; i < outputs; ++i)
      api(d->SetRenderTarget(i, rt[i].p), "MRT");
    api(d->SetDepthStencilSurface(depth.p), "depth bind");
    api(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, .55f, 0),
        "depth clear");
    const D3DVIEWPORT9 viewport{0, 0, W, H, 0, 1};
    api(d->SetViewport(&viewport), "viewport");
    const std::pair<D3DRENDERSTATETYPE, DWORD> states[] = {
        {D3DRS_ZENABLE, TRUE},
        {D3DRS_ZWRITEENABLE, FALSE},
        {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
        {D3DRS_ALPHABLENDENABLE, !measure},
        {D3DRS_SRCBLEND, D3DBLEND_ONE},
        {D3DRS_DESTBLEND,
         (c == 20 || c == 29) ? D3DBLEND_INVSRCCOLOR : D3DBLEND_ONE},
        {D3DRS_BLENDOP, D3DBLENDOP_ADD},
        {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
        {D3DRS_ALPHATESTENABLE, c == 16 || c == 17},
        {D3DRS_ALPHAFUNC, c == 17 ? D3DCMP_GREATER : D3DCMP_GREATEREQUAL},
        {D3DRS_ALPHAREF, 128},
        {D3DRS_CULLMODE, D3DCULL_NONE},
        {D3DRS_FILLMODE, D3DFILL_SOLID},
        {D3DRS_SHADEMODE, c == 14 ? D3DSHADE_FLAT : D3DSHADE_GOURAUD},
        {D3DRS_WRAP0, c == 15 ? D3DWRAP_U : 0},
        {D3DRS_FOGENABLE, FALSE},
        {D3DRS_STENCILENABLE, FALSE},
        {D3DRS_SCISSORTESTENABLE, FALSE},
        {D3DRS_SRGBWRITEENABLE, FALSE},
        {D3DRS_DITHERENABLE, FALSE},
        {D3DRS_CLIPPING, TRUE},
        {D3DRS_CLIPPLANEENABLE, 0},
        {D3DRS_COLORWRITEENABLE, 15},
        {D3DRS_COLORWRITEENABLE1, 15},
        {D3DRS_COLORWRITEENABLE2, 15}};
    for (auto state : states)
      api(d->SetRenderState(state.first, state.second), "render state");
    api(d->SetPixelShader(ps), "pixel shader");
    api(d->BeginScene(), "BeginScene");
    api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0,
                                c == 29 ? 4 : 2),
        "source DIP");
    api(d->EndScene(), "EndScene");
    std::array<Image, 3> result;
    for (unsigned i = 0; i < outputs; ++i) {
      api(d->GetRenderTargetData(rt[i].p, readback.p), "GPU readback");
      D3DLOCKED_RECT lock{};
      api(readback->LockRect(&lock, nullptr, D3DLOCK_READONLY),
          "readback lock");
      result[i].resize(W * H * 4);
      for (unsigned y = 0; y < H; ++y)
        std::memcpy(result[i].data() + y * W * 4,
                    static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch,
                    W * 8);
      api(readback->UnlockRect(), "readback unlock");
    }
    return result;
  }
};
void raw(const char *name, const Image &im) {
  FILE *f = std::fopen(name, "wb");
  need(f != nullptr, "witness file");
  need(std::fwrite(im.data(), 2, im.size(), f) == im.size(), "witness bytes");
  std::fclose(f);
}
double energy(double sample, double h, double gain) {
  sample = std::clamp(sample, 0., 65504.);
  return std::clamp(std::min(std::pow(sample, 2.2), 65504.) * h * gain, 0.,
                    65504.);
}
// FP16 readback quantizes the independent sample/h measurements. Propagate each
// half-ULP interval through monotonic decode/cap/gain, then allow the measured
// shader POW/FP16 arithmetic tolerance. Exact zero remains an exact-bit
// contract.
std::pair<double, double> envelope(unsigned short x) {
  double mid = fp16(x);
  if (!x)
    return {0, fp16(1) / 2.};
  if (x >= 0x7bff)
    return {mid, mid};
  return {(fp16(x - 1) + mid) / 2, (mid + fp16(x + 1)) / 2};
}
struct Measurement {
  unsigned rgb = 0, alpha = 0, e = 0, mask = 0, survival = 0;
  double max_native = 0, max_energy = 0;
  double overlap_actual = 0, overlap_shared = 0, overlap_q = 0;
};
Measurement compare(const std::array<Image, 3> &native,
                    const std::array<Image, 3> &actual,
                    const std::array<Image, 3> &reference, unsigned outputs,
                    float gain, bool alpha_test, bool overlap) {
  Measurement m;
  for (unsigned i = 0; i < W * H * 4; ++i) {
    if (actual[0][i] != native[0][i]) {
      if (i % 4 == 3)
        ++m.alpha;
      else
        ++m.rgb;
      m.max_native =
          std::max(m.max_native,
                   std::fabs(double(fp16(actual[0][i])) - fp16(native[0][i])));
    }
  }
  for (unsigned p = 0; p < W * H; ++p) {
    unsigned i = p * 4;
    const bool covered =
        alpha_test ? native[0][i + 3] != native[0][3] : reference[2][i] != 0;
    m.survival += covered;
    if (alpha_test && covered != (reference[2][i] != 0))
      ++m.mask;
    if (outputs >= 2) {
      for (unsigned k = 0; k < 4; ++k) {
        const auto bits = actual[1][i + k];
        const double got = fp16(bits);
        if (k == 3 || !covered || gain == 0 || reference[0][i + k] == 0 ||
            reference[1][i] == 0) {
          if (bits != 0)
            ++m.e;
          continue;
        }
        const auto sample = envelope(reference[0][i + k]),
                   h = envelope(reference[1][i]);
        double low = energy(sample.first, h.first, gain),
               high = energy(sample.second, h.second, gain);
        if (overlap) {
          // Two overlapping particles in ONE DIP. Shared MRT screen blending
          // uses this RT's E as INVSRCCOLOR; it cannot attenuate E by native q.
          auto shared = [](double e) {
            return e + (1 - e) * fp16(half(float(e)));
          };
          low = shared(low);
          high = shared(high);
          if (k == 0 && m.overlap_shared == 0) {
            const double t = fp16(reference[0][i]),
                         fade = fp16(reference[1][i]);
            const double e = energy(t, fade, gain), q = t * fade;
            m.overlap_actual = got;
            m.overlap_shared = shared(e);
            m.overlap_q = e + (1 - q) * fp16(half(float(e)));
          }
        }
        const double tolerance =
            .00004 + .003 * std::max(std::abs(low), std::abs(high));
        const double error = std::max({low - got, got - high, 0.});
        m.max_energy = std::max(m.max_energy, error / tolerance);
        if (!std::isfinite(got) || error > tolerance)
          ++m.e;
      }
    }
    if (outputs >= 3)
      for (unsigned k = 0; k < 3; ++k)
        if (actual[2][i + k] != (covered ? 0x3c00 : 0))
          ++m.mask;
  }
  return m;
}
} // namespace
int main(int argc, char **argv) {
  try {
    need(argc == 2, "usage: fixture.exe local-program-directory");
    Fixture f;
    unsigned rows = 0, unsupported = 0, parity_failures = 0,
             emission_failures = 0;
    bool saved[6]{};
    LARGE_INTEGER frequency;
    need(QueryPerformanceFrequency(&frequency), "QPC frequency");
    for (unsigned p = 0; p < 9; ++p) {
      const auto &pair = pairs[p];
      const auto vw = load(argv[1], "vs", pair.vs),
                 pw = load(argv[1], "ps", pair.ps);
      need(vw[0] == 0xfffe0101u && pw[0] == 0xffff0101u,
           "untouched SM1 models");
      need(x3m::renderer::linear_emission_sm1_pair_reviewed(hash(vw), hash(pw)),
           "exact pair admission");
      Com<IDirect3DVertexShader9> vs;
      Com<IDirect3DPixelShader9> original, measuring[2], promoted[6][4];
      HRESULT creation[6][4]{};
      api(f.d->CreateVertexShader(reinterpret_cast<const DWORD *>(vw.data()),
                                  &vs.p),
          "original VS1");
      api(f.d->CreatePixelShader(reinterpret_cast<const DWORD *>(pw.data()),
                                 &original.p),
          "original PS1");
      for (unsigned pp = 0; pp < 2; ++pp) {
        const auto code = witness(pair.layout == 2, pp != 0);
        api(f.d->CreatePixelShader(reinterpret_cast<const DWORD *>(code.data()),
                                   &measuring[pp].p),
            "measurement PS2");
      }
      for (unsigned mode = 0; mode < 6; ++mode)
        for (unsigned g = 0; g < 4; ++g) {
          x3m::renderer::LinearEmissionSm1Config config;
          config.gain = g == 0 ? 1.f : g == 1 ? 0.f : g == 2 ? 2.5f : .25f;
          config.outputs = static_cast<x3m::renderer::LinearEmissionSm1Outputs>(
              mode % 3 + 1);
          config.native_partial_precision = mode >= 3;
          Words code;
          const auto transformed =
              x3m::renderer::linear_emission_sm1_pixel_variant(
                  pw.data(), pw.size(), config, code);
          need(transformed == x3m::renderer::LinearEmissionResult::Applied,
               "exact source transform");
          LARGE_INTEGER start, end;
          QueryPerformanceCounter(&start);
          creation[mode][g] = f.d->CreatePixelShader(
              reinterpret_cast<const DWORD *>(code.data()),
              &promoted[mode][g].p);
          QueryPerformanceCounter(&end);
          std::printf("SM1_CREATE pair=%u mode=%u gain_index=%u hr=%08lx "
                      "words=%zu ms=%.9g\n",
                      p, mode, g, creation[mode][g], code.size(),
                      1000. * double(end.QuadPart - start.QuadPart) /
                          frequency.QuadPart);
        }
      // Projected TSS is an explicit negative admission witness: undefined
      // original oT0.w cannot justify a promoted TEXLD projection. Never submit
      // an enhanced draw in this state; restore the original application's
      // state immediately.
      api(f.d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
                                    D3DTTFF_COUNT4 | D3DTTFF_PROJECTED),
          "projected negative setter");
      DWORD tss = 0;
      api(f.d->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &tss),
          "projected negative getter");
      need(bool(tss & D3DTTFF_PROJECTED), "projected state observed");
      std::printf("SM1_PROJECTED pair=%u flags=%08lx enhanced_draws=0 "
                  "admitted=0 reason=undefined_source_w\n",
                  p, tss);
      api(f.d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
                                    D3DTTFF_DISABLE),
          "projected state restore");
      for (unsigned c = 0; c < CASES; ++c) {
        f.inputs(c, pair.layout, vs.p);
        const auto native = f.render(original.p, 1, c);
        std::array<Image, 3> ref[2] = {f.render(measuring[0].p, 3, c, true),
                                       f.render(measuring[1].p, 3, c, true)};
        for (unsigned mode = 0; mode < 6; ++mode) {
          ++rows;
          unsigned g = c == 19   ? 1
                       : c == 25 ? 2
                       : c == 30 ? 3
                                 : 0,
                   outputs = mode % 3 + 1;
          float gain = g == 0 ? 1.f : g == 1 ? 0.f : g == 2 ? 2.5f : .25f;
          if (FAILED(creation[mode][g])) {
            ++unsupported;
            std::printf("SM1_CASE pair=%u case=%u name=%s mode=%u "
                        "status=unsupported hr=%08lx boundary=%u\n",
                        p, c, names[c], mode, creation[mode][g],
                        unsigned(c == 22 || c == 30));
            continue;
          }
          auto actual = f.render(promoted[mode][g].p, outputs, c);
          auto m = compare(native, actual, ref[mode / 3], outputs, gain,
                           c == 16 || c == 17, c == 29);
          const bool parity = m.rgb || m.alpha, emission = m.e || m.mask;
          parity_failures += parity;
          emission_failures += emission;
          std::printf(
              "SM1_CASE pair=%u case=%u name=%s mode=%u status=measured "
              "rgb_diff=%u alpha_diff=%u e_diff=%u mask_diff=%u survived=%u "
              "max_native=%.9g max_energy_fraction=%.9g boundary=%u\n",
              p, c, names[c], mode, m.rgb, m.alpha, m.e, m.mask, m.survival,
              m.max_native, m.max_energy, unsigned(c == 22 || c == 30));
          if (c == 29 && outputs >= 2) {
            std::printf("SM1_OVERLAP pair=%u mode=%u dip=1 triangles=4 "
                        "layers=2 actual_e=%.9g shared_e=%.9g q_e=%.9g "
                        "gap=%.9g q_policy_implemented=0\n",
                        p, mode, m.overlap_actual, m.overlap_shared,
                        m.overlap_q, m.overlap_shared - m.overlap_q);
            if (p == 0) {
              char path[80];
              std::snprintf(path, sizeof path,
                            "overlap_mode%u_emission.rgba16f", mode);
              raw(path, actual[1]);
              std::snprintf(path, sizeof path, "overlap_mode%u_native.rgba16f",
                            mode);
              raw(path, native[0]);
            }
          }
          if ((parity || emission) && !saved[mode]) {
            saved[mode] = true;
            char prefix[80], path[96];
            std::snprintf(prefix, sizeof prefix, "failure_p%u_c%u_m%u", p, c,
                          mode);
            std::snprintf(path, sizeof path, "%s_native.rgba16f", prefix);
            raw(path, native[0]);
            for (unsigned i = 0; i < outputs; ++i) {
              std::snprintf(path, sizeof path, "%s_actual%u.rgba16f", prefix,
                            i);
              raw(path, actual[i]);
            }
            for (unsigned i = 0; i < 3; ++i) {
              std::snprintf(path, sizeof path, "%s_reference%u.rgba16f", prefix,
                            i);
              raw(path, ref[mode / 3][i]);
            }
            unsigned first = 0;
            while (first < native[0].size() &&
                   native[0][first] == actual[0][first])
              ++first;
            std::printf("SM1_WITNESS pair=%u case=%u mode=%u prefix=%s "
                        "native_lane=%u native_bits=%04x promoted_bits=%04x\n",
                        p, c, mode, prefix, first,
                        first < native[0].size() ? native[0][first] : 0,
                        first < actual[0].size() ? actual[0][first] : 0);
          }
        }
      }
      if (p == 4)
        f.reset();
    }
    std::printf("SM1_COMPLETE pairs=9 cases=%u rows=%u unsupported=%u "
                "parity_failures=%u emission_failures=%u reset=1\n",
                CASES, rows, unsupported, parity_failures, emission_failures);
    return 0;
  } catch (const std::exception &e) {
    std::printf("SM1_ABORT reason=%s\n", e.what());
    return 1;
  }
}
