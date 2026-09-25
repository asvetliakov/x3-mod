// S3 (docs/architecture/taa-high-resolution.md), lattice mode: the 5-tap bilinear Catmull-Rom history, the only form since the
// 16-tap point programs (--taa-history-taps 16) were removed on 2026-09-25.
//   FILTER_PROBE: the backend's LINEAR filter on the two history formats (A16B16G16R16F colour, R32F mask): every texel
//   centre addressed as the resolve does ((i + 0.5) * (1 / W)) returns that texel bit for bit (the thin programs have no
//   rest branch, so a still scene rests on it), and the sub-texel weight error against an exact bilinear (reported).
//   HISTORY_TAPS_NO_FILTER: GetDirect3D refused during initialize (the filter query cannot run): the pass reports
//   bilinear_history_available() false / "adapter_query" and initialize is refused (D3DERR_NOTAVAILABLE: no fallback
//   program set), leaving no program and no device reference. The standalone TemporalPass::query_history_filtering (what
//   MotionOutput asks at attach, before any pass exists) refuses the same way and holds no reference; on the real table it
//   passes (both formats filter on this backend).
//   HISTORY_TAPS_RESET: a 5-tap pass across a device Reset resumes from an empty history, byte-identical to a fresh pass.
// Hostile-state restoration of the 5-tap draw (its LINEAR s11 / s12 bindings included: Snapshot compares all 16 samplers)
// is covered by every base-mode case, which runs the default 5-tap programs.
namespace history_taps {
// Refuses IDirect3DDevice9::GetDirect3D (slot 6) while alive.
struct FactoryRefusal {
    using Get=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3D9**);
    static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9*,IDirect3D9** out){++refused;if(out)*out=nullptr;return D3DERR_NOTAVAILABLE;}
    explicit FactoryRefusal(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);auto fn=&hook;std::memcpy(&table[6],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~FactoryRefusal(){*reinterpret_cast<void***>(device)=previous;}
};
// Unbinds every texture and secondary target and puts the back buffer back as RT0 (as Fixture's destructor does), so no
// default-pool object of a case stays referenced by the device (a Reset needs them all released).
void release_bindings(IDirect3DDevice9* d){for(UINT n=0;n<20;++n)d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr);for(UINT i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
    d->SetDepthStencilSurface(nullptr);Com<IDirect3DSurface9> back;check("taps back buffer",d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p));check("taps RT0 back buffer",d->SetRenderTarget(0,back.p));d->SetPixelShader(nullptr);}
