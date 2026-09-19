// Far-stabiliser cases of temporal_pass_fixture.cpp (lattice mode);
// docs/architecture/taa-distant-line-fade.md section 9. Included after
// temporal_line_inc.h: uses its LineConfig, line_model (the 2-D CPU oracle with
// the far gate), FlickerRun, EdgeScene, Snapshot, Fault.
//
// Scene "interior facets": three vertical bands of static routed geometry, value
// 0.035: near (x < 11, depth 0.99, farw 0), mid (11 <= x < 21, farw 0.4) and far
// (x >= 21, depth 0.99992, farw 1) under the gate d0 = 0.9995, d1 = 0.9999; over
// them horizontal facets 0.3 px tall, value 0.95 (contrast 27), pitch 4 px, each
// 1e-5 nearer than its band, static or drifting 0.04 px/frame along y. No
// sentinel anywhere. 8-sample Halton jitter, 256 frames, the last 64 analysed.
constexpr float farBandDepth[3]={.99f,.99966f,.99992f},farFacetStep=1e-5f;
constexpr UINT farBandEdge[4]={0,11,21,32};
constexpr unsigned farFrames=256,farAnalysed=64;
double farDrift=0;
std::vector<EdgeObject> far_objects(unsigned n){std::vector<EdgeObject> o;
    for(unsigned b=0;b<3;++b)o.push_back({double(farBandEdge[b]),0,double(farBandEdge[b+1]),32,.035f,farBandDepth[b],0,0});
    const double phase=std::fmod(2.13+farDrift*n,4.);
    for(double top=phase;top<30;top+=4)if(top>=2)for(unsigned b=0;b<3;++b)o.push_back({double(farBandEdge[b]),top,double(farBandEdge[b+1]),top+.3,.95f,farBandDepth[b]-farFacetStep,0,farDrift});
    return o;}
double far_velocity(double nearest){for(float band:farBandDepth)if(nearest==double(band-farFacetStep))return farDrift;return 0;}
// Fails the creation of A8R8G8B8 render-target textures (the mask targets) while alive.
struct MaskCreationFault {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
        if(format==D3DFMT_A8R8G8B8&&(usage&D3DUSAGE_RENDERTARGET)){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}return original(d,w,h,levels,usage,format,pool,out,shared);}
    explicit MaskCreationFault(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[23],sizeof original);auto fn=&hook;std::memcpy(&table[23],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~MaskCreationFault(){*reinterpret_cast<void***>(device)=previous;}
};
struct FarRun : FlickerRun { std::vector<std::vector<float>> mask; bool masksFailed=false; };
FarRun far_sequence(EdgeScene& s,const DWORD* resolver,const LineConfig& c,unsigned frames,bool validGate=true,bool failMasks=false){
    TemporalPass pass;check("far initialize",pass.initialize(s.d,nullptr,resolver));const bool farOn=c.farW>0||c.farA>0;
    if(c.thin>0&&!farOn){check("far configure flicker",pass.configure_flicker());}
    if(c.A>0){check("far configure line",pass.configure_line_filter());}
    if(farOn){check("far configure",pass.configure_far());check("far configure is idempotent",pass.configure_far());require(pass.far_available(),"far-stabiliser program created on this device");}
    const FlickerConfig f{c.name,c.thin,0,.1f,.5f,false,false,.9f};FarRun run;bool sequence=true;
    for(unsigned n=0;n<frames;++n){const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(far_objects(n),EdgeBackground{.035f,farBandDepth[0],1},jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));
        auto in=flicker_inputs(s,f,jx,jy,false);in.line_filter=c.A;in.line_width=c.width;in.far_weight=c.farW;in.far_filter=c.farA;in.far_d0=farD0;in.far_inv=validGate?farInv:0;
        Output out;check("far Begin resolve",s.d->BeginScene());
        if(failMasks&&n==0){MaskCreationFault fault(s.d);check(c.name,pass.run(in,&out));require(MaskCreationFault::refused>0,"mask creation fault reached");}else check(c.name,pass.run(in,&out));
        check("far End resolve",s.d->EndScene());
        sequence=sequence&&out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0);
        run.output.push_back(s.read(out.color));if(out.age)run.age.push_back(s.read(out.age));if(out.stabiliser_mask&&n+1==frames)run.mask.push_back(s.read(out.stabiliser_mask));}
    run.masksFailed=pass.line_masks_failed();require(sequence,"far history follows the sequence (a mask fallback included)");return run;}
