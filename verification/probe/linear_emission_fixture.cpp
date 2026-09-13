// Detached authored-shader experiment. No game shaders or renderer linkage.
// run_linear_emission.py owns the independent per-store FP16 oracle.
#define WIN32_LEAN_AND_MEAN
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
void need(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
void api(HRESULT h) {
  if (FAILED(h)) {
    std::printf("API_FAIL %08lx\n", h);
    throw std::runtime_error("D3D9 API");
  }
}
template <class T> struct Com {
  T *p = nullptr;
  Com() = default;
  Com(const Com &) = delete;
  ~Com() {
    if (p)
      p->Release();
  }
  T *operator->() const { return p; }
};
using Words = std::vector<DWORD>;
DWORD reg(unsigned t, unsigned n) {
  return 0x80000000u | ((t & 7) << 28) | ((t & 24) << 8) | n;
}
DWORD dst(unsigned t, unsigned n, unsigned mask = 15) {
  return reg(t, n) | (mask << 16);
}
DWORD src(unsigned t, unsigned n, unsigned sw = 0xe4, bool neg = false) {
  return reg(t, n) | (sw << 16) | (neg ? 1u << 24 : 0);
}
DWORD bits(float f) {
  DWORD b;
  std::memcpy(&b, &f, 4);
  return b;
}
void ins(Words &w, unsigned op, std::initializer_list<DWORD> a) {
  w.push_back(op | (unsigned(a.size()) << 24));
  w.insert(w.end(), a);
}
void def(Words &w, unsigned n, float x, float y, float z, float a) {
  ins(w, 81, {dst(2, n), bits(x), bits(y), bits(z), bits(a)});
}
// r0 holds RGBA. MAX/MIN operands, floors and exact-zero selection match the
// qualified RGB contract. Alpha never enters arithmetic or the RGB sanitizer.
void gamma(Words &w, bool encode) {
  ins(w, 11, {dst(0, 1, 7), src(0, 0), src(2, 20, 0)});
  ins(w, 10, {dst(0, 1, 7), src(0, 1), src(2, 20, 0x55)});
  ins(w, 11, {dst(0, 2, 7), src(0, 1), src(2, 20, encode ? 0xff : 0xaa)});
  for (unsigned k = 0; k < 3; ++k)
    ins(w, 32,
        {dst(0, 2, 1u << k), src(0, 2, k * 0x55),
         src(2, 21, encode ? 0x55 : 0)});
  ins(w, 88, {dst(0, 0, 7), src(0, 1, 0xe4, true), src(2, 20, 0), src(0, 2)});
  if (!encode) {
    ins(w, 11, {dst(0, 0, 7), src(0, 0), src(2, 20, 0)});
    ins(w, 10, {dst(0, 0, 7), src(0, 0), src(2, 20, 0x55)});
  }
}
// 0 constant,1 native source,2 linear source,3 decode,4 encode,5 copy,
// 6 deliberately wrong scene-end layer combine. All bytecode is authored here.
Words shader(unsigned kind) {
  Words w{0xffff0300};
  if (kind >= 1) {
    ins(w, 31, {0x80000005, dst(1, 0, 3)});
    ins(w, 31, {0x90000000, dst(10, 0)});
    if (kind == 6)
      ins(w, 31, {0x90000000, dst(10, 1)});
  }
  if (kind == 1 || kind == 2)
    ins(w, 31, {0x8000000a, dst(1, 1, 1)});
  def(w, 20, 0, 65504, 1e-10f, 1e-22f);
  def(w, 21, 2.2f, 1.f / 2.2f, 0, 0);
  if (kind >= 1)
    ins(w, 66, {dst(0, 0), src(1, 0), src(10, 0)});
  else
    ins(w, 1, {dst(0, 0), src(2, 0)});
  if (kind == 1 || kind == 2) {
    ins(w, 5,
        {dst(0, 0, 7), src(0, 0),
         src(2, 0)}); // sampled RGB only; alpha stays raw
    ins(w, 4,
        {dst(0, 0, 7), src(0, 0), src(2, 2),
         src(2, 3)}); // authored affine before decode
    if (kind == 2)
      gamma(w, false);
    ins(w, 5,
        {dst(0, 0, 7), src(0, 0),
         src(1, 1, 0)}); // interpolated native fade after decode
    if (kind == 2)
      ins(w, 5, {dst(0, 0, 7), src(0, 0), src(2, 1, 0)});
  }
  if (kind == 3)
    gamma(w, false);
  if (kind == 6) {
    ins(w, 66, {dst(0, 3), src(1, 0), src(10, 1)});
    ins(w, 2, {dst(0, 0, 7), src(0, 0), src(0, 3)});
  }
  if (kind == 4 || kind == 6)
    gamma(w, true);
  ins(w, 1, {dst(8, 0), src(0, 0)});
  w.push_back(0xffff);
  return w;
}
// SM3 must pair with SM3, including on native Windows. This authored VS passes
// explicit clip positions/UVs; it does not depend on a fixed-function adapter.
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3
Words vertex_shader() {
  Words w{0xfffe0300};
  ins(w, 31, {0x80000000, dst(1, 0)});
  ins(w, 31, {0x80000005, dst(1, 1, 3)});
  ins(w, 31, {0x8000000a, dst(1, 2, 1)});
  ins(w, 31, {0x80000000, dst(6, 0)});
  ins(w, 31, {0x80000005, dst(6, 1, 3)});
  ins(w, 31, {0x8000000a, dst(6, 2, 1)});
  ins(w, 1, {dst(6, 0), src(1, 0)});
  ins(w, 1, {dst(6, 1, 3), src(1, 1)});
  ins(w, 1, {dst(6, 2, 1), src(1, 2)});
  w.push_back(0xffff);
  return w;
}
struct Op {
  unsigned kind;
  float x0, y0, x1, y1, z, color[4], fade, gain;
  unsigned affine;
};
struct Header {
  unsigned id, mode, mask, alpha, write, fault, pattern, flags, count;
};
static_assert(sizeof(Op) == 52 && sizeof(Header) == 36, "case ABI");
struct Case {
  Header h{};
  std::vector<Op> ops;
};
struct Vertex {
  float x, y, z, rhw, u, v, fade;
};
struct Target {
  Com<IDirect3DTexture9> texture;
  Com<IDirect3DSurface9> surface;
};
struct Saved {
  Com<IDirect3DStateBlock9> block;
  Com<IDirect3DSurface9> rt, depth;
  explicit Saved(IDirect3DDevice9 *d) {
    api(d->CreateStateBlock(D3DSBT_ALL, &block.p));
  }
  void capture(IDirect3DDevice9 *d) {
    if (rt.p) {
      rt.p->Release();
      rt.p = nullptr;
    }
    if (depth.p) {
      depth.p->Release();
      depth.p = nullptr;
    }
    api(block->Capture());
    api(d->GetRenderTarget(0, &rt.p));
    api(d->GetDepthStencilSurface(&depth.p));
  }
  void restore(IDirect3DDevice9 *d) {
    api(d->SetTexture(0, nullptr));
    api(d->SetTexture(1, nullptr));
    api(d->SetRenderTarget(0, rt.p));
    api(d->SetDepthStencilSurface(depth.p));
    api(block->Apply());
  }
};
float half(unsigned short h) {
  unsigned sign = unsigned(h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 1023,
           b;
  if (e == 0) {
    if (!m)
      b = sign;
    else {
      int p = -14;
      while (!(m & 1024)) {
        m <<= 1;
        --p;
      }
      b = sign | unsigned(p + 127) << 23 | (m & 1023) << 13;
    }
  } else
    b = sign | (e == 31 ? 255 : e + 112) << 23 | m << 13;
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}
struct Outcome {
  unsigned accepted = 0, fallback = 0, incomplete = 0, restored = 1,
           brackets = 0, replays = 0;
};
struct Fixture {
  IDirect3DDevice9 *d;
  unsigned width, height, rt_slots;
  Target scene, scratch, layer, mask, probe;
  Com<IDirect3DSurface9> depth;
  Com<IDirect3DPixelShader9> ps[7];
  Com<IDirect3DVertexShader9> vs;
  Com<IDirect3DVertexDeclaration9> declaration;
  Com<IDirect3DTexture9> source_texture[2];
  Com<IDirect3DQuery9> event;
  Saved source, replay;
  Fixture(IDirect3DDevice9 *device, unsigned w, unsigned h)
      : d(device), width(w), height(h), source(d), replay(d) {
    D3DCAPS9 caps{};
    api(d->GetDeviceCaps(&caps));
    rt_slots = caps.NumSimultaneousRTs;
    auto vertex = vertex_shader();
    api(d->CreateVertexShader(vertex.data(), &vs.p));
    D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,
         0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_TEXCOORD, 0},
        {0, 24, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,
         0},
        D3DDECL_END()};
    api(d->CreateVertexDeclaration(elements, &declaration.p));
    const unsigned short texels[4][4] = {{0x3c00, 0x3c00, 0x3c00, 0x3000},
                                         {0x3800, 0x3c00, 0x3a00, 0x3800},
                                         {0x3c00, 0x3800, 0x3800, 0},
                                         {0x3a00, 0x3400, 0x3c00, 0x3c00}};
    for (unsigned t = 0; t < 2; ++t) {
      unsigned side = t ? 2 : 1;
      api(d->CreateTexture(side, side, 1, 0, D3DFMT_A16B16G16R16F,
                           D3DPOOL_MANAGED, &source_texture[t].p, nullptr));
      D3DLOCKED_RECT l;
      api(source_texture[t]->LockRect(0, &l, nullptr, 0));
      for (unsigned y = 0; y < side; ++y)
        for (unsigned x = 0; x < side; ++x)
          std::memcpy(static_cast<char *>(l.pBits) + y * l.Pitch + x * 8,
                      texels[y * side + x], 8);
      api(source_texture[t]->UnlockRect(0));
    }
    for (auto *t : {&scene, &scratch, &layer})
      target(*t, D3DFMT_A16B16G16R16F);
    for (auto *t : {&mask, &probe})
      target(*t, D3DFMT_R32F);
    api(d->CreateDepthStencilSurface(w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                                     TRUE, &depth.p, nullptr));
    for (unsigned i = 0; i < 7; ++i) {
      auto words = shader(i);
      api(d->CreatePixelShader(words.data(), &ps[i].p));
    }
    api(d->CreateQuery(D3DQUERYTYPE_EVENT, &event.p));
  }
  ~Fixture() {
    d->SetTexture(0, nullptr);
    d->SetTexture(1, nullptr);
    d->SetPixelShader(nullptr);
    d->SetVertexShader(nullptr);
    d->SetVertexDeclaration(nullptr);
    d->SetDepthStencilSurface(nullptr);
  }
  void target(Target &t, D3DFORMAT f) {
    api(d->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, f,
                         D3DPOOL_DEFAULT, &t.texture.p, nullptr));
    api(t.texture->GetSurfaceLevel(0, &t.surface.p));
  }
  void rs(D3DRENDERSTATETYPE s, DWORD v) { api(d->SetRenderState(s, v)); }
  void bind(Target &t) {
    api(d->SetTexture(0, nullptr));
    api(d->SetTexture(1, nullptr));
    api(d->SetRenderTarget(0, t.surface.p));
  }
  void base() {
    D3DVIEWPORT9 v{0, 0, width, height, 0, 1};
    api(d->SetViewport(&v));
    api(d->SetVertexShader(vs.p));
    api(d->SetVertexDeclaration(declaration.p));
    RECT full_rect{0, 0, LONG(width), LONG(height)};
    api(d->SetScissorRect(&full_rect));
    rs(D3DRS_SCISSORTESTENABLE, FALSE);
    rs(D3DRS_CULLMODE, D3DCULL_NONE);
    rs(D3DRS_ZENABLE, FALSE);
    rs(D3DRS_ZWRITEENABLE, FALSE);
    rs(D3DRS_STENCILENABLE, FALSE);
    rs(D3DRS_STENCILWRITEMASK, 0);
    rs(D3DRS_ALPHATESTENABLE, FALSE);
    rs(D3DRS_ALPHABLENDENABLE, FALSE);
    rs(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    rs(D3DRS_SRGBWRITEENABLE, FALSE);
    rs(D3DRS_COLORWRITEENABLE, 15);
    rs(D3DRS_FOGENABLE, FALSE);
    rs(D3DRS_LIGHTING, FALSE);
    for (unsigned s = 0; s < 2; ++s) {
      api(d->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT));
      api(d->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
      api(d->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
      api(d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
      api(d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
      api(d->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE));
    }
  }
  void quad(const Op &o, unsigned flags = 0) {
    // D3D9 pixel centers are integral. Apply the half-pixel offset once in
    // clip space so transfer UVs hit the identical source texel centers.
    float vw = flags & 16 ? width / 2.f : float(width),
          vh = flags & 16 ? height / 2.f : float(height);
    float l = 2 * o.x0 - 1 - 1.f / vw, r = 2 * o.x1 - 1 - 1.f / vw,
          t = 1 - 2 * o.y0 + 1.f / vh, b = 1 - 2 * o.y1 + 1.f / vh;
    float right_fade = flags & 8 ? o.fade * .5f : o.fade;
    Vertex v[] = {{l, t, o.z, 1, 0, 0, o.fade},
                  {r, t, o.z, 1, 1, 0, right_fade},
                  {l, b, o.z, 1, 0, 1, o.fade},
                  {r, b, o.z, 1, 1, 1, right_fade}};
    api(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex)));
  }
  Op full() {
    return Op{0, 0, 0, 1, 1, .75f, {.125f, .25f, .375f, .25f}, 1, 1, 0};
  }
  void transfer(Target &from, Target &to, unsigned program) {
    bind(to);
    api(d->SetDepthStencilSurface(nullptr));
    base();
    // This detached device owns sole RT0; additional attachments never exist.
    for (unsigned i = 1; i < rt_slots; ++i)
      api(d->SetRenderTarget(i, nullptr));
    api(d->SetTexture(0, from.texture.p));
    if (program == 6)
      api(d->SetTexture(1, layer.texture.p));
    api(d->SetPixelShader(ps[program].p));
    quad(full());
    api(d->SetTexture(0, nullptr));
    api(d->SetTexture(1, nullptr));
  }
  void prepare(const Case &c, const Op &o, unsigned program) {
    bind(scene);
    api(d->SetDepthStencilSurface(depth.p));
    base();
    rs(D3DRS_ZENABLE, TRUE);
    rs(D3DRS_ZFUNC, D3DCMP_LESS);
    rs(D3DRS_ZWRITEENABLE, o.kind == 0);
    api(d->SetPixelShader(ps[program].p));
    if (program == 1 || program == 2)
      api(d->SetTexture(0, source_texture[c.h.flags & 4 ? 1 : 0].p));
    if (c.h.flags & 16) {
      D3DVIEWPORT9 v{width / 4, height / 4, width / 2, height / 2, 0, 1};
      api(d->SetViewport(&v));
    }
    api(d->SetPixelShaderConstantF(0, o.color, 1));
    float f[] = {o.gain, o.fade, 0, 0}, mul[] = {1, 1, 1, 1},
          add[] = {0, 0, 0, 0};
    if (o.affine) {
      mul[0] = .75f;
      mul[1] = .5f;
      mul[2] = 1.25f;
      add[0] = .03125f;
      add[1] = .0625f;
      add[2] = -.03125f;
    }
    api(d->SetPixelShaderConstantF(1, f, 1));
    api(d->SetPixelShaderConstantF(2, mul, 1));
    api(d->SetPixelShaderConstantF(3, add, 1));
    rs(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    rs(D3DRS_SRCBLENDALPHA, c.h.alpha == 1   ? D3DBLEND_ZERO
                            : c.h.alpha == 2 ? D3DBLEND_SRCALPHA
                                             : D3DBLEND_ONE);
    rs(D3DRS_DESTBLENDALPHA,
       c.h.alpha == 2 ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE);
    if (o.kind != 0) {
      rs(D3DRS_ALPHABLENDENABLE, TRUE);
      rs(D3DRS_BLENDOP, D3DBLENDOP_ADD);
      rs(D3DRS_SRCBLEND, D3DBLEND_ONE);
      rs(D3DRS_DESTBLEND, o.kind == 2 ? D3DBLEND_INVSRCCOLOR : D3DBLEND_ONE);
      rs(D3DRS_COLORWRITEENABLE, c.h.write);
      if (c.h.alpha && o.kind != 2) {
        rs(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        rs(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
        rs(D3DRS_SRCBLENDALPHA,
           c.h.alpha == 1 ? D3DBLEND_ZERO : D3DBLEND_SRCALPHA);
        rs(D3DRS_DESTBLENDALPHA,
           c.h.alpha == 1 ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
      }
    }
    if (c.h.flags & 1) {
      RECT r{LONG(width / 4), LONG(height / 4), LONG(3 * width / 4),
             LONG(3 * height / 4)};
      api(d->SetScissorRect(&r));
      rs(D3DRS_SCISSORTESTENABLE, TRUE);
    }
  }
  // Conservative producer: known emitters retain depth rejection; unsupported
  // synthetic screen/late writers relax it. No retained geometry or consumer.
  void coverage(const Case &c, const Op &o, bool relaxed, Outcome &out) {
    replay.capture(d);
    bind(mask);
    api(d->SetDepthStencilSurface(replay.depth.p));
    api(replay.block->Apply()); // RT/DS are explicit; restore source
                                // viewport/input/samplers after bind.
    rs(D3DRS_ALPHABLENDENABLE, FALSE);
    rs(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    rs(D3DRS_COLORWRITEENABLE, 1);
    rs(D3DRS_ZWRITEENABLE, FALSE);
    rs(D3DRS_STENCILENABLE, FALSE);
    rs(D3DRS_STENCILWRITEMASK, 0);
    rs(D3DRS_ZENABLE, relaxed ? FALSE : TRUE);
    api(d->SetPixelShader(ps[0].p));
    float one[] = {1, 1, 1, 1};
    api(d->SetPixelShaderConstantF(0, one, 1));
    quad(o, c.h.flags);
    replay.restore(d);
    ++out.replays;
  }
  void initialize(const Case &c) {
    bind(scene);
    api(d->SetDepthStencilSurface(depth.p));
    base();
    api(d->Clear(0, nullptr,
                 D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1,
                 1));
    api(d->BeginScene());
    Case background = c;
    background.h.flags = 0;
    Op o = full();
    prepare(background, o, 0);
    quad(o);
    if (c.h.pattern)
      for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
          o = full();
          o.x0 = float(x) / width;
          o.x1 = float(x + 1) / width;
          o.y0 = float(y) / height;
          o.y1 = float(y + 1) / height;
          o.color[0] = float(x + 16 * y + 1) / 256;
          o.color[1] = float(17 * x + 5 * y + 1) / 64;
          o.color[2] = float(7 * x + 11 * y + 1) / 128;
          if (x == 0 && y == 0) {
            o.color[0] = 0;
            o.color[1] = 154.625f;
            o.color[2] = 200;
          }
          if (x == 1 && y == 0) {
            o.color[0] = std::ldexp(1.f, -24);
            o.color[1] = 1.f / 1024;
            o.color[2] = 1.f / 16;
          }
          if (x == 2 && y == 0)
            o.color[0] = std::copysign(
                0.f, -1.f); // Verify ingress sign in raw baseline.
          prepare(background, o, 0);
          rs(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
          quad(o);
        }
    api(d->EndScene());
    bind(layer);
    api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    bind(mask);
    api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    bind(scene);
    fence();
  }
  bool state_ok(const Case &c, const Op &o) {
    Com<IDirect3DSurface9> rt, ds;
    Com<IDirect3DPixelShader9> p;
    api(d->GetRenderTarget(0, &rt.p));
    HRESULT dh = d->GetDepthStencilSurface(&ds.p);
    if (dh != D3DERR_NOTFOUND)
      api(dh);
    api(d->GetPixelShader(&p.p));
    if (rt.p != scene.surface.p || ds.p != depth.p || p.p != ps[1].p)
      return false;
    for (auto s :
         {D3DRS_ZWRITEENABLE, D3DRS_COLORWRITEENABLE, D3DRS_ALPHABLENDENABLE,
          D3DRS_SCISSORTESTENABLE, D3DRS_SEPARATEALPHABLENDENABLE}) {
      DWORD v;
      api(d->GetRenderState(s, &v));
      DWORD expected = s == D3DRS_ZWRITEENABLE        ? FALSE
                       : s == D3DRS_COLORWRITEENABLE  ? c.h.write
                       : s == D3DRS_SCISSORTESTENABLE ? bool(c.h.flags & 1)
                       : s == D3DRS_SEPARATEALPHABLENDENABLE ? bool(c.h.alpha)
                                                             : TRUE;
      if (v != expected)
        return false;
    }
    const std::pair<D3DRENDERSTATETYPE, DWORD> required[] = {
        {D3DRS_ZENABLE, TRUE},
        {D3DRS_ZFUNC, D3DCMP_LESS},
        {D3DRS_BLENDOP, D3DBLENDOP_ADD},
        {D3DRS_SRCBLEND, D3DBLEND_ONE},
        {D3DRS_DESTBLEND, D3DBLEND_ONE},
        {D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD},
        {D3DRS_SRCBLENDALPHA, c.h.alpha == 1   ? D3DBLEND_ZERO
                              : c.h.alpha == 2 ? D3DBLEND_SRCALPHA
                                               : D3DBLEND_ONE},
        {D3DRS_DESTBLENDALPHA,
         c.h.alpha == 2 ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE},
        {D3DRS_SRGBWRITEENABLE, FALSE},
        {D3DRS_ALPHATESTENABLE, FALSE},
        {D3DRS_STENCILENABLE, FALSE},
        {D3DRS_STENCILWRITEMASK, 0},
        {D3DRS_CULLMODE, D3DCULL_NONE}};
    for (auto state : required) {
      DWORD value;
      api(d->GetRenderState(state.first, &value));
      if (value != state.second)
        return false;
    }
    D3DVIEWPORT9 v{}, wanted{c.h.flags & 16 ? width / 4 : 0,
                             c.h.flags & 16 ? height / 4 : 0,
                             c.h.flags & 16 ? width / 2 : width,
                             c.h.flags & 16 ? height / 2 : height,
                             0,
                             1};
    api(d->GetViewport(&v));
    if (std::memcmp(&v, &wanted, sizeof(v)))
      return false;
    RECT rect{},
        wanted_rect{c.h.flags & 1 ? LONG(width / 4) : 0,
                    c.h.flags & 1 ? LONG(height / 4) : 0,
                    c.h.flags & 1 ? LONG(3 * width / 4) : LONG(width),
                    c.h.flags & 1 ? LONG(3 * height / 4) : LONG(height)};
    api(d->GetScissorRect(&rect));
    if (std::memcmp(&rect, &wanted_rect, sizeof(rect)))
      return false;
    Com<IDirect3DVertexShader9> vertex;
    Com<IDirect3DVertexDeclaration9> decl;
    Com<IDirect3DBaseTexture9> tex;
    api(d->GetVertexShader(&vertex.p));
    api(d->GetVertexDeclaration(&decl.p));
    api(d->GetTexture(0, &tex.p));
    if (vertex.p != vs.p || decl.p != declaration.p ||
        tex.p != source_texture[c.h.flags & 4 ? 1 : 0].p)
      return false;
    for (auto state : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_ADDRESSU,
                       D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE}) {
      DWORD value;
      api(d->GetSamplerState(0, state, &value));
      DWORD wanted_value =
          state == D3DSAMP_SRGBTEXTURE ? FALSE
          : state == D3DSAMP_MINFILTER || state == D3DSAMP_MAGFILTER
              ? DWORD(D3DTEXF_POINT)
              : DWORD(D3DTADDRESS_CLAMP);
      if (value != wanted_value)
        return false;
    }
    float color[4];
    api(d->GetPixelShaderConstantF(0, color, 1));
    return std::memcmp(color, o.color, sizeof(color)) == 0;
  }

  Outcome run(const Case &c, bool checks = true) {
    Outcome out;
    bool active = false;
    Op last{};
    bool refused =
        c.h.fault == 1 || c.h.fault == 2 || c.h.fault == 6 || c.h.fault == 7;
    unsigned mode = refused ? 0 : c.h.mode;
    out.fallback = refused;
    api(d->BeginScene());
    if (c.h.mask) {
      bind(mask);
      api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    }
    // Wrapper-controlled refusal before the selected operation. Capture refusal
    // leaves native state untouched; decode refusal exercises captured restore;
    // first-source refusal also exercises a completed decode before declining.
    if (c.h.fault == 2 || c.h.fault == 6 || c.h.fault == 7) {
      need(!c.ops.empty(), "refusal source");
      prepare(c, c.ops.front(), 1);
      if (c.h.fault != 6) {
        source.capture(d);
        if (c.h.fault == 7)
          transfer(scene, scratch, 3);
        source.restore(d);
        if (checks)
          need(state_ok(c, c.ops.front()), "pre-source refusal restore");
      }
    }
    auto close = [&]() {
      if (!active)
        return true;
      if (c.h.fault == 3) {
        source.restore(d);
        out.incomplete = 1;
        out.restored = checks ? state_ok(c, last) : 1;
        active = false;
        return false;
      }
      transfer(scratch, scene, 4);
      if (c.h.fault == 4) {
        out.incomplete = 1;
        out.restored = checks ? state_ok(c, last) : 0;
        need(!out.restored, "injected restore failure did not disturb state");
        active = false;
        return false;
      }
      source.restore(d);
      if (checks)
        need(state_ok(c, last), "source state restore mismatch");
      active = false;
      return true;
    };
    for (const Op &o : c.ops) {
      if (o.kind != 1 && active && !close())
        break;
      if (o.kind == 4)
        continue;
      if (o.kind == 1 && mode == 1) {
        if (c.h.fault == 8 && out.accepted) {
          close();
          out.incomplete = 1;
          break;
        }
        // Prepare/capture each intended source independently. Transfer state or
        // previous emitter constants must never leak into the following draw.
        prepare(c, o, 1);
        source.capture(d);
        last = o;
        if (!active) {
          if (c.h.fault == 2)
            throw std::runtime_error("unreachable refused source");
          transfer(scene, scratch, 3);
          active = true;
          ++out.brackets;
        }
        bind(scratch);
        api(d->SetDepthStencilSurface(source.depth.p));
        api(source.block
                ->Apply()); // preserves complete source state after RT switch
        api(d->SetPixelShader(ps[2].p));
        quad(o, c.h.flags);
        ++out.accepted;
        if (c.h.fault == 5) {
          out.incomplete = 1;
          transfer(scratch, scene, 4);
          source.restore(d);
          active = false;
          break;
        }
        if (c.h.mask)
          coverage(c, o, false, out);
        if ((c.h.flags & 2) && !close())
          break;
      } else if (o.kind == 1 && mode == 2) {
        prepare(c, o, 2);
        bind(layer);
        D3DVIEWPORT9 layer_view{c.h.flags & 16 ? width / 4 : 0,
                                c.h.flags & 16 ? height / 4 : 0,
                                c.h.flags & 16 ? width / 2 : width,
                                c.h.flags & 16 ? height / 2 : height,
                                0,
                                1};
        api(d->SetViewport(&layer_view));
        api(d->SetTexture(0, source_texture[c.h.flags & 4 ? 1 : 0].p));
        rs(D3DRS_COLORWRITEENABLE, 7);
        quad(o, c.h.flags);
        ++out.accepted;
        prepare(c, o, 1);
        rs(D3DRS_COLORWRITEENABLE, c.h.write & 8);
        quad(o, c.h.flags);
        if (c.h.mask)
          coverage(c, o, false, out);
      } else {
        prepare(c, o, o.kind == 0 || o.kind == 2 ? 0 : 1);
        quad(o, c.h.flags);
        if (c.h.mask && o.kind != 0)
          coverage(c, o, o.kind != 1, out);
      }
    }
    if (active)
      close();
    if (mode == 2) {
      transfer(scene, scratch, 3);
      transfer(scratch, scene, 6);
    }
    api(d->EndScene());
    fence();
    // Refusal injections are before any accepted source, not simulated partial
    // API execution. Post-source failures stop the frame; this teardown is not
    // native recovery and no accepted geometry is replayed into native color.
    if (c.h.fault == 4)
      source.restore(d);
    return out;
  }
  void fence() {
    api(event->Issue(D3DISSUE_END));
    DWORD start = GetTickCount();
    HRESULT h;
    while ((h = event->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE) {
      need(GetTickCount() - start < 10000, "EVENT timeout");
      Sleep(0);
    }
    api(h);
  }
  std::vector<float> read(Target &t, bool rgba) {
    Com<IDirect3DSurface9> sys;
    api(d->CreateOffscreenPlainSurface(
        width, height, rgba ? D3DFMT_A16B16G16R16F : D3DFMT_R32F,
        D3DPOOL_SYSTEMMEM, &sys.p, nullptr));
    api(d->GetRenderTargetData(t.surface.p, sys.p));
    D3DLOCKED_RECT l;
    api(sys->LockRect(&l, nullptr, D3DLOCK_READONLY));
    std::vector<float> v(width * height * (rgba ? 4 : 1));
    for (unsigned y = 0; y < height; ++y)
      for (unsigned x = 0; x < width; ++x) {
        const char *row = static_cast<const char *>(l.pBits) + y * l.Pitch;
        if (rgba) {
          auto p = reinterpret_cast<const unsigned short *>(row) + 4 * x;
          for (unsigned k = 0; k < 4; ++k)
            v[(y * width + x) * 4 + k] = half(p[k]);
        } else
          v[y * width + x] = reinterpret_cast<const float *>(row)[x];
      }
    api(sys->UnlockRect());
    return v;
  }
  std::vector<float> depth_probe() {
    bind(probe);
    api(d->SetDepthStencilSurface(depth.p));
    base();
    api(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    api(d->BeginScene());
    rs(D3DRS_ZENABLE, TRUE);
    rs(D3DRS_ZFUNC, D3DCMP_LESS);
    rs(D3DRS_STENCILENABLE, TRUE);
    rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    rs(D3DRS_STENCILREF, 1);
    rs(D3DRS_STENCILMASK, 255);
    rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    rs(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    rs(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    api(d->SetPixelShader(ps[0].p));
    Op o = full();
    o.z = .1f;
    float c[] = {2, 0, 0, 0};
    api(d->SetPixelShaderConstantF(0, c, 1));
    quad(o);
    o.z = .5f;
    c[0] = 1;
    api(d->SetPixelShaderConstantF(0, c, 1));
    quad(o);
    api(d->EndScene());
    fence();
    return read(probe, false);
  }
};
std::vector<Case> load(const char *path) {
  std::ifstream f(path, std::ios::binary);
  need(bool(f), "case file");
  unsigned n;
  f.read(reinterpret_cast<char *>(&n), 4);
  need(n && n < 1000, "case count");
  std::vector<Case> cs(n);
  for (auto &c : cs) {
    f.read(reinterpret_cast<char *>(&c.h), sizeof(c.h));
    need(c.h.count < 10000, "op count");
    c.ops.resize(c.h.count);
    f.read(reinterpret_cast<char *>(c.ops.data()), c.ops.size() * sizeof(Op));
    need(bool(f), "case framing");
  }
  need(f.peek() == EOF, "case trailing data");
  return cs;
}
Case bench(unsigned mode, unsigned mask, unsigned bursts, unsigned isolated) {
  Case c;
  c.h.mode = mode;
  c.h.mask = mask;
  c.h.alpha = 0;
  c.h.write = 15;
  c.h.flags = isolated ? 2 : 0;
  for (unsigned i = 0; i < bursts; ++i) {
    c.ops.push_back(Op{1,
                       .125f,
                       .125f,
                       .625f,
                       .75f,
                       .4f,
                       {.125f, .25f, .0625f, .125f},
                       .75f,
                       1,
                       1});
    c.ops.push_back(Op{1,
                       .375f,
                       .25f,
                       .875f,
                       .875f,
                       .4f,
                       {.25f, .0625f, .375f, 0},
                       .5f,
                       1,
                       0});
    c.ops.push_back(
        Op{2, 0, 0, 1, 1, .4f, {.03125f, .0625f, .125f, 0}, 1, 1, 0});
  }
  return c;
}
int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  HWND window = nullptr;
  HMODULE runtime = nullptr;
  try {
    need(argc == 3, "arguments: cases.bin pixels.bin");
    auto cases = load(argv[1]);
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "X3LinearEmissionFixture";
    need(RegisterClassA(&wc) != 0, "RegisterClass");
    window = CreateWindowA(wc.lpszClassName, "Detached ordered emission",
                           WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr,
                           nullptr, wc.hInstance, nullptr);
    need(window != nullptr, "window");
    runtime = LoadLibraryA("d3d9.dll");
    need(runtime != nullptr, "d3d9");
    using Create = IDirect3D9 *(WINAPI *)(UINT);
    Create create;
    auto address = GetProcAddress(runtime, "Direct3DCreate9");
    std::memcpy(&create, &address, sizeof(create));
    need(create != nullptr, "Direct3DCreate9");
    {
      Com<IDirect3D9> factory;
      factory.p = create(D3D_SDK_VERSION);
      need(factory.p != nullptr, "factory");
      Com<IDirect3DDevice9> device;
      D3DPRESENT_PARAMETERS p{};
      p.Windowed = TRUE;
      p.SwapEffect = D3DSWAPEFFECT_DISCARD;
      p.hDeviceWindow = window;
      p.BackBufferWidth = 16;
      p.BackBufferHeight = 16;
      p.BackBufferFormat = D3DFMT_UNKNOWN;
      p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
      api(factory->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                    D3DCREATE_FPU_PRESERVE,
                                &p, &device.p));
      D3DCAPS9 caps{};
      api(device->GetDeviceCaps(&caps));
      std::printf("CAPS vs=%08lx ps=%08lx rt=%lu\n", caps.VertexShaderVersion,
                  caps.PixelShaderVersion, caps.NumSimultaneousRTs);
      need(caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) &&
               caps.NumSimultaneousRTs >= 1 &&
               caps.VertexShaderVersion >= D3DVS_VERSION(3, 0),
           "SM3/RT slots");
      D3DDISPLAYMODE display{};
      api(factory->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &display));
      auto format = [&](D3DFORMAT f, DWORD usage, D3DRESOURCETYPE type,
                        const char *name) {
        HRESULT h = factory->CheckDeviceFormat(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, display.Format, usage, type, f);
        std::printf("FORMAT name=%s hr=%08lx\n", name, h);
        api(h);
      };
      format(D3DFMT_A16B16G16R16F, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
             "fp16_rt");
      format(D3DFMT_A16B16G16R16F,
             D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
             D3DRTYPE_TEXTURE, "fp16_blend");
      format(D3DFMT_R32F, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, "r32f_rt");
      format(D3DFMT_D24S8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, "d24s8");
      for (auto f : {D3DFMT_A16B16G16R16F, D3DFMT_R32F}) {
        HRESULT h =
            factory->CheckDepthStencilMatch(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                            display.Format, f, D3DFMT_D24S8);
        std::printf("DEPTH_MATCH format=%u hr=%08lx\n", unsigned(f), h);
        api(h);
      }
      need((caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) != 0,
           "separate-alpha blend cap");
      Com<IDirect3DSurface9> back;
      api(device->GetRenderTarget(0, &back.p));
      {
        Fixture f(device.p, 16, 16);
        std::ofstream raw(argv[2], std::ios::binary);
        need(bool(raw), "raw output");
        for (const auto &c : cases) {
          f.initialize(c);
          auto out = f.run(c);
          auto rgb = f.read(f.scene, true), mask = f.read(f.mask, false),
               depth = f.depth_probe();
          raw.write(reinterpret_cast<const char *>(&c.h.id), 4);
          for (auto *v : {&rgb, &mask, &depth})
            raw.write(reinterpret_cast<const char *>(v->data()), v->size() * 4);
          std::printf("CASE id=%u accepted=%u fallback=%u incomplete=%u "
                      "restored=%u brackets=%u replays=%u\n",
                      c.h.id, out.accepted, out.fallback, out.incomplete,
                      out.restored, out.brackets, out.replays);
        }
        need(bool(raw), "raw write");
        api(device->SetRenderTarget(0, back.p));
      }
      LARGE_INTEGER frequency;
      need(QueryPerformanceFrequency(&frequency), "QPC frequency");
      for (auto size :
           {std::pair<unsigned, unsigned>{1280, 768}, {1920, 1080}}) {
        Fixture f(device.p, size.first, size.second);
        for (unsigned bursts : {1u, 16u}) {
          for (unsigned iteration = 0; iteration < 60; ++iteration) {
            unsigned variant =
                (iteration / 6) % 2 ? 5 - iteration % 6 : iteration % 6;
            unsigned mode = variant / 2 ? 1 : 0, mask = variant % 2,
                     isolated = variant / 2 == 2;
            Case c = bench(mode, mask, bursts, isolated);
            f.initialize(c);
            LARGE_INTEGER begin, end;
            QueryPerformanceCounter(&begin);
            auto out = f.run(c, false);
            QueryPerformanceCounter(&end);
            need(!out.incomplete, "benchmark incomplete");
            if (iteration >= 12)
              std::printf("TIMING width=%u height=%u bursts=%u variant=%u "
                          "sample=%u completed_ms=%.9f\n",
                          size.first, size.second, bursts, variant,
                          iteration - 12,
                          1000. * double(end.QuadPart - begin.QuadPart) /
                              frequency.QuadPart);
          }
        }
        api(device->SetRenderTarget(0, back.p));
      }
      api(device->SetTexture(0, nullptr));
      api(device->SetTexture(1, nullptr));
      api(device->SetPixelShader(nullptr));
      api(device->SetDepthStencilSurface(nullptr));
      api(device->SetRenderTarget(0, back.p));
    }
    DestroyWindow(window);
    window = nullptr;
    FreeLibrary(runtime);
    runtime = nullptr;
    UnregisterClassA("X3LinearEmissionFixture", GetModuleHandle(nullptr));
    std::printf("RESULT pass cases=%u shaders=24\n", unsigned(cases.size()));
    return 0;
  } catch (const std::exception &e) {
    std::printf("FAIL %s\n", e.what());
    if (window)
      DestroyWindow(window);
    if (runtime)
      FreeLibrary(runtime);
    return 1;
  }
}
