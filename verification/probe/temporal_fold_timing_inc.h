// FOLD_TIMING (docs/architecture/taa-mask-fold.md sections 3, 6 and 8): the TAA sub-passes of the camera gate at the target
// resolution, event-query drained around each (gpu_sync_timing::Marks, the flight's --gpu-sync-timing boundaries): taa_copy,
// taa_mask (the removed tests draw), taa_box, taa_resolve, and the whole run. The fixture's CPU wall clock, not GPU or game time.
// Content "station": the left 40 % sky (unrouted, depth sentinel), the right 60 % routed far panels (depth 0.9999, farw 0.8)
// with a 1024 x 512 lattice of 1 px gaps at a 7.5 px pitch (search-only: no vote) and, over the sky, 512 x 256 of voted 1 px
// struts every 4 px (lane .a = 0.5). rest: a static camera; pan: a 1 px/frame camera pan that the routed content follows (the
// camera term open, the screen gate closed: the box and the resolve's box term run on the region). Configurations: the fold
// with the launcher's default source (vote: the search skipped) and with both (the search on), each with the flown options
// (far weight 0.985, thin region 0.97, camera gate, thin vote, emissive E = 1, the S4 half-resolution box). Built with
// X3M_FOLD_BASELINE against the pre-fold tree (the tests draw, the sentinel stabiliser 0.7 as flown in Run 82 A launch 1)
// the same rows are the baseline the fold is compared with (docs/verification/temporal-resolve.md, "Mask fold").
namespace fold_timing {
struct Marks final : x3m::gpu_sync_timing::Marks {
    IDirect3DQuery9* query=nullptr;LARGE_INTEGER frequency{};double ms[x3m::gpu_sync_timing::pass_count]{};std::int64_t start[x3m::gpu_sync_timing::pass_count]{};unsigned failures=0;
    static std::int64_t now(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart;}
    bool wait() noexcept {if(FAILED(query->Issue(D3DISSUE_END)))return false;const auto t0=now();HRESULT hr;while((hr=query->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){if(now()-t0>frequency.QuadPart*10)return false;Sleep(0);}return SUCCEEDED(hr);}
    void drain(){if(!wait())throw std::runtime_error("fold timing drain");}
    static bool timed(unsigned pass){namespace g=x3m::gpu_sync_timing;return pass==g::TaaCopy||pass==g::TaaMask||pass==g::TaaBox||pass==g::TaaResolve;}
    void begin(unsigned pass) noexcept override {if(timed(pass)){failures+=!wait();start[pass]=now();}}
    void end(unsigned pass) noexcept override {if(timed(pass)&&start[pass]){failures+=!wait();ms[pass]+=1000.*double(now()-start[pass])/double(frequency.QuadPart);start[pass]=0;}}
};
void run(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver,const UINT W,const UINT H){
    namespace g=x3m::gpu_sync_timing;
    Com<IDirect3DQuery9> query;check("fold timing query",d->CreateQuery(D3DQUERYTYPE_EVENT,&query.p));
    Com<IDirect3DSurface9> saved;check("fold timing save",d->GetRenderTarget(0,&saved.p));D3DVIEWPORT9 vp{};check("fold timing viewport",d->GetViewport(&vp));
    for(UINT n=0;n<20;++n)check("fold timing unbind",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
    Com<IDirect3DTexture9> colour,lane,motion;
    check("fold timing colour",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&colour.p,nullptr));
    check("fold timing lane",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&lane.p,nullptr));
    check("fold timing motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));
    Com<ID3DXBuffer> flatCode,uvCode;Com<IDirect3DPixelShader9> flat,uvShader;
    compile(compiler,"float4 c0:register(c0);float4 main():COLOR0{return c0;}","ps_3_0",&flatCode.p);
    compile(compiler,"float4 c0:register(c0);float4 main(float2 uv:TEXCOORD0):COLOR0{return float4(uv+c0.xy,c0.z,c0.w);}","ps_3_0",&uvCode.p);
    check("fold timing flat",d->CreatePixelShader(static_cast<DWORD*>(flatCode->GetBufferPointer()),&flat.p));check("fold timing uv",d->CreatePixelShader(static_cast<DWORD*>(uvCode->GetBufferPointer()),&uvShader.p));
    struct V{float x,y,z,rhw,u,v;};
    auto rect=[&](float l,float t,float r,float b){const V v[]={{l-.5f,t-.5f,.5f,1,(l)/W,(t)/H},{r-.5f,t-.5f,.5f,1,(r)/W,(t)/H},{l-.5f,b-.5f,.5f,1,(l)/W,(b)/H},{r-.5f,b-.5f,.5f,1,(r)/W,(b)/H}};
        check("fold timing rect",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));};
    // TEXCOORD0 = (x + 0.5) / W at pixel x's centre: the motion shader writes the pixel's own UV plus c0.xy (the previous position).
    auto fill=[&](IDirect3DTexture9* target,const float sky[4],const float panel[4],const float gap[4],const float strut[4],bool uv,float dx){
        Com<IDirect3DSurface9> level;check("fold timing level",target->GetSurfaceLevel(0,&level.p));check("fold timing target",d->SetRenderTarget(0,level.p));
        D3DVIEWPORT9 full{0,0,W,H,0,1};check("fold timing fill viewport",d->SetViewport(&full));check("fold timing fill Begin",d->BeginScene());
        check("fold timing fvf",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check("fold timing vs",d->SetVertexShader(nullptr));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE})check("fold timing rs",d->SetRenderState(state,FALSE));
        check("fold timing cull",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check("fold timing cwe",d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        const float skyX=std::floor(.4f*W);
        auto shade=[&](const float* c,bool routed){if(uv&&routed){const float k[4]={c[0]+dx,c[1],c[2],c[3]};check("fold timing uv PS",d->SetPixelShader(uvShader.p));check("fold timing c0",d->SetPixelShaderConstantF(0,k,1));}
            else{check("fold timing flat PS",d->SetPixelShader(flat.p));check("fold timing c0",d->SetPixelShaderConstantF(0,c,1));}};
        shade(sky,false);rect(0,0,skyX,float(H));
        shade(panel,true);rect(skyX,0,float(W),float(H));
        // The lattice: 1 px gaps every 7.5 px over a 1024 x 512 patch of the panels.
        const float lx=skyX+256,ly=256;shade(gap,false);
        for(float x=lx;x<lx+1024;x+=7.5f){const float gx=std::floor(x);rect(gx,ly,gx+1,ly+512);}
        for(float y=ly;y<ly+512;y+=7.5f){const float gy=std::floor(y);rect(lx,gy,lx+1024,gy+1);}
        // Voted struts over the sky: 1 px wide every 4 px, 512 x 256.
        shade(strut,true);for(float x=128;x<128+512;x+=4)rect(x,256,x+1,512);
        check("fold timing fill End",d->EndScene());};
    const float px=1.f/W;
    auto content=[&](bool pan){const float dx=pan?-px:0.f;
        {const float sky[4]={.02f,.02f,.02f,1},panel[4]={.3f,.3f,.3f,1},gap[4]={.02f,.02f,.02f,1},strut[4]={.6f,.6f,.6f,1};fill(colour.p,sky,panel,gap,strut,false,0);}
        {const float sky[4]={-1,-1,-1,-1},panel[4]={.9999f,.9999f,5000,1},gap[4]={-1,-1,-1,-1},strut[4]={.9999f,.9999f,5000,.5f};fill(lane.p,sky,panel,gap,strut,false,0);}
        {const float sky[4]={0,0,0,-1},panel[4]={0,0,.9999f,1},gap[4]={0,0,0,-1},strut[4]={0,0,.9999f,1};fill(motion.p,sky,panel,gap,strut,true,dx);}
        check("fold timing restore",d->SetRenderTarget(0,saved.p));check("fold timing restore viewport",d->SetViewport(&vp));};
    auto inputs=[&](bool pan,unsigned source){FrameInputs in{};in.color=colour.p;in.current_depth=lane.p;in.motion=motion.p;in.width=W;in.height=H;in.epoch=1;in.weight=.9f;
        std::copy(identity,identity+16,in.clip_to_previous);if(pan)in.clip_to_previous[3]=-2.f/W;
        in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=true;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=false;
        in.far_weight=.985f;in.far_d0=.9995f;in.far_inv=2000;in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;in.thin_region_hold_frames=8;in.thin_vote=source!=3;in.thin_region_emissive=1;
        in.thin_region_source=source==2?x3m::renderer::ThinRegionSource::Vote:x3m::renderer::ThinRegionSource::Both;
#ifdef X3M_FOLD_BASELINE
        in.sentinel_strength=.7f;in.sentinel_emitter=1;
#endif
        return in;};
#ifdef X3M_FOLD_BASELINE
    const char* const build="baseline";const unsigned configs[]={0};const char* const names[]={"today"};
#else
    const char* const build="fold";const unsigned configs[]={2,0,3};const char* const names[]={"vote","both","thin_vote_off"};
#endif
    constexpr unsigned count=sizeof(configs)/sizeof(configs[0]);
    for(bool pan:{false,true}){content(pan);
        TemporalPass passes[count];Marks marks[count];double total[count]{};Output out;
        for(unsigned i=0;i<count;++i){check("fold timing initialize",passes[i].initialize(d,nullptr,resolver,nullptr,nullptr,reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program())));
            check("fold timing configure far",passes[i].configure_far());check("fold timing box half",passes[i].configure_box_resolution(2));check("fold timing thin vote",passes[i].configure_thin_vote());
#ifdef X3M_FOLD_BASELINE
            check("fold timing sentinel",passes[i].configure_sentinel());
#endif
            marks[i].query=query.p;QueryPerformanceFrequency(&marks[i].frequency);}
        for(unsigned warm=0;warm<12;++warm)for(unsigned i=0;i<count;++i){check("fold timing warm",passes[i].run(inputs(pan,configs[i]),&out));marks[i].drain();}
        for(unsigned i=0;i<count;++i)passes[i].configure_sync_timing(&marks[i]);
        constexpr unsigned rounds=8;
        for(unsigned round=0;round<rounds;++round)for(unsigned step=0;step<count;++step){const unsigned i=(round+step)%count;marks[i].drain();const auto t0=Marks::now();check("fold timing run",passes[i].run(inputs(pan,configs[i]),&out));marks[i].drain();
            total[i]+=1000.*double(Marks::now()-t0)/double(marks[i].frequency.QuadPart)/rounds;require(passes[i].diagnostics().region_hold&&passes[i].diagnostics().box_half,"fold timing: the camera gate ran with the half-resolution box");}
        for(unsigned i=0;i<count;++i){passes[i].configure_sync_timing(nullptr);require(marks[i].failures==0,"fold timing: every boundary drained");
            std::printf("FOLD_TIMING build=%s width=%u height=%u rounds=%u content=station motion=%s config=%s source_drawn=%s copy_ms=%.4f mask_ms=%.4f box_ms=%.4f resolve_ms=%.4f taa_ms=%.4f run_ms=%.4f scope=cpu_wall_with_event_query_drain_fixture\n",
                build,W,H,rounds,pan?"pan":"rest",names[i],x3m::renderer::thin_region_source_name(passes[i].diagnostics().thin_region_source),marks[i].ms[g::TaaCopy]/rounds,marks[i].ms[g::TaaMask]/rounds,marks[i].ms[g::TaaBox]/rounds,marks[i].ms[g::TaaResolve]/rounds,
                (marks[i].ms[g::TaaCopy]+marks[i].ms[g::TaaMask]+marks[i].ms[g::TaaBox]+marks[i].ms[g::TaaResolve])/rounds,total[i]);}}
    check("fold timing restore target",d->SetRenderTarget(0,saved.p));check("fold timing restore vp",d->SetViewport(&vp));
}
} // namespace fold_timing
void fold_timing_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){std::puts("FOLD_TIMING_CASES");fold_timing::run(d,compiler,resolver,5120,1440);}
