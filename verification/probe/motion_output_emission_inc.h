// Focused capture -> MotionOutput -> real HDR owner exchange -> supplemental
// TemporalPass exercise. Original game programs are supplied locally at
// runtime.
void run_emission_integration(Fixture &f, const char *vertex_path,
                              const char *pixel_path) {
  require(f.seam && f.enabled && f.taa && f.hdr_agx && f.emission_status &&
              f.emission_fault && f.emission_readback,
          "emission integration seam");
  // Exact fixture inventory mirrors the reviewed production admission table.
  // Shader objects are created through the capture device, so the actual runtime
  // registration/cache must recognize every native pair without an object scope.
  struct EmissionPair { unsigned vertex, pixel; };
  const unsigned long long vertex_ids[] = {
      0xd5e1c75351ed3f04ull, 0x32e75459998d0388ull, 0x089091aab2d5eb13ull,
      0x5b7a3ccd9e7df00aull, 0x6435a84d8ac5908eull, 0x89193868c61c3846ull,
      0xa520be365951c9dcull, 0xcfb2c31707d545bcull};
  const unsigned long long pixel_ids[] = {
      0x8360f422de08b5bdull, 0x9975b706e5a1c999ull, 0xff2473e73a6bdfa1ull,
      0x8559522220507d5eull, 0x875e780adb131b16ull, 0x39f3b4d5b6a5aaedull,
      0x47e15e20d63b0e93ull, 0x846c5c1a549f9491ull, 0xc6dacb8f74b65c97ull,
      0xf0c91793a75e1203ull};
  const EmissionPair pairs[] = {
      {0,0},{1,1},{1,2},{2,3},{2,4},{3,1},{3,2},{4,5},{4,6},{4,7},
      {4,8},{5,0},{5,9},{6,3},{6,4},{7,5},{7,6},{7,7},{7,8},{0,9}};
  Com<IDirect3DVertexShader9> original_vertices[8];
  Com<IDirect3DPixelShader9> original_pixels[10];
  auto sibling = [](const char *path, const char *kind,
                    unsigned long long hash) {
    const std::string original(path);
    const auto slash = original.find_last_of("/\\");
    char name[40];
    std::snprintf(name, sizeof name, "%s_%016llx.bin", kind, hash);
    return (slash == std::string::npos ? std::string{} :
            original.substr(0, slash + 1)) + name;
  };
  // The benchmark retains its one-pair startup and timed work exactly.
  for (unsigned i = 0; i < (f.emission_bench ? 1u : 8u); ++i) {
    const auto path = i == 0 ? std::string(vertex_path) :
                              sibling(vertex_path, "vs", vertex_ids[i]);
    const auto words = load(path.c_str());
    require(fnv(words.data(), words.size() * 4) == vertex_ids[i],
            "reviewed original emission VS");
    api(f.d->CreateVertexShader(reinterpret_cast<const DWORD *>(words.data()),
                                &original_vertices[i].p),
        "emission original VS");
  }
  for (unsigned i = 0; i < (f.emission_bench ? 1u : 10u); ++i) {
    const auto path = i == 0 ? std::string(pixel_path) :
                              sibling(pixel_path, "ps", pixel_ids[i]);
    const auto words = load(path.c_str());
    require(fnv(words.data(), words.size() * 4) == pixel_ids[i],
            "reviewed original emission PS");
    require(words[0] == (i == 6 || i >= 8 ? 0xffff0201u : 0xffff0200u),
            "untouched emission pixel model");
    api(f.d->CreatePixelShader(reinterpret_cast<const DWORD *>(words.data()),
                               &original_pixels[i].p),
        "emission original PS");
  }
  Com<IDirect3DVertexDeclaration9> declaration;
  Com<IDirect3DVertexBuffer9> vertices;
  Com<IDirect3DIndexBuffer9> indices;
  const D3DVERTEXELEMENT9 elements[] = {
      {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,
       0},
      {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,
       0},
      D3DDECL_END()};
  api(f.d->CreateVertexDeclaration(elements, &declaration.p),
      "emission declaration");
  const float l = -1 - 1.f / f.W, r = 1 - 1.f / f.W, t = 1 + 1.f / f.H,
              b = -1 + 1.f / f.H;
  const float quad[20] = {l, t, .1f, 0, 0, r, t, .1f, 1, 0,
                          l, b, .1f, 0, 1, r, b, .1f, 1, 1};
  api(f.d->CreateVertexBuffer(sizeof quad, 0, 0, D3DPOOL_MANAGED, &vertices.p,
                              nullptr),
      "emission vertices");
  void *data = nullptr;
  api(vertices->Lock(0, 0, &data, 0), "emission vertex lock");
  std::memcpy(data, quad, sizeof quad);
  api(vertices->Unlock(), "emission vertex unlock");
  api(f.d->CreateIndexBuffer(8, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices.p,
                             nullptr),
      "emission indices");
  const unsigned short order[] = {0, 1, 2, 3};
  api(indices->Lock(0, 0, &data, 0), "emission index lock");
  std::memcpy(data, order, sizeof order);
  api(indices->Unlock(), "emission index unlock");
  Com<IDirect3DTexture9> textures[2];
  const float texels[2][4] = {{.5f, .25f, .125f, .125f},
                              {.125f, .375f, .5f, .25f}};
  for (unsigned i = 0; i < 2; ++i) {
    api(f.d->CreateTexture(1, 1, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_MANAGED,
                           &textures[i].p, nullptr),
        "emission source texture");
    D3DLOCKED_RECT lock{};
    api(textures[i]->LockRect(0, &lock, nullptr, 0), "emission texture lock");
    for (unsigned k = 0; k < 4; ++k)
      static_cast<unsigned short *>(lock.pBits)[k] =
          float_to_half(texels[i][k]);
    api(textures[i]->UnlockRect(0), "emission texture unlock");
  }
  // Four distinct RGB texels expose DEFAULT's native UV matrix versus direct
  // INSTANCE UV. Constant exactly representable alpha retains the source-once
  // oracle at every pixel. This resource is never used by the prior 12 frames.
  const float corpus_texels[4][4] = {
      {.5f,.25f,.125f,.125f}, {.125f,.375f,.5f,.125f},
      {.375f,.125f,.25f,.125f}, {.25f,.5f,.375f,.125f}};
  Com<IDirect3DTexture9> corpus_texture;
  if (!f.emission_bench) {
    api(f.d->CreateTexture(2, 2, 1, 0, D3DFMT_A16B16G16R16F,
                           D3DPOOL_MANAGED, &corpus_texture.p, nullptr),
        "emission native UV texture");
    D3DLOCKED_RECT lock{};
    api(corpus_texture->LockRect(0, &lock, nullptr, 0), "emission UV lock");
    for (unsigned y = 0; y < 2; ++y)
      for (unsigned x = 0; x < 2; ++x)
        for (unsigned k = 0; k < 4; ++k)
          reinterpret_cast<unsigned short *>(
              static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch)[x*4+k] =
              float_to_half(corpus_texels[y*2+x][k]);
    api(corpus_texture->UnlockRect(0), "emission UV unlock");
  }
  // Identical texels at both levels exercise real mip bias without changing
  // any material color/alpha input. Managed storage also survives Reset.
  f.textures[0].reset();
  api(f.d->CreateTexture(2, 2, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                         &f.textures[0].p, nullptr),
      "ordinary mip texture");
  {
    const DWORD texel = 0xbf804020u;
    D3DLOCKED_RECT lock{};
    api(f.textures[0]->LockRect(1, &lock, nullptr, 0), "ordinary mip lock");
    std::memcpy(lock.pBits, &texel, sizeof texel);
    api(f.textures[0]->UnlockRect(1), "ordinary mip unlock");
  }
  unsigned submissions = 0, numeric = 0, ordered_overwrites = 0;
  unsigned pixel_checks = 0;
  HRESULT diagnostic_hr = S_OK;
  unsigned diagnostic_linear = 0, diagnostic_native = 0;
  float diagnostic_fade = 0;
  const float *diagnostic_texel = texels[0];
  double maximum = 0;
  Com<IDirect3DQuery9> completion;
  LARGE_INTEGER frequency{};
  if (f.emission_bench) {
    api(f.d->CreateQuery(D3DQUERYTYPE_EVENT, &completion.p),
        "emission completion query");
    require(QueryPerformanceFrequency(&frequency), "emission QPC frequency");
  }
  auto fence = [&]() {
    api(completion->Issue(D3DISSUE_END), "emission EVENT issue");
    HRESULT hr;
    while ((hr = completion->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE)
      Sleep(0);
    api(hr, "emission EVENT completion");
  };
  auto write_raw = [&](const char *label, const std::vector<float> &values) {
    char name[80];
    std::snprintf(name, sizeof name, "emission_%s_%llu.rgba32f", label,
                  f.frame);
    FILE *file = std::fopen(name, "wb");
    require(file != nullptr, "emission readback file");
    require(std::fwrite(values.data(), sizeof(float), values.size(), file) ==
                values.size(),
            "emission readback bytes");
    std::fclose(file);
  };
  // Identical assertion accounting, with successful pixel checks summarized
  // per frame. Retain the first failing actual input/output pair before throw.
  auto pixel_check = [&](bool ok, const char *label, unsigned index,
                         unsigned lane, unsigned source, double wanted,
                         const std::vector<float> &before,
                         const std::vector<float> &after) {
    ++pixel_checks;
    if (ok) {
      ++checks;
      return;
    }
    std::printf("EMISSION_PIXEL_DIFF frame=%llu source=%u x=%u y=%u lane=%u "
                "before=%a actual=%a expected=%a before_alpha=%a "
                "actual_alpha=%a source_alpha=%a label=%s\n",
                f.frame, source, (index / 4) % f.W, (index / 4) / f.W, lane,
                double(before[index + lane]), double(after[index + lane]),
                wanted, double(before[index + 3]), double(after[index + 3]),
                source < 2 ? double(diagnostic_texel[3]) : 0., label);
    std::printf(
        "EMISSION_PIXEL_CONTEXT hr=%08lx linear=%u native=%u fade=%a gain=1\n",
        diagnostic_hr, diagnostic_linear, diagnostic_native,
        double(diagnostic_fade));
    if (source < 2)
      std::printf("EMISSION_SOURCE_RGBA r=%a g=%a b=%a a=%a\n",
                  double(diagnostic_texel[0]), double(diagnostic_texel[1]),
                  double(diagnostic_texel[2]), double(diagnostic_texel[3]));
    write_raw("failure_before", before);
    write_raw("failure_after", after);
    require(false, label);
  };
  auto bias_check = [&](const char *label, DWORD expected) {
    const DWORD actual = f.sampler_bias(0);
    if (actual != expected)
      std::printf(
          "EMISSION_MIP_DIFF frame=%llu label=%s actual=%08lx expected=%08lx\n",
          f.frame, label, actual, expected);
    require(actual == expected, label);
  };
  const unsigned frame_count = f.emission_bench ? 12u :
      12u + 2u * unsigned(sizeof pairs / sizeof pairs[0]);
  for (unsigned frame = 0; frame < frame_count; ++frame) {
    const bool corpus = frame >= 12;
    const unsigned pair_index = corpus ? (frame - 12) / 2 : 0;
    const auto pair = pairs[pair_index];
    const bool instance = pair.vertex >= 3 && pair.vertex <= 6;
    const bool faded = pair.pixel != 3 && pair.pixel != 4;
    const bool affine_source = pair.pixel != 2 && pair.pixel != 4 &&
                               pair.pixel != 5 && pair.pixel != 6;
    const unsigned first_pixel_check = pixel_checks;
    if (frame == 10 && !f.emission_bench)
      f.reset();
    f.frame_begin();
    f.linear_material_inputs();
    api(f.d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT),
        "ordinary A mip filter");
    bias_check("ordinary A begins with restored bias", 0);
    f.write_reserved();
    f.draw(f.a, 0, 0, 0, true, true,
           frame != 0 && (frame != 10 || f.emission_bench), Alter::None, false);
    bias_check("ordinary A routed mip bias", f.float_bits(f.mip_bias));
    unsigned issued = corpus                                    ? unsigned(frame % 2 == 0)
                      : f.emission_bench                          ? 1u
                      : frame == 2                                ? 2u
                      : (frame == 0 || frame == 3 || frame == 11) ? 0u
                                                                  : 1u;
    unsigned fault = f.emission_bench ? 0
                     : frame == 5     ? 3
                     : frame == 6     ? 6
                     : frame == 7     ? 101
                     : frame == 9     ? 7
                                      : 0;
    const bool failed_source = !f.emission_bench && frame == 8;
    const float fade = corpus ? (faded ? .25f : 1.f) :
                       !f.emission_bench && frame == 4 ? 0.f : .5f;
    if (corpus)
      std::printf("EMISSION_CORPUS frame=%u pair=%u vs=%016llx ps=%016llx "
                  "instance=%u affine=%u fade=%u fog=%u disappeared=%u\n",
                  frame, pair_index, vertex_ids[pair.vertex],
                  pixel_ids[pair.pixel], unsigned(instance),
                  unsigned(affine_source), unsigned(faded), unsigned(faded),
                  unsigned(issued == 0));
    unsigned w = f.W, h = f.H;
    auto read_scene = [&]() {
      // Diagnostic resource lifetime can call the hooked device Release and
      // restore borrowed state. Make that boundary explicit using the public
      // logical RT getter, so this oracle does not depend on backend callbacks.
      {
        Com<IDirect3DSurface9> logical;
        api(f.d->GetRenderTarget(0, &logical.p),
            "HDR readback application RT0");
      }
      bias_check("HDR readback begins with restored bias", 0);
      auto image = f.hdr_image(&w, &h);
      bias_check("HDR readback leaves restored bias", 0);
      return image;
    };
    auto before = f.emission_bench ? std::vector<float>{} : read_scene();
    require(w == f.W && h == f.H, "emission A dimensions");
    std::vector<float> expected_mask(
        f.emission_bench ? 0 : std::size_t(f.W) * f.H * 4, 0);
    for (unsigned source = 0; source < issued; ++source) {
      f.scope(nullptr);
      f.scene_states();
      api(f.d->SetVertexShader(original_vertices[pair.vertex].p),
          "emission VS bind");
      api(f.d->SetPixelShader(original_pixels[pair.pixel].p),
          "emission PS bind");
      api(f.d->SetVertexDeclaration(declaration.p),
          "emission declaration bind");
      api(f.d->SetStreamSource(0, vertices.p, 0, 20), "emission stream bind");
      api(f.d->SetStreamSourceFreq(0, 1), "emission stream frequency");
      api(f.d->SetIndices(failed_source ? nullptr : indices.p),
          "emission index bind");
      api(f.d->SetVertexShaderConstantF(0, identity, 4), "emission WVP");
      const float uv[] = {corpus ? -1.f : 1.f, 0, corpus ? 1.f : 0.f, 0,
                          0, corpus ? -1.f : 1.f, corpus ? 1.f : 0.f, 0};
      const float fade_value[] = {corpus ? .5f : fade, 0, 0, 0};
      const BOOL fog = corpus && faded;
      if (!instance)
        api(f.d->SetVertexShaderConstantF(faded ? 10 : 4, uv, 2),
            "emission native DEFAULT UV");
      if (faded) {
        if (corpus) {
          // Native fog distance is exactly two at every vertex: world position
          // (0,0,1), camera (0,0,3). clamp(1.5-.5*2)*.5 = .25.
          const float world[] = {0,0,0,0, 0,0,0,0, 0,0,0,1};
          const float camera[] = {1,0,0,0, 0,1,0,0, 0,0,1,3};
          const float fog_clip[] = {1.5f,.5f,0,0};
          api(f.d->SetVertexShaderConstantF(4, world, 3), "emission world");
          api(f.d->SetVertexShaderConstantF(7, camera, 3), "emission camera");
          api(f.d->SetVertexShaderConstantF(instance ? 11 : 13, fog_clip, 1),
              "emission native fog clip");
        }
        api(f.d->SetVertexShaderConstantF(instance ? 10 : 12, fade_value, 1),
            "emission native fade");
        api(f.d->SetVertexShaderConstantB(0, &fog, 1), "emission native fog");
      }
      const float affine[] = {.75f, .125f,  0,     .03125f, 0,     .5f,
                              .25f, .0625f, .125f, 0,       .875f, -.03125f};
      api(f.d->SetPixelShaderConstantF(0, affine, 3), "emission affine");
      for (unsigned stage = 0; stage < 4; ++stage)
        api(f.d->SetTexture(stage, stage == 0 ?
            (corpus ? corpus_texture.p : textures[source].p) : nullptr),
            "emission texture binding");
      api(f.d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE),
          "original emission mip filter");
      api(f.d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE),
          "emission data sampler");
      api(f.d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE),
          "emission no depth write");
      api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE),
          "emission blending");
      api(f.d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE),
          "emission source one");
      api(f.d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE),
          "emission destination one");
      api(f.d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD), "emission add");
      RECT rect{LONG((source ? 3 : 1) * f.W / 8), LONG(f.H / 4),
                LONG((source ? 7 : 5) * f.W / 8), LONG(3 * f.H / 4)};
      api(f.d->SetScissorRect(&rect), "emission scissor");
      api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE),
          "emission scissor enable");
      if (f.emission_bench) {
        // Source setters, initial motion and frame M clear are outside.
        // This window includes capture/native DIP, copy/E-clear/compose,
        // real Hdr owning exchange, supplemental TAA and AgX publication.
        fence();
        LARGE_INTEGER begin, end;
        QueryPerformanceCounter(&begin);
        api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0, 4, 0, 2),
            "timed original emission DIP");
        ++submissions;
        api(f.d->SetDepthStencilSurface(nullptr),
            "timed terminal depth unbind");
        api(f.d->StretchRect(f.back.p, nullptr, f.bloom_surface.p, nullptr,
                             D3DTEXF_NONE),
            "timed terminal publication");
        api(f.d->EndScene(), "timed EndScene");
        fence();
        QueryPerformanceCounter(&end);
        if (frame >= 4)
          std::printf("EMISSION_TIMING sample=%u enabled=%u width=1920 "
                      "height=1080 completed_ms=%.9f\n",
                      frame - 4, f.emissions_enabled,
                      1000. * double(end.QuadPart - begin.QuadPart) /
                          frequency.QuadPart);
        api(f.d->SetDepthStencilSurface(f.depth.p), "benchmark depth restore");
        api(f.d->Present(nullptr, nullptr, nullptr, nullptr),
            "benchmark Present");
        ++f.frame;
        ++f.frames_since_reset;
        break;
      }
      // snapshot() also gets RT0 (restoring borrowed MRT/mip state), but reads
      // samplers first. Establish the application view before sampling it.
      {
        Com<IDirect3DSurface9> logical;
        api(f.d->GetRenderTarget(0, &logical.p),
            "source snapshot application RT0");
      }
      bias_check("source snapshot has restored ordinary bias", 0);
      const auto state = f.snapshot();
      if (f.emissions_enabled && fault)
        f.emission_fault(f.d.p, fault, 1);
      const unsigned prior = f.emission_status(f.d.p, 5),
                     prior_native = f.emission_status(f.d.p, 6);
      const HRESULT hr =
          f.d->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 0, 4, 0, 2);
      ++submissions;
      ++f.draw_index;
      if (!(failed_source ? FAILED(hr) : SUCCEEDED(hr)))
        std::printf("EMISSION_SOURCE_DIFF frame=%llu source=%u actual_hr=%08lx "
                    "expected_failure=%u\n",
                    f.frame, source, hr, failed_source);
      require(failed_source ? FAILED(hr) : SUCCEEDED(hr),
              "original DIP result retained");
      f.compare(state, f.snapshot(), "emission original source restoration");
      bias_check("emission source leaves restored ordinary bias", 0);
      auto after = read_scene();
      const bool linear = f.emission_status(f.d.p, 5) > prior;
      const bool native = f.emission_status(f.d.p, 6) > prior_native;
      diagnostic_hr = hr;
      diagnostic_linear = linear;
      diagnostic_native = native;
      diagnostic_fade = fade;
      for (UINT y = 0; y < f.H; ++y)
        for (UINT x = 0; x < f.W; ++x) {
          const unsigned tx = (x >= f.W / 2) ^ !instance;
          const unsigned ty = (y >= f.H / 2) ^ !instance;
          const float *texel = corpus ? corpus_texels[ty*2+tx] : texels[source];
          diagnostic_texel = texel;
          double rgb[] = {texel[0], texel[1], texel[2]};
          if (affine_source) {
            rgb[0] = .75 * texel[0] + .125 * texel[1] + .03125;
            rgb[1] = .5 * texel[1] + .25 * texel[2] + .0625;
            rgb[2] = .125 * texel[0] + .875 * texel[2] - .03125;
          }
          const unsigned index = (y * f.W + x) * 4;
          const bool covered = LONG(x) >= rect.left && LONG(x) < rect.right &&
                               LONG(y) >= rect.top && LONG(y) < rect.bottom;
          if (covered && !failed_source) {
            for (unsigned k = 0; k < 3; ++k) {
              double wanted = before[index + k];
              if (linear) {
                const double energy =
                    std::pow(std::max(rgb[k], 0.), 2.2) * fade;
                if (energy > 0)
                  wanted = std::pow(
                      std::pow(std::max(wanted, 0.), 2.2) + energy, 1. / 2.2);
              } else
                wanted += rgb[k] * fade;
              const double fraction = std::fabs(after[index + k] - wanted) /
                                      (.00003 + .003 * std::fabs(wanted));
              maximum = std::max(maximum, fraction);
              pixel_check(
                  fraction <= 1,
                  "integrated original linear/native numerical equation", index,
                  k, source, wanted, before, after);
              ++numeric;
            }
            // Both authored increments keep every covered destination sum
            // exactly representable in FP16; no alpha tolerance/rounding
            // oracle.
            pixel_check(after[index + 3] == before[index + 3] + texel[3],
                        "original source alpha submitted once", index, 3,
                        source, before[index + 3] + texel[3], before, after);
            if (f.emissions_enabled && (linear || native))
              for (unsigned k = 0; k < 3; ++k)
                expected_mask[index + k] += 1;
          } else {
            unsigned lane = 0;
            while (lane < 3 &&
                   !std::memcmp(&after[index + lane], &before[index + lane],
                                sizeof(float)))
              ++lane;
            pixel_check(
                !std::memcmp(&after[index], &before[index], 4 * sizeof(float)),
                "source-excluded actual A remains exact", index, lane, source,
                before[index + lane], before, after);
          }
        }
      before = std::move(after);
    }
    if (f.emission_bench)
      continue;
    // A failed native call rejects the selector for the rest of this frame.
    // The following ordinary draw still submits once, but cannot route/jitter.
    if (failed_source)
      f.scene_rejected = true;
    // Healthy frames route motion/depth again after the emission MRT borrow.
    f.scene_states();
    f.material_state();
    f.linear_material_inputs();
    api(f.d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT),
        "ordinary B mip filter");
    bias_check("ordinary B begins with restored bias", 0);
    // The caller changed the input layout for its original emission source.
    // Ordinary material buffers contain FLOAT16_4 attributes, not that source's
    // FLOAT3/FLOAT2 layout; draw() changes the stream but retains its
    // declaration.
    api(f.d->SetVertexDeclaration(f.declaration.p),
        "restore ordinary material declaration");
    {
      Com<IDirect3DVertexDeclaration9> ordinary;
      api(f.d->GetVertexDeclaration(&ordinary.p),
          "ordinary material declaration guard");
      require(ordinary.p == f.declaration.p,
              "ordinary material uses its exact declaration");
    }
    f.draw(f.b, .75f, 0, 0, true, !failed_source,
           !failed_source && frame != 0 && frame != 9 && frame != 10,
           Alter::None, false);
    bias_check("ordinary B expected mip bias",
               failed_source ? 0 : f.float_bits(f.mip_bias));
    f.emission_reference_color = read_scene();
    unsigned opaque_pixels = 0;
    for (UINT y = 0; y < f.H; ++y)
      for (UINT x = 0; x < f.W; ++x) {
        double ox, oy, wc;
        f.object_point(x - (failed_source ? 0 : f.jx),
                       y - (failed_source ? 0 : f.jy), .75f, 0, ox, oy, wc);
        if (!covers_b(ox, oy) || f.edge_distance(f.b, ox, oy) < .01)
          continue;
        const unsigned index = (y * f.W + x) * 4;
        ++opaque_pixels;
        for (unsigned k = 0; k < 3; ++k) {
          pixel_check(f.emission_reference_color[index + k] == 1.f,
                      "later opaque material overwrites ordered emission",
                      index, k, 2, 1., before, f.emission_reference_color);
        }
        if (std::memcmp(&before[index], &f.emission_reference_color[index],
                        3 * sizeof(float)))
          ++ordered_overwrites;
      }
    require(opaque_pixels > 0, "later ordinary draw interior witness");
    write_raw("color", f.emission_reference_color);
    f.emission_mask_valid = f.emissions_enabled && f.emission_status(f.d.p, 1);
    f.emission_reference_mask.assign(std::size_t(f.W) * f.H * 4, 0);
    if (f.emission_mask_valid) {
      api(f.emission_readback(f.d.p, 3, f.emission_reference_mask.data(),
                              unsigned(f.emission_reference_mask.size()), &w,
                              &h),
          "emission mask readback");
      for (std::size_t i = 0; i < expected_mask.size(); ++i)
        if (i % 4 < 3)
          pixel_check(f.emission_reference_mask[i] == expected_mask[i],
                      "persistent supplemental RGB union", unsigned(i - i % 4),
                      unsigned(i % 4), 2, expected_mask[i], expected_mask,
                      f.emission_reference_mask);
    }
    write_raw("mask", f.emission_reference_mask);
    std::printf("EMISSION_LIVE frame=%llu enabled=%u draws=%u mask_valid=%u "
                "prepared=%u linear=%u native=%u incomplete=%u refused=%u "
                "suppressed=%u exchanged=%u original_calls=%u source_hr=%08x "
                "fault=%u\n",
                f.frame, f.emissions_enabled, issued, f.emission_mask_valid,
                f.emission_status(f.d.p, 4), f.emission_status(f.d.p, 5),
                f.emission_status(f.d.p, 6), f.emission_status(f.d.p, 7),
                f.emission_status(f.d.p, 8), f.emission_status(f.d.p, 9),
                f.emission_status(f.d.p, 10), f.emission_status(f.d.p, 12),
                f.emission_status(f.d.p, 11), fault);
    if (failed_source) {
      // FailedCall is terminal for selection. This is an ordinary copy/flush,
      // not a TAA publication, and must never seed reference history.
      api(f.d->SetDepthStencilSurface(nullptr), "rejected frame depth unbind");
      const auto state = f.snapshot();
      api(f.d->StretchRect(f.back.p, nullptr, f.bloom_surface.p, nullptr,
                           D3DTEXF_NONE),
          "rejected frame original copy");
      f.compare(state, f.snapshot(), "rejected frame original copy state");
      const auto image = f.color_image();
      require(image == f.color_image(f.bloom_surface.p),
              "rejected frame native copy equals unresolved display");
      f.reference.pass.invalidate();
      f.history_dropped = true;
      f.camera_history = {};
      ++taa_skipped_frames;
      api(f.d->EndScene(), "rejected frame EndScene");
      const auto presented = f.color_image();
      require(presented == image,
              "rejected frame EndScene preserves the actual native copy");
      f.write_presented(presented);
      f.previous_presented = presented;
      f.verify_motion(); // A-only RT1/RT2, with B's actual unjittered geometry.
      api(f.d->SetDepthStencilSurface(f.depth.p),
          "rejected frame depth restore");
      api(f.d->Present(nullptr, nullptr, nullptr, nullptr),
          "rejected frame Present");
      ++f.frame;
      ++f.frames_since_reset;
      std::printf("EMISSION_REJECTED frame=%u source_failed=1 original_b=1 "
                  "b_routed=0 b_jittered=0 taa=0 history_seeded=0 copy_exact=1 "
                  "native_hr=%08lx\n",
                  frame, diagnostic_hr);
    } else
      f.frame_end();
    bias_check("terminal publication leaves restored bias", 0);
    std::printf("EMISSION_PIXELS frame=%u checks=%u\n", frame,
                pixel_checks - first_pixel_check);
  }
  api(f.d->SetIndices(nullptr), "emission final indices release");
  api(f.d->SetStreamSource(0, nullptr, 0, 0), "emission final stream release");
  if (f.emission_bench)
    std::printf("EMISSION_BENCH frames=12 samples=8 warmups=4 submissions=%u "
                "coverage=0.25\n",
                submissions);
  else
    std::printf("EMISSION_CHECKS frames=%u submissions=%u numeric=%u "
                "max_fraction=%.9g reset=1 ordered_overwrites=%u\n",
                frame_count, submissions, numeric, maximum, ordered_overwrites);
}
