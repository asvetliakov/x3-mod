// Thin-region cases of temporal_pass_fixture.cpp (lattice mode); docs/architecture/taa-lattice-crawl.md section 13.
// Included after temporal_far_inc.h: uses LineConfig / line_model (the 2-D oracle with the thin-region gate), FarRun,
// MaskCreationFault, EdgeScene, Snapshot, Fault.
//
// Scene "arm": horizontal shards 0.8 px tall, pitch 2.37 px, value 1 at depth 0.99, x in [2, 11), over the depth
// sentinel (value 0.25, motion alpha -1, policy 2 with the identity camera): under the 8-phase jitter their coverage
// toggles against the sentinel, a fringe wider than the 3x3 clip box, so the clip snaps the history to the current
// phase. A plain 8x8 square (depth 0.98) at x in [21, 29) is the ordinary silhouette: no 7-tap line through any of its
// pixels changes depth class twice, the grown region ends at x = 19, and it must stay bit-identical.
constexpr unsigned thinFrames=128,thinAnalysed=32;
double thinDrift=0;unsigned thinMoveFrom=~0u;
std::vector<EdgeObject> thin_objects(unsigned n){std::vector<EdgeObject> o;const double moved=n>thinMoveFrom?thinDrift*(n-thinMoveFrom):thinMoveFrom==~0u?thinDrift*n:0,phase=std::fmod(2.31+moved,2.37);
    for(double top=phase;top<30;top+=2.37)if(top>=2)o.push_back({2,top,11,top+.8,1,lineDepth,0,n>thinMoveFrom||thinMoveFrom==~0u?thinDrift:0});
    o.push_back({21,12,29,20,1,squareDepth,0,0});return o;}
double thin_velocity(double nearest){return nearest==double(lineDepth)?thinDrift:0;}
FarRun thin_sequence(EdgeScene& s,const DWORD* resolver,const LineConfig& c,unsigned frames,bool failMasks=false){
    TemporalPass pass;check("thin initialize",pass.initialize(s.d,nullptr,resolver));const bool on=c.thinW>0||c.farW>0;
    if(on){check("thin configure",pass.configure_far());require(pass.far_available(),"thin-region program created on this device");}
    const FlickerConfig f{c.name,0,0,.1f,.5f,false,false,.9f};FarRun run;bool sequence=true;
    for(unsigned n=0;n<frames;++n){const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(thin_objects(n),sentinelBackground,jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));
        auto in=flicker_inputs(s,f,jx,jy,true);in.thin_region_weight=c.thinW;in.thin_region_relax=c.relax;in.far_weight=c.farW;in.far_d0=farD0;in.far_inv=farInv;in.far_speed_lo=farLo;in.far_speed_hi=farHi;
        Output out;check("thin Begin resolve",s.d->BeginScene());
        if(failMasks&&n==0){MaskCreationFault fault(s.d);check(c.name,pass.run(in,&out));}else check(c.name,pass.run(in,&out));
        check("thin End resolve",s.d->EndScene());
        sequence=sequence&&out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0);
        run.output.push_back(s.read(out.color));if(out.age)run.age.push_back(s.read(out.age));if(out.stabiliser_mask&&n+1==frames)run.mask.push_back(s.read(out.stabiliser_mask));}
    run.masksFailed=pass.line_masks_failed();require(sequence,"thin-region history follows the sequence");return run;}
// Temporal ripple of the shard region / peak-to-peak over the last jitter cycles.
void thin_ripple(const FarRun& r,double& rms,double& p2p){double sum=0;unsigned count=0;p2p=0;const unsigned N=unsigned(r.output.size());
    for(UINT y=5;y<27;++y)for(UINT x=4;x<9;++x){double lo=1e9,hi=-1e9,mean=0;for(unsigned n=N-thinAnalysed;n<N;++n){const double v=px(r.output[n],x,y);lo=std::min(lo,v);hi=std::max(hi,v);mean+=v/thinAnalysed;}
        for(unsigned n=N-thinAnalysed;n<N;++n){const double e=px(r.output[n],x,y)-mean;sum+=e*e;++count;}p2p=std::max(p2p,hi-lo);}
    rms=std::sqrt(sum/count);}
