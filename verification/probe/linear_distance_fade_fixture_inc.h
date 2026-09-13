// Included only by the detached linear-material executable's explicit fade
// mode.
#include "../../src/renderer/linear_emission_pass.h"
#include "../../src/renderer/quad_vertex_program.h"
#include <algorithm>
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
  Com<IDirect3DBaseTexture9> textures[4];
  DWORD rs[std::size(watched)]{}, ss[4][std::size(samplers)]{};
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
    for (unsigned i = 0; i < 4; ++i) {
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
    for (unsigned i = 0; i < 4; ++i)
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
  Target(IDirect3DDevice9 *d, unsigned size) {
    api(d->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, format,
                         D3DPOOL_DEFAULT, &texture.p, nullptr));
    api(texture->GetSurfaceLevel(0, &surface.p));
  }
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
  Draw(IDirect3DDevice9 *device, const Case &c, unsigned step, unsigned width)
      : d(device) {
    // Ordinary source, two truly overlapping quads in one DIP, or two ordered
    // DIPs with different rectangles. Integer pixel bounds avoid edge
    // ambiguity.
    unsigned lo = step ? 6 : 2, hi = step ? 14 : 10;
    unsigned copies = c.reverse == 1 ? 2 : 1;
    primitives = 2 * copies;
    float verts[8][14]{};
    unsigned short indices[12]{};
    for (unsigned copy = 0; copy < copies; ++copy) {
      unsigned base = 4 * copy;
      const unsigned xy[4][2] = {{lo, lo}, {hi, lo}, {lo, hi}, {hi, hi}};
      for (unsigned n = 0; n < 4; ++n) {
        auto &v = verts[base + n];
        v[0] = 2.f * (xy[n][0] - .25f) / width - 1;
        v[1] = 1 - 2.f * (xy[n][1] - .25f) / width;
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
  const float alpha[4] = {1, 0, 0, 0};
  const unsigned vi = pair_v[c.pair];
  api(d->SetVertexShaderConstantF((vi == 9 || vi == 12) ? 18 : 39, alpha, 1));
  draw.bind();
}
void equal(const std::vector<Pixel> &a, const std::vector<Pixel> &b,
           const char *message) {
  require(a.size() == b.size() &&
              !std::memcmp(a.data(), b.data(), a.size() * sizeof(Pixel)),
          message);
}
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
    for (auto c : cases) {
      if (after_reset && (c.id % 10) != 0)
        continue;
      require(c.pair >= 110 && c.pair < 116 && c.reverse <= 3 && c.affine <= 5,
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
        dump("M", c.id, step, gpu.read(pass.coverage_target(), format));
        dump("C", c.id, step, gpu.read(scene.surface.p, format));
        if (current.f[6] == 0)
          equal(gpu.read(scene.surface.p, format), initial,
                "zero-alpha repeated bracket is exact raw A");
      }
      require(pass.allocations() == allocations, "no per-draw pool allocation");
      std::printf("FADE_CASE id=%u pair=%u steps=%u native=%u brackets=%u "
                  "fault=%u reset=%u\n",
                  c.id, c.pair, steps, case_native, case_brackets, c.affine,
                  unsigned(after_reset));
      ++checked;
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
  }
  const ULONG final_refs = d->AddRef();
  d->Release();
  require(final_refs == start_refs, "all experiment resources retired");
  std::printf("FADE_BATCH reset=%u cases=%u brackets=%u native=%u restored=%u "
              "refs_before=%lu refs_after=%lu\n",
              unsigned(after_reset), checked, brackets, native_calls, restored,
              start_refs, final_refs);
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
  std::printf("FADE_RESULT PASS reset=1 partial_vs_failures=%u\n",
              fade_fixture::failed_vs_calls);
}
