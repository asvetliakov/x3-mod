// Component-only integration, included after the detached fixture helpers.
// Original source geometry is indexed and submitted exactly once by this
// caller.
using EmissionPass = x3m::renderer::LinearEmissionPass;
using PassImage = x3m::renderer::LinearEmissionImage;
using PassFault = x3m::renderer::LinearEmissionPassFault;
std::vector<float> pass_read(MrtFixture &f, IDirect3DSurface9 *surface) {
  Target t;
  t.surface.p = surface;
  surface->AddRef();
  return f.read(t, true);
}
// Narrow native-table twins reject APIs unavailable on the advertised caps.
// Actual source draws still use the real device; only injected calls are
// tested.
static void **pass_real_methods = nullptr;
static unsigned pass_cap_twin = 0, pass_unsupported_calls = 0;
bool pass_unsupported(D3DRENDERSTATETYPE state) {
  if (state == D3DRS_COLORWRITEENABLE3)
    return pass_cap_twin != 0;
  return pass_cap_twin == 1 &&
         (state == D3DRS_COLORWRITEENABLE || state == D3DRS_COLORWRITEENABLE1 ||
          state == D3DRS_COLORWRITEENABLE2 ||
          state == D3DRS_SEPARATEALPHABLENDENABLE);
}
HRESULT WINAPI pass_get_state(IDirect3DDevice9 *d, D3DRENDERSTATETYPE state,
                              DWORD *value) {
  if (pass_unsupported(state)) {
    ++pass_unsupported_calls;
    return D3DERR_INVALIDCALL;
  }
  using Fn = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DRENDERSTATETYPE, DWORD *);
  return reinterpret_cast<Fn>(pass_real_methods[58])(d, state, value);
}
HRESULT WINAPI pass_set_state(IDirect3DDevice9 *d, D3DRENDERSTATETYPE state,
                              DWORD value) {
  if (pass_unsupported(state)) {
    ++pass_unsupported_calls;
    return D3DERR_INVALIDCALL;
  }
  using Fn = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DRENDERSTATETYPE, DWORD);
  return reinterpret_cast<Fn>(pass_real_methods[57])(d, state, value);
}
struct PassHarness {
  MrtFixture &f;
  EmissionPass local_pass;
  EmissionPass &pass;
  void *native[119]{};
  Com<IDirect3DSurface9> owner;
  Com<IDirect3DVertexBuffer9> vertices;
  Com<IDirect3DIndexBuffer9> indices;
  std::uint64_t frame = 0;
  unsigned submissions = 0;
  PassHarness(MrtFixture &fixture, unsigned twin = 0,
              EmissionPass *retained = nullptr)
      : f(fixture), pass(retained ? *retained : local_pass) {
    std::copy_n(*reinterpret_cast<void ***>(f.d), 119, native);
    D3DCAPS9 caps{};
    D3DDISPLAYMODE display{};
    api(f.d->GetDeviceCaps(&caps));
    api(f.d->GetDisplayMode(0, &display));
    if (twin) {
      pass_real_methods = *reinterpret_cast<void ***>(f.d);
      pass_cap_twin = twin;
      pass_unsupported_calls = 0;
      native[57] = reinterpret_cast<void *>(&pass_set_state);
      native[58] = reinterpret_cast<void *>(&pass_get_state);
      caps.NumSimultaneousRTs = 3;
      if (twin == 1)
        caps.PrimitiveMiscCaps &=
            ~(D3DPMISCCAPS_INDEPENDENTWRITEMASKS |
              D3DPMISCCAPS_SEPARATEALPHABLEND | D3DPMISCCAPS_COLORWRITEENABLE);
    }
    if (!pass.references())
      api(pass.attach(f.d, twin ? native : *reinterpret_cast<void ***>(f.d),
                      caps, display.Format, D3DFMT_D24S8));
    const unsigned prior_allocations = pass.allocations();
    api(pass.ensure_targets(f.width, f.height));
    need(pass.references() == 8 && pass.allocations() == prior_allocations + 4,
         "pass initial resource accounting");
    Target initial;
    f.target(initial, D3DFMT_A16B16G16R16F);
    owner.p = initial.surface.p;
    initial.surface.p = nullptr;
    api(f.d->CreateVertexBuffer(32 + 4 * sizeof(Vertex), D3DUSAGE_WRITEONLY, 0,
                                D3DPOOL_MANAGED, &vertices.p, nullptr));
    api(f.d->CreateIndexBuffer(8, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
                               D3DPOOL_MANAGED, &indices.p, nullptr));
    void *data = nullptr;
    api(indices->Lock(0, 0, &data, 0));
    const unsigned short order[] = {0, 1, 2, 3};
    std::memcpy(data, order, sizeof(order));
    api(indices->Unlock());
  }
  void source(const Case &cs, unsigned i) {
    Target target;
    target.surface.p = owner.p;
    owner->AddRef();
    f.source_state(cs, i, target, false);
    const auto &o = cs.ops[i];
    float vw = cs.h.flags & 16 ? f.width / 2.f : float(f.width),
          vh = cs.h.flags & 16 ? f.height / 2.f : float(f.height);
    float l = 2 * o.x0 - 1 - 1 / vw, r = 2 * o.x1 - 1 - 1 / vw,
          t = 1 - 2 * o.y0 + 1 / vh, b = 1 - 2 * o.y1 + 1 / vh;
    Vertex v[] = {{l, t, o.z, 1, 0, 0, o.fade},
                  {r, t, o.z, 1, 1, 0, o.fade},
                  {l, b, o.z, 1, 0, 1, o.fade},
                  {r, b, o.z, 1, 1, 1, o.fade}};
    void *data = nullptr;
    api(vertices->Lock(32, sizeof(v), &data, 0));
    std::memcpy(data, v, sizeof(v));
    api(vertices->Unlock());
    api(f.d->SetStreamSource(0, vertices.p, 32, sizeof(Vertex)));
    api(f.d->SetStreamSourceFreq(0, 1));
    api(f.d->SetStreamSource(1, vertices.p, 0, sizeof(Vertex)));
    api(f.d->SetStreamSourceFreq(1, 1));
    api(f.d->SetIndices(indices.p));
    f.rs(D3DRS_COLORWRITEENABLE1, pass_cap_twin == 1 ? 15 : 3);
    f.rs(D3DRS_COLORWRITEENABLE2, pass_cap_twin == 1 ? 15 : 5);
  }
  std::vector<DWORD> snapshot() {
    std::vector<DWORD> result;
    auto identity = [&](IUnknown *object) {
      Com<IUnknown> canonical;
      if (object)
        api(object->QueryInterface(IID_IUnknown,
                                   reinterpret_cast<void **>(&canonical.p)));
      result.push_back(DWORD(reinterpret_cast<std::uintptr_t>(canonical.p)));
    };
    for (unsigned i = 1; i < f.rt_slots; ++i) {
      Com<IDirect3DSurface9> rt;
      HRESULT hr = f.d->GetRenderTarget(i, &rt.p);
      need(SUCCEEDED(hr) || (hr == D3DERR_NOTFOUND && !rt.p),
           "snapshot RT query");
      identity(rt.p);
    }
    Com<IDirect3DSurface9> depth;
    HRESULT depth_hr = f.d->GetDepthStencilSurface(&depth.p);
    need(SUCCEEDED(depth_hr) || (depth_hr == D3DERR_NOTFOUND && !depth.p),
         "snapshot DS query");
    identity(depth.p);
    for (unsigned i = 0; i < 3; ++i) {
      Com<IDirect3DBaseTexture9> texture;
      api(f.d->GetTexture(i, &texture.p));
      identity(texture.p);
    }
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps;
    Com<IDirect3DVertexDeclaration9> declaration;
    api(f.d->GetVertexShader(&vs.p));
    api(f.d->GetPixelShader(&ps.p));
    api(f.d->GetVertexDeclaration(&declaration.p));
    identity(vs.p);
    identity(ps.p);
    identity(declaration.p);
    DWORD fvf;
    api(f.d->GetFVF(&fvf));
    result.push_back(fvf);
    const D3DRENDERSTATETYPE state[] = {D3DRS_ZENABLE,
                                        D3DRS_ZWRITEENABLE,
                                        D3DRS_ZFUNC,
                                        D3DRS_ALPHATESTENABLE,
                                        D3DRS_ALPHAFUNC,
                                        D3DRS_ALPHAREF,
                                        D3DRS_ALPHABLENDENABLE,
                                        D3DRS_SRCBLEND,
                                        D3DRS_DESTBLEND,
                                        D3DRS_BLENDOP,
                                        D3DRS_SEPARATEALPHABLENDENABLE,
                                        D3DRS_SRCBLENDALPHA,
                                        D3DRS_DESTBLENDALPHA,
                                        D3DRS_BLENDOPALPHA,
                                        D3DRS_CULLMODE,
                                        D3DRS_FILLMODE,
                                        D3DRS_COLORWRITEENABLE,
                                        D3DRS_COLORWRITEENABLE1,
                                        D3DRS_COLORWRITEENABLE2,
                                        D3DRS_COLORWRITEENABLE3,
                                        D3DRS_SCISSORTESTENABLE,
                                        D3DRS_STENCILENABLE,
                                        D3DRS_STENCILWRITEMASK,
                                        D3DRS_FOGENABLE,
                                        D3DRS_DITHERENABLE,
                                        D3DRS_SRGBWRITEENABLE,
                                        D3DRS_CLIPPLANEENABLE,
                                        D3DRS_WRAP0,
                                        D3DRS_MULTISAMPLEMASK,
                                        D3DRS_CLIPPING};
    for (auto key : state) {
      DWORD v;
      api(f.d->GetRenderState(key, &v));
      result.push_back(v);
    }
    for (unsigned s = 0; s < 3; ++s)
      for (auto key : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
                       D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE,
                       D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS}) {
        DWORD v;
        api(f.d->GetSamplerState(s, key, &v));
        result.push_back(v);
      }
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    api(f.d->GetViewport(&viewport));
    api(f.d->GetScissorRect(&scissor));
    auto bytes = [&](const void *data, unsigned size) {
      const auto *p = static_cast<const DWORD *>(data);
      result.insert(result.end(), p, p + size / 4);
    };
    bytes(&viewport, sizeof(viewport));
    bytes(&scissor, sizeof(scissor));
    float constants[32][4];
    api(f.d->GetPixelShaderConstantF(0, constants[0], 32));
    bytes(constants, sizeof(constants));
    api(f.d->GetVertexShaderConstantF(0, constants[0], 16));
    bytes(constants, 16 * 16);
    for (unsigned s = 0; s < 2; ++s) {
      Com<IDirect3DVertexBuffer9> stream;
      UINT offset, stride, freq;
      api(f.d->GetStreamSource(s, &stream.p, &offset, &stride));
      api(f.d->GetStreamSourceFreq(s, &freq));
      need(stream.p == vertices.p && offset == (s ? 0u : 32u) &&
               stride == sizeof(Vertex) && freq == 1,
           "pass indexed input state");
    }
    Com<IDirect3DIndexBuffer9> index;
    api(f.d->GetIndices(&index.p));
    need(index.p == indices.p, "pass changed indices");
    return result;
  }
  void seed(const Case &cs) {
    f.initialize_mrt(cs);
    Target destination;
    destination.surface.p = owner.p;
    owner->AddRef();
    api(f.d->BeginScene());
    f.copy(f.scene, destination);
    api(f.d->EndScene());
    f.fence();
    source(cs, 0);
    auto saved = snapshot();
    auto started = pass.begin_frame(++frame);
    need(started.ready && started.state_preserved && snapshot() == saved,
         "pass frame clear/restoration");
    need(!pass.begin_frame(frame).ready, "pass double frame clear");
  }
  x3m::renderer::LinearEmissionBoundary boundary(const Case &cs, unsigned i) {
    return {owner.p, f.source_pixel(cs, i, true), frame, true};
  }
  void dip() {
    api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0, 4, 0, 2));
    ++submissions;
  }
  void adopt(bool checks = true) {
    auto **slot = pass.owning_candidate();
    need(slot && *slot && *slot != owner.p, "pass owning candidate");
    if (checks) {
      Com<IDirect3DSurface9> physical;
      Com<IUnknown> got, wanted;
      api(f.d->GetRenderTarget(0, &physical.p));
      api(physical->QueryInterface(IID_IUnknown,
                                   reinterpret_cast<void **>(&got.p)));
      api((*slot)->QueryInterface(IID_IUnknown,
                                  reinterpret_cast<void **>(&wanted.p)));
      need(got.p == wanted.p, "candidate physical RT0 identity");
    }
    if (checks)
      need(FAILED(pass.acknowledge_exchange(true)),
           "pass acknowledgement without exchange");
    std::swap(owner.p, *slot);
    api(pass.acknowledge_exchange(true));
    need(!pass.owning_candidate() && !pass.reference_accounting_busy(),
         "pass pending references after publication");
  }
  x3m::renderer::LinearEmissionCompletion
  execute(const Case &cs, unsigned i, bool checks = true,
          HRESULT source_result = S_OK) {
    auto state = checks ? snapshot() : std::vector<DWORD>{};
    unsigned allocated = pass.allocations();
    auto prep = pass.prepare(boundary(cs, i));
    need(prep.ready, "component prepare");
    need(pass.reference_accounting_busy() && !pass.coverage_valid(),
         "pass transaction coverage unavailable");
    dip();
    auto done = pass.finish(source_result);
    need(!pass.coverage_valid(), "pending coverage unavailable");
    if (checks) {
      need(snapshot() == state, "component source state changed");
      need(pass.allocations() == allocated, "per-draw target allocation");
    }
    return done;
  }
};
void pass_faults(PassHarness &h, const Case &source_case) {
  auto &f = h.f;
  Case cs = source_case;
  cs.ops.resize(1);
  cs.h.count = 1;
  unsigned checks = 0;
  for (auto fault : {PassFault::Save, PassFault::Copy, PassFault::EmissionClear,
                     PassFault::SourceBind}) {
    h.seed(cs);
    h.source(cs, 0);
    api(f.d->BeginScene());
    auto enhanced = h.execute(cs, 0);
    need(enhanced.image == PassImage::Linear,
         "refusal prelude enhanced source");
    h.adopt();
    api(f.d->EndScene());
    f.fence();
    auto before_a = pass_read(f, h.owner.p),
         before_m = pass_read(f, h.pass.coverage_target());
    h.source(cs, 0);
    auto state = h.snapshot();
    unsigned draws = h.submissions;
    h.pass.inject(fault);
    api(f.d->BeginScene());
    auto prep = h.pass.prepare(h.boundary(cs, 0));
    need(!prep.ready && prep.state_preserved && h.snapshot() == state &&
             !h.pass.reference_accounting_busy() && h.pass.coverage_valid(),
         "clean preparation refusal preserves enhanced coverage");
    api(f.d->EndScene());
    f.fence();
    auto after_a = pass_read(f, h.owner.p),
         after_m = pass_read(f, h.pass.coverage_target());
    need(std::memcmp(before_a.data(), after_a.data(), before_a.size() * 4) ==
                 0 &&
             std::memcmp(before_m.data(), after_m.data(),
                         before_m.size() * 4) == 0,
         "clean refusal changed A or prior enhanced M");
    api(f.d->BeginScene());
    h.dip();
    api(f.d->EndScene());
    need(h.submissions == draws + 1, "clean refusal native source once");
    ++checks;
  }
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    h.seed(cs);
    h.source(cs, 0);
    unsigned draws = h.submissions;
    api(f.d->BeginScene());
    if (scenario == 0)
      h.pass.inject(PassFault::Composite);
    if (scenario == 2 || scenario == 3)
      h.pass.inject(PassFault::Restore);
    auto done = h.execute(cs, 0, true, scenario == 1 ? E_FAIL : S_OK);
    if (scenario == 0)
      need(done.image == PassImage::Native && FAILED(done.composition) &&
               done.source == S_OK,
           "composition refusal native candidate");
    if (scenario == 1)
      need(done.image == PassImage::Incomplete && FAILED(done.source) &&
               !h.pass.coverage_valid(),
           "failed source never native parity");
    if (scenario == 2 || scenario == 3) {
      need(!done.candidate_bound && done.image == PassImage::Incomplete &&
               !h.pass.owning_candidate(),
           "restoration failure not publication");
      if (scenario == 3)
        h.pass.inject(PassFault::RecoveryRestore);
      done = h.pass.recover_native();
      need(h.pass.recover_native().image == PassImage::None,
           "repeated recovery forbidden");
      need(scenario == 3
               ? (!done.candidate_bound && done.image == PassImage::Incomplete)
               : (done.candidate_bound && done.image == PassImage::Native),
           "explicit recovery result");
    }
    if (scenario == 4) {
      need(h.pass.acknowledge_exchange(false) == S_FALSE,
           "failed exchange retains candidate ownership");
      need(!h.pass.prepare(h.boundary(cs, 0)).ready,
           "pending publication blocks another source");
      done = h.pass.recover_native();
      need(done.image == PassImage::Native && done.candidate_bound,
           "publication refusal B recovery");
    }
    if (done.candidate_bound)
      h.adopt();
    if (scenario == 1) {
      auto retained = h.pass.fixture_completion();
      need(retained.image == PassImage::Incomplete &&
               retained.source == E_FAIL && !h.pass.coverage_valid() &&
               !h.pass.prepare(h.boundary(cs, 0)).ready,
           "partial B exchange must remain incomplete and blocked");
    }
    api(f.d->EndScene());
    need(h.submissions == draws + 1,
         "source failure/recovery repeated geometry");
    if (scenario == 3) {
      h.pass.before_reset();
      api(h.pass.ensure_targets(16, 16));
    }
    ++checks;
  }
  // Exact admission boundaries: these prepare refusals touch no device state.
  for (auto state : {D3DRS_ALPHATESTENABLE, D3DRS_ZENABLE}) {
    h.seed(cs);
    h.source(cs, 0);
    f.rs(state, state == D3DRS_ZENABLE ? D3DZB_USEW : TRUE);
    auto before = h.snapshot();
    api(f.d->BeginScene());
    auto prep = h.pass.prepare(h.boundary(cs, 0));
    api(f.d->EndScene());
    need(!prep.ready && prep.state_preserved && h.snapshot() == before &&
             h.pass.coverage_valid(),
         "unsupported native source preserves enhanced coverage");
    ++checks;
  }
  // Hostile frame-clear state and populated M. Native Clear must clear its full
  // RGB surface while preserving original depth/stencil and every saved state.
  h.seed(cs);
  Target mask;
  mask.surface.p = h.pass.coverage_target();
  mask.surface->AddRef();
  f.single(mask);
  f.base();
  api(f.d->Clear(0, nullptr, D3DCLEAR_TARGET, 0xffffffffu, 1, 0));
  h.source(cs, 0);
  D3DVIEWPORT9 viewport{4, 4, 8, 8, 0, 1};
  RECT rect{5, 5, 6, 6};
  api(f.d->SetViewport(&viewport));
  api(f.d->SetScissorRect(&rect));
  f.rs(D3DRS_SCISSORTESTENABLE, TRUE);
  f.rs(D3DRS_COLORWRITEENABLE, 0);
  f.rs(D3DRS_ZWRITEENABLE, TRUE);
  f.rs(D3DRS_STENCILENABLE, TRUE);
  f.rs(D3DRS_STENCILWRITEMASK, 255);
  auto hostile = h.snapshot();
  auto frame = h.pass.begin_frame(++h.frame);
  need(frame.ready && frame.state_preserved && h.snapshot() == hostile,
       "hostile clear state restored");
  auto cleared = pass_read(f, h.pass.coverage_target());
  for (float value : cleared)
    need(value == 0 && !std::signbit(value), "frame mask full clear");
  auto depth = f.depth_probe(true);
  for (float value : depth)
    need(value == 1, "mask clear changed original depth/stencil");
  ++checks;
  h.source(cs, 0);
  h.pass.inject(PassFault::FrameClear);
  frame = h.pass.begin_frame(++h.frame);
  need(!frame.ready && frame.state_preserved && !h.pass.coverage_valid(),
       "failed frame clear mask unavailable");
  ++checks;
  h.pass.inject(PassFault::Allocation);
  need(FAILED(h.pass.ensure_targets(32, 16)), "allocation failure surfaced");
  need(h.pass.references() == 8, "allocation failure retained old pool");
  ++checks;
  for (bool pending : {false, true}) {
    h.seed(cs);
    h.source(cs, 0);
    api(f.d->BeginScene());
    auto prepared = h.pass.prepare(h.boundary(cs, 0));
    need(prepared.ready, "Reset bracket preparation");
    if (pending) {
      h.dip();
      auto done = h.pass.finish(S_OK);
      need(done.candidate_bound, "Reset pending candidate");
    }
    api(f.d->EndScene());
    Com<IUnknown> before_b, before_e, before_m, before_candidate;
    api(h.pass.fixture_native()->QueryInterface(
        IID_IUnknown, reinterpret_cast<void **>(&before_b.p)));
    api(h.pass.fixture_energy()->QueryInterface(
        IID_IUnknown, reinterpret_cast<void **>(&before_e.p)));
    api(h.pass.coverage_target()->QueryInterface(
        IID_IUnknown, reinterpret_cast<void **>(&before_m.p)));
    if (pending)
      api((*h.pass.owning_candidate())
              ->QueryInterface(IID_IUnknown,
                               reinterpret_cast<void **>(&before_candidate.p)));
    h.pass.before_reset();
    need(h.pass.references() == 4 && !h.pass.reference_accounting_busy() &&
             !h.pass.owning_candidate() && !h.pass.coverage_valid(),
         "Reset clears transaction references");
    for (unsigned slot = 0; slot < f.rt_slots; ++slot) {
      Com<IDirect3DSurface9> target;
      HRESULT hr = f.d->GetRenderTarget(slot, &target.p);
      need(SUCCEEDED(hr) || (hr == D3DERR_NOTFOUND && !target.p),
           "Reset attachment query");
      if (target.p) {
        Com<IUnknown> identity;
        api(target->QueryInterface(IID_IUnknown,
                                   reinterpret_cast<void **>(&identity.p)));
        need(identity.p != before_b.p && identity.p != before_e.p &&
                 identity.p != before_m.p && identity.p != before_candidate.p,
             "owned target still bound after Reset cleanup");
      }
    }
    Com<IDirect3DSurface9> reset_depth;
    HRESULT depth_hr = f.d->GetDepthStencilSurface(&reset_depth.p);
    need((SUCCEEDED(depth_hr) || depth_hr == D3DERR_NOTFOUND) && !reset_depth.p,
         "Reset retained incompatible depth");
    api(h.pass.ensure_targets(16, 16));
    ++checks;
  }
  std::printf("PASS_FAULTS checks=%u source_replays=0\n", checks);
}
void pass_timings(IDirect3DDevice9 *device, const char *programs,
                  const char *variants, IDirect3DSurface9 *back) {
  LARGE_INTEGER frequency;
  need(QueryPerformanceFrequency(&frequency), "pass QPC frequency");
  for (auto size : {std::pair<unsigned, unsigned>{1280, 768}, {1920, 1080}}) {
    MrtFixture f(device, size.first, size.second, false, programs, variants,
                 true);
    PassHarness h(f);
    Case cs = bench(1, 0, 1, 1);
    cs.ops.resize(1);
    cs.h.count = 1;
    cs.h.flags = 32 | 2 | (2u << 16);
    cs.ops[0].affine = 0;
    for (unsigned pair = 0; pair < 10; ++pair)
      for (unsigned order = 0; order < 2; ++order) {
        unsigned variant = pair % 2 ? 1 - order : order;
        h.seed(cs);
        h.source(cs, 0);
        f.fence();
        unsigned allocated = h.pass.allocations();
        LARGE_INTEGER begin, end;
        QueryPerformanceCounter(&begin);
        api(device->BeginScene());
        if (variant) {
          auto done = h.execute(cs, 0, false);
          need(done.image == PassImage::Linear && done.candidate_bound,
               "timed component source");
          h.adopt(false);
        } else
          h.dip();
        api(device->EndScene());
        f.fence();
        QueryPerformanceCounter(&end);
        need(h.pass.allocations() == allocated, "timed steady allocation");
        if (pair >= 2)
          std::printf("PASS_TIMING width=%u height=%u variant=%u pair=%u "
                      "order=%u completed_ms=%.9f\n",
                      size.first, size.second, variant, pair - 2, order,
                      1000. * double(end.QuadPart - begin.QuadPart) /
                          frequency.QuadPart);
      }
    h.pass.detach();
    f.single(f.scene);
    api(device->SetRenderTarget(0, back));
  }
}
void pass_experiment(IDirect3DDevice9 *device, const std::vector<Case> &cases,
                     const char *path, IDirect3DSurface9 *back,
                     const char *programs, const char *variants,
                     EmissionPass &retained) {
  D3DCAPS9 caps{};
  api(device->GetDeviceCaps(&caps));
  std::printf("MRT_CAPS slots=%lu postblend=%u independent_masks=%u\n",
              caps.NumSimultaneousRTs,
              unsigned(bool(caps.PrimitiveMiscCaps &
                            D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)),
              unsigned(bool(caps.PrimitiveMiscCaps &
                            D3DPMISCCAPS_INDEPENDENTWRITEMASKS)));
  MrtFixture f(device, 16, 16, false, programs, variants, true);
  PassHarness h(f, 0, &retained);
  std::ofstream raw(path, std::ios::binary);
  need(bool(raw), "pass raw output");
  unsigned comparisons = 0;
  for (const auto &cs : cases) {
    // Independent qualified fixture uses untouched original PS baseline and
    // retained two-output B/E parity; the component then runs its own DIP path.
    f.initialize_mrt(cs);
    auto stats = f.run_mrt(cs);
    auto expected = f.read(*f.a, true), expected_b = f.read(*f.b, true),
         expected_e = f.read(*f.e, true), expected_m = f.read(f.mask, true);
    h.seed(cs);
    api(device->BeginScene());
    for (unsigned i = 0; i < cs.ops.size(); ++i) {
      h.source(cs, i);
      auto done = h.execute(cs, i);
      need(done.image == PassImage::Linear && done.candidate_bound &&
               SUCCEEDED(done.source) && SUCCEEDED(done.composition) &&
               SUCCEEDED(done.restore),
           "pass linear result");
      h.adopt();
    }
    api(device->EndScene());
    f.fence();
    auto color = pass_read(f, h.owner.p),
         native = pass_read(f, h.pass.fixture_native()),
         energy = pass_read(f, h.pass.fixture_energy()),
         mask = pass_read(f, h.pass.coverage_target());
    for (auto pair :
         {std::pair<const std::vector<float> *, const std::vector<float> *>{
              &color, &expected},
          {&native, &expected_b},
          {&energy, &expected_e},
          {&mask, &expected_m}})
      need(std::memcmp(pair.first->data(), pair.second->data(),
                       pair.first->size() * 4) == 0,
           "pass differs from independent qualified fixture");
    comparisons += 4096;
    auto depth = f.depth_probe(true);
    raw.write(reinterpret_cast<const char *>(&cs.h.id), 4);
    for (auto *v : {&color, &depth, &native, &energy, &mask})
      raw.write(reinterpret_cast<const char *>(v->data()), v->size() * 4);
    std::printf("MRT_CASE id=%u sources=%u bursts=%u copy=%u native=%u zero=%u "
                "alpha=%u minuszero=%u capzero=%u infinite=%u fallback=%u "
                "incomplete=%u refused=%u\n",
                cs.h.id, stats.sources, stats.bursts, stats.copies,
                stats.native_channels, stats.zero_channels,
                stats.alpha_channels, stats.negative_zero_channels,
                stats.cap_zero_channels, stats.infinite_channels,
                stats.fallbacks, stats.incomplete, stats.refused);
    std::printf("PASS_CASE id=%u channels=4096 draws=%u allocations=0\n",
                cs.h.id, unsigned(cs.ops.size()));
  }
  std::printf("PASS_CHECKS channels=%u draws=%u\n", comparisons, h.submissions);
  pass_faults(h, cases.at(2));
  need(bool(raw), "pass raw completion");
  h.pass.before_reset();
  need(h.pass.references() == 4 && !h.pass.coverage_target(),
       "pass Reset releases default pool");
  for (unsigned twin = 1; twin <= 2; ++twin) {
    PassHarness limited(f, twin);
    Case cs = cases.at(2);
    cs.ops.resize(1);
    cs.h.count = 1;
    cs.h.alpha = 0;
    limited.seed(cs);
    limited.source(cs, 0);
    api(device->BeginScene());
    auto done = limited.execute(cs, 0);
    need(done.image == PassImage::Linear && done.candidate_bound,
         "limited-cap source support");
    limited.adopt();
    api(device->EndScene());
    limited.pass.detach();
    need(pass_unsupported_calls == 0, "unsupported render state accessed");
    pass_cap_twin = 0;
  }
  std::printf("PASS_CAPS twins=2 forbidden_calls=0\n");
  pass_timings(device, programs, variants, back);
  f.single(f.scene);
  api(device->SetRenderTarget(0, back));
}

