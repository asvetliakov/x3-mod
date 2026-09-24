// S4 (docs/architecture/taa-high-resolution.md S4; taa-plan-lifted-slot-cap.md step 2), lattice mode: the camera gate's box at
// half resolution (TemporalPass::configure_box_resolution(2); thin_box_{rows,columns}_half_ps.hlsl). Included after
// temporal_region_hold_inc.h: uses thin_sequence (thinBoxDivisor, thinRecordBoxes), region_hold::oracle, line_model
// (oracleBoxHalf), thin_ripple, hold_tests_error and the scene hooks.
//   BOX_HALF_STATE: the setting's refusals, a refused program (the pass stays at full resolution, every target bit for bit a
//   pass that was never configured), an odd frame size (that run draws the full-resolution box), refused half-resolution
//   targets (full resolution until Reset), hostile state (c12 included), a failed columns draw, Reset, a switch back and forth
//   inside one history.
//   BOX_HALF_CONTAINMENT: every standard scene run twice, full and half, with the box targets read back each frame: where the
//   full-resolution box was computed, the half-resolution block texel is computed and its bounds contain the full ones
//   channel by channel (no tolerance); where only the half-resolution one was computed it contains the pixel's own 3x3
//   (the clip the resolve takes there at full resolution). Rest, drift, the pan, the stale patch, the box domain (k = 0.5,
//   a non-finite tap), the sentinel facets at rest and under a reversing pan, the emitter crossing the sky, the stop after a pan.
//   BOX_HALF_ORACLE / _RIPPLE / _PAN_STOP / _STALE / _EMITTER: the half-resolution runs against the CPU oracle with the block
//   box (the thin-region bounds), and the rest ripple, stop-after-pan, stale-ghost and emitter rows within their bounds.
//   BOX_HALF_TIMING: the box stage alone (TaaBox sub-pass boundaries, event-query drained) at both resolutions on the
//   all-unrouted-sentinel pan frame at 1280x768 and 5120x1440: the fixture's CPU wall clock, not GPU or game time.
namespace box_half {
constexpr UINT S=EdgeScene::S;
using region_hold::Oracle;using region_hold::oracle;
// Refuses CreatePixelShader of the half-resolution rows program while alive (a device that refuses it at creation).
struct HalfRefusal {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,const DWORD* words,IDirect3DPixelShader9** out){
        if(words==reinterpret_cast<const DWORD*>(x3m::renderer::temporal_thin_box_rows_half_program())){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}
        return original(d,words,out);}
    explicit HalfRefusal(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[106],sizeof original);auto fn=&hook;std::memcpy(&table[106],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~HalfRefusal(){*reinterpret_cast<void***>(device)=previous;}
};
// Refuses A16B16G16R16F render-target textures of width `width` (and height `height` unless 0) while alive: the half-resolution
// row and box targets (S/2 x (S/2 + 1) and S/2 x S/2), or the box pair alone.
struct HalfTargetFault {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
    static inline Create original=nullptr;static inline unsigned refused=0,width=0,height=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
        if(format==D3DFMT_A16B16G16R16F&&(usage&D3DUSAGE_RENDERTARGET)&&w==width&&(!height||h==height)){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}
        return original(d,w,h,levels,usage,format,pool,out,shared);}
    HalfTargetFault(IDirect3DDevice9* d,UINT w,UINT hgt=0):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[23],sizeof original);auto fn=&hook;std::memcpy(&table[23],&fn,sizeof fn);refused=0;width=w;height=hgt;*reinterpret_cast<void***>(d)=table;}
    ~HalfTargetFault(){*reinterpret_cast<void***>(device)=previous;}
};
std::vector<float> read_target(IDirect3DDevice9* d,IDirect3DTexture9* t){UINT w=0,h=0;return read_fp16(d,t,w,h);}