double fp16_ulp(double v){return std::ldexp(1.,std::ilogb(std::max(std::fabs(v),6.103515625e-05))-10);}
void filter_probe(IDirect3DDevice9* d,Compiler compiler){
    Com<ID3DXBuffer> code;compile(compiler,"sampler2D t:register(s0);float4 c:register(c0);float4 main(float2 vpos:VPOS):COLOR0{"
        "float i=floor(vpos.x);float2 uv=c.w>0.5?float2((0.5+i*c.y)*c.x,0.5):float2((i+0.5)*c.x,0.5);return tex2Dlod(t,float4(uv,0,0));}","ps_3_0",&code.p);
    Com<IDirect3DPixelShader9> ps;check("filter probe PS",d->CreatePixelShader(static_cast<DWORD*>(code->GetBufferPointer()),&ps.p));
    // (width, mode): centres of every texel at the fixture size, 1280 and 5120; the fraction ramp f = k / 1024 between texels 0 and 1.
    for(D3DFORMAT format:{D3DFMT_A16B16G16R16F,D3DFMT_R32F})for(UINT width:{32u,1280u,5120u,2u}){const bool ramp=width==2;const UINT out=ramp?1024:width;
        Com<IDirect3DTexture9> source,target;Com<IDirect3DSurface9> targetSurface,readback;
        check("filter probe source",d->CreateTexture(width,1,1,0,format,D3DPOOL_MANAGED,&source.p,nullptr));
        std::vector<float> value(width);for(UINT i=0;i<width;++i)value[i]=ramp?float(i):.25f+float((i*37)%64)/64.f; // exact in FP16; neighbours differ
        {D3DLOCKED_RECT lock{};check("filter probe lock",source->LockRect(0,&lock,nullptr,0));for(UINT i=0;i<width;++i){if(format==D3DFMT_R32F)std::memcpy(static_cast<char*>(lock.pBits)+i*4,&value[i],4);
            else{const unsigned short h=toHalf(value[i]),v[4]={h,h,h,toHalf(1)};std::memcpy(static_cast<char*>(lock.pBits)+i*8,v,8);}}check("filter probe unlock",source->UnlockRect(0));}
        check("filter probe target",d->CreateTexture(out,1,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&target.p,nullptr));check("filter probe target surface",target->GetSurfaceLevel(0,&targetSurface.p));
        check("filter probe readback",d->CreateOffscreenPlainSurface(out,1,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));
        check("filter probe RT",d->SetRenderTarget(0,targetSurface.p));check("filter probe DS",d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 vp{0,0,out,1,0,1};check("filter probe VP",d->SetViewport(&vp));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})check("filter probe RS",d->SetRenderState(state,FALSE));
        check("filter probe cull",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check("filter probe mask",d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})check("filter probe sampler",d->SetSamplerState(0,p.first,p.second));
        check("filter probe texture",d->SetTexture(0,source.p));check("filter probe bind",d->SetPixelShader(ps.p));check("filter probe VS",d->SetVertexShader(nullptr));check("filter probe FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        const float constants[4]={1.f/float(width),1.f/1024.f,0,ramp?1.f:0.f};check("filter probe constants",d->SetPixelShaderConstantF(0,constants,1));
        struct V{float x,y,z,rhw,u,v;};const V quad[]={{-.5f,-.5f,.5f,1,0,0},{float(out)-.5f,-.5f,.5f,1,1,0},{-.5f,.5f,.5f,1,0,1},{float(out)-.5f,.5f,.5f,1,1,1}};
        check("filter probe Begin",d->BeginScene());check("filter probe draw",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));check("filter probe End",d->EndScene());
        check("filter probe unbind",d->SetTexture(0,nullptr));check("filter probe read",d->GetRenderTargetData(targetSurface.p,readback.p));
        D3DLOCKED_RECT lock{};check("filter probe read lock",readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> result(out);std::memcpy(result.data(),lock.pBits,out*4);check("filter probe read unlock",readback->UnlockRect());
        const char* name=format==D3DFMT_R32F?"r32f":"fp16";
        if(ramp){double worst=0,sum=0;for(UINT k=0;k<out;++k){const double e=std::fabs(double(result[k])-k/1024.);worst=std::max(worst,e);sum+=e;}
            std::printf("FILTER_PROBE format=%s mode=fraction samples=%u max_error=%.8f mean_error=%.8f half_way=%.6f\n",name,out,worst,sum/out,result[512]);
            ++numeric_checks;require(worst<=1./128&&std::fabs(result[512]-.5)<=1./128,"LINEAR filter interpolates between texels (not point, not beyond 1/128)");}
        else{unsigned mismatch=0;double worst=0;for(UINT i=0;i<out;++i){if(result[i]!=value[i])++mismatch;worst=std::max(worst,std::fabs(double(result[i])-value[i]));}
            std::printf("FILTER_PROBE format=%s mode=centre width=%u mismatched=%u max_error=%.8f\n",name,width,mismatch,worst);
            ++numeric_checks;require(mismatch==0,"LINEAR filter at every texel centre addressed as the resolve does returns that texel bit for bit");}
    }
}
struct TapsRun { std::vector<std::vector<float>> current,output; std::vector<unsigned> drawn; bool bilinear=false; const char* reason=""; float maskMax[4]{}; }; // maskMax: per channel over frames (far programs)
// One sequence: farProgram 0 the plain resolve, 2 the far program (thin region 0.97 without the camera gate, W_FAR 0.985).
TapsRun taps_sequence(EdgeScene& s,const DWORD* resolver,double vx,double vy,unsigned frames,unsigned farProgram,unsigned firstFrame=0,TemporalPass* external=nullptr){
    constexpr UINT S=EdgeScene::S,P=EdgeScene::P;TemporalPass local;TemporalPass& pass=external?*external:local;TapsRun run;
    if(!external){check("taps initialize",pass.initialize(s.d,nullptr,resolver));
        if(farProgram){check("taps configure far",pass.configure_far());require(pass.far_available(),"taps: far programs available");}}
    run.bilinear=pass.bilinear_history_available();run.reason=pass.bilinear_history_reason();
    for(unsigned f=0;f<frames;++f){const unsigned n=firstFrame+f,index=n%P+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        std::vector<EdgeObject> scene;for(unsigned i=0;i<4;++i)scene.push_back({4.31+4*i+vx*n,3.27+vy*n,5.31+4*i+vx*n,15.27+vy*n,1,.5f,vx,vy});
        scene.push_back({21.13+vx*n,3.41+vy*n,28.13+vx*n,14.41+vy*n,.6f,.5f,vx,vy});
        s.render(scene,EdgeBackground{.25f,.9f,1},jx,jy);run.current.push_back(s.read(s.color.p));
        FrameInputs in;in.color=s.color.p;in.current_depth=s.depth32.p;in.motion=s.motion.p;in.width=S;in.height=S;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=.9f;in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;
        in.sentinel_camera=true;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        if(farProgram){in.far_weight=.985f;in.far_d0=.9995f;in.far_inv=1.f/(.9999f-.9995f);in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
            in.thin_region_weight=.97f;in.thin_region_relax=1;}
        Output out;check("taps Begin resolve",s.d->BeginScene());check("taps run",pass.run(in,&out));check("taps End resolve",s.d->EndScene());
        require(out.color&&out.used_history==(f>0),"taps: history follows the sequence");
        run.output.push_back(s.read(out.color));run.drawn.push_back(pass.diagnostics().history_taps);
        if(out.stabiliser_mask){const auto mask=s.read(out.stabiliser_mask);for(UINT i=0;i<S*S;++i)for(UINT ch=0;ch<4;++ch)run.maskMax[ch]=std::max(run.maskMax[ch],mask[i*4+ch]);}}
    return run;}