void pass_after_reset(IDirect3DDevice9 *device, EmissionPass &retained,
                      const Case &input, IDirect3DSurface9 *back,
                      const char *programs, const char *variants) {
  need(retained.references() == 4,
       "same-instance retained programs after native Reset");
  MrtFixture f(device, 16, 16, false, programs, variants, true);
  PassHarness h(f, 0, &retained);
  Case cs = input;
  cs.ops.resize(1);
  cs.h.count = 1;
  cs.h.flags |= 2;
  f.initialize_mrt(cs);
  f.run_mrt(cs);
  auto expected = f.read(*f.a, true);
  h.seed(cs);
  h.source(cs, 0);
  api(device->BeginScene());
  auto done = h.execute(cs, 0);
  need(done.image == PassImage::Linear && done.candidate_bound,
       "same-instance post-Reset transaction");
  h.adopt();
  api(device->EndScene());
  f.fence();
  auto actual = pass_read(f, h.owner.p);
  need(std::memcmp(actual.data(), expected.data(),
                   actual.size() * sizeof(float)) == 0 &&
           retained.coverage_valid() && h.submissions == 1,
       "post-Reset source/program equality");
  retained.detach();
  need(retained.references() == 0, "post-Reset detach references");
  f.single(f.scene);
  api(device->SetRenderTarget(0, back));
  std::printf("PASS_RESET passed=1 retained_programs=4 recreated_targets=4 "
              "frame_clear=1 transaction=1\n");
}