// ---- BOX_HALF_STATE ----
void state_cases(EdgeScene& s,const DWORD* resolver){
    IDirect3DDevice9* const d=s.d;s.render(thin_objects(0),sentinelBackground,0,0);Output out;const FlickerConfig none{"box-half-validation",0,0,.1f,.5f,false,.9f};
    auto in=flicker_inputs(s,none,0,0,true);in.caller_scene_open=false;in.thin_region_weight=.97f;in.thin_region_camera_gate=true;
    // The setting: 1 or 2 only; 2 creates the pair once.
    TemporalPass pass;check("box half initialize",pass.initialize(d,nullptr,resolver));
    const bool settings=pass.configure_box_resolution(0)==E_INVALIDARG&&pass.configure_box_resolution(3)==E_INVALIDARG&&pass.box_resolution()==1&&SUCCEEDED(pass.configure_box_resolution(1))&&pass.box_resolution()==1;
    check("box half configure far",pass.configure_far());check("box half configure",pass.configure_box_resolution(2));check("box half configure is idempotent",pass.configure_box_resolution(2));
    // A refused program: the pass stays at full resolution, bit for bit a pass never configured (colour, age, box targets).
    bool refusedPath=false;
    {TemporalPass refused,plain;check("box half refusal initialize",refused.initialize(d,nullptr,resolver));check("box half plain initialize",plain.initialize(d,nullptr,resolver));
        check("box half refusal configure far",refused.configure_far());check("box half plain configure far",plain.configure_far());HRESULT created=S_OK;
        {HalfRefusal refusal(d);created=refused.configure_box_resolution(2);}
        bool same=true;
        for(unsigned n=0;n<3;++n){Output a,b;check("box half refused run",refused.run(in,&a));check("box half plain run",plain.run(in,&b));
            same=same&&!refused.diagnostics().box_half&&std::strcmp(refused.diagnostics().box_resolution_reason,"not_requested")==0&&read_target(d,a.color)==read_target(d,b.color)&&read_target(d,a.box_low)==read_target(d,b.box_low)&&read_target(d,a.box_high)==read_target(d,b.box_high)&&s.read(a.age)==s.read(b.age);}
        refusedPath=created==D3DERR_OUTOFVIDEOMEMORY&&HalfRefusal::refused==1&&refused.box_resolution()==1&&same;}
    // Half resolution: the box targets are S/2 x S/2; hostile state (c12 included) restored.
    check("box half run",pass.run(in,&out));UINT bw=0,bh=0;read_fp16(d,out.box_low,bw,bh);
    const bool halfRun=pass.diagnostics().box_half&&std::strcmp(pass.diagnostics().box_resolution_reason,"half")==0&&bw==S/2&&bh==S/2;
    in.sentinel_strength=.7f;check("box half configure sentinel",pass.configure_sentinel());
    const float junk[4]={9,8,7,6};for(UINT reg:{0u,4u,6u,11u,12u,22u,23u,24u})check("box half hostile constant",d->SetPixelShaderConstantF(reg,junk,1));
    for(UINT slot:{0u,1u,2u,3u,7u,8u,9u,10u})check("box half hostile texture",d->SetTexture(slot,s.wave.p));
    check("box half hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));{D3DVIEWPORT9 odd{1,2,7,5,0,1};check("box half hostile viewport",d->SetViewport(&odd));}
    {Snapshot before(d);check("box half hostile run",pass.run(in,&out));before.equals(d,"half-resolution box run restores c0..c7, c11, c12, c22..c24, samplers 0..3 and 7..10, RT1, COLORWRITEENABLE1 and the viewport");}
    const bool hostileHalf=pass.diagnostics().box_half;
    // Draws of a half run with the stabiliser: tests (1), rows (2), columns (3), resolve (4).
    bool faults=true;
    for(unsigned call:{2u,3u}){Output failed;{Fault fault(d,call);faults=faults&&pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid;}
        check("box half recovery",pass.run(in,&out));faults=faults&&out.color&&!out.used_history&&pass.diagnostics().box_half;}
    // Back to full and to half inside one history: the targets change size, the history is kept.
    check("box half continues",pass.run(in,&out));check("box half to full",pass.configure_box_resolution(1));check("box half full run",pass.run(in,&out));read_fp16(d,out.box_low,bw,bh);
    bool toggles=out.used_history&&!pass.diagnostics().box_half&&bw==S;
    check("box half to half",pass.configure_box_resolution(2));check("box half again",pass.run(in,&out));read_fp16(d,out.box_low,bw,bh);toggles=toggles&&out.used_history&&pass.diagnostics().box_half&&bw==S/2;
    // Refused half-resolution targets (not a lost device) after a Reset: that run and the next draw the full-resolution box
    // (reason "target", no retry), Reset re-arms.
    pass.before_reset();pass.after_reset(S_OK);bool targets=false;
    {HalfTargetFault fault(d,S/2);check("box half targets refused",pass.run(in,&out));read_fp16(d,out.box_low,bw,bh);
        targets=HalfTargetFault::refused>0&&pass.box_half_failed()&&pass.box_half_result()==D3DERR_OUTOFVIDEOMEMORY&&!pass.diagnostics().box_half&&std::strcmp(pass.diagnostics().box_resolution_reason,"target")==0&&pass.diagnostics().region_hold&&bw==S;
        const unsigned tries=HalfTargetFault::refused;check("box half after refused targets",pass.run(in,&out));targets=targets&&HalfTargetFault::refused==tries&&out.used_history&&!pass.diagnostics().box_half;}
    pass.before_reset();pass.after_reset(S_OK);check("box half re-armed",pass.run(in,&out));read_fp16(d,out.box_low,bw,bh);
    bool rearmed=pass.diagnostics().box_half&&!pass.box_half_failed()&&bw==S/2&&pass.box_resolution()==2;
    // The half-resolution box pair alone refused (its row pair created): the same fall back to the full-resolution box.
    pass.before_reset();pass.after_reset(S_OK);
    {HalfTargetFault fault(d,S/2,S/2);check("box half box pair refused",pass.run(in,&out));read_fp16(d,out.box_low,bw,bh);
        targets=targets&&HalfTargetFault::refused>0&&pass.box_half_failed()&&!pass.camera_gate_failed()&&!pass.diagnostics().box_half&&std::strcmp(pass.diagnostics().box_resolution_reason,"target")==0&&pass.diagnostics().region_hold&&bw==S;}
    pass.before_reset();pass.after_reset(S_OK);check("box half re-armed again",pass.run(in,&out));read_fp16(d,out.box_low,bw,bh);
    rearmed=rearmed&&pass.diagnostics().box_half&&!pass.box_half_failed()&&bw==S/2;
    // An odd frame size: that run draws the full-resolution box (the resolve's point read needs an even size).
    bool odd=false;
    {constexpr UINT O=S-1;Com<IDirect3DTexture9> colour,depth,motion;Com<IDirect3DSurface9> surface;
        check("odd colour",d->CreateTexture(O,O,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&colour.p,nullptr));
        check("odd depth",d->CreateTexture(O,O,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));
        check("odd motion",d->CreateTexture(O,O,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));
        for(IDirect3DTexture9* t:{colour.p,depth.p,motion.p}){Com<IDirect3DSurface9> level;check("odd level",t->GetSurfaceLevel(0,&level.p));s.target(level.p);check("odd clear",d->Clear(0,nullptr,D3DCLEAR_TARGET,t==colour.p?D3DCOLOR_ARGB(255,64,64,64):0,1,0));}
        s.target(s.colorSurface.p);
        TemporalPass oddPass;check("odd initialize",oddPass.initialize(d,nullptr,resolver));check("odd configure far",oddPass.configure_far());check("odd configure box",oddPass.configure_box_resolution(2));
        FrameInputs o=in;o.color=colour.p;o.current_depth=depth.p;o.motion=motion.p;o.width=o.height=O;o.sentinel_strength=0;
        check("odd run",oddPass.run(o,&out));read_fp16(d,out.box_low,bw,bh);
        odd=oddPass.diagnostics().region_hold&&!oddPass.diagnostics().box_half&&std::strcmp(oddPass.diagnostics().box_resolution_reason,"odd_size")==0&&bw==O;}
    std::printf("BOX_HALF_STATE settings=%u refused_program_full_identical=%u half_targets=%u hostile_half=%u failed_draws=%u toggles_keep_history=%u targets_refused_full=%u rearmed=%u odd_size_full=%u\n",
        unsigned(settings),unsigned(refusedPath),unsigned(halfRun),unsigned(hostileHalf),unsigned(faults),unsigned(toggles),unsigned(targets),unsigned(rearmed),unsigned(odd));
    ++numeric_checks;require(settings&&refusedPath,"box resolution: 1 or 2 only; a refused half-resolution program keeps the full-resolution box bit for bit");
    ++numeric_checks;require(halfRun&&hostileHalf&&faults&&toggles,"half-resolution box: S/2 targets, hostile state restored, failed rows / columns draws publish nothing, a switch keeps the history");
    ++numeric_checks;require(targets&&rearmed&&odd,"refused half-resolution targets and an odd frame size fall back to the full-resolution box (reasons target / odd_size); Reset re-arms");
    for(UINT slot:{0u,1u,2u,3u,4u,6u,7u,8u,9u,10u,11u,12u})check("box half unbind",d->SetTexture(slot,nullptr));
    s.target(s.colorSurface.p);
}

// ---- containment ----
struct Containment{unsigned frames=0,compared=0,violations=0,markerMissing=0,halfOnly=0,halfOnlyViolations=0,skipped=0;double worst=0;};
Containment containment(const FarRun& full,const FarRun& half){
    Containment c;constexpr UINT H=S/2;
    require(full.boxWidth==S&&half.boxWidth==H&&full.boxLow.size()==half.boxLow.size()&&full.boxLow.size()==full.current.size(),"containment: both runs recorded their box targets every frame");
    for(unsigned n=0;n<full.boxLow.size();++n){++c.frames;
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const std::size_t f=(std::size_t(y)*S+x)*4,h=(std::size_t(y>>1)*H+(x>>1))*4;
            if(!oracle_finite(px(full.current[n],x,y))){++c.skipped;continue;} // current-only there: the resolve reads no box
            const bool fullMarked=full.boxLow[n][f+3]>.5f,halfMarked=half.boxLow[n][h+3]>.5f;
            double lo[3],hi[3];bool reference=fullMarked;
            if(fullMarked){++c.compared;if(!halfMarked){++c.markerMissing;++c.violations;continue;}for(unsigned k=0;k<3;++k){lo[k]=full.boxLow[n][f+k];hi[k]=full.boxHigh[n][f+k];}}
            else if(halfMarked){++c.halfOnly;reference=true;for(unsigned k=0;k<3;++k){lo[k]=1e30;hi[k]=-1e30;}
                // The pixel's own 3x3 min / max of finite taps, weighed as the box programs weigh (the fixture scenes are grey: luma = value).
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const UINT tx=UINT(std::min(std::max(int(x)+dx,0),int(S)-1)),ty=UINT(std::min(std::max(int(y)+dy,0),int(S)-1));
                    for(unsigned k=0;k<3;++k){const double v=px(full.current[n],tx,ty,k);if(!oracle_finite(v))continue;const double q=float(oracle_weigh(v));lo[k]=std::min(lo[k],q);hi[k]=std::max(hi[k],q);}}}
            if(!reference)continue;
            bool bad=false;
            for(unsigned k=0;k<3;++k){if(lo[k]>hi[k])continue;const double hl=half.boxLow[n][h+k],hh=half.boxHigh[n][h+k];
                // Half-only pixels compare FP16 against the double weighing of the CPU: one FP16 step of slack there, none elsewhere.
                const double slack=fullMarked?0:std::ldexp(std::max(std::fabs(lo[k]),std::fabs(hi[k])),-10);
                if(!(hl<=lo[k]+slack)||!(hh>=hi[k]-slack)){bad=true;c.worst=std::max({c.worst,hl-lo[k],hi[k]-hh});}}
            if(bad){if(fullMarked)++c.violations;else ++c.halfOnlyViolations;}}}
    return c;}
