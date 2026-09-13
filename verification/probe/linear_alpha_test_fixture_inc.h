// Selected detached cutout qualification. Fixture only: no runtime admission.
// Native oC0 alpha and the actual fixed-function comparison own coverage.
struct AlphaCutout {
  Gpu gpu;
  IDirect3DDevice9* d;
  Com<IDirect3DTexture9> alpha_texture[2][2]; // float/UNORM, diffuse/lightmap
  Com<IDirect3DSurface9> ds;
  Com<IDirect3DVertexShader9> probe_vs;
  Com<IDirect3DPixelShader9> probe_ps;
  unsigned checks=0;
  explicit AlphaCutout(IDirect3DDevice9* device, Shaders& shaders)
      : gpu(device,shaders,16),d(device) {
    const DWORD required=D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING |
      D3DPMISCCAPS_INDEPENDENTWRITEMASKS | D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS;
    require((shaders.caps.PrimitiveMiscCaps & required)==required &&
      (shaders.caps.AlphaCmpCaps & D3DPCMPCAPS_GREATEREQUAL),"cutout MRT/alpha caps");
    Com<IDirect3D9> factory; api(d->GetDirect3D(&factory.p));
    D3DDISPLAYMODE display{}; api(factory->GetAdapterDisplayMode(0,&display));
    for (auto format : {D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F,D3DFMT_R32F})
      api(factory->CheckDeviceFormat(0,D3DDEVTYPE_HAL,display.Format,
        D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
        D3DRTYPE_TEXTURE,format));
    api(d->CreateDepthStencilSurface(16,16,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,
                                    TRUE,&ds.p,nullptr));
    for (unsigned format=0;format<2;++format) for (auto& t:alpha_texture[format])
      api(d->CreateTexture(16,16,2,0,format ? D3DFMT_A8R8G8B8 : D3DFMT_A32B32G32R32F,
                          D3DPOOL_MANAGED,&t.p,nullptr));
    // Fullscreen diagnostic probes use only public shader/DS APIs. FLOAT3
    // declaration supplies position W=1; no game shader bytes are synthesized.
    const DWORD vs[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,
      0x0200001f,0x80000000,0xe00f0000,
      0x02000001,0xe00f0000,0x90e40000,0x0000ffff};
    const DWORD ps[]={0xffff0300,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
    api(d->CreateVertexShader(vs,&probe_vs.p)); api(d->CreatePixelShader(ps,&probe_ps.p));
  }
  ~AlphaCutout() { d->SetDepthStencilSurface(nullptr); }
  void check(bool ok,const char* name) { ++checks; require(ok,name); }
  static bool same(const Pixel& a,const Pixel& b,unsigned lanes=4) {
    return std::memcmp(a.f,b.f,lanes*4)==0;
  }
  void setup(const Case& c,unsigned mode,bool raw=false) {
    Case render=c; if (raw) render.fp16=0;
    gpu.state(render,mode);
    // Scoped binary fields: f47 AlphaValue, f48 UV jitter in pixels,
    // f49 texture/filter variant (0 float point, 1 UNORM point, 2 UNORM linear),
    // f50 selected mip (0/1). Production and other fixture modes are unchanged.
    for (unsigned sampler=0;sampler<7;++sampler)
      api(d->SetSamplerState(sampler,D3DSAMP_MAXMIPLEVEL,0));
    const unsigned format=c.f[49]==0 ? 0 : 1;
    const float threshold=1.f/255.f;
    const float pattern[8]={0,std::nextafter(threshold,0.f),threshold,
      std::nextafter(threshold,1.f),1.f/512.f,1.f/128.f,.5f,1};
    const unsigned codes[8]={0,1,2,0,3,127,255,0};
    for (unsigned which=0;which<2;++which) {
      for (unsigned level=0;level<2;++level) {
        D3DLOCKED_RECT lock{}; api(alpha_texture[format][which]->LockRect(level,&lock,nullptr,0));
        const unsigned size=16u>>level;
        for (unsigned y=0;y<size;++y) for (unsigned x=0;x<size;++x) {
          const unsigned index=(x+(which ? 3 : 0)+level)%8;
          const float* rgb=c.f+(which ? 7 : 3);
          auto* dest=static_cast<char*>(lock.pBits)+y*lock.Pitch+x*(format ? 4 : 16);
          if (format) {
            const DWORD value=D3DCOLOR_ARGB(codes[index],unsigned(std::lround(rgb[0]*255)),
                unsigned(std::lround(rgb[1]*255)),unsigned(std::lround(rgb[2]*255)));
            std::memcpy(dest,&value,4);
          } else {
            const float value[4]={rgb[0],rgb[1],rgb[2],pattern[index]};
            std::memcpy(dest,value,16);
          }
        }
        api(alpha_texture[format][which]->UnlockRect(level));
      }
      const unsigned sampler=which ? (c.pair==21 ? 3 : 2) : 0;
      api(d->SetTexture(sampler,alpha_texture[format][which].p));
      for (auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})
        api(d->SetSamplerState(sampler,s,c.f[49]==2 ? D3DTEXF_LINEAR : D3DTEXF_POINT));
      api(d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_POINT));
      api(d->SetSamplerState(sampler,D3DSAMP_MAXMIPLEVEL,DWORD(c.f[50])));
    }
    const float alpha[4]={c.f[47],0,0,0}; api(d->SetVertexShaderConstantF(39,alpha,1));
    // The existing oversized triangle covers only half its UV domain. Scale
    // to one screen-wide texture and offset to exact texel centers for point.
    const float uv[8]={2,0,(.5f+c.f[48])/16,0,0,2,.5f/16,0};
    api(d->SetVertexShaderConstantF(37,uv,2));
    api(d->SetRenderState(D3DRS_ALPHATESTENABLE,!raw));
    api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL));
    api(d->SetRenderState(D3DRS_ALPHAREF,1));
    api(d->SetRenderState(D3DRS_COLORWRITEENABLE,raw ? 15 : 7));
  }
  std::vector<Pixel> color(unsigned fp16) {
    return gpu.read(gpu.color[fp16].p,fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A32B32G32R32F);
  }
  std::vector<bool> probe(float z,bool stencil,unsigned reference) {
    api(d->SetRenderTarget(2,nullptr)); api(d->SetRenderTarget(1,nullptr));
    api(d->SetRenderTarget(0,gpu.color[0].p));
    api(d->SetVertexShader(probe_vs.p)); api(d->SetPixelShader(probe_ps.p));
    const float white[4]={1,1,1,1}; api(d->SetPixelShaderConstantF(0,white,1));
    api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    api(d->SetRenderState(D3DRS_ZENABLE,!stencil));
    api(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    api(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));
    api(d->SetRenderState(D3DRS_STENCILENABLE,stencil));
    api(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));
    api(d->SetRenderState(D3DRS_STENCILREF,reference));
    api(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));
    api(d->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));
    api(d->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));
    api(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
    const float vertices[3][14]={{-1,1,z},{3,1,z},{-1,-3,z}};
    api(d->BeginScene()); api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,56)); api(d->EndScene());
    const auto pixels=color(0); std::vector<bool> result;
    for (const auto& p:pixels) {
      check(p.f[0]==0 || p.f[0]==1,"binary hardware depth/stencil probe");
      result.push_back(p.f[0]==1);
    }
    return result;
  }
  void pixel_twin(unsigned mode,bool writes,const Pixel& after,const Pixel& native,
                  const Pixel& poison,const Pixel& full_color,const Pixel& motion,
                  const Pixel& wanted_motion,const Pixel& depth,const Pixel& wanted_depth) {
    check(std::memcmp(after.f+3,poison.f+3,4)==0,"retained RT0 alpha mask7");
    if (mode<2) check(same(after,native),"native versus motion cutout RGB and coverage");
    if (!writes) check(same(after,poison),"rejected color unchanged");
    else for (unsigned k=0;k<3;++k)
      check(after.f[k]==full_color.f[k],"passing color equals own unmasked shader");
    check(same(motion,wanted_motion),"motion ownership/rejection");
    check(same(depth,wanted_depth,1),"depth MRT ownership/rejection");
  }
  void test(const Case& c) {
    check((c.pair==0 || c.pair==21) && c.fp16==1 && c.depth<2 && c.reverse<2 &&
          c.flags<=2 && c.f[47]>=0 && c.f[47]<=1 && c.f[49]>=0 && c.f[49]<=2 &&
          c.f[50]>=0 && c.f[50]<=1,"owned cutout case");
    std::vector<Pixel> raw[3],full_color[3],full_motion[3],full_depth[3];
    for (unsigned mode=0;mode<3;++mode) {
      setup(c,mode,true); gpu.draw(c); raw[mode]=color(0);
      setup(c,mode); api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
      gpu.draw(c); full_color[mode]=color(1);
      if (mode) {
        full_motion[mode]=gpu.read(gpu.motion.p,D3DFMT_A32B32G32R32F);
        if (c.depth) full_depth[mode]=gpu.read(gpu.current.p,D3DFMT_R32F);
      }
    }
    for (unsigned i=0;i<256;++i) {
      check(std::isfinite(raw[0][i].f[3]),"finite native pre-storage alpha");
      for (unsigned mode=1;mode<3;++mode) {
        check(std::memcmp(raw[0][i].f+3,raw[mode][i].f+3,4)==0,"exact FP32 native shader alpha");
        if (mode==1) check(same(raw[0][i],raw[1][i]),"exact original plus motion RGBA");
      }
      check(same(full_motion[1][i],full_motion[2][i]),"unmasked motion twin");
      check(std::fabs(full_motion[1][i].f[0]-(float(i%16)/16-.046875f))<1e-6f &&
        std::fabs(full_motion[1][i].f[1]-(float(i/16)/16+.0546875f))<1e-6f && full_motion[1][i].f[2]==.5f &&
        full_motion[1][i].f[3]==1,"independent full motion reference");
      if (c.depth) check(same(full_depth[1][i],full_depth[2][i],1) &&
                         full_depth[1][i].f[0]==.5f,"independent current depth reference");
    }
    // Retain the first ordered row as raw alpha bits, independent of FP16 RT0
    // storage and framebuffer alpha masking. No CPU comparison substitutes for
    // the driver's native GREATEREQUAL/ref1 coverage below.
    std::printf("CUTOUT_ALPHA id=%u bits=",c.id);
    for (unsigned x=0;x<16;++x) {
      unsigned bits; std::memcpy(&bits,raw[0][x].f+3,4);
      std::printf("%s%08x",x ? "," : "",bits);
    }
    std::printf("\nCUTOUT_RGB id=%u rgb=%.9g,%.9g,%.9g\n",c.id,
      raw[2][0].f[0],raw[2][0].f[1],raw[2][0].f[2]);
    std::vector<bool> coverage;
    for (unsigned scene=0;scene<6;++scene) {
      const bool reject_depth=scene&1,stencil=scene&2;
      const float initial_depth=reject_depth ? .25f : scene>=4 ? .75f : 1.f;
      std::vector<Pixel> native;
      for (unsigned mode=0;mode<3;++mode) {
        setup(c,mode);
        const auto depth_state=[&]() {
        api(d->SetDepthStencilSurface(ds.p));
        api(d->SetRenderState(D3DRS_ZENABLE,TRUE));
        api(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
        api(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        api(d->SetRenderState(D3DRS_STENCILENABLE,stencil));
        api(d->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE,FALSE));
        api(d->SetRenderState(D3DRS_STENCILMASK,255));
        api(d->SetRenderState(D3DRS_STENCILWRITEMASK,255));
        api(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));
        api(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_INCRSAT));
        api(d->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_DECRSAT));
        };
        depth_state();
        api(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,scene>=4 ? 1.f : initial_depth,5));
        for (auto* target:{gpu.color[1].p,gpu.motion.p,gpu.current.p})
          api(d->ColorFill(target,nullptr,D3DCOLOR_ARGB(173,241,19,211)));
        if (scene>=4) {
          // Actual opaque writer behind/in front of the cutout, with a different
          // previous transform. Holes must retain this writer's correspondence.
          api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
          const float z[4]={0,0,0,initial_depth},previous_x[4]={1,0,0,-.25f};
          api(d->SetVertexShaderConstantF(26,z,1));
          api(d->SetVertexShaderConstantF(254,z,1));
          api(d->SetVertexShaderConstantF(252,previous_x,1));
          gpu.draw(c);
          if (mode) {
            const auto background=gpu.read(gpu.motion.p,D3DFMT_A32B32G32R32F),
                       background_depth=gpu.read(gpu.current.p,D3DFMT_R32F);
            for (unsigned i=0;i<256;++i) {
              check(std::fabs(background[i].f[0]-(float(i%16)/16-.109375f))<1e-6f &&
                std::fabs(background[i].f[1]-(float(i/16)/16+.0546875f))<1e-6f &&
                background[i].f[2]==initial_depth && background[i].f[3]==1,
                "independent opaque background correspondence");
              if (c.depth) check(background_depth[i].f[0]==initial_depth,"opaque background current depth");
            }
          }
          setup(c,mode); depth_state();
        }
        const auto poison=color(1),poison_motion=gpu.read(gpu.motion.p,D3DFMT_A32B32G32R32F),
                   poison_depth=gpu.read(gpu.current.p,D3DFMT_R32F);
        gpu.draw(c);
        const auto after=color(1),motion=gpu.read(gpu.motion.p,D3DFMT_A32B32G32R32F),
                   depth=gpu.read(gpu.current.p,D3DFMT_R32F);
        if (!mode) native=after;
        if (!scene && !mode) {
          for (unsigned i=0;i<256;++i) coverage.push_back(!same(after[i],poison[i],3));
          unsigned passed=0,edges=0;
          for (unsigned y=0;y<16;++y) for (unsigned x=0;x<16;++x) {
            passed+=coverage[y*16+x];
            if (x) edges+=coverage[y*16+x]!=coverage[y*16+x-1];
          }
          check(c.f[47]==0 ? passed==0 : passed>0 && passed<256 && edges>0,
                "nonvacuous neighboring native cutout coverage");
          std::printf("CUTOUT_COVERAGE id=%u passed=%u rejected=%u edges=%u row=",c.id,passed,256-passed,edges);
          for (unsigned x=0;x<16;++x) std::printf("%u",unsigned(coverage[x]));
          std::printf("\n");
        }
        for (unsigned i=0;i<256;++i) {
          const bool writes=coverage[i] && !reject_depth;
          pixel_twin(mode,writes,after[i],native[i],poison[i],full_color[mode][i],motion[i],
            mode && writes ? full_motion[mode][i] : poison_motion[i],depth[i],
            mode && c.depth && writes ? full_depth[mode][i] : poison_depth[i]);
        }
        const auto changed=probe(.5f,false,0),unchanged=probe(initial_depth,false,0),
          stencil_unchanged=probe(0,true,5),stencil_changed=probe(0,true,reject_depth ? 4 : 6);
        for (unsigned i=0;i<256;++i) {
          const bool writes=coverage[i] && !reject_depth;
          check(changed[i]==writes && unchanged[i]==!writes,"hardware depth exact coverage");
          check(stencil_unchanged[i]==!(stencil && coverage[i]) &&
                stencil_changed[i]==(stencil && coverage[i]),"hardware stencil exact coverage");
        }
        std::printf("CUTOUT_TWIN id=%u scene=%u mode=%u pixels=256 bad=0\n",c.id,scene,mode);
      }
    }
  }
};
void alpha_test_cutout_fixture(IDirect3DDevice9* d,Shaders& shaders,const std::vector<Case>& cases) {
  AlphaCutout fixture(d,shaders);
  for (const auto& c:cases) fixture.test(c);
  std::printf("CUTOUT_RESULT cases=%zu scenes=%zu twins=%zu checks=%u\n",
    cases.size(),cases.size()*6,cases.size()*18,fixture.checks);
}
