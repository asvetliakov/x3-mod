// Thin-region cases of temporal_pass_fixture.cpp (lattice mode); docs/architecture/taa-lattice-crawl.md section 13.
// Included after temporal_far_inc.h: uses LineConfig / line_model (the 2-D oracle with the thin-region gate), FarRun,
// MaskCreationFault, EdgeScene, Snapshot, Fault.
//
// Scene "arm": horizontal shards 0.8 px tall, pitch 2.37 px, value 1 at depth 0.99, x in [2, 11), over the depth
// sentinel (value 0.25, motion alpha -1, policy 2 with the identity camera): under the 8-phase jitter their coverage
// toggles against the sentinel, a fringe wider than the 3x3 clip box, so the clip snaps the history to the current
// phase. A plain 8x8 square (depth 0.98) at x in [21, 29) is the ordinary silhouette: no 7-tap line through any of its
// pixels changes depth class twice, the grown region ends at x = 19, and it must stay bit-identical.
//
// Scene "pan" (section 32.1; thinPanX != 0): the same shards spanning the whole width (rows are uniform along x, so a fixed
// pixel sees the same content every frame and the ripple metric needs no tracking) while the camera translates by thinPanX
// px/frame along x (clip_to_previous; the sentinel background follows it under policy 2) and the shards, routed, move with
// it: screen speed thinPanX, camera-relative speed 0. Content enters from the left border, so a pixel at x holds at most
// x / thinPanX frames of history; the metrics use x in [18, 28). Optionally a bright independent mover (value 4, depth 0.5,
// 6 px square in rows [8, 14)) crosses the field at thinPatchV px/frame from frame thinPatchFrom, and a 6x6 patch of value 4
// is written into the pass's history after frame thinInjectFrame (the stale-history witness of section 15 / 32).
constexpr unsigned thinFrames=128,thinAnalysed=32;
double thinDrift=0;unsigned thinMoveFrom=~0u;
double thinPanX=0,thinPatchV=0;unsigned thinPatchFrom=~0u,thinInjectFrame=~0u;constexpr float patchDepth=.5f,patchValue=4;
// thinBadTap: one routed pixel of value 65504 (above the resolve's finite limit 65000) at (23, 12) of the pan scene, inside the
// injected patch. thinBadMotion != 0: a routed 2x2 object on the static arm with that x velocity (1e30: the speed overflows to a non-finite value
// inside the mask program; NaN: a NaN correspondence). thinK: luminance k.
bool thinBadTap=false;double thinBadMotion=0;float thinK=0;
constexpr int injectRect[4]={20,9,26,15};
std::vector<EdgeObject> thin_objects(unsigned n){std::vector<EdgeObject> o;constexpr double S=EdgeScene::S;
    if(thinPanX!=0){for(double top=2.31;top<30;top+=2.37)if(top>=2)o.push_back({0,top,S,top+.8,1,lineDepth,thinPanX,0});
        if(thinBadTap)o.push_back({23,12,24,13,65504.f,lineDepth,thinPanX,0});
        if(thinPatchFrom!=~0u&&n>=thinPatchFrom){const double l=-8+thinPatchV*(n-thinPatchFrom);if(l<S&&l+6>0)o.push_back({l,8,l+6,14,patchValue,patchDepth,thinPatchV,0});}
        return o;}
    const double moved=n>thinMoveFrom?thinDrift*(n-thinMoveFrom):thinMoveFrom==~0u?thinDrift*n:0,phase=std::fmod(2.31+moved,2.37);
    for(double top=phase;top<30;top+=2.37)if(top>=2)o.push_back({2,top,11,top+.8,1,lineDepth,0,n>thinMoveFrom||thinMoveFrom==~0u?thinDrift:0});
    o.push_back({21,12,29,20,1,squareDepth,0,0});if(thinBadMotion!=0)o.push_back({6,13,8,15,1,lineDepth,thinBadMotion,0});return o;}