void print(const char* scene,const Containment& c,bool sameOutput){
    std::printf("BOX_HALF_CONTAINMENT scene=%s frames=%u compared_px=%u violations=%u marker_missing=%u half_only_px=%u half_only_violations=%u nonfinite_skipped=%u worst=%.6f output_identical=%u\n",
        scene,c.frames,c.compared,c.violations,c.markerMissing,c.halfOnly,c.halfOnlyViolations,c.skipped,c.worst,unsigned(sameOutput));}
FarRun run_at(EdgeScene& s,const DWORD* resolver,const LineConfig& c,unsigned frames,unsigned divisor){thinBoxDivisor=divisor;thinRecordBoxes=true;FarRun r=thin_sequence(s,resolver,c,frames);thinBoxDivisor=1;thinRecordBoxes=false;return r;}
// Runs the scene at both resolutions, checks containment (one numerical check) and returns the pair. opens: the scene opens the
// box somewhere (a pan, or the sentinel stabiliser); a static scene without the stabiliser opens it nowhere at either resolution,
// and then the two runs must be the same bit for bit.
std::pair<FarRun,FarRun> contained(EdgeScene& s,const DWORD* resolver,const LineConfig& c,unsigned frames,const char* scene,bool opens=true){
    auto full=run_at(s,resolver,c,frames,1),half=run_at(s,resolver,c,frames,2);const Containment k=containment(full,half);
    const bool same=full.output==half.output&&full.age==half.age;print(scene,k,same);
    ++numeric_checks;require(k.violations==0&&k.halfOnlyViolations==0&&(opens?k.compared>0:k.compared==0&&k.halfOnly==0&&same),
        "half-resolution box contains the full-resolution box at every pixel and frame (and the 3x3 where only it runs); where neither opens the runs are identical");
    return {std::move(full),std::move(half)};}
