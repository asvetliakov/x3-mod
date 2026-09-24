// S3 (docs/architecture/taa-high-resolution.md), lattice mode: the 5-tap bilinear Catmull-Rom history of the default
// programs against the 16-tap point twins (TemporalPass::configure_history_taps(16), --taa-history-taps 16).
//   FILTER_PROBE: the backend's LINEAR filter on the two history formats (A16B16G16R16F colour, R32F mask): every texel
//   centre addressed as the resolve does ((i + 0.5) * (1 / W)) returns that texel bit for bit (the thin programs have no
//   rest branch, so a still scene rests on it), and the sub-texel weight error against an exact bilinear (reported).
//   HISTORY_TAPS: a 1-px lattice plus a flat block, at rest and drifting (0.30, 0.20) px/frame (fractional on both axes,
//   so the dropped corners carry weight), through the plain and the flown far-camera program: at rest the two forms
//   agree within one FP16 ulp; under drift the plain program's per-pixel difference stays inside the 3x3 clip's reach
//   (|5-tap - 16-tap| <= w (max - min) of the current 3x3, plus one FP16 ulp); the 16-tap run is byte-identical to the
//   16-tap words bound as the caller's resolve; Diagnostics::history_taps names the program drawn.
//   HISTORY_TAPS_FALLBACK: GetDirect3D refused during initialize (the filter query cannot run): the pass reports
//   bilinear_history_available() false / "adapter_query" and every run draws the 16-tap programs, byte-identical to 16.
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
// One sequence: farProgram 0 the plain resolve, 2 the flown far-camera program (thin region 0.97, camera gate, W_FAR 0.985).
TapsRun taps_sequence(EdgeScene& s,const DWORD* resolver,double vx,double vy,unsigned frames,unsigned taps,unsigned farProgram,bool refuseFactory=false,unsigned firstFrame=0,TemporalPass* external=nullptr){
    constexpr UINT S=EdgeScene::S,P=EdgeScene::P;TemporalPass local;TemporalPass& pass=external?*external:local;TapsRun run;
    if(!external){if(refuseFactory){FactoryRefusal refusal(s.d);check("taps initialize (factory refused)",pass.initialize(s.d,nullptr,resolver));require(FactoryRefusal::refused>0,"taps: the filter query reached GetDirect3D");}
        else check("taps initialize",pass.initialize(s.d,nullptr,resolver));
        check("taps configure",pass.configure_history_taps(taps));
        if(farProgram){check("taps configure far",pass.configure_far());require(pass.camera_gate_available(),"taps: far-camera programs available");}}
    run.bilinear=pass.bilinear_history_available();run.reason=pass.bilinear_history_reason();
    for(unsigned f=0;f<frames;++f){const unsigned n=firstFrame+f,index=n%P+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        std::vector<EdgeObject> scene;for(unsigned i=0;i<4;++i)scene.push_back({4.31+4*i+vx*n,3.27+vy*n,5.31+4*i+vx*n,15.27+vy*n,1,.5f,vx,vy});
        scene.push_back({21.13+vx*n,3.41+vy*n,28.13+vx*n,14.41+vy*n,.6f,.5f,vx,vy});
        s.render(scene,EdgeBackground{.25f,.9f,1},jx,jy);run.current.push_back(s.read(s.color.p));
        FrameInputs in;in.color=s.color.p;in.current_depth=s.depth32.p;in.motion=s.motion.p;in.width=S;in.height=S;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=.9f;in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;
        in.sentinel_camera=true;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        if(farProgram){in.far_weight=.985f;in.far_d0=.9995f;in.far_inv=1.f/(.9999f-.9995f);in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
            in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;}
        Output out;check("taps Begin resolve",s.d->BeginScene());check("taps run",pass.run(in,&out));check("taps End resolve",s.d->EndScene());
        require(out.color&&out.used_history==(f>0),"taps: history follows the sequence");
        run.output.push_back(s.read(out.color));run.drawn.push_back(pass.diagnostics().history_taps);
        if(out.stabiliser_mask){const auto mask=s.read(out.stabiliser_mask);for(UINT i=0;i<S*S;++i)for(UINT ch=0;ch<4;++ch)run.maskMax[ch]=std::max(run.maskMax[ch],mask[i*4+ch]);}}
    return run;}
