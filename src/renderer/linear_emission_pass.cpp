#include "linear_emission_pass.h"
#include "quad_vertex_program.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <new>
namespace x3m::renderer {
namespace {
// Public IDirect3DDevice9 ABI, matching HdrPass and abi_check.cpp.
enum Slot : unsigned {
  GetDirect3D = 6,
  GetCreation = 9,
  GetBackBuffer = 18,
  CreateTexture = 23,
  SetRt = 37,
  GetRt = 38,
  SetDepth = 39,
  GetDepth = 40,
  Clear = 43,
  SetViewport = 47,
  GetViewport = 48,
  SetRs = 57,
  GetRs = 58,
  GetTexture = 64,
  SetTexture = 65,
  GetSampler = 68,
  SetSampler = 69,
  SetScissor = 75,
  GetScissor = 76,
  DrawUp = 83,
  CreateDecl = 86,
  SetDecl = 87,
  GetDecl = 88,
  SetFvf = 89,
  GetFvf = 90,
  CreateVs = 91,
  SetVs = 92,
  GetVs = 93,
  SetStream = 100,
  GetStream = 101,
  SetFreq = 102,
  GetFreq = 103,
  CreatePs = 106,
  SetPs = 107,
  GetPs = 108
};
template <class T> void drop(T *&p) noexcept {
  if (p) {
    T *v = p;
    p = nullptr;
    v->Release();
  }
}
// Independent getters may return different interface pointers for one COM
// object. Canonical IUnknown is the identity contract; slots still use their
// exact owned pointer values when checking an ownership exchange.
HRESULT object_identity(IUnknown *a, IUnknown *b, bool &equal) noexcept {
  equal = false;
  if (a == b) {
    equal = true;
    return S_OK;
  }
  if (!a || !b)
    return S_OK;
  IUnknown *aa = nullptr, *bb = nullptr;
  HRESULT ha = a->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&aa));
  HRESULT hb = b->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&bb));
  HRESULT hr = FAILED(ha)     ? ha
               : FAILED(hb)   ? hb
               : (!aa || !bb) ? E_NOINTERFACE
                              : S_OK;
  if (SUCCEEDED(hr))
    equal = aa == bb;
  drop(aa);
  drop(bb);
  return hr;
}
bool same_object(IUnknown *a, IUnknown *b) noexcept {
  bool equal = false;
  return SUCCEEDED(object_identity(a, b, equal)) && equal;
}
constexpr D3DRENDERSTATETYPE states[] = {D3DRS_ZENABLE,
                                         D3DRS_ZWRITEENABLE,
                                         D3DRS_ALPHATESTENABLE,
                                         D3DRS_ALPHABLENDENABLE,
                                         D3DRS_SEPARATEALPHABLENDENABLE,
                                         D3DRS_CULLMODE,
                                         D3DRS_FILLMODE,
                                         D3DRS_COLORWRITEENABLE,
                                         D3DRS_SCISSORTESTENABLE,
                                         D3DRS_STENCILENABLE,
                                         D3DRS_STENCILWRITEMASK,
                                         D3DRS_FOGENABLE,
                                         D3DRS_SRGBWRITEENABLE,
                                         D3DRS_CLIPPLANEENABLE,
                                         D3DRS_DITHERENABLE,
                                         D3DRS_WRAP0,
                                         D3DRS_MULTISAMPLEMASK,
                                         D3DRS_CLIPPING,
                                         D3DRS_SRCBLEND,
                                         D3DRS_DESTBLEND,
                                         D3DRS_BLENDOP,
                                         D3DRS_COLORWRITEENABLE1,
                                         D3DRS_COLORWRITEENABLE2,
                                         D3DRS_COLORWRITEENABLE3
                                         , D3DRS_BLENDOPALPHA,
                                         D3DRS_SRCBLENDALPHA,
                                         D3DRS_DESTBLENDALPHA
};
constexpr DWORD fullscreen[] = {FALSE,
                                FALSE,
                                FALSE,
                                FALSE,
                                FALSE,
                                D3DCULL_NONE,
                                D3DFILL_SOLID,
                                15,
                                FALSE,
                                FALSE,
                                0,
                                FALSE,
                                FALSE,
                                0,
                                FALSE,
                                0,
                                0xffffffffu,
                                FALSE,
                                D3DBLEND_ONE,
                                D3DBLEND_ZERO,
                                D3DBLENDOP_ADD,
                                15,
                                15,
                                15
                                , D3DBLENDOP_ADD,
                                D3DBLEND_ONE,
                                D3DBLEND_ZERO
};
constexpr D3DSAMPLERSTATETYPE samplers[] = {
    D3DSAMP_MINFILTER,   D3DSAMP_MAGFILTER,    D3DSAMP_MIPFILTER,
    D3DSAMP_ADDRESSU,    D3DSAMP_ADDRESSV,     D3DSAMP_SRGBTEXTURE,
    D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS};
constexpr DWORD sample_values[] = {D3DTEXF_POINT,
                                   D3DTEXF_POINT,
                                   D3DTEXF_NONE,
                                   D3DTADDRESS_CLAMP,
                                   D3DTADDRESS_CLAMP,
                                   FALSE,
                                   0,
                                   0};
static_assert(std::size(states) == std::size(fullscreen) &&
              std::size(samplers) == std::size(sample_values));