void oracle_row(const char* scene,const FarRun& half,const LineConfig& c,unsigned from=0){oracleBoxHalf=true;const Oracle o=oracle(half,c,from);const double maskError=hold_tests_error(half);oracleBoxHalf=false;
    std::printf("BOX_HALF_ORACLE scene=%s oracle_error=%.6f age_oracle_error=%.6f tests_mask_error=%.6f oracle_skipped_px=%u\n",scene,o.colour,o.age,maskError,oracleSkipped);
    metric("half-resolution box: shader matches the 2-D CPU oracle with the block box within the FP16 bound",o.colour,0,.0006/(1-.97));metric("half-resolution box: age matches the oracle",o.age,0,0);}

void rows(EdgeScene& s,const DWORD* resolver){
    const LineConfig base{"thin-base",0,0},on97{"thin-region-0.97",0,0,0,0,.97f,1},hold97{"thin-region-0.97-camera-gate-hold",0,0,0,0,.97f,1,true};
    // Arm scene at rest and drifting: containment, oracle, rest ripple against the installed resolve (the hold rows' bound).
    for(double drift:{0.,.3}){thinDrift=drift;thinMoveFrom=~0u;const auto pair=contained(s,resolver,hold97,thinFrames,drift==0?"arm_rest":"arm_drift_0.30",false);
        oracle_row(drift==0?"arm_rest":"arm_drift_0.30",pair.second,hold97);
        if(drift==0){const auto baseRun=thin_sequence(s,resolver,base,thinFrames);double baseRms=0,baseP2p=0,fullRms=0,fullP2p=0,rms=0,p2p=0;thin_ripple(baseRun,baseRms,baseP2p);thin_ripple(pair.first,fullRms,fullP2p);thin_ripple(pair.second,rms,p2p);
            unsigned squareDiffers=0;for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=20;x+3<S;++x)squareDiffers+=std::memcmp(&pair.second.output[n][(y*S+x)*4],&baseRun.output[n][(y*S+x)*4],4*sizeof(float))!=0;
            std::printf("BOX_HALF_RIPPLE scene=arm_rest base_rms_codes=%.3f full_rms_codes=%.3f half_rms_codes=%.3f base_p2p_codes=%.1f full_p2p_codes=%.1f half_p2p_codes=%.1f half_over_full_rms=%.4f square_differs=%u\n",
                255*baseRms,255*fullRms,255*rms,255*baseP2p,255*fullP2p,255*p2p,rms/fullRms,squareDiffers);
            ++numeric_checks;require(rms<=.35*baseRms&&p2p<=.5*baseP2p&&squareDiffers==0,"half-resolution box, static shards: ripple <= 0.35 x and peak-to-peak <= 0.5 x the installed resolve; the plain silhouette bit-identical");}}
    thinDrift=0;
    // Pan 0.5 px/frame: containment, oracle; the stale patch with the bright mover; the box domain (k = 0.5, a non-finite tap).
    {thinPanX=cameraPanX=.5;const auto pair=contained(s,resolver,hold97,thinFrames,"pan_0.50");oracle_row("pan_0.50",pair.second,hold97);
        const auto screenRun=thin_sequence(s,resolver,on97,thinFrames);
        thinPatchV=2;thinPatchFrom=40;thinInjectFrame=oracleInjectFrame=100;std::copy(injectRect,injectRect+4,oracleInjectRect);oracleInjectValue=patchValue;
        const auto screenPatch=thin_sequence(s,resolver,on97,thinFrames);const auto patchPair=contained(s,resolver,hold97,thinFrames,"pan_stale_patch");
        double added[3]={0,0,0},excess=0;const FarRun* runs[3]={&screenPatch,&patchPair.first,&patchPair.second};const FarRun* clean[3]={&screenRun,&pair.first,&pair.second};
        for(unsigned n=61;n<100;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x)excess=std::max(excess,double(px(patchPair.second.output[n],x,y))-1);
        for(unsigned which=0;which<3;++which)for(int y=injectRect[1]-1;y<=injectRect[3];++y)for(int x=injectRect[0]-1;x<=injectRect[2];++x)added[which]=std::max(added[which],double(px(runs[which]->output[101],UINT(x),UINT(y))-px(clean[which]->output[101],UINT(x),UINT(y))));
        oracle_row("pan_stale_patch",patchPair.second,hold97,101);
        std::printf("BOX_HALF_STALE pan=0.50 mover_px_per_frame=2 excess_after_mover=%.6f screen_added_frame101=%.6f full_added_frame101=%.6f half_added_frame101=%.6f half_over_screen=%.4f half_over_full=%.4f unbounded_estimate=%.4f\n",
            excess,added[0],added[1],added[2],added[2]/std::max(added[0],1e-9),added[2]/std::max(added[1],1e-9),.97*(patchValue-.25));
        metric("half-resolution box, pan + bright mover: no ghost above the scene's maximum after the mover left",excess,0,2./255);
        ++numeric_checks;require(added[0]>.1&&added[2]<=2*added[0]&&added[2]<.5*.97*(patchValue-.25),"half-resolution box, stale patch: at most 2 x the screen gate's clipped value, well below the clip-off estimate");
        thinPatchV=0;thinPatchFrom=~0u;thinInjectFrame=oracleInjectFrame=~0u;
        {thinBadTap=true;thinK=.5f;oracleK=.5;const auto bad=contained(s,resolver,hold97,thinFrames,"pan_box_domain_k0.5_nonfinite");oracle_row("pan_box_domain_k0.5_nonfinite",bad.second,hold97);thinBadTap=false;thinK=0;oracleK=0;}
        thinPanX=cameraPanX=0;}
    // Stop after a pan (the hold rows' bound: step rms at most 0.15 x the plain resolve's).
    {thinPanX=cameraPanX=.5;thinPanStop=64;constexpr unsigned frames=96;const auto baseRun=thin_sequence(s,resolver,base,frames);const auto pair=contained(s,resolver,hold97,frames,"pan_stop");
        auto step=[&](const FarRun& r,unsigned from,unsigned to){double sum=0;unsigned count=0;for(unsigned n=from;n<to;++n)for(UINT y=5;y<27;++y)for(UINT x=18;x<28;++x){const double e=double(px(r.output[n],x,y))-double(px(r.output[n-1],x,y));sum+=e*e;++count;}return 255*std::sqrt(sum/count);};
        const double half8=step(pair.second,65,73),base8=step(baseRun,65,73),half24=step(pair.second,73,frames),base24=step(baseRun,73,frames),full8=step(pair.first,65,73),full24=step(pair.first,73,frames);
        std::printf("BOX_HALF_PAN_STOP pan=0.50 stop_frame=64 half_first8=%.3f full_first8=%.3f base_first8=%.3f half_next24=%.3f full_next24=%.3f base_next24=%.3f\n",half8,full8,base8,half24,full24,base24);
        ++numeric_checks;require(half8<=.15*base8&&half24<=.15*base24,"half-resolution box, stop after a pan: step rms at most 0.15 x the plain resolve's, the 8 frames after the stop and the 24 after");
        thinPanStop=~0u;thinPanX=cameraPanX=0;}
    // Sentinel facets (S = 0.7) at rest and under a reversing vertical 2 px/frame pan: containment, oracle, flicker < 0.6 x S = 0.
    {thinSentinel=true;LineConfig holdOff=hold97,holdOn=hold97;holdOff.name="sentinel-0-hold";holdOn.name="sentinel-0.7-hold";holdOn.sentS=.7f;
        struct PanCase{const char* mode;double pan;bool alternating;unsigned lag;};const PanCase pans[]={{"rest",0,false,1},{"reversing",2,true,2}};
        for(const PanCase& pc:pans){cameraPanVertical=pc.alternating;cameraPanAlternates=pc.alternating;cameraPanSpeed=pc.pan;cameraPanY=0;oracleSkipCeiling=pc.pan<1?0:unsigned(std::ceil(pc.pan)+1)*(S-6)*thinFrames;
            const std::string scene=std::string("sentinel_")+pc.mode;const auto baseRun=thin_sequence(s,resolver,holdOff,thinFrames);const auto pair=contained(s,resolver,holdOn,thinFrames,scene.c_str());oracle_row(scene.c_str(),pair.second,holdOn);
            auto flicker=[&](const FarRun& r){double sum=0;unsigned count=0;for(unsigned n=thinFrames-thinAnalysed;n<thinFrames;++n)for(UINT y=8;y<28;++y)for(UINT x=4;x<28;++x){const double e=double(px(r.output[n],x,y))-double(px(r.output[n-pc.lag],x,y));sum+=e*e;++count;}return std::sqrt(sum/count);};
            const double baseRms=flicker(baseRun),fullRms=flicker(pair.first),rms=flicker(pair.second);
            std::printf("BOX_HALF_SENTINEL pan=%.2f pan_mode=%s strength=0.70 base_flicker_codes=%.3f full_flicker_codes=%.3f half_flicker_codes=%.3f half_flicker_ratio=%.4f full_flicker_ratio=%.4f\n",pc.pan,pc.mode,255*baseRms,255*fullRms,255*rms,rms/baseRms,fullRms/baseRms);
            ++numeric_checks;require(rms<.6*baseRms,"half-resolution box, sentinel facets: flicker below 0.6 x the S = 0 hold run");}
        cameraPanVertical=cameraPanAlternates=false;cameraPanSpeed=cameraPanY=0;oracleSkipCeiling=0;thinSentinel=false;}
    // An emitter (value 4, 8 px wide) crossing the sky at 6 px/frame, camera at rest, S = 0.7 with E = 1: containment and the
    // trail behind the bar at both resolutions (trail = columns off the bar more than 0.01 above the sky, per row, worst frame).
    {thinSentinel=true;sentinelFacets=false;sentinelBarFrom=16;constexpr unsigned frames=16+10;LineConfig holdOn=hold97;holdOn.name="emitter-0.7-hold";holdOn.sentS=.7f;
        const auto pair=contained(s,resolver,holdOn,frames,"sentinel_emitter");unsigned trail[2]{},after[2]{};double reach[2]{};
        for(unsigned which=0;which<2;++which){const FarRun& run=which?pair.second:pair.first;
            for(unsigned n=sentinelBarFrom;n<frames;++n){const double l=-8+sentinelBarV*(n-sentinelBarFrom);for(UINT y=8;y<24;++y){unsigned columns=0;
                for(UINT x=0;x<S;++x){const double excess=double(px(run.output[n],x,y))-.25;const bool bar=px(run.current[n],x,y)>.3f;
                    if(l>=S){after[which]+=excess!=0;continue;}
                    if(!bar&&excess>.01){++columns;reach[which]=std::max(reach[which],double(x)<l?l-double(x):double(x)-(l+7));}}
                trail[which]=std::max(trail[which],columns);}}}
        std::printf("BOX_HALF_EMITTER bar_value=%.1f px_per_frame=%.0f trail_px_full=%u trail_px_half=%u reach_px_full=%.0f reach_px_half=%.0f nonzero_px_after_exit_full=%u nonzero_px_after_exit_half=%u\n",
            double(sentinelBarValue),sentinelBarV,trail[0],trail[1],reach[0],reach[1],after[0],after[1]);
        ++numeric_checks;require(trail[1]<=2&&reach[1]<=2&&after[1]==0,"half-resolution box, emitter over the sky: trail within 2 px (the block's inner 4x4; 1 px at full resolution), none after the bar has left");
        oracle_row("sentinel_emitter",pair.second,holdOn);
        // The same bar crossing the props (a geometry square and a routed glass quad): blocks that straddle the square's
        // silhouette next to the bar take the 8x8 of every tap. Containment only: the CPU oracle does not model a colour-only
        // bar over routed geometry (the full-resolution run misses it by the same 3.06 / 25).
        sentinelProps=true;const auto props=contained(s,resolver,holdOn,frames,"sentinel_emitter_silhouette");sentinelProps=false;
        sentinelFacets=true;sentinelBarFrom=~0u;thinSentinel=false;}
    // A tap whose luma is just above E in FP32 (about 1.0002 for E = 1) but E after the FP16 rounding the full-resolution rows
    // apply: dim at full resolution, so it must not be bright at half resolution (containment only: the oracle's grey model
    // reads one channel).
    {thinSentinel=true;sentinelFacets=false;thinTintTap=true;LineConfig holdOn=hold97;holdOn.name="tint-0.7-hold";holdOn.sentS=.7f;
        contained(s,resolver,holdOn,32,"sentinel_tap_above_e_fp32");thinTintTap=false;sentinelFacets=true;thinSentinel=false;}
    // A non-finite 3x3 block beside a static bright bar (the fail-safe of an empty box; the pixels there are current-only).
    {thinSentinel=true;sentinelFacets=false;sentinelBadBlock=true;LineConfig holdOn=hold97;holdOn.name="nonfinite-0.7-hold";holdOn.sentS=.7f;
        const auto pair=contained(s,resolver,holdOn,32,"sentinel_nonfinite_block");oracle_row("sentinel_nonfinite_block",pair.second,holdOn);
        sentinelBadBlock=false;sentinelFacets=true;thinSentinel=false;}
}