double thin_velocity(double nearest){return nearest==double(lineDepth)?thinDrift:0;}
double thin_velocity_x(double nearest){return nearest==double(patchDepth)?thinPatchV:cameraPanX;}
// Fails the creation of A16B16G16R16F render-target textures (the box targets of the camera gate) while alive.
struct BoxCreationFault {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
        if(format==D3DFMT_A16B16G16R16F&&(usage&D3DUSAGE_RENDERTARGET)){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}return original(d,w,h,levels,usage,format,pool,out,shared);}
    explicit BoxCreationFault(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[23],sizeof original);auto fn=&hook;std::memcpy(&table[23],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~BoxCreationFault(){*reinterpret_cast<void***>(device)=previous;}
};
// failBoxes: the first camera-gate run (frame 1; frame 0 runs the screen gate so the histories exist) meets a box-target
// creation failure; the pass falls back to the screen gate for the session.
FarRun thin_sequence(EdgeScene& s,const DWORD* resolver,const LineConfig& c,unsigned frames,bool failMasks=false,bool failBoxes=false){
    TemporalPass pass;check("thin initialize",pass.initialize(s.d,nullptr,resolver));const bool on=c.thinW>0||c.farW>0;constexpr UINT S=EdgeScene::S;
    if(on){check("thin configure",pass.configure_far());require(pass.far_available(),"thin-region program created on this device");if(c.camera)require(pass.camera_gate_available(),"camera-gate programs created on this device");}
    const FlickerConfig f{c.name,0,0,.1f,.5f,false,false,.9f};FarRun run;bool sequence=true;
    for(unsigned n=0;n<frames;++n){const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(thin_objects(n),sentinelBackground,jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));
        auto in=flicker_inputs(s,f,jx,jy,true);in.thin_region_weight=c.thinW;in.thin_region_relax=c.relax;in.far_weight=c.farW;in.far_d0=farD0;in.far_inv=farInv;in.far_speed_lo=farLo;in.far_speed_hi=farHi;
        in.luminance_k=thinK;in.thin_region_camera_gate=c.camera&&!(failBoxes&&n==0);if(thinPanX!=0)in.clip_to_previous[3]=float(-2*thinPanX/S); // previous clip x = x - 2 pan / S: content moved right by pan px
        Output out;check("thin Begin resolve",s.d->BeginScene());
        if(failMasks&&n==0){MaskCreationFault fault(s.d);check(c.name,pass.run(in,&out));}
        else if(failBoxes&&n==1){BoxCreationFault fault(s.d);check(c.name,pass.run(in,&out));require(BoxCreationFault::refused>0&&pass.camera_gate_failed(),"box-target creation fault reached; camera gate fell back");}
        else check(c.name,pass.run(in,&out));
        check("thin End resolve",s.d->EndScene());
        sequence=sequence&&out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0);
        run.output.push_back(s.read(out.color));if(out.age)run.age.push_back(s.read(out.age));if(out.stabiliser_mask&&n+1==frames)run.mask.push_back(s.read(out.stabiliser_mask));
        if(n==thinInjectFrame){ // stale history: the bright patch written into the history the next frame reads (the oracle injects the same values)
            Com<IDirect3DSurface9> level;check("thin inject level",out.color->GetSurfaceLevel(0,&level.p));s.target(level.p);check("thin inject Begin",s.d->BeginScene());check("thin inject flat",s.d->SetPixelShader(s.flat.p));
            s.constant(patchValue,patchValue,patchValue,1);s.quad(injectRect[0],injectRect[1],injectRect[2],injectRect[3],0,0);check("thin inject End",s.d->EndScene());s.target(s.colorSurface.p);}}
    run.masksFailed=pass.line_masks_failed();require(sequence,"thin-region history follows the sequence");return run;}