void history_taps_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver,D3DPRESENT_PARAMETERS& pp){
    std::puts("HISTORY_TAPS_CASES");
    filter_probe(d,compiler);release_bindings(d);
    // Without the filter query the pass is refused (the 5-tap resolve would sample LINEAR undefined; no 16-tap fallback set).
    {auto device_refs=[d]{d->AddRef();return unsigned(d->Release());};const unsigned refs_before=device_refs();
        TemporalPass pass;HRESULT refused=S_OK;{FactoryRefusal refusal(d);refused=pass.initialize(d,nullptr,resolver);}
        const unsigned held=device_refs()-refs_before; // device references the refused pass kept
        std::printf("HISTORY_TAPS_NO_FILTER bilinear=%u reason=%s initialize=%08lx references=%u\n",unsigned(pass.bilinear_history_available()),pass.bilinear_history_reason(),(unsigned long)refused,held);
        ++numeric_checks;require(refused==D3DERR_NOTAVAILABLE&&FactoryRefusal::refused>0&&!pass.bilinear_history_available()&&std::strcmp(pass.bilinear_history_reason(),"adapter_query")==0&&held==0,
                                 "history taps: without the filter query initialize is refused (no fallback program set) and holds no device reference");}
    {auto device_refs=[d]{d->AddRef();return unsigned(d->Release());};const unsigned refs_before=device_refs();
        const char* refused_reason="unset";HRESULT refused=S_OK;{FactoryRefusal refusal(d);refused=TemporalPass::query_history_filtering(d,nullptr,&refused_reason);}
        const char* passed_reason="unset";const HRESULT passed=TemporalPass::query_history_filtering(d,nullptr,&passed_reason);
        const unsigned held=device_refs()-refs_before;
        std::printf("HISTORY_FILTER_QUERY refused=%08lx refused_reason=%s passed=%08lx passed_reason=%s references=%u\n",(unsigned long)refused,refused_reason,(unsigned long)passed,passed_reason,held);
        ++numeric_checks;require(FAILED(refused)&&std::strcmp(refused_reason,"adapter_query")==0&&passed==S_OK&&std::strcmp(passed_reason,"ok")==0&&held==0,
                                 "history taps: the standalone filter query refuses without the adapter query, passes on this backend and holds no reference");}
    release_bindings(d);
    // Reset: two frames, before_reset / Reset / after_reset, two more frames from an empty history = a fresh pass on those frames.
    {TemporalPass pass;check("taps Reset initialize",pass.initialize(d,nullptr,resolver));check("taps Reset configure far",pass.configure_far());
        size_t beforeFrames=0;{EdgeScene before(d,compiler);beforeFrames=taps_sequence(before,resolver,.30,.20,2,2,0,&pass).output.size();}release_bindings(d);
        pass.before_reset();check("taps Reset",d->Reset(&pp));pass.after_reset(S_OK);
        TapsRun resumed,fresh;{EdgeScene after(d,compiler);resumed=taps_sequence(after,resolver,.30,.20,2,2,2,&pass);fresh=taps_sequence(after,resolver,.30,.20,2,2,2);}release_bindings(d);
        const bool drawn=std::all_of(resumed.drawn.begin(),resumed.drawn.end(),[](unsigned t){return t==5;});
        std::printf("HISTORY_TAPS_RESET before_frames=%zu drawn_5=%u identical_to_fresh=%u\n",beforeFrames,unsigned(drawn),unsigned(same_rgb(resumed.output,fresh.output)));
        ++numeric_checks;require(drawn&&same_rgb(resumed.output,fresh.output),"history taps: a 5-tap pass across a device Reset resumes from an empty history, byte-identical to a fresh pass");}
}
} // namespace history_taps