// Same authored gamma22 arithmetic and per-channel selection qualified by the
// detached original fixture. Definitions are shader-local; no application
// vertex/pixel constants are uploaded or changed by the transfer programs.
constexpr DWORD copy_words[] = {
#include "linear_emission_copy_clear_inc.h"
};
#ifdef X3M_LINEAR_EMISSION_PASS_FIXTURE
constexpr DWORD separate_copy_words[] = {
#include "linear_emission_copy_inc.h"
};
#endif
constexpr DWORD composite_words[] = {
#include "linear_emission_composite_inc.h"
};
constexpr DWORD source_over_words[] = {
#include "linear_distance_fade_composite_inc.h"
};
struct Saved {
  IDirect3DSurface9 *rt[4]{};
  IDirect3DSurface9 *depth = nullptr;
  IDirect3DBaseTexture9 *texture[3]{};
  IDirect3DVertexShader9 *vs = nullptr;
  IDirect3DPixelShader9 *ps = nullptr;
  IDirect3DVertexDeclaration9 *declaration = nullptr;
  IDirect3DVertexBuffer9 *stream = nullptr;
  D3DVIEWPORT9 viewport{};
  RECT scissor{};
  DWORD fvf = 0, frequency = 1;
  UINT offset = 0, stride = 0;
  DWORD rs[std::size(states)]{}, ss[3][std::size(samplers)]{};
  void release() noexcept {
    for (auto &p : rt)
      drop(p);
    for (auto &p : texture)
      drop(p);
    drop(depth);
    drop(vs);
    drop(ps);
    drop(declaration);
    drop(stream);
  }
  DWORD state(D3DRENDERSTATETYPE key) const noexcept {
    for (unsigned i = 0; i < std::size(states); ++i)
      if (states[i] == key)
        return rs[i];
    return 0;
  }
};
} // namespace
struct LinearEmissionPass::Impl {
  IDirect3DDevice9 *device = nullptr;
  void *const *native = nullptr;
  D3DCAPS9 caps9{};
  D3DFORMAT depth_format = D3DFMT_UNKNOWN;
  LinearEmissionPassCaps caps{};
  IDirect3DSurface9 *b = nullptr, *e = nullptr, *c = nullptr, *m = nullptr;
  IDirect3DVertexShader9 *vs = nullptr;
  IDirect3DVertexDeclaration9 *declaration = nullptr;
  IDirect3DPixelShader9 *copy = nullptr, *composite = nullptr, *source_over_composite = nullptr;
  bool source_over = false;
  UINT width = 0, height = 0;
  unsigned allocation_count = 0, rt_count = 0;
  Saved saved{};
  enum class Phase { Idle, Prepared, Pending } phase = Phase::Idle;
  std::uint64_t frame = 0;
  bool has_frame = false, mask_valid = false, blocked = false,
       recovered = false;
  IDirect3DSurface9 **selected = nullptr;
  IDirect3DSurface9 *selected_surface = nullptr;
  IDirect3DSurface9 *input_scene =
      nullptr; // exact borrowed caller ownership slot value
  LinearEmissionCompletion completion{};
#ifdef X3M_LINEAR_EMISSION_PASS_FIXTURE
  bool fused_copy = true;
  LinearEmissionPassFault fault_kind = LinearEmissionPassFault::None;
  unsigned fault_count = 0;
  bool fault(LinearEmissionPassFault f) noexcept {
    if (f != fault_kind || !fault_count)
      return false;
    --fault_count;
    return true;
  }
#else
  static constexpr bool fused_copy = true;
  static bool fault(LinearEmissionPassFault) noexcept { return false; }
#endif
  template <class... A> HRESULT call(unsigned slot, A... args) const noexcept {
    using F = HRESULT(WINAPI *)(IDirect3DDevice9 *, A...);
    return reinterpret_cast<F>(native[slot])(device, args...);
  }
  bool owned(IDirect3DSurface9 *p) const noexcept {
    return p && (same_object(p, b) || same_object(p, e) || same_object(p, c) ||
                 same_object(p, m));
  }
  bool supported_state(D3DRENDERSTATETYPE state) const noexcept {
    // Additive-only callers retain the old native getter/setter inventory.
    if (state == D3DRS_BLENDOPALPHA || state == D3DRS_SRCBLENDALPHA || state == D3DRS_DESTBLENDALPHA)
      return source_over;
    if (state == D3DRS_COLORWRITEENABLE)
      return (caps9.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE) != 0;
    if (state == D3DRS_SEPARATEALPHABLENDENABLE)
      return (caps9.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) != 0;
    unsigned slot = state == D3DRS_COLORWRITEENABLE1   ? 1
                    : state == D3DRS_COLORWRITEENABLE2 ? 2
                    : state == D3DRS_COLORWRITEENABLE3 ? 3
                                                       : 0;
    return !slot || (slot < rt_count && (caps9.PrimitiveMiscCaps &
                                         D3DPMISCCAPS_INDEPENDENTWRITEMASKS));
  }
  bool distinct_from_pool(IDirect3DSurface9 *surface) noexcept {
    for (auto *target : {b, e, c, m}) {
      bool equal = false;
      if (FAILED(object_identity(surface, target, equal)) || equal)
        return false;
    }
    return true;
  }
  HRESULT save() noexcept {
    saved.release();
    if (fault(LinearEmissionPassFault::Save))
      return E_FAIL;
    HRESULT hr = S_OK;
    for (unsigned i = 0; i < rt_count; ++i) {
      hr = call(GetRt, DWORD(i), &saved.rt[i]);
      if (FAILED(hr) && !(i && hr == D3DERR_NOTFOUND && !saved.rt[i]))
        return hr;
    }
    if (!saved.rt[0])
      return E_FAIL;
    hr = call(GetDepth, &saved.depth);
    if (FAILED(hr) && !(hr == D3DERR_NOTFOUND && !saved.depth))
      return hr;
#define GET(slot, ...)                                                         \
  do {                                                                         \
    hr = call(slot, __VA_ARGS__);                                              \
    if (FAILED(hr))                                                            \
      return hr;                                                               \
  } while (false)
    GET(GetViewport, &saved.viewport);
    GET(GetScissor, &saved.scissor);
    GET(GetFvf, &saved.fvf);
    GET(GetDecl, &saved.declaration);
    GET(GetVs, &saved.vs);
    GET(GetPs, &saved.ps);
    GET(GetStream, UINT(0), &saved.stream, &saved.offset, &saved.stride);
    GET(GetFreq, UINT(0), &saved.frequency);
    for (unsigned i = 0; i < 3; ++i) {
      GET(GetTexture, DWORD(i), &saved.texture[i]);
      for (unsigned j = 0; j < std::size(samplers); ++j) {
        GET(GetSampler, DWORD(i), samplers[j], &saved.ss[i][j]);
      }
    }
    for (unsigned i = 0; i < std::size(states); ++i) {
      if (supported_state(states[i])) {
        GET(GetRs, states[i], &saved.rs[i]);
      }
    }
#undef GET
    return S_OK;
  }
  HRESULT restore(IDirect3DSurface9 *target) noexcept {
    // Continue every step after failure: restoration is not transactional.
    HRESULT first = S_OK;
    auto step = [&](HRESULT h) {
      if (SUCCEEDED(first) && FAILED(h))
        first = h;
    };
    for (unsigned i = 0; i < 3; ++i)
      step(call(SetTexture, DWORD(i),
                static_cast<IDirect3DBaseTexture9 *>(nullptr)));
    for (unsigned i = 1; i < rt_count; ++i)
      step(call(SetRt, DWORD(i), static_cast<IDirect3DSurface9 *>(nullptr)));
    step(call(SetRt, DWORD(0), target));
    for (unsigned i = 1; i < rt_count; ++i)
      step(call(SetRt, DWORD(i), saved.rt[i]));
    step(call(SetDepth, saved.depth));
    step(call(SetViewport, &saved.viewport));
    step(call(SetScissor, &saved.scissor));
    if (saved.fvf)
      step(call(SetFvf, saved.fvf));
    else
      step(call(SetDecl, saved.declaration));
    step(call(SetVs, saved.vs));
    step(call(SetPs, saved.ps));
    step(call(SetStream, UINT(0), saved.stream, saved.offset, saved.stride));
    step(call(SetFreq, UINT(0), saved.frequency));
    for (unsigned i = 0; i < 3; ++i) {
      step(call(SetTexture, DWORD(i), saved.texture[i]));
      for (unsigned j = 0; j < std::size(samplers); ++j)
        step(call(SetSampler, DWORD(i), samplers[j], saved.ss[i][j]));
    }
    for (unsigned i = 0; i < std::size(states); ++i)
      if (supported_state(states[i]))
        step(call(SetRs, states[i], saved.rs[i]));
    return first;
  }
  HRESULT target(IDirect3DSurface9 *surface) noexcept {
    HRESULT hr;
    for (unsigned i = 0; i < 3; ++i)
      if (FAILED(hr = call(SetTexture, DWORD(i),
                           static_cast<IDirect3DBaseTexture9 *>(nullptr))))
        return hr;
    for (unsigned i = 1; i < rt_count; ++i)
      if (FAILED(hr = call(SetRt, DWORD(i),
                           static_cast<IDirect3DSurface9 *>(nullptr))))
        return hr;
    if (FAILED(hr = call(SetDepth, static_cast<IDirect3DSurface9 *>(nullptr))))
      return hr;
    return call(SetRt, DWORD(0), surface);
  }
  HRESULT full_state() noexcept {
    HRESULT hr;
    D3DVIEWPORT9 viewport{0, 0, width, height, 0, 1};
    RECT scissor{0, 0, LONG(width), LONG(height)};
    if (FAILED(hr = call(SetViewport, &viewport)) ||
        FAILED(hr = call(SetScissor, &scissor)) ||
        FAILED(hr = call(SetVs, vs)) ||
        FAILED(hr = call(SetDecl, declaration)) ||
        FAILED(hr = call(SetFreq, UINT(0), UINT(1))))
      return hr;
    for (unsigned i = 0; i < std::size(states); ++i)
      if (supported_state(states[i]) &&
          FAILED(hr = call(SetRs, states[i], fullscreen[i])))
        return hr;
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned j = 0; j < std::size(samplers); ++j)
        if (FAILED(
                hr = call(SetSampler, DWORD(i), samplers[j], sample_values[j])))
          return hr;
    return S_OK;
  }
  HRESULT clear(IDirect3DSurface9 *surface) noexcept {
    // Native Clear uses target selection and viewport clipping, not the
    // transfer shader, vertex stream or sampler state. Keep full coverage
    // explicit without the draw-only setup traffic.
    HRESULT hr = target(surface);
    D3DVIEWPORT9 viewport{0, 0, width, height, 0, 1};
    RECT scissor{0, 0, LONG(width), LONG(height)};
    if (SUCCEEDED(hr))
      hr = call(SetViewport, &viewport);
    if (SUCCEEDED(hr))
      hr = call(SetScissor, &scissor);
    if (SUCCEEDED(hr))
      hr = call(SetRs, D3DRS_SCISSORTESTENABLE, DWORD(FALSE));
    if (SUCCEEDED(hr))
      hr = call(Clear, DWORD(0), static_cast<const D3DRECT *>(nullptr),
                DWORD(D3DCLEAR_TARGET), D3DCOLOR(0), 1.f, DWORD(0));
    return hr;
  }
  HRESULT draw(IDirect3DSurface9 *destination, IDirect3DSurface9 *a,
               bool combine) noexcept {
    IDirect3DTexture9 *views[3]{};
    IDirect3DSurface9 *sources[] = {a, e, b};
    HRESULT hr = S_OK;
    for (unsigned i = 0; i < (combine ? 3u : 1u); ++i) {
      hr = sources[i]->GetContainer(IID_IDirect3DTexture9,
                                    reinterpret_cast<void **>(&views[i]));
      if (FAILED(hr) || !views[i]) {
        if (SUCCEEDED(hr))
          hr = E_NOINTERFACE;
        break;
      }
    }
    if (SUCCEEDED(hr))
      hr = target(destination);
    // A and M remain untouched. The copy writes exact B and initializes E in
    // one rasterization; full_state disables blending and enables both masks.
    // target() detached every extra attachment, including supplemental M.
    if (SUCCEEDED(hr) && !combine && fused_copy)
      hr = call(SetRt, DWORD(1), e);
    if (SUCCEEDED(hr))
      hr = full_state();
    for (unsigned i = 0; i < (combine ? 3u : 1u) && SUCCEEDED(hr); ++i)
      hr = call(SetTexture, DWORD(i),
                static_cast<IDirect3DBaseTexture9 *>(views[i]));
    if (SUCCEEDED(hr))
      hr = call(SetPs, combine ? (source_over ? source_over_composite : composite) : copy);
    if (SUCCEEDED(hr)) {
      QuadVertex vertices[4];
      quad_vertices(width, height, vertices);
      hr = call(DrawUp, D3DPT_TRIANGLESTRIP, UINT(2),
                static_cast<const void *>(vertices), UINT(sizeof(QuadVertex)));
    }
    for (auto &p : views)
      drop(p);
    return hr;
  }
  bool source_ok(const LinearEmissionBoundary &boundary) noexcept {
    if (!same_object(saved.rt[0], boundary.scene) || !saved.depth ||
        !distinct_from_pool(boundary.scene))
      return false;
    for (unsigned i = 1; i < rt_count; ++i)
      if (saved.rt[i])
        return false;
    D3DSURFACE_DESC a{}, depth{};
    if (FAILED(boundary.scene->GetDesc(&a)) ||
        FAILED(saved.depth->GetDesc(&depth)))
      return false;
    if (a.Format != D3DFMT_A16B16G16R16F || a.Pool != D3DPOOL_DEFAULT ||
        a.Usage != D3DUSAGE_RENDERTARGET || a.Width != width ||
        a.Height != height || a.MultiSampleType != D3DMULTISAMPLE_NONE ||
        a.MultiSampleQuality)
      return false;
    if (depth.Format != depth_format || depth.Width < width ||
        depth.Height < height || depth.MultiSampleType != D3DMULTISAMPLE_NONE ||
        depth.MultiSampleQuality)
      return false;
    if (source_over) {
      return boundary.augmented_vertex &&
             saved.state(D3DRS_ALPHABLENDENABLE) &&
             saved.state(D3DRS_BLENDOP) == D3DBLENDOP_ADD &&
             saved.state(D3DRS_SRCBLEND) == D3DBLEND_SRCALPHA &&
             saved.state(D3DRS_DESTBLEND) == D3DBLEND_INVSRCALPHA &&
             saved.state(D3DRS_COLORWRITEENABLE) == 7 &&
             !saved.state(D3DRS_SEPARATEALPHABLENDENABLE) &&
             saved.state(D3DRS_ZENABLE) == D3DZB_TRUE &&
             !saved.state(D3DRS_ALPHATESTENABLE) &&
             !saved.state(D3DRS_ZWRITEENABLE) &&
             !saved.state(D3DRS_STENCILENABLE) && !saved.state(D3DRS_FOGENABLE) &&
             !saved.state(D3DRS_DITHERENABLE) &&
             !saved.state(D3DRS_SRGBWRITEENABLE) && !saved.ss[0][5];
    }
    return saved.state(D3DRS_ALPHABLENDENABLE) &&
           saved.state(D3DRS_BLENDOP) == D3DBLENDOP_ADD &&
           saved.state(D3DRS_SRCBLEND) == D3DBLEND_ONE &&
           saved.state(D3DRS_DESTBLEND) == D3DBLEND_ONE &&
           (!supported_state(D3DRS_COLORWRITEENABLE) ||
            saved.state(D3DRS_COLORWRITEENABLE) == 15) &&
           saved.state(D3DRS_ZENABLE) == D3DZB_TRUE &&
           !saved.state(D3DRS_ALPHATESTENABLE) &&
           !saved.state(D3DRS_ZWRITEENABLE) &&
           !saved.state(D3DRS_STENCILENABLE) && !saved.state(D3DRS_FOGENABLE) &&
           !saved.state(D3DRS_DITHERENABLE) &&
           !saved.state(D3DRS_SRGBWRITEENABLE) && !saved.ss[0][5];
  }
  void reset_targets() noexcept {
    if (device && native && (b || e || c || m)) {
      for (unsigned i = 0; i < 3; ++i) {
        IDirect3DBaseTexture9 *texture = nullptr;
        IDirect3DTexture9 *tex = nullptr;
        IDirect3DSurface9 *level = nullptr;
        HRESULT hr = call(GetTexture, DWORD(i), &texture);
        if (SUCCEEDED(hr) && texture)
          hr = texture->QueryInterface(IID_IDirect3DTexture9,
                                       reinterpret_cast<void **>(&tex));
        if (SUCCEEDED(hr) && tex)
          hr = tex->GetSurfaceLevel(0, &level);
        if (SUCCEEDED(hr) && owned(level))
          call(SetTexture, DWORD(i),
               static_cast<IDirect3DBaseTexture9 *>(nullptr));
        // A failing API may still populate an output. Always balance it.
        drop(level);
        drop(tex);
        drop(texture);
      }
      // Break owned MRT attachments before replacing RT0 with the
      // backbuffer; mixed formats need not be supported by the device.
      for (unsigned remaining = rt_count; remaining > 0; --remaining) {
        const unsigned i = remaining - 1;
        IDirect3DSurface9 *rt = nullptr;
        if (SUCCEEDED(call(GetRt, DWORD(i), &rt)) && owned(rt)) {
          if (i)
            call(SetRt, DWORD(i), static_cast<IDirect3DSurface9 *>(nullptr));
          else {
            // An interrupted bracket may retain a depth surface
            // incompatible with the backbuffer dimensions or MSAA.
            call(SetDepth, static_cast<IDirect3DSurface9 *>(nullptr));
            IDirect3DSurface9 *back = nullptr;
            if (SUCCEEDED(call(GetBackBuffer, UINT(0), UINT(0),
                               D3DBACKBUFFER_TYPE_MONO, &back)) &&
                back)
              call(SetRt, DWORD(0), back);
            drop(back);
          }
        }
        drop(rt);
      }
    }
    saved.release();
    drop(b);
    drop(e);
    drop(c);
    drop(m);
    width = height = 0;
    phase = Phase::Idle;
    selected = nullptr;
    selected_surface = nullptr;
    has_frame = mask_valid = false;
    blocked = false;
  }
};
LinearEmissionPass::~LinearEmissionPass() { detach(); }
const LinearEmissionPassCaps &LinearEmissionPass::caps() const noexcept {
  static const LinearEmissionPassCaps off{};
  return impl_ ? impl_->caps : off;
}
HRESULT LinearEmissionPass::attach(IDirect3DDevice9 *device,
                                   void *const *native, const D3DCAPS9 &caps9,
                                   D3DFORMAT format, D3DFORMAT depth,
                                   unsigned requested_policies) noexcept {
  detach();
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
  if (fixture_source_over_) requested_policies = 2;
#endif
  if (!device || !native || !requested_policies || (requested_policies & ~3u))
    return E_INVALIDARG;
  impl_ = new (std::nothrow) Impl;
  if (!impl_) return E_OUTOFMEMORY;
  auto &p = *impl_;
  p.device = device; p.native = native; p.caps9 = caps9;
  p.depth_format = depth;
  p.rt_count = std::min(4u, unsigned(caps9.NumSimultaneousRTs));
  p.caps.reason = "caps";
  if (caps9.NumSimultaneousRTs < 3 ||
      caps9.PixelShaderVersion < D3DPS_VERSION(3, 0) ||
      caps9.VertexShaderVersion < D3DVS_VERSION(3, 0) ||
      !(caps9.PrimitiveMiscCaps & D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING))
    return D3DERR_NOTAVAILABLE;
  unsigned supported = requested_policies;
  constexpr DWORD fade_caps = D3DPMISCCAPS_SEPARATEALPHABLEND |
      D3DPMISCCAPS_INDEPENDENTWRITEMASKS | D3DPMISCCAPS_COLORWRITEENABLE;
  if ((caps9.PrimitiveMiscCaps & fade_caps) != fade_caps) supported &= ~2u;
  if (!supported) { p.caps.reason = "source-over caps"; return D3DERR_NOTAVAILABLE; }
  // A transient format query or program failure cannot masquerade as an
  // immutable unsupported producer. NOTAVAILABLE is the format-cap refusal.
  p.caps.supported_policies = supported;
  IDirect3D9 *factory = nullptr;
  D3DDEVICE_CREATION_PARAMETERS creation{};
  HRESULT hr = p.call(GetDirect3D, &factory);
  if (SUCCEEDED(hr) && !factory) hr = E_FAIL;
  if (SUCCEEDED(hr)) hr = p.call(GetCreation, &creation);
  if (SUCCEEDED(hr))
    hr = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
        D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
        D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
  if (SUCCEEDED(hr))
    hr = factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
                                    format, 0, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
  if (SUCCEEDED(hr))
    hr = factory->CheckDepthStencilMatch(creation.AdapterOrdinal, creation.DeviceType,
                                         format, D3DFMT_A16B16G16R16F, depth);
  drop(factory);
  p.caps.formats = hr;
  if (FAILED(hr)) {
    if (hr == D3DERR_NOTAVAILABLE) p.caps.supported_policies = 0;
    p.caps.reason = "formats"; return hr;
  }
#ifdef X3M_LINEAR_EMISSION_PASS_FIXTURE
  p.fused_copy = !fixture_separate_copy_;
  hr = p.call(CreatePs, p.fused_copy ? copy_words : separate_copy_words, &p.copy);
#else
  hr = p.call(CreatePs, copy_words, &p.copy);
#endif
  if (SUCCEEDED(hr) && !p.copy) hr = E_FAIL;
  if (SUCCEEDED(hr)) {
    hr = p.call(CreateVs, reinterpret_cast<const DWORD *>(quad_vertex_program()), &p.vs);
    if (SUCCEEDED(hr) && !p.vs) hr = E_FAIL;
  }
  if (SUCCEEDED(hr)) {
    hr = p.call(CreateDecl, quad_declaration, &p.declaration);
    if (SUCCEEDED(hr) && !p.declaration) hr = E_FAIL;
  }
  HRESULT first = hr;
  if (SUCCEEDED(hr)) {
    for (unsigned policy : {1u, 2u}) {
      if (!(supported & policy)) continue;
      auto *&program = policy == 1 ? p.composite : p.source_over_composite;
      const DWORD *words = policy == 1 ? composite_words : source_over_words;
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
      if (policy == 2 && fixture_source_over_) words = fixture_source_over_;
#endif
      hr = p.call(CreatePs, words, &program);
      if (SUCCEEDED(hr) && !program) hr = E_FAIL;
      if (SUCCEEDED(hr)) p.caps.available_policies |= policy;
      else { drop(program); if (SUCCEEDED(first)) first = hr; }
    }
  }
  p.caps.programs = first;
  p.caps.enabled = p.caps.available_policies != 0;
  if (!p.caps.enabled) {
    drop(p.copy); drop(p.composite); drop(p.source_over_composite);
    drop(p.vs); drop(p.declaration);
  }
  p.caps.reason = FAILED(first) ? "programs" : "ok";
  return first;
}
HRESULT LinearEmissionPass::ensure_targets(UINT width, UINT height) noexcept {
  if (!impl_ || !impl_->caps.enabled || !width || !height)
    return E_INVALIDARG;
  auto &p = *impl_;
  if (p.phase != Impl::Phase::Idle)
    return D3DERR_INVALIDCALL;
  if (width == p.width && height == p.height && p.b && p.e && p.c && p.m)
    return S_OK;
  if (width > p.caps9.MaxTextureWidth || height > p.caps9.MaxTextureHeight)
    return D3DERR_NOTAVAILABLE;
  if (p.fault(LinearEmissionPassFault::Allocation))
    return E_OUTOFMEMORY;
  IDirect3DSurface9 *surfaces[4]{};
  HRESULT hr = S_OK;
  for (auto &surface : surfaces) {
    IDirect3DTexture9 *texture = nullptr;
    hr = p.call(CreateTexture, width, height, UINT(1),
                DWORD(D3DUSAGE_RENDERTARGET), D3DFMT_A16B16G16R16F,
                D3DPOOL_DEFAULT, &texture, static_cast<HANDLE *>(nullptr));
    if (SUCCEEDED(hr) && !texture)
      hr = E_FAIL;
    if (SUCCEEDED(hr))
      hr = texture->GetSurfaceLevel(0, &surface);
    if (SUCCEEDED(hr) && !surface)
      hr = E_FAIL;
    drop(texture);
    if (FAILED(hr))
      break;
  }
  if (SUCCEEDED(hr))
    for (unsigned i = 0; i < 4; ++i)
      for (unsigned j = 0; j < i; ++j) {
        bool equal = false;
        HRESULT identity = object_identity(surfaces[i], surfaces[j], equal);
        if (FAILED(identity) || equal)
          hr = FAILED(identity) ? identity : E_INVALIDARG;
      }
  if (SUCCEEDED(hr)) {
    p.reset_targets();
    p.allocation_count += 4;
    p.b = surfaces[0];
    p.e = surfaces[1];
    p.c = surfaces[2];
    p.m = surfaces[3];
    p.width = width;
    p.height = height;
  } else
    for (auto &surface : surfaces)
      drop(surface);
  return hr;
}
LinearEmissionPreparation
LinearEmissionPass::begin_frame(std::uint64_t frame) noexcept {
  LinearEmissionPreparation out;
  if (!impl_ || !impl_->m)
    return out;
  auto &p = *impl_;
  if (p.phase != Impl::Phase::Idle || (p.has_frame && p.frame == frame))
    return out;
  p.mask_valid = false;
  p.blocked = true;
  p.source_over = false;
  out.saved = p.save();
  if (FAILED(out.saved)) {
    p.saved.release();
    return out;
  }
  out.operation =
      p.fault(LinearEmissionPassFault::FrameClear) ? E_FAIL : p.clear(p.m);
  out.restore = p.restore(p.saved.rt[0]);
  out.state_preserved = SUCCEEDED(out.restore);
  p.saved.release();
  out.ready = SUCCEEDED(out.operation) && out.state_preserved;
  p.has_frame = true;
  p.frame = frame;
  p.mask_valid = out.ready;
  p.blocked = !out.ready;
  return out;
}
LinearEmissionPreparation
LinearEmissionPass::prepare(const LinearEmissionBoundary &boundary) noexcept {
  LinearEmissionPreparation out;
  if (!impl_)
    return out;
  auto &p = *impl_;
  if (!p.caps.enabled || !p.b || !p.has_frame || p.blocked ||
      p.phase != Impl::Phase::Idle || !boundary.admitted || !boundary.scene ||
      !boundary.augmented || boundary.frame != p.frame)
    return out;
  if (boundary.policy != LinearCompositionPolicy::AdditiveEmission && boundary.policy != LinearCompositionPolicy::DistanceFade) {
    out.operation = E_INVALIDARG; return out;
  }
  p.source_over = boundary.policy == LinearCompositionPolicy::DistanceFade;
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
  if (fixture_source_over_) p.source_over = true;
#endif
  const auto policy = p.source_over ? LinearCompositionPolicy::DistanceFade : LinearCompositionPolicy::AdditiveEmission;
  if (!p.caps.supports(policy)) { out.operation = D3DERR_NOTAVAILABLE; return out; }
  out.saved = p.save();
  if (FAILED(out.saved)) {
    p.saved.release();
    return out;
  }
  if (!p.source_ok(boundary)) {
    out.operation = D3DERR_INVALIDCALL;
    p.saved.release();
    return out;
  }
  out.operation = p.fault(LinearEmissionPassFault::Copy)
                      ? E_FAIL
                      : p.draw(p.b, boundary.scene, false);
  if (SUCCEEDED(out.operation)) {
    // Retain the preparation failure boundary after initialization, including
    // partial-copy failures: neither B nor E is published before the source.
    // A clean restore therefore preserves A and all earlier coverage in M.
    if (p.fault(LinearEmissionPassFault::EmissionClear)) out.operation = E_FAIL;
    else if (!p.fused_copy) out.operation = p.clear(p.e);
  }
  if (SUCCEEDED(out.operation))
    out.operation = p.restore(p.b);
  if (SUCCEEDED(out.operation))
    out.operation = p.call(SetRt, DWORD(1), p.e);
  if (SUCCEEDED(out.operation))
    out.operation = p.call(SetRt, DWORD(2), p.m);
  if (SUCCEEDED(out.operation) && p.supported_state(D3DRS_COLORWRITEENABLE1))
    out.operation = p.call(SetRs, D3DRS_COLORWRITEENABLE1, DWORD(15));
  if (SUCCEEDED(out.operation) && p.supported_state(D3DRS_COLORWRITEENABLE2))
    out.operation = p.call(SetRs, D3DRS_COLORWRITEENABLE2, DWORD(15));
  if (SUCCEEDED(out.operation) && p.source_over) {
    out.operation = p.call(SetRs, D3DRS_COLORWRITEENABLE2, DWORD(7));
    if (SUCCEEDED(out.operation)) out.operation = p.call(SetRs, D3DRS_SEPARATEALPHABLENDENABLE, DWORD(TRUE));
    if (SUCCEEDED(out.operation)) out.operation = p.call(SetRs, D3DRS_BLENDOPALPHA, DWORD(D3DBLENDOP_ADD));
    if (SUCCEEDED(out.operation)) out.operation = p.call(SetRs, D3DRS_SRCBLENDALPHA, DWORD(D3DBLEND_ONE));
    if (SUCCEEDED(out.operation)) out.operation = p.call(SetRs, D3DRS_DESTBLENDALPHA, DWORD(D3DBLEND_INVSRCALPHA));
  }
  if (SUCCEEDED(out.operation))
    out.operation = p.call(SetViewport, &p.saved.viewport);
  if (SUCCEEDED(out.operation))
    out.operation = p.call(SetScissor, &p.saved.scissor);
  if (SUCCEEDED(out.operation)) {
    if (p.fault(LinearEmissionPassFault::SourceBind)) out.operation = E_FAIL;
    else if (p.source_over) out.operation = p.call(SetVs, boundary.augmented_vertex);
    if (SUCCEEDED(out.operation)) out.operation = p.call(SetPs, boundary.augmented);
  }
  if (FAILED(out.operation)) {
    out.restore = p.restore(boundary.scene);
    if (p.fault(LinearEmissionPassFault::Restore))
      out.restore = E_FAIL;
    out.state_preserved = SUCCEEDED(out.restore);
    // Complete rollback leaves A and every prior enhanced footprint in M
    // intact. This source will run natively and was never enhanced.
    p.blocked = !out.state_preserved;
    p.mask_valid &= out.state_preserved;
    p.saved.release();
    return out;
  }
  p.input_scene = boundary.scene;
  p.phase = Impl::Phase::Prepared;
  p.recovered = false;
  out.ready = true;
  return out;
}
LinearEmissionCompletion LinearEmissionPass::finish(HRESULT source) noexcept {
  if (!impl_ || impl_->phase != Impl::Phase::Prepared)
    return {};
  auto &p = *impl_;
  p.completion = {};
  p.completion.source = source;
  if (FAILED(source)) {
    p.completion.image = LinearEmissionImage::Incomplete;
    p.mask_valid = false;
    p.blocked = true;
    p.selected = &p.b;
  } else {
    p.completion.composition = p.fault(LinearEmissionPassFault::Composite)
                                   ? E_FAIL
                                   : p.draw(p.c, p.saved.rt[0], true);
    p.completion.image = SUCCEEDED(p.completion.composition)
                             ? LinearEmissionImage::Linear
                             : LinearEmissionImage::Native;
    p.selected = SUCCEEDED(p.completion.composition) ? &p.c : &p.b;
  }
  p.selected_surface = *p.selected;
  p.completion.restore = p.restore(p.selected_surface);
  if (p.fault(LinearEmissionPassFault::Restore))
    p.completion.restore = E_FAIL;
  p.completion.candidate_bound = SUCCEEDED(p.completion.restore);
  if (!p.completion.candidate_bound) {
    p.completion.image = LinearEmissionImage::Incomplete;
    p.mask_valid = false;
    p.blocked = true;
  }
  p.phase = Impl::Phase::Pending;
  return p.completion;
}
IDirect3DSurface9 **LinearEmissionPass::owning_candidate() noexcept {
  return impl_ && impl_->phase == Impl::Phase::Pending &&
                 impl_->completion.candidate_bound
             ? impl_->selected
             : nullptr;
}
HRESULT LinearEmissionPass::acknowledge_exchange(bool exchanged) noexcept {
  if (!impl_ || impl_->phase != Impl::Phase::Pending || !impl_->selected)
    return D3DERR_INVALIDCALL;
  auto &p = *impl_;
  if (!exchanged)
    return *p.selected == p.selected_surface ? S_FALSE : E_INVALIDARG;
  if (!p.completion.candidate_bound || *p.selected != p.input_scene ||
      *p.selected == p.selected_surface)
    return E_INVALIDARG;
  p.saved.release();
  p.phase = Impl::Phase::Idle;
  p.selected = nullptr;
  p.selected_surface = nullptr;
  return S_OK;
}
LinearEmissionCompletion LinearEmissionPass::recover_native() noexcept {
  if (!impl_ || impl_->phase != Impl::Phase::Pending || impl_->recovered)
    return {};
  auto &p = *impl_;
  if (!p.selected || *p.selected != p.selected_surface)
    return {}; // ownership already exchanged
  p.recovered = true;
  p.selected = &p.b;
  p.selected_surface = p.b;
  p.completion.restore = p.restore(p.b);
  if (p.fault(LinearEmissionPassFault::RecoveryRestore))
    p.completion.restore = E_FAIL;
  p.completion.candidate_bound = SUCCEEDED(p.completion.restore);
  p.completion.image =
      p.completion.candidate_bound && SUCCEEDED(p.completion.source)
          ? LinearEmissionImage::Native
          : LinearEmissionImage::Incomplete;
  if (!p.completion.candidate_bound) {
    p.mask_valid = false;
    p.blocked = true;
  }
  return p.completion;
}
IDirect3DSurface9 *LinearEmissionPass::coverage_target() const noexcept {
  return impl_ ? impl_->m : nullptr;
}
bool LinearEmissionPass::coverage_valid() const noexcept {
  return impl_ && impl_->phase == Impl::Phase::Idle && impl_->mask_valid &&
         !impl_->blocked;
}
unsigned LinearEmissionPass::allocations() const noexcept {
  return impl_ ? impl_->allocation_count : 0;
}
bool LinearEmissionPass::reference_accounting_busy() const noexcept {
  return impl_ && (impl_->phase != Impl::Phase::Idle || impl_->saved.rt[0]);
}
unsigned LinearEmissionPass::references() const noexcept {
  if (!impl_)
    return 0;
  const auto &p = *impl_;
  return !!p.b + !!p.e + !!p.c + !!p.m + !!p.vs + !!p.copy + !!p.composite +
         !!p.declaration + !!p.source_over_composite;
}
void LinearEmissionPass::before_reset() noexcept {
  if (impl_)
    impl_->reset_targets();
}
void LinearEmissionPass::detach() noexcept {
  if (!impl_)
    return;
  impl_->reset_targets();
  drop(impl_->copy);
  drop(impl_->composite);
  drop(impl_->source_over_composite);
  drop(impl_->vs);
  drop(impl_->declaration);
  delete impl_;
  impl_ = nullptr;
}
#ifdef X3M_LINEAR_EMISSION_PASS_FIXTURE
void LinearEmissionPass::inject(LinearEmissionPassFault f,
                                unsigned count) noexcept {
  if (impl_) {
    impl_->fault_kind = f;
    impl_->fault_count = count;
  }
}
LinearEmissionCompletion
LinearEmissionPass::fixture_completion() const noexcept {
  return impl_ ? impl_->completion : LinearEmissionCompletion{};
}
IDirect3DSurface9 *LinearEmissionPass::fixture_native() const noexcept {
  return impl_ ? impl_->b : nullptr;
}
IDirect3DSurface9 *LinearEmissionPass::fixture_energy() const noexcept {
  return impl_ ? impl_->e : nullptr;
}
#endif
} // namespace x3m::renderer