struct Compare { unsigned differing=0; double maxAbs=0,maxUlps=0,maxBoundRatio=0; unsigned boundExceeded=0; };
// Per pixel and channel over frames 1..; the bound (plain program): |a - b| <= w (max - min) of the current 3x3 + one FP16 ulp.
Compare compare(const TapsRun& a,const TapsRun& b){constexpr UINT S=EdgeScene::S;Compare c;
    for(size_t n=1;n<a.output.size();++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x)for(UINT ch=0;ch<3;++ch){
        const double va=px(a.output[n],x,y,ch),vb=px(b.output[n],x,y,ch),diff=std::fabs(va-vb);if(diff>0)++c.differing;
        c.maxAbs=std::max(c.maxAbs,diff);c.maxUlps=std::max(c.maxUlps,diff/fp16_ulp(std::max(std::fabs(va),std::fabs(vb))));
        double lo=1e30,hi=-1e30;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const double q=px(a.current[n],x+dx,y+dy,ch);lo=std::min(lo,q);hi=std::max(hi,q);}
        const double bound=.9*(hi-lo)+fp16_ulp(std::max(std::fabs(va),std::fabs(vb)));if(diff>bound)++c.boundExceeded;if(diff>0)c.maxBoundRatio=std::max(c.maxBoundRatio,diff/bound);}
    return c;}