// Temporal ripple (mean |second difference| / 2) of the facet rows over the analysed frames, per band.
double far_ripple(const FarRun& r,unsigned band){double sum=0;unsigned count=0;
    for(unsigned n=farFrames-farAnalysed;n+1<farFrames;++n)for(UINT y=4;y<28;++y)for(UINT x=farBandEdge[band]+2;x+2<farBandEdge[band+1];++x){sum+=std::fabs(px(r.output[n+1],x,y)-2*px(r.output[n],x,y)+px(r.output[n-1],x,y))/2;++count;}
    return sum/count;}
void far_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("FAR_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    struct Hooks{Hooks(){line_velocity=far_velocity;}~Hooks(){line_velocity=line_velocity_default;farD0=farInv=0;farDrift=0;}} hooks;
    // ---- the CPU gate: the engine's projection at 1280 px, footprints 80 .. 130 units/px ----
    {float d0=0,inv=0;const bool ok=x3::temporal::far_gate(.8f,1.000003f,-6.000018f,1280,80,130,d0,inv);
        const double z0=80*.8*1280/2,z1=130*.8*1280/2,e0=1.000003-6.000018/z0,e1=1.000003-6.000018/z1;
        std::printf("FAR_GATE ok=%u d0=%.8f inv=%.3f expected_d0=%.8f expected_inv=%.3f\n",unsigned(ok),d0,inv,e0,1/(e1-e0));
        ++numeric_checks;require(ok&&std::fabs(d0-e0)<1e-6&&std::fabs(inv*(e1-e0)-1)<.01,"far gate equals the footprint inversion of the projection");
        double last=-1;bool monotone=true,ends=false;for(unsigned i=0;i<=64;++i){const double z=20000+i*1000.,depth=1.000003-6.000018/z;const double w=std::min(std::max((float(depth)-d0)*inv,0.f),1.f);monotone=monotone&&w>=last;last=w;if(i==0)ends=w==0;}
        ++numeric_checks;require(monotone&&ends&&last==1,"farw is monotone across a depth ramp, 0 near and 1 far");
        float a=1,b=1;bool refused=true;
        const float invalid[][5]={{0,1.000003f,-6,80,130},{.8f,1.000003f,6,80,130},{.8f,0,-6,80,130},{.8f,1.000003f,-6,130,80},{.8f,1.000003f,-6,0,130},{NAN,1,-6,80,130},{.8f,1.000003f,-6,80,2e6f},{.8f,1.000003f,-6,1e-4f,2e-4f},{.8f,1.000003f,-6.000018f,9e5f,1e6f}}; // the last: F0 so near the far plane that d0 and d1 collapse in float32
        for(auto& p:invalid)
            refused=refused&&!x3::temporal::far_gate(p[0],p[1],p[2],1280,p[3],p[4],a,b)&&a==0&&b==0;
        refused=refused&&!x3::temporal::far_gate(.8f,1.000003f,-6,0,80,130,a,b);
        ++numeric_checks;require(refused,"invalid projection or footprints: no gate (d0 = inv = 0)");}
    farD0=.9995f;farInv=1.f/(.9999f-.9995f);
    const LineConfig base{"far-base",false,0,0,0},weight{"far-weight-0.985",false,0,0,0,1,.985f,0},filter{"far-filter-A1",false,0,0,0,1,0,1},both{"far-weight+filter",false,0,0,0,1,.985f,1},
        lined{"far-weight+filter+line-A1",true,1,0,0,1,.985f,1},soft{"far-weight+soft-0.75",false,0,.75f,0,1,.985f,0};
    // ---- validation and refusals, hostile state, failed draw, Reset ----
    {s.render(far_objects(0),EdgeBackground{.035f,farBandDepth[0],1},0,0);Output out;const FlickerConfig none{"far-validation",0,0,.1f,.5f,false,false,.9f};
        TemporalPass bare;check("far bare initialize",bare.initialize(d,nullptr,resolver));auto in=flicker_inputs(s,none,0,0,false);in.caller_scene_open=false;in.far_weight=.985f;in.far_d0=farD0;in.far_inv=farInv;
        require(!bare.far_available()&&bare.run(in,&out)==E_INVALIDARG,"far stabiliser without configure_far is refused");
        // (any ps_3_0 program stands in for the filtered one: only its presence matters to the refusal below)
        TemporalPass pass;check("far validation initialize",pass.initialize(d,nullptr,resolver,nullptr,nullptr,nullptr,resolver));check("far validation configure",pass.configure_far());check("far validation flicker",pass.configure_flicker());check("far validation line",pass.configure_line_filter());
        for(float bad:{-.1f,.5f,.995f,NAN}){in.far_weight=bad;require(pass.run(in,&out)==E_INVALIDARG,"far weight outside {0} U [weight, 0.99] is refused");}in.far_weight=.985f;
        for(float bad:{-1.f,4.5f,NAN}){in.far_filter=bad;require(pass.run(in,&out)==E_INVALIDARG,"far filter outside [0, 4] is refused");}in.far_filter=0;
        for(auto gate:{std::pair<float,float>{-.1f,.25f},{.25f,.25f},{.3f,.25f},{.03f,65.f},{NAN,.25f}}){in.far_speed_lo=gate.first;in.far_speed_hi=gate.second;require(pass.run(in,&out)==E_INVALIDARG,"far speed gate needs 0 <= LO < HI <= 64");}in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
        for(float bad:{-1.f,NAN,INFINITY}){in.far_inv=bad;require(pass.run(in,&out)==E_INVALIDARG,"far gate slope must be finite and >= 0");}in.far_inv=farInv;
        in.weight=0;require(pass.run(in,&out)==E_INVALIDARG,"far weight over a history weight of 0 is refused");in.weight=.9f;
        in.motion_policy=MotionPolicy::KnownCameraOnly;in.motion=nullptr;require(pass.run(in,&out)==E_INVALIDARG,"far stabiliser without per-pixel motion is refused");in.motion_policy=MotionPolicy::PerPixel;in.motion=s.motion.p;
        in.far_d0=NAN;require(pass.run(in,&out)==E_INVALIDARG,"far gate origin must be finite");in.far_d0=farD0;
        in.thin_clip=.75f;in.adaptive_weight=.97f;in.adaptive_lo=.1f;in.adaptive_hi=.5f;require(pass.run(in,&out)==E_INVALIDARG,"far stabiliser beside the adaptive weight is refused (one gate)");in.adaptive_weight=0;in.thin_clip=0;
        in.current_filter=1;require(pass.run(in,&out)==E_INVALIDARG,"far stabiliser beside the global current filter is refused");in.current_filter=0;
        in.far_filter=1;in.line_filter=2;require(pass.run(in,&out)==E_INVALIDARG,"far filter beside a line filter of another A is refused");in.line_filter=1;require(SUCCEEDED(pass.run(in,&out)),"far filter beside a line filter of the same A runs");in.line_filter=0;
        require(SUCCEEDED(pass.run(in,&out))&&out.age&&out.stabiliser_mask,"far run publishes the age target and the mask");
        const float junk[4]={9,8,7,6};for(UINT r:{5u,22u,24u})check("far hostile constant",d->SetPixelShaderConstantF(r,junk,1));
        check("far hostile s8",d->SetTexture(8,s.wave.p));check("far hostile s8 min",d->SetSamplerState(8,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("far hostile s8 u",d->SetSamplerState(8,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));check("far hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
        {Snapshot before(d);check("far hostile run",pass.run(in,&out));before.equals(d,"far run restores c5, c22, c24, sampler 8, RT1 and COLORWRITEENABLE1");}
        {Output failed;{Fault fault(d,1);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!failed.age&&!pass.diagnostics().history_valid,"failed far mask draw publishes nothing");}
            check("far recovery",pass.run(in,&out));require(out.color&&!out.used_history,"after a failed run the far resolve restarts without history");}
        pass.before_reset();pass.after_reset(S_OK);check("far after Reset",pass.run(in,&out));require(out.color&&!out.used_history&&pass.far_available()&&!pass.line_masks_failed(),"Reset protocol keeps the far program and recreates the masks");
        check("far unbind s8",d->SetTexture(8,nullptr));state_checks+=1;s.target(s.colorSurface.p);}
    // ---- static and drifting facets ----
    // Speed ramp of the far weight (default gate 0.03 .. 0.25 px/frame): 0 and 0.04 (t = 0 / 0.045), 0.14 (t = 0.5), 0.30 (past HI: the base weight).
    double rampRatio[4]{};unsigned rampIndex=0;
    for(double drift:{0.,.04,.14,.3}){farDrift=drift;const auto baseRun=far_sequence(s,resolver,base,farFrames);const double baseRipple[3]={far_ripple(baseRun,0),far_ripple(baseRun,1),far_ripple(baseRun,2)};
        for(const LineConfig* c:{&base,&weight,&filter,&both,&lined,&soft}){const auto run=c==&base?baseRun:far_sequence(s,resolver,*c,farFrames);const auto model=line_model(run,*c);double oracle=0,ageOracle=0;unsigned nearDiffers=0,nearPixels=0;
            for(unsigned n=0;n<farFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));
                if(!run.age.empty())ageOracle=std::max(ageOracle,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));
                if(x<9&&c->A<=0){++nearPixels;nearDiffers+=std::memcmp(&run.output[n][(y*S+x)*4],&baseRun.output[n][(y*S+x)*4],4*sizeof(float))!=0;}}
            // Oracle ripple of the far band from the model's own images.
            FarRun modelled;modelled.output.resize(farFrames);for(unsigned n=0;n<farFrames;++n){modelled.output[n].assign(S*S*4,0);for(UINT i=0;i<S*S;++i)modelled.output[n][i*4]=model.color[n][i];}
            const double ripple[3]={far_ripple(run,0),far_ripple(run,1),far_ripple(run,2)},oracleFar=far_ripple(modelled,2);
            // The published mask of the last frame against the gate: g on the weight component, r on the filter component (centre columns of each band).
            double maskError=0;if(!run.mask.empty())for(unsigned b=0;b<3;++b){const UINT x=(farBandEdge[b]+farBandEdge[b+1])/2;for(UINT y=4;y<28;++y){const float w=far_gate_weight(px(run.depth.back(),x,y));
                maskError=std::max(maskError,double(std::fabs(px(run.mask[0],x,y,1)-(c->farW>0?w:0))));if(c->A<=0)maskError=std::max(maskError,double(std::fabs(px(run.mask[0],x,y,0)-(c->farA>0?w:0))));}}
            std::printf("FAR_STABILISER drift=%.2f config=%s oracle_error=%.6f age_oracle_error=%.6f near_px=%u near_differs=%u ripple_near=%.6f ripple_mid=%.6f ripple_far=%.6f ratio_near=%.4f ratio_mid=%.4f ratio_far=%.4f oracle_ratio_far=%.4f mask_error=%.6f\n",
                drift,c->name,oracle,ageOracle,nearPixels,nearDiffers,ripple[0],ripple[1],ripple[2],ripple[0]/baseRipple[0],ripple[1]/baseRipple[1],ripple[2]/baseRipple[2],oracleFar/baseRipple[2],maskError);
            metric((std::string("far ")+c->name+": shader matches the 2-D CPU oracle within the FP16 bound").c_str(),oracle,0,.0006/(1-(c->farW>0?c->farW:.9)));
            if(c->farW>0||c->farA>0){metric((std::string("far ")+c->name+": age target matches the CPU oracle").c_str(),ageOracle,0,0);
                metric((std::string("far ")+c->name+": published mask equals the quantised gate").c_str(),maskError,0,.5/255);
                metric((std::string("far ")+c->name+": far-band ripple ratio as the oracle's").c_str(),ripple[2]/baseRipple[2],oracleFar/baseRipple[2],.03);}
            if(c==&weight){rampRatio[rampIndex]=ripple[2]/baseRipple[2];
                if(drift>=.25){++numeric_checks;require(same_rgb(run.output,baseRun.output),"far weight at a speed past HI is the plain resolve bit for bit, every pixel");}}
            if(c->A<=0){++numeric_checks;require(nearPixels>0&&nearDiffers==0,"near pixels (farw 0) bit-identical to the ungated plain resolve, all four channels");}
            if(drift==0&&c->farW>0&&c->thin<=0){++numeric_checks;require(ripple[2]<=.25*baseRipple[2]&&ripple[1]<ripple[0]&&ripple[1]>ripple[2],"static far ripple <= 0.25 x base; the mid band lies between");}
            // (The filter alone does not lower this scene's ripple: a 0.3-px facet toggles whole rows and the mean over rows conserves it. Its
            // effect is reported and held to the oracle; the real-dump replay is its evidence.)
            // Over a whole jitter cycle: on a phase whose samples miss every facet the 3x3 box collapses and every config is the background exactly.
            if(c==&filter){double changed=0;for(unsigned n=farFrames-latticePhases;n<farFrames;++n)for(UINT y=4;y<28;++y)for(UINT x=23;x<30;++x)changed=std::max(changed,double(std::fabs(px(run.output[n],x,y)-px(baseRun.output[n],x,y))));
                std::printf("FAR_FILTER_EFFECT drift=%.2f far_band_max_difference=%.6f\n",drift,changed);++numeric_checks;require(changed>.005,"far filter alone acts on the far band");}}
    ++rampIndex;}
    std::printf("FAR_SPEED_RAMP lo=%.3f hi=%.3f ratio_v0=%.4f ratio_v0.04=%.4f ratio_v0.14=%.4f ratio_v0.30=%.4f\n",farLo,farHi,rampRatio[0],rampRatio[1],rampRatio[2],rampRatio[3]);
    ++numeric_checks;require(rampRatio[0]<=rampRatio[1]+.01&&rampRatio[1]<rampRatio[2]&&rampRatio[2]<rampRatio[3]&&rampRatio[3]==1,"far weight falls continuously with the screen speed: ripple ratio monotone from W_FAR to the base weight");
    farDrift=0;
    // ---- gate off (invalid projection this frame) and mask-target creation failure: the plain resolve bit for bit, history kept ----
    {const auto baseRun=far_sequence(s,resolver,base,32),gateOff=far_sequence(s,resolver,both,32,false),failed=far_sequence(s,resolver,both,32,true,true);
        ++numeric_checks;require(same_rgb(baseRun.output,gateOff.output)&&!gateOff.masksFailed,"far stabiliser with inv = 0 (no valid projection) is the plain resolve bit for bit");
        ++numeric_checks;require(same_rgb(baseRun.output,failed.output)&&failed.masksFailed,"mask-target creation failure: option off for the session, plain resolve bit for bit, history kept");
        // A Reset re-arms one attempt: fail the first run, Reset, and the next run has its masks again.
        {TemporalPass pass;check("far retry initialize",pass.initialize(d,nullptr,resolver));check("far retry configure",pass.configure_far());const FlickerConfig none{"far-retry",0,0,.1f,.5f,false,false,.9f};
            s.render(far_objects(0),EdgeBackground{.035f,farBandDepth[0],1},0,0);auto in=flicker_inputs(s,none,0,0,false);in.caller_scene_open=false;in.far_weight=.985f;in.far_d0=farD0;in.far_inv=farInv;Output out;
            {MaskCreationFault fault(d);check("far retry failed run",pass.run(in,&out));}const bool failedOnce=pass.line_masks_failed()&&!out.stabiliser_mask&&out.color;
            check("far retry second run",pass.run(in,&out));const bool sticky=pass.line_masks_failed()&&!out.stabiliser_mask&&out.used_history;
            pass.before_reset();pass.after_reset(S_OK);check("far retry after Reset",pass.run(in,&out));
            ++numeric_checks;require(failedOnce&&sticky&&!pass.line_masks_failed()&&out.stabiliser_mask,"mask failure is sticky within the session and re-armed once by a Reset");s.target(s.colorSurface.p);}
        const LineConfig lineOnly{"line-A1-mask-failure",true,1,0,0};const auto lineFailed=far_sequence(s,resolver,lineOnly,32,true,true);
        ++numeric_checks;require(same_rgb(baseRun.output,lineFailed.output)&&lineFailed.masksFailed,"mask-target creation failure under the line filter: plain resolve, history kept");}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