// ---- BOX_HALF_TIMING: the box stage alone at both resolutions, 1280x768 ----
// The pass's TaaBox boundaries: drained (event query) at both, the wall clock between summed. A failed drain inside the pass
// (noexcept) is counted and fails the row afterwards.
struct BoxMarks final : x3m::gpu_sync_timing::Marks {
    IDirect3DQuery9* query=nullptr;LARGE_INTEGER frequency{};double ms=0;std::int64_t start=0;unsigned failures=0;
    static std::int64_t now(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart;}
    bool wait() noexcept {if(FAILED(query->Issue(D3DISSUE_END)))return false;const auto t0=now();HRESULT hr;while((hr=query->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){if(now()-t0>frequency.QuadPart*10)return false;Sleep(0);}return SUCCEEDED(hr);}
    void drain(){if(!wait())throw std::runtime_error("box timing drain");}
    void begin(unsigned pass) noexcept override {if(pass==x3m::gpu_sync_timing::TaaBox){failures+=!wait();start=now();}}
    void end(unsigned pass) noexcept override {if(pass==x3m::gpu_sync_timing::TaaBox&&start){failures+=!wait();ms+=1000.*double(now()-start)/double(frequency.QuadPart);start=0;}}
};
// W x H: 1280x768 (the fixture's usual timing size) and 5120x1440 (the target resolution; the fixture's clock there too, reported
// beside the flown figures, never in place of them).
void timing(IDirect3DDevice9* d,EdgeScene& s,const DWORD* resolver,const UINT W,const UINT H){
    Com<IDirect3DQuery9> query;check("box timing query",d->CreateQuery(D3DQUERYTYPE_EVENT,&query.p));
    Com<IDirect3DTexture9> color,depth,motion;Com<IDirect3DSurface9> saved;check("box timing save",d->GetRenderTarget(0,&saved.p));D3DVIEWPORT9 vp{};check("box timing viewport",d->GetViewport(&vp));
    check("box timing color",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));
    check("box timing depth",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));
    check("box timing motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));
    for(UINT n=0;n<20;++n)check("box timing unbind",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
    // The all-unrouted-sentinel frame of LINE_TIMING_SENTINEL: depth -1, motion alpha -1, a grey sky; the stabiliser opens the box everywhere.
    struct V{float x,y,z,rhw,u,v;};const V quad[]={{-.5f,-.5f,.5f,1,0,0},{W-.5f,-.5f,.5f,1,1,0},{-.5f,H-.5f,.5f,1,0,1},{W-.5f,H-.5f,.5f,1,1,1}};D3DVIEWPORT9 full{0,0,W,H,0,1};
    const float fills[3][4]={{.25f,.25f,.25f,1},{-1,0,0,0},{0,0,0,-1}};IDirect3DTexture9* targets[3]={color.p,depth.p,motion.p};
    for(unsigned i=0;i<3;++i){Com<IDirect3DSurface9> level;check("box timing level",targets[i]->GetSurfaceLevel(0,&level.p));s.target(level.p);check("box timing fill viewport",d->SetViewport(&full));check("box timing Begin",d->BeginScene());
        check("box timing flat",d->SetPixelShader(s.flat.p));s.constant(fills[i][0],fills[i][1],fills[i][2],fills[i][3]);check("box timing fill",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));check("box timing End",d->EndScene());}
    check("box timing restore",d->SetRenderTarget(0,saved.p));check("box timing restore viewport",d->SetViewport(&vp));
    FrameInputs in{};in.color=color.p;in.current_depth=depth.p;in.motion=motion.p;in.width=W;in.height=H;in.epoch=1;in.weight=.9f;std::copy(identity,identity+16,in.clip_to_previous);in.clip_to_previous[3]=-2.f/W;
    in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=true;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=false;
    in.thin_region_weight=.97f;in.thin_region_camera_gate=true;in.sentinel_strength=.7f;in.sentinel_emitter=1;
    TemporalPass passes[2];BoxMarks marks[2];double total[2]{};Output out;
    for(unsigned i=0;i<2;++i){check("box timing initialize",passes[i].initialize(d,nullptr,resolver));check("box timing configure far",passes[i].configure_far());check("box timing configure sentinel",passes[i].configure_sentinel());
        check("box timing resolution",passes[i].configure_box_resolution(i?2:1));marks[i].query=query.p;QueryPerformanceFrequency(&marks[i].frequency);}
    for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<2;++which){check("box timing warm",passes[which].run(in,&out));marks[which].drain();}
    for(unsigned which=0;which<2;++which){passes[which].configure_sync_timing(&marks[which]);marks[which].ms=0;}
    constexpr unsigned rounds=8;
    for(unsigned round=0;round<rounds;++round)for(unsigned step=0;step<2;++step){const unsigned which=(round+step)%2;marks[which].drain();const auto t0=BoxMarks::now();check("box timing run",passes[which].run(in,&out));marks[which].drain();
        total[which]+=1000.*double(BoxMarks::now()-t0)/double(marks[which].frequency.QuadPart)/rounds;require(passes[which].diagnostics().box_half==(which==1)&&passes[which].diagnostics().region_hold,"box timing: the configured resolution drew");}
    for(unsigned which=0;which<2;++which)passes[which].configure_sync_timing(nullptr);
    require(marks[0].failures==0&&marks[1].failures==0&&marks[0].ms>0&&marks[1].ms>0,"box timing: every TaaBox boundary drained");
    std::printf("BOX_HALF_TIMING width=%u height=%u rounds=%u content=all_unrouted_sentinel_pan box_full_ms=%.4f box_half_ms=%.4f box_delta_ms=%.4f run_full_ms=%.4f run_half_ms=%.4f scope=cpu_wall_with_event_query_drain_fixture\n",
        W,H,rounds,marks[0].ms/rounds,marks[1].ms/rounds,marks[1].ms/rounds-marks[0].ms/rounds,total[0],total[1]);
    check("box timing restore target",d->SetRenderTarget(0,saved.p));check("box timing restore vp",d->SetViewport(&vp));
}
} // namespace box_half
void box_half_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("BOX_HALF_CASES");EdgeScene s(d,compiler);
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    struct Hooks{Hooks(){line_velocity=thin_velocity;line_velocity_x=thin_velocity_x;farD0=.98f;farInv=200;}
        ~Hooks(){line_velocity=line_velocity_default;line_velocity_x=line_velocity_x_default;thinDrift=0;thinMoveFrom=~0u;thinBadTap=false;thinK=0;oracleK=0;thinPanX=cameraPanX=thinPatchV=0;thinPatchFrom=thinInjectFrame=oracleInjectFrame=~0u;farD0=farInv=0;
            cameraPanAlternates=cameraPanVertical=false;cameraPanSpeed=cameraPanY=0;thinSentinel=false;sentinelFacets=true;sentinelBarFrom=~0u;sentinelProps=false;sentinelBadBlock=false;oracleSkipCeiling=0;thinPanStop=~0u;armPanX=0;thinBoxDivisor=1;thinRecordBoxes=false;oracleBoxHalf=false;thinTintTap=false;}} hooks;
    box_half::state_cases(s,resolver);
    box_half::rows(s,resolver);
    box_half::timing(d,s,resolver,1280,768);box_half::timing(d,s,resolver,5120,1440);
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