void history_taps_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver,D3DPRESENT_PARAMETERS& pp){
    std::puts("HISTORY_TAPS_CASES");namespace r=x3m::renderer;
    filter_probe(d,compiler);release_bindings(d);
    const DWORD* taps16=reinterpret_cast<const DWORD*>(r::temporal_resolve_taps16_program());
    {TemporalPass pass;check("taps validation initialize",pass.initialize(d,nullptr,resolver));
        bool refused=true;for(unsigned t:{0u,4u,6u,15u,17u})refused=refused&&pass.configure_history_taps(t)==E_INVALIDARG&&pass.history_taps()==5;
        const bool set=pass.configure_history_taps(16)==S_OK&&pass.history_taps()==16&&pass.configure_history_taps(5)==S_OK&&pass.history_taps()==5;
        std::printf("HISTORY_TAPS_CONFIG bilinear=%u reason=%s invalid_refused=%u set=%u\n",unsigned(pass.bilinear_history_available()),pass.bilinear_history_reason(),unsigned(refused),unsigned(set));
        ++numeric_checks;require(refused&&set&&pass.bilinear_history_available()&&std::strcmp(pass.bilinear_history_reason(),"ok")==0,"history taps: 5 / 16 accepted, anything else refused; the FP16 / R32F filter query passes on this device");}
    constexpr unsigned N=32;
    {EdgeScene s(d,compiler);
    struct Motion{const char* name;double vx,vy;};const Motion motions[]={{"rest",0,0},{"drift",.30,.20}};
    std::vector<TapsRun> fives;
    for(unsigned program:{0u,2u})for(const Motion& m:motions){
        const auto five=taps_sequence(s,resolver,m.vx,m.vy,N,5,program),sixteen=taps_sequence(s,resolver,m.vx,m.vy,N,16,program);fives.push_back(five);
        const Compare c=compare(five,sixteen);
        const bool drawn=std::all_of(five.drawn.begin(),five.drawn.end(),[](unsigned t){return t==5;})&&std::all_of(sixteen.drawn.begin(),sixteen.drawn.end(),[](unsigned t){return t==16;});
        bool caller=true;if(!program){const auto bound=taps_sequence(s,taps16,m.vx,m.vy,N,5,0);caller=same_rgb(bound.output,sixteen.output);}
        std::printf("HISTORY_TAPS program=%s motion=%s frames=%u differing=%u max_abs=%.8f max_ulps=%.2f max_bound_ratio=%.4f bound_exceeded=%u drawn=%u caller_16_identical=%u\n",
                    program?"far_camera":"plain",m.name,N,c.differing,c.maxAbs,c.maxUlps,c.maxBoundRatio,c.boundExceeded,unsigned(drawn),unsigned(caller));
        ++numeric_checks;require(drawn&&caller,"history taps: Diagnostics::history_taps names the program drawn; 16 binds the embedded 16-tap words");
        ++numeric_checks;if(m.vx==0)require(c.maxUlps<=1,"history taps: a still scene is identical within one FP16 ulp (Catmull-Rom of a constant history is the constant)");
        else if(!program)require(c.differing>0&&c.boundExceeded==0,"history taps: under diagonal drift the forms differ and the plain program's difference stays within w times the current 3x3 range");
        else require(c.differing>0,"history taps: under diagonal drift the far-camera forms differ (reported)");}
    // Report only: in this scene (depths 0.5 / 0.9 against the far gate's 0.9995, objects at 0.36 px/frame against the
    // 0.03 .. 0.25 speed gate) every gate of the far-camera program is expected to stay closed, which makes its output the
    // plain program's; the mask maxima (r filter weight, g farw, b thin-region strength, a the screen-gate strength) say
    // whether that holds, and the identity says whether the far_camera rows are an independent check.
    for(unsigned motion=0;motion<2;++motion){const TapsRun& plain=fives[motion];const TapsRun& farCamera=fives[2+motion];
        std::printf("HISTORY_TAPS_FAR_IDLE motion=%s far_camera_equals_plain=%u mask_max_r=%.4f mask_max_g=%.4f mask_max_b=%.4f mask_max_a=%.4f\n",motions[motion].name,
                    unsigned(same_rgb(plain.output,farCamera.output,false)),farCamera.maskMax[0],farCamera.maskMax[1],farCamera.maskMax[2],farCamera.maskMax[3]);}
    // The filter query refused: every slot holds the 16-tap words, whatever was asked.
    for(unsigned program:{0u,2u}){const auto fallback=taps_sequence(s,resolver,.30,.20,N,5,program,true),sixteen=taps_sequence(s,resolver,.30,.20,N,16,program);
        const bool drawn=std::all_of(fallback.drawn.begin(),fallback.drawn.end(),[](unsigned t){return t==16;});
        std::printf("HISTORY_TAPS_FALLBACK program=%s bilinear=%u reason=%s drawn_16=%u identical_to_16=%u\n",program?"far_camera":"plain",unsigned(fallback.bilinear),fallback.reason,unsigned(drawn),unsigned(same_rgb(fallback.output,sixteen.output)));
        ++numeric_checks;require(!fallback.bilinear&&std::strcmp(fallback.reason,"adapter_query")==0&&drawn&&same_rgb(fallback.output,sixteen.output),"history taps: without the filter query every run draws the 16-tap programs, byte-identical");}}
    release_bindings(d);
    // Reset: two frames, before_reset / Reset / after_reset, two more frames from an empty history = a fresh pass on those frames.
    {TemporalPass pass;check("taps Reset initialize",pass.initialize(d,nullptr,resolver));check("taps Reset configure far",pass.configure_far());
        size_t beforeFrames=0;{EdgeScene before(d,compiler);beforeFrames=taps_sequence(before,resolver,.30,.20,2,5,2,false,0,&pass).output.size();}release_bindings(d);
        pass.before_reset();check("taps Reset",d->Reset(&pp));pass.after_reset(S_OK);
        TapsRun resumed,fresh;{EdgeScene after(d,compiler);resumed=taps_sequence(after,resolver,.30,.20,2,5,2,false,2,&pass);fresh=taps_sequence(after,resolver,.30,.20,2,5,2,false,2);}release_bindings(d);
        const bool drawn=std::all_of(resumed.drawn.begin(),resumed.drawn.end(),[](unsigned t){return t==5;});
        std::printf("HISTORY_TAPS_RESET before_frames=%zu drawn_5=%u identical_to_fresh=%u\n",beforeFrames,unsigned(drawn),unsigned(same_rgb(resumed.output,fresh.output)));
        ++numeric_checks;require(drawn&&same_rgb(resumed.output,fresh.output),"history taps: a 5-tap pass across a device Reset resumes from an empty history, byte-identical to a fresh pass");}
}
} // namespace history_taps