// Temporal ripple of the shard region / peak-to-peak over the last jitter cycles, columns [x0, x1).
void thin_ripple(const FarRun& r,double& rms,double& p2p,UINT x0=4,UINT x1=9){double sum=0;unsigned count=0;p2p=0;const unsigned N=unsigned(r.output.size());
    for(UINT y=5;y<27;++y)for(UINT x=x0;x<x1;++x){double lo=1e9,hi=-1e9,mean=0;for(unsigned n=N-thinAnalysed;n<N;++n){const double v=px(r.output[n],x,y);lo=std::min(lo,v);hi=std::max(hi,v);mean+=v/thinAnalysed;}
        for(unsigned n=N-thinAnalysed;n<N;++n){const double e=px(r.output[n],x,y)-mean;sum+=e*e;++count;}p2p=std::max(p2p,hi-lo);}
    rms=std::sqrt(sum/count);}
void thin_region_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("THIN_REGION_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    struct Hooks{Hooks(){line_velocity=thin_velocity;line_velocity_x=thin_velocity_x;farD0=.98f;farInv=200;} // far gate for the combined config: farw 1 on the shards (0.99), 0 on the square (0.98)
        ~Hooks(){line_velocity=line_velocity_default;line_velocity_x=line_velocity_x_default;thinDrift=0;thinMoveFrom=~0u;thinBadTap=false;thinBadMotion=0;thinK=0;oracleK=0;thinPanX=cameraPanX=thinPatchV=0;thinPatchFrom=thinInjectFrame=oracleInjectFrame=~0u;farD0=farInv=0;}} hooks;
    const LineConfig base{"thin-base",false,0,0,0},on97{"thin-region-0.97",false,0,0,0,1,0,0,.97f,1},on985{"thin-region-0.985",false,0,0,0,1,0,0,.985f,1},half{"thin-region-0.97-relax-0.5",false,0,0,0,1,0,0,.97f,.5f},weightOnly{"thin-region-0.97-relax-0",false,0,0,0,1,0,0,.97f,0},withFar{"thin-region-0.97+far-weight-0.985",false,0,0,0,1,.985f,0,.97f,1},
        camera97{"thin-region-0.97-camera-gate",false,0,0,0,1,0,0,.97f,1,true};
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
        // ---- camera gate (section 32.1): refusals, hostile state, failed box draw, Reset ----
        require(pass.camera_gate_available(),"camera-gate programs created by configure_far");
        in.thin_region_camera_gate=true;in.line_filter=1;check("thin camera line configure",pass.configure_line_filter());require(pass.run(in,&out)==E_INVALIDARG,"camera gate beside the line filter is refused (the mask's line channel carries the second gate)");in.line_filter=0;
        require(bare.run(in,&out)==E_INVALIDARG,"camera gate without configure_far is refused");
        in.thin_region_weight=0;require(SUCCEEDED(pass.run(in,&out))&&!out.stabiliser_mask,"camera gate without the thin region is ignored (plain resolve)");in.thin_region_weight=.97f;
        require(SUCCEEDED(pass.run(in,&out))&&out.age&&out.stabiliser_mask,"camera-gate run publishes the age target and the mask");
        for(UINT r:{0u,4u,5u,6u,22u,24u})check("thin camera hostile constant",d->SetPixelShaderConstantF(r,junk,1));
        check("thin camera hostile s9",d->SetTexture(9,s.wave.p));check("thin camera hostile s10",d->SetTexture(10,s.wave.p));check("thin camera hostile s9 min",d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("thin camera hostile s10 u",d->SetSamplerState(10,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));check("thin camera hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
        {Snapshot before(d);check("thin camera hostile run",pass.run(in,&out));before.equals(d,"camera-gate run restores c0..c7, c22, c24, samplers 8..10, RT1 and COLORWRITEENABLE1");}
        {Output failed;{Fault fault(d,4);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid,"failed box draw (the fourth draw) publishes nothing");}
            check("thin camera recovery",pass.run(in,&out));require(out.color&&!out.used_history,"after a failed box draw the camera-gate resolve restarts without history");}
        pass.before_reset();pass.after_reset(S_OK);check("thin camera after Reset",pass.run(in,&out));require(out.color&&!out.used_history&&out.stabiliser_mask&&pass.camera_gate_available()&&!pass.camera_gate_failed(),"Reset protocol keeps the camera-gate programs and recreates the box targets");
        in.thin_region_camera_gate=false;check("thin unbind s9",d->SetTexture(9,nullptr));check("thin unbind s10",d->SetTexture(10,nullptr));
        check("thin unbind s8",d->SetTexture(8,nullptr));check("thin unbind s4",d->SetTexture(4,nullptr));state_checks+=2;s.target(s.colorSurface.p);}
    // ---- static shards, and drifting inside the gate (0.12 px/frame, t = 0.41) and past HI (0.30) ----
    double staticRms[2]{};
    for(double drift:{0.,.12,.3}){thinDrift=drift;thinMoveFrom=~0u;const auto baseRun=thin_sequence(s,resolver,base,thinFrames);double baseRms=0,baseP2p=0;thin_ripple(baseRun,baseRms,baseP2p);FarRun screenRun;
        for(const LineConfig* c:{&base,&on97,&on985,&half,&weightOnly,&withFar,&camera97}){const auto run=c==&base?baseRun:thin_sequence(s,resolver,*c,thinFrames);const auto model=line_model(run,*c);double oracle=0,ageOracle=0,rms=0,p2p=0,maskError=0;unsigned squareDiffers=0,squarePixels=0;thin_ripple(run,rms,p2p);
            if(c==&on97)screenRun=run;
            for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));
                if(!run.age.empty())ageOracle=std::max(ageOracle,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));
                if(x>=20){++squarePixels;squareDiffers+=std::memcmp(&run.output[n][(y*S+x)*4],&baseRun.output[n][(y*S+x)*4],4*sizeof(float))!=0;}}
            if(!run.mask.empty())for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){maskError=std::max(maskError,double(std::fabs(px(run.mask[0],x,y,2)-quantise8(thin_region_strength(run.depth.back(),int(x),int(y),c->camera)))));
                if(c->camera)maskError=std::max(maskError,double(std::fabs(px(run.mask[0],x,y,3)-quantise8(thin_region_strength(run.depth.back(),int(x),int(y),false)))));}
            std::printf("THIN_REGION drift=%.2f config=%s oracle_error=%.6f age_oracle_error=%.6f mask_error=%.6f shard_rms_codes=%.3f shard_p2p_codes=%.1f rms_ratio=%.4f square_px=%u square_differs=%u\n",drift,c->name,oracle,ageOracle,maskError,255*rms,255*p2p,rms/baseRms,squarePixels,squareDiffers);
            metric((std::string("thin region ")+c->name+": shader matches the 2-D CPU oracle within the FP16 bound").c_str(),oracle,0,.0006/(1-(c->thinW>0?c->thinW:.9)));
            if(c->thinW>0){metric((std::string("thin region ")+c->name+": age target matches the CPU oracle").c_str(),ageOracle,0,0);
                metric((std::string("thin region ")+c->name+": published gate equals the oracle's (fragmented 7x7, closed by the fastest pixel)").c_str(),maskError,0,.5/255);}
            ++numeric_checks;require(squarePixels>0&&squareDiffers==0,"plain silhouette (the square and everything right of x = 20) bit-identical to the plain resolve, all four channels");
            if(drift==0&&c==&on97){staticRms[0]=baseRms;staticRms[1]=rms;++numeric_checks;require(rms<=.35*baseRms&&p2p<=.5*baseP2p,"static shards: ripple <= 0.35 x and peak-to-peak <= 0.5 x the installed resolve");}
            if(drift==0&&c==&weightOnly){++numeric_checks;require(rms>staticRms[1],"the weight without the clip relaxation leaves more ripple (the clip is the cause)");}
            // (Measured 1.10 x the thin region alone: pixels whose depth toggles with the phase alternate between the two weight targets.)
            if(drift==0&&c==&withFar){++numeric_checks;require(rms<=.15*baseRms&&rms<=staticRms[1]*1.25,"far stabiliser + thin region (the expected default pair): shard ripple <= 0.15 x the installed resolve and within 1.25 x the thin region alone");}
            if(drift>=.25&&c->thinW>0){++numeric_checks;require(same_rgb(run.output,baseRun.output),"past the speed gate the thin-region run is the plain resolve bit for bit, every pixel");}
            // Section 32.1 decision 2: with a static camera the camera-relative speed IS the screen speed (at rest both 0, under object
            // drift both the drift), so the camera gate's run is the screen gate's bit for bit, colour, alpha, age and mask.
            if(c==&camera97){unsigned gateDiffers=0;for(UINT i=0;i<S*S;++i)gateDiffers+=run.mask[0][i*4+2]!=screenRun.mask[0][i*4+2]||run.mask[0][i*4+3]!=run.mask[0][i*4+2]; // the camera mask's a channel is the screen strength: equal to b here
                std::printf("THIN_REGION_CAMERA_STATIC drift=%.2f colour_identical=%u age_identical=%u gate_differs=%u\n",drift,unsigned(same_rgb(run.output,screenRun.output)),unsigned(same_rgb(run.age,screenRun.age)),gateDiffers);
                ++numeric_checks;require(same_rgb(run.output,screenRun.output)&&same_rgb(run.age,screenRun.age)&&gateDiffers==0,"camera gate with a static camera (rest, drift inside the gate, drift past it): bit-identical to the screen gate, every pixel, all four channels, age and gate");}}}
    // ---- motion starts after 64 static frames (0.4 px/frame): the gate closes at once; the stabilised history is released by the clip ----
    {thinDrift=.4;thinMoveFrom=64;const auto baseRun=thin_sequence(s,resolver,base,thinFrames),run=thin_sequence(s,resolver,on97,thinFrames);double first=0,late=0,trail=0,baseTrail=0;
        for(unsigned n=65;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x<20;++x){const double e=std::fabs(px(run.output[n],x,y)-px(baseRun.output[n],x,y));if(n==65)first=std::max(first,e);if(n>=65+24)late=std::max(late,e);
            if(px(run.depth[n],x,y)<=-.5f){trail=std::max(trail,std::fabs(double(px(run.output[n],x,y))-.25)*(n>=65+8));baseTrail=std::max(baseTrail,std::fabs(double(px(baseRun.output[n],x,y))-.25)*(n>=65+8));}}
        std::printf("THIN_REGION_MOTION_START first_frame_difference=%.6f after_24_frames=%.6f background_trail=%.6f base_background_trail=%.6f\n",first,late,trail,baseTrail);
        metric("thin region: 24 frames after motion starts the output is the installed resolve's",late,0,2./255);
        metric("thin region: from 8 frames after motion starts, uncovered background carries at most 0.04 more shard colour than under the installed resolve (ghost bound; 0 after 24 frames above)",std::max(trail-baseTrail,0.),0,.04);
        thinMoveFrom=~0u;thinDrift=0;}
    // ---- camera pan at 0.5 px/frame (section 32.1): the screen gate closes the whole field (0.5 > HI), the camera gate keeps it open ----
    // Runs: screen and camera gates, clean; then the same with the bright independent mover (frames 40..59) and the stale-history
    // patch injected after frame 100. Metrics on columns [18, 28) (>= 36 frames of history at 0.5 px/frame).
    {thinPanX=cameraPanX=.5;const auto baseRun=thin_sequence(s,resolver,base,thinFrames),screenRun=thin_sequence(s,resolver,on97,thinFrames),cameraRun=thin_sequence(s,resolver,camera97,thinFrames);
        double baseRms=0,baseP2p=0,screenRms=0,screenP2p=0,cameraRms=0,cameraP2p=0;thin_ripple(baseRun,baseRms,baseP2p,18,28);thin_ripple(screenRun,screenRms,screenP2p,18,28);thin_ripple(cameraRun,cameraRms,cameraP2p,18,28);
        double screenShare=0,cameraShare=0,cameraScreenChannel=0;unsigned window=0;
        for(UINT y=5;y<27;++y)for(UINT x=18;x<28;++x){++window;screenShare+=px(screenRun.mask[0],x,y,2)>0;cameraShare+=px(cameraRun.mask[0],x,y,2)>0;cameraScreenChannel=std::max(cameraScreenChannel,double(px(cameraRun.mask[0],x,y,3)));}
        screenShare/=window;cameraShare/=window;
        for(const auto* pair:{&screenRun,&cameraRun}){const bool camera=pair==&cameraRun;const auto model=line_model(*pair,camera?camera97:on97);double oracle=0,ageOracle=0,maskError=0;
            for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(pair->output[n],x,y)-model.color[n][y*S+x])));ageOracle=std::max(ageOracle,double(std::fabs(px(pair->age[n],x,y)-model.age[n][y*S+x])));}
            for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){maskError=std::max(maskError,double(std::fabs(px(pair->mask[0],x,y,2)-quantise8(thin_region_strength(pair->depth.back(),int(x),int(y),camera)))));if(camera)maskError=std::max(maskError,double(std::fabs(px(pair->mask[0],x,y,3)-quantise8(thin_region_strength(pair->depth.back(),int(x),int(y),false)))));}
            std::printf("THIN_REGION_PAN pan=0.50 config=%s oracle_error=%.6f age_oracle_error=%.6f mask_error=%.6f\n",camera?camera97.name:on97.name,oracle,ageOracle,maskError);
            metric(camera?"pan, camera gate: shader matches the 2-D CPU oracle (camera-relative gate, 7x7 box) within the FP16 bound":"pan, screen gate: shader matches the 2-D CPU oracle within the FP16 bound",oracle,0,.0006/(1-.97));
            metric(camera?"pan, camera gate: age target matches the CPU oracle":"pan, screen gate: age target matches the CPU oracle",ageOracle,0,0);
            metric(camera?"pan, camera gate: published gates (b camera, a screen) equal the oracle's":"pan, screen gate: published gate equals the oracle's",maskError,0,.5/255);}
        std::printf("THIN_REGION_CAMERA pan=0.50 base_rms_codes=%.3f screen_rms_codes=%.3f camera_rms_codes=%.3f camera_over_screen=%.4f screen_p2p_codes=%.1f camera_p2p_codes=%.1f screen_gate_share=%.4f camera_gate_share=%.4f camera_mask_screen_channel_max=%.4f\n",255*baseRms,255*screenRms,255*cameraRms,cameraRms/screenRms,255*screenP2p,255*cameraP2p,screenShare,cameraShare,cameraScreenChannel);
        ++numeric_checks;require(same_rgb(screenRun.output,baseRun.output)&&screenShare==0,"pan past HI: the screen-gated thin region is the plain resolve bit for bit and its published gate is closed everywhere");
        ++numeric_checks;require(cameraShare>=.99&&cameraScreenChannel==0,"pan past HI: the camera gate keeps the whole shard field open (share >= 0.99) while the mask's screen-gate channel reads closed");
        ++numeric_checks;require(cameraRms<=.5*screenRms&&cameraP2p<=.6*screenP2p,"pan past HI: the camera gate halves the shard ripple (rms <= 0.5 x, peak-to-peak <= 0.6 x the screen gate)");
        // The bright mover crosses the field at 2 px/frame (camera-relative 1.5 px/frame: it closes the gate around itself in both
        // modes); after it has left (frame 60) nothing may exceed the scene's own maximum (1) by more than 2 codes in either mode.
        // The patch injected after frame 100 measures the stale-history bound at frame 101 against the clean runs.
        thinPatchV=2;thinPatchFrom=40;thinInjectFrame=oracleInjectFrame=100;std::copy(injectRect,injectRect+4,oracleInjectRect);oracleInjectValue=patchValue;
        const auto screenPatch=thin_sequence(s,resolver,on97,thinFrames),cameraPatch=thin_sequence(s,resolver,camera97,thinFrames);
        double excess[2]={0,0},added[2]={0,0},addedLate[2]={0,0},oracle[2]={0,0};
        for(unsigned which=0;which<2;++which){const auto& run=which?cameraPatch:screenPatch;const auto& clean=which?cameraRun:screenRun;const auto model=line_model(run,which?camera97:on97);
            for(unsigned n=61;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){if(n<100)excess[which]=std::max(excess[which],double(px(run.output[n],x,y))-1);
                if(n>100)oracle[which]=std::max(oracle[which],double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));}
            for(int y=injectRect[1]-1;y<=injectRect[3];++y)for(int x=injectRect[0]-1;x<=injectRect[2];++x){added[which]=std::max(added[which],double(px(run.output[101],UINT(x),UINT(y))-px(clean.output[101],UINT(x),UINT(y))));addedLate[which]=std::max(addedLate[which],double(px(run.output[105],UINT(x),UINT(y))-px(clean.output[105],UINT(x),UINT(y))));}}
        std::printf("THIN_REGION_STALE pan=0.50 mover_px_per_frame=2 injected_value=%.1f screen_excess_after_mover=%.6f camera_excess_after_mover=%.6f screen_added_frame101=%.6f camera_added_frame101=%.6f camera_over_screen=%.4f screen_added_frame105=%.6f camera_added_frame105=%.6f unbounded_estimate=%.4f screen_oracle_error=%.6f camera_oracle_error=%.6f\n",
            patchValue,excess[0],excess[1],added[0],added[1],added[1]/std::max(added[0],1e-9),addedLate[0],addedLate[1],.97*(patchValue-.25),oracle[0],oracle[1]);
        metric("pan + bright mover, screen gate: no ghost above the scene's maximum after the mover left",excess[0],0,2./255);
        metric("pan + bright mover, camera gate: no ghost above the scene's maximum after the mover left (the mover closes the gate around itself)",excess[1],0,2./255);
        metric("pan + stale patch, camera gate: shader matches the oracle's 7x7 box clip of the injected history (frames 101..127)",oracle[1],0,.0006/(1-.97));
        metric("pan + stale patch, screen gate: shader matches the oracle on the injected history (frames 101..127)",oracle[0],0,.0006/(1-.97));
        ++numeric_checks;require(added[0]>.1&&added[1]<=2*added[0],"stale patch (value 4) one frame after injection: the camera gate's 7x7 box bounds it to at most 2 x the screen gate's clipped value");
        ++numeric_checks;require(added[1]<.5*.97*(patchValue-.25),"stale patch: the box binds (well below the clip-off estimate 0.97 x (4 - 0.25))");
        thinPatchV=0;thinPatchFrom=~0u;
        // The box pass's own weighting and finite rule: k = 0.5 and one current pixel of 65504 (not finite for the resolve) inside
        // the injected patch. A box that kept the bad tap would not bind around it (maximum 65504); one that weighed differently
        // from the resolve would clip to the wrong bound. Both show as oracle error on frames 101..127.
        {thinBadTap=true;thinK=.5f;oracleK=.5;const auto run=thin_sequence(s,resolver,camera97,thinFrames);const auto model=line_model(run,camera97);double error=0,beside=0,bad=0;unsigned badPixels=0;
            for(unsigned n=101;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){const double e=std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x]);error=std::max(error,e);
                if(px(run.current[n],x,y)>65000){++badPixels;bad=std::max(bad,double(std::fabs(px(run.output[n],x,y))));}
                if(n==101&&int(x)>=injectRect[0]&&int(x)<injectRect[2]&&int(y)>=injectRect[1]&&int(y)<injectRect[3]&&px(run.current[n],x,y)<=65000)beside=std::max(beside,double(px(run.output[n],x,y)));}
            std::printf("THIN_REGION_BOX_DOMAIN pan=0.50 k=0.5 bad_tap_value=65504 bad_pixel_frames=%u bad_pixel_output_max=%.6f oracle_error=%.6f patch_output_max_frame101=%.6f\n",badPixels,bad,error,beside);
            metric("pan + stale patch, k = 0.5 and a non-finite current tap: shader matches the oracle (box weighed as the resolve, bad tap ignored)",error,0,.0006/(1-.97));
            ++numeric_checks;require(badPixels>0&&bad==0&&beside<1.5,"non-finite tap: exercised, resolved black, and the stale patch beside it stays bounded by the finite 7x7 colours");
            thinBadTap=false;thinK=0;oracleK=0;}
        thinInjectFrame=oracleInjectFrame=~0u;
        // Box-target creation failure on the first camera-gate frame: the screen gate for the rest of the session, history kept.
        {const auto fell=thin_sequence(s,resolver,camera97,32,false,true),screen32=thin_sequence(s,resolver,on97,32);
            ++numeric_checks;require(same_rgb(fell.output,screen32.output)&&same_rgb(fell.age,screen32.age),"box-target creation failure: the camera gate falls back to the screen gate bit for bit, history kept");}
        thinPanX=cameraPanX=0;}
    // ---- non-finite routed motion on the static arm (a routed 2x2 object at (6..7, 13..14)) ----
    // 1e30 px/frame: the speed overflows inside the mask program. The camera mask carries openness (saturate(1 - inf) = 0) and must
    // close both gates within 8 px of every covered pixel, as the plain mask closes its one; the output equals the screen gate's bit
    // for bit. NaN: reported only. This backend compiles shaders with fast-math semantics, under which no in-shader expression is
    // reliable on a NaN (the plain mask's closure reads open as well); the openness form is closed under IEEE and D3D UNORM rules.
    for(double bad:{1e30,double(NAN)}){thinBadMotion=bad;const bool asserted=bad==bad;const auto screenRun=thin_sequence(s,resolver,on97,32),cameraRun=thin_sequence(s,resolver,camera97,32);double open[2]={0,0},cameraScreenChannel=0;unsigned covered=0;
        const auto motion=s.read(s.motion.p); // the last frame's motion target: the covered pixels are those whose alpha is 1 and whose depth is the object's but lie off the shard rows' own motion
        for(UINT y=12;y<16;++y)for(UINT x=5;x<9;++x){const float u=px(motion,x,y,0);if(px(motion,x,y,3)==1&&!(std::fabs(u)<=2)){++covered;
            for(int dy=-8;dy<=8;++dy)for(int dx=-8;dx<=8;++dx){const UINT qx=UINT(std::min(std::max(int(x)+dx,0),int(S)-1)),qy=UINT(std::min(std::max(int(y)+dy,0),int(S)-1));
                open[0]=std::max(open[0],double(px(screenRun.mask[0],qx,qy,2)));open[1]=std::max(open[1],double(px(cameraRun.mask[0],qx,qy,2)));cameraScreenChannel=std::max(cameraScreenChannel,double(px(cameraRun.mask[0],qx,qy,3)));}}}
        const bool identical=same_rgb(cameraRun.output,screenRun.output)&&same_rgb(cameraRun.age,screenRun.age);
        std::printf("THIN_REGION_BAD_MOTION kind=%s asserted=%u covered_px=%u screen_gate_max_within_8px=%.4f camera_gate_max_within_8px=%.4f camera_screen_channel_max=%.4f identical_to_screen_gate=%u\n",asserted?"overflow_1e30":"nan",unsigned(asserted),covered,open[0],open[1],cameraScreenChannel,unsigned(identical));
        if(asserted){++numeric_checks;require(covered>0&&open[1]==0&&cameraScreenChannel==0&&open[0]==0,"non-finite speed (overflow): the camera mask closes both gates within 8 px, as the plain mask closes its one");
            ++numeric_checks;require(identical,"non-finite speed (overflow): camera-gate output and age equal the screen gate's bit for bit");}}
    thinBadMotion=0;
    // ---- mask-target creation failure: option off for the session, plain resolve bit for bit, history kept ----
    {const auto baseRun=thin_sequence(s,resolver,base,32),failed=thin_sequence(s,resolver,on97,32,true);
        ++numeric_checks;require(same_rgb(baseRun.output,failed.output)&&failed.masksFailed,"mask-target creation failure under the thin region: plain resolve, history kept");}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