void thin_region_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("THIN_REGION_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    struct Hooks{Hooks(){line_velocity=thin_velocity;farD0=.98f;farInv=200;} // far gate for the combined config: farw 1 on the shards (0.99), 0 on the square (0.98)
        ~Hooks(){line_velocity=line_velocity_default;thinDrift=0;thinMoveFrom=~0u;farD0=farInv=0;}} hooks;
    const LineConfig base{"thin-base",false,0,0,0},on97{"thin-region-0.97",false,0,0,0,1,0,0,.97f,1},on985{"thin-region-0.985",false,0,0,0,1,0,0,.985f,1},half{"thin-region-0.97-relax-0.5",false,0,0,0,1,0,0,.97f,.5f},weightOnly{"thin-region-0.97-relax-0",false,0,0,0,1,0,0,.97f,0},withFar{"thin-region-0.97+far-weight-0.985",false,0,0,0,1,.985f,0,.97f,1};
    // ---- refusals, hostile state, failed draw, Reset ----
    {s.render(thin_objects(0),sentinelBackground,0,0);Output out;const FlickerConfig none{"thin-validation",0,0,.1f,.5f,false,false,.9f};
        TemporalPass bare;check("thin bare initialize",bare.initialize(d,nullptr,resolver));auto in=flicker_inputs(s,none,0,0,true);in.caller_scene_open=false;in.thin_region_weight=.97f;
        require(bare.run(in,&out)==E_INVALIDARG,"thin region without configure_far is refused");
        TemporalPass pass;check("thin validation initialize",pass.initialize(d,nullptr,resolver,nullptr,nullptr,nullptr,resolver));check("thin validation configure",pass.configure_far());check("thin validation flicker",pass.configure_flicker());
        for(float bad:{-.1f,.5f,.995f,NAN}){in.thin_region_weight=bad;require(pass.run(in,&out)==E_INVALIDARG,"thin-region weight outside {0} U [weight, 0.99] is refused");}in.thin_region_weight=.97f;
        for(float bad:{-.1f,1.5f,NAN}){in.thin_region_relax=bad;require(pass.run(in,&out)==E_INVALIDARG,"thin-region relax outside [0, 1] is refused");}in.thin_region_relax=1;
        in.thin_clip=.75f;require(pass.run(in,&out)==E_INVALIDARG,"thin region beside the 3x3 thin clip is refused (the program has no sentinel soft clip)");in.thin_region_weight=0;in.far_weight=.985f;require(pass.run(in,&out)==E_INVALIDARG,"far stabiliser beside the 3x3 thin clip is refused");in.far_weight=0;in.thin_region_weight=.97f;in.thin_clip=0;
        in.thin_clip=.75f;in.adaptive_weight=.97f;require(pass.run(in,&out)==E_INVALIDARG,"thin region beside the adaptive weight is refused");in.thin_clip=0;in.adaptive_weight=0;
        in.motion_policy=MotionPolicy::KnownCameraOnly;in.motion=nullptr;require(pass.run(in,&out)==E_INVALIDARG,"thin region without per-pixel motion is refused");in.motion_policy=MotionPolicy::PerPixel;in.motion=s.motion.p;
        require(SUCCEEDED(pass.run(in,&out))&&out.age&&out.stabiliser_mask,"thin-region run publishes the age target and the mask");
        const float junk[4]={9,8,7,6};for(UINT r:{0u,3u,5u,6u,22u,24u})check("thin hostile constant",d->SetPixelShaderConstantF(r,junk,1));
        check("thin hostile s8",d->SetTexture(8,s.wave.p));check("thin hostile s4",d->SetTexture(4,s.wave.p));check("thin hostile s8 min",d->SetSamplerState(8,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("thin hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
        {Snapshot before(d);check("thin hostile run",pass.run(in,&out));before.equals(d,"thin-region run restores c0..c7, c22, c24, samplers 4 and 8, RT1 and COLORWRITEENABLE1");}
        {Output failed;{Fault fault(d,2);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid,"failed second mask draw publishes nothing");}
            check("thin recovery",pass.run(in,&out));require(out.color&&!out.used_history,"after a failed run the thin-region resolve restarts without history");}
        pass.before_reset();pass.after_reset(S_OK);check("thin after Reset",pass.run(in,&out));require(out.color&&!out.used_history&&out.stabiliser_mask,"Reset protocol recreates the masks and restarts the history");
        check("thin unbind s8",d->SetTexture(8,nullptr));check("thin unbind s4",d->SetTexture(4,nullptr));state_checks+=1;s.target(s.colorSurface.p);}
    // ---- static shards, and drifting inside the gate (0.12 px/frame, t = 0.41) and past HI (0.30) ----
    double staticRms[2]{};
    for(double drift:{0.,.12,.3}){thinDrift=drift;thinMoveFrom=~0u;const auto baseRun=thin_sequence(s,resolver,base,thinFrames);double baseRms=0,baseP2p=0;thin_ripple(baseRun,baseRms,baseP2p);
        for(const LineConfig* c:{&base,&on97,&on985,&half,&weightOnly,&withFar}){const auto run=c==&base?baseRun:thin_sequence(s,resolver,*c,thinFrames);const auto model=line_model(run,*c);double oracle=0,ageOracle=0,rms=0,p2p=0,maskError=0;unsigned squareDiffers=0,squarePixels=0;thin_ripple(run,rms,p2p);
            for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));
                if(!run.age.empty())ageOracle=std::max(ageOracle,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));
                if(x>=20){++squarePixels;squareDiffers+=std::memcmp(&run.output[n][(y*S+x)*4],&baseRun.output[n][(y*S+x)*4],4*sizeof(float))!=0;}}
            if(!run.mask.empty())for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x)maskError=std::max(maskError,double(std::fabs(px(run.mask[0],x,y,2)-quantise8(thin_region_strength(run.depth.back(),int(x),int(y))))));
            std::printf("THIN_REGION drift=%.2f config=%s oracle_error=%.6f age_oracle_error=%.6f mask_error=%.6f shard_rms_codes=%.3f shard_p2p_codes=%.1f rms_ratio=%.4f square_px=%u square_differs=%u\n",drift,c->name,oracle,ageOracle,maskError,255*rms,255*p2p,rms/baseRms,squarePixels,squareDiffers);
            metric((std::string("thin region ")+c->name+": shader matches the 2-D CPU oracle within the FP16 bound").c_str(),oracle,0,.0006/(1-(c->thinW>0?c->thinW:.9)));
            if(c->thinW>0){metric((std::string("thin region ")+c->name+": age target matches the CPU oracle").c_str(),ageOracle,0,0);
                metric((std::string("thin region ")+c->name+": published gate equals the oracle's (fragmented 7x7, closed by the fastest pixel)").c_str(),maskError,0,.5/255);}
            ++numeric_checks;require(squarePixels>0&&squareDiffers==0,"plain silhouette (the square and everything right of x = 20) bit-identical to the plain resolve, all four channels");
            if(drift==0&&c==&on97){staticRms[0]=baseRms;staticRms[1]=rms;++numeric_checks;require(rms<=.35*baseRms&&p2p<=.5*baseP2p,"static shards: ripple <= 0.35 x and peak-to-peak <= 0.5 x the installed resolve");}
            if(drift==0&&c==&weightOnly){++numeric_checks;require(rms>staticRms[1],"the weight without the clip relaxation leaves more ripple (the clip is the cause)");}
            // (Measured 1.10 x the thin region alone: pixels whose depth toggles with the phase alternate between the two weight targets.)
            if(drift==0&&c==&withFar){++numeric_checks;require(rms<=.15*baseRms&&rms<=staticRms[1]*1.25,"far stabiliser + thin region (the expected default pair): shard ripple <= 0.15 x the installed resolve and within 1.25 x the thin region alone");}
            if(drift>=.25&&c->thinW>0){++numeric_checks;require(same_rgb(run.output,baseRun.output),"past the speed gate the thin-region run is the plain resolve bit for bit, every pixel");}}}
    // ---- motion starts after 64 static frames (0.4 px/frame): the gate closes at once; the stabilised history is released by the clip ----
    {thinDrift=.4;thinMoveFrom=64;const auto baseRun=thin_sequence(s,resolver,base,thinFrames),run=thin_sequence(s,resolver,on97,thinFrames);double first=0,late=0,trail=0,baseTrail=0;
        for(unsigned n=65;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x<20;++x){const double e=std::fabs(px(run.output[n],x,y)-px(baseRun.output[n],x,y));if(n==65)first=std::max(first,e);if(n>=65+24)late=std::max(late,e);
            if(px(run.depth[n],x,y)<=-.5f){trail=std::max(trail,std::fabs(double(px(run.output[n],x,y))-.25)*(n>=65+8));baseTrail=std::max(baseTrail,std::fabs(double(px(baseRun.output[n],x,y))-.25)*(n>=65+8));}}
        std::printf("THIN_REGION_MOTION_START first_frame_difference=%.6f after_24_frames=%.6f background_trail=%.6f base_background_trail=%.6f\n",first,late,trail,baseTrail);
        metric("thin region: 24 frames after motion starts the output is the installed resolve's",late,0,2./255);
        metric("thin region: from 8 frames after motion starts, uncovered background carries at most 0.04 more shard colour than under the installed resolve (ghost bound; 0 after 24 frames above)",std::max(trail-baseTrail,0.),0,.04);
        thinMoveFrom=~0u;thinDrift=0;}
    // ---- mask-target creation failure: option off for the session, plain resolve bit for bit, history kept ----
    {const auto baseRun=thin_sequence(s,resolver,base,32),failed=thin_sequence(s,resolver,on97,32,true);
        ++numeric_checks;require(same_rgb(baseRun.output,failed.output)&&failed.masksFailed,"mask-target creation failure under the thin region: plain resolve, history kept");}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
