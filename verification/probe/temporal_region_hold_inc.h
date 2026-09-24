// A' (docs/architecture/taa-plan-lifted-slot-cap.md step 1), lattice mode: the camera gate's only path since the dilated camera
// chain and --taa-region-hold were removed (2026-09-24). Included after temporal_thin_region_inc.h: uses thin_sequence /
// thin_objects / thin_ripple, hold_tests_error, line_model (the camera gate's holds), fragmented, quantise8 and the scene hooks.
//   REGION_HOLD_IDENTITY: the hold program with holds reading 0 (whole age counts in the previous age target) against the
//   camera program of the removed dilated chain (compiled here from resolve.hlsl with X3M_CAMERA_GATE / X3M_FAR_STABILIZE,
//   checked byte-identical to the removed embedded words) on the per-pixel composition of the same tests texels (what its y
//   draw wrote with an 11x11 / 17x17 window of one pixel, the two openness values taken as the smaller of this pixel's and its
//   nearest-depth 3x3 neighbour's, the resolve's own dilation, replicated on the CPU), drawn directly on synthetic inputs
//   (rest and fractional motion, routed / unrouted / sentinel pixels, every class code, camera openness above screen
//   openness, k = 0 and 0.5, S = 0 and 1): colour bytes identical, and the age equals the reference's count plus the hold
//   fraction the CPU encodes from the tests texel.
//   REGION_HOLD_STATE: refusals, the camera-gate programs refused at creation (no camera-gate path; the far program stays),
//   hostile state (c11 included), a failed box draw, Reset, 16 taps (a camera-gate run refused), the history restart when a
//   run leaves the camera gate, and the mask targets (one on a camera-gate run, two on the screen gate).
//   THIN_REGION_HOLD / _MOTION_START / _PAN / _STALE / _SENTINEL: the thin-region rows of section 13 / 32 with the hold, against
//   the CPU oracle extended by the holds (line_model with the camera gate, fed the published tests target of every frame).
//   THIN_REGION_HOLD_FADE_OWNER (docs/architecture/fade-rt2-ownership.md): a far routed square switching sentinel -> valid once.
namespace region_hold {
constexpr UINT S=EdgeScene::S;
struct Oracle{double colour=0,age=0;};
Oracle oracle(const FarRun& run,const LineConfig& c,unsigned from=0){const auto model=line_model(run,c,&run.mask);Oracle o;
    for(unsigned n=from;n<run.output.size();++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){o.colour=std::max(o.colour,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));
        if(!run.age.empty())o.age=std::max(o.age,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));}
    return o;}

// ---- REGION_HOLD_IDENTITY ----
// The camera program of the removed dilated chain (resolve_far_camera.hlsl: the two defines and resolve.hlsl, no production
// program since 2026-09-24): word count and FNV-1a 64 of its embedded words at 5a4bbd52 (bytecode_sha256 773ab6cc2a3a1741...).
constexpr std::size_t removedCameraWords=2196;constexpr std::uint64_t removedCameraFnv=0x457159f1f8e5c6b3ull;
std::uint64_t fnv1a(const unsigned char* p,std::size_t n){std::uint64_t h=0xcbf29ce484222325ull;for(std::size_t i=0;i<n;++i){h^=p[i];h*=0x100000001b3ull;}return h;}
void identity_cases(EdgeScene& helper,Compiler compiler){IDirect3DDevice9* const d=helper.d;
    namespace r=x3m::renderer;
    Com<IDirect3DPixelShader9> held,composed;
    check("hold identity program",d->CreatePixelShader(reinterpret_cast<const DWORD*>(r::temporal_resolve_far_camera_hold_program()),&held.p));
    {Com<ID3DXBuffer> code;compile(compiler,"#define X3M_CAMERA_GATE 1\n#define X3M_FAR_STABILIZE 1\n"+resolveSource,"ps_3_0",&code.p);
        const std::size_t words=code->GetBufferSize()/sizeof(DWORD);const std::uint64_t hash=fnv1a(static_cast<const unsigned char*>(code->GetBufferPointer()),code->GetBufferSize());
        std::printf("REGION_HOLD_IDENTITY_REFERENCE words=%zu fnv1a=%016llx removed_words=%zu removed_fnv1a=%016llx identical=%u\n",words,(unsigned long long)hash,removedCameraWords,(unsigned long long)removedCameraFnv,unsigned(words==removedCameraWords&&hash==removedCameraFnv));
        ++numeric_checks;require(words==removedCameraWords&&hash==removedCameraFnv,"hold identity: the reference compiled from resolve.hlsl is the removed camera program word for word");
        check("hold identity reference program",d->CreatePixelShader(static_cast<DWORD*>(code->GetBufferPointer()),&composed.p));}
    enum{Current,Depth,Previous,PreviousDepth,Motion,Age,Tests,Composed,BoxLow,BoxHigh,Inputs};
    const D3DFORMAT formats[Inputs]={D3DFMT_A16B16G16R16F,D3DFMT_R32F,D3DFMT_A16B16G16R16F,D3DFMT_R32F,D3DFMT_A32B32G32R32F,D3DFMT_R32F,D3DFMT_A8R8G8B8,D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F,D3DFMT_A16B16G16R16F};
    Com<IDirect3DTexture9> input[Inputs];
    for(unsigned i=0;i<Inputs;++i)check("hold identity input",d->CreateTexture(S,S,1,0,formats[i],D3DPOOL_MANAGED,&input[i].p,nullptr));
    std::uint32_t state=0x9e3779b9u;auto next=[&](){state=state*1664525u+1013904223u;return state>>8;};auto unit=[&](){return double(next()%65536)/65536.;};
    const double ju=.137/S,jv=-.211/S;
    std::vector<float> depth(S*S),currentColour(S*S*4),previousColour(S*S*4),motion(S*S*4),age(S*S),boxLow(S*S*4),boxHigh(S*S*4);std::vector<unsigned char> tests(S*S*4);
    unsigned kinds[4]{}; // unrouted sentinel, routed at rest, routed moving, unrouted geometry
    for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const UINT i=y*S+x;const unsigned kind=next()%8;
        const float dv=kind==0?-1.f:float(.95+.001*(next()%40));depth[i]=dv;
        const double ox=kind>=3&&kind<=5?(double(next()%7)-3)*.13:0,oy=kind>=3&&kind<=5?(double(next()%5)-2)*.11:0;
        motion[i*4+0]=float((x+.5+ox)/S-ju);motion[i*4+1]=float((y+.5+oy)/S-jv);motion[i*4+2]=dv;motion[i*4+3]=kind==0?-1.f:kind<=5?1.f:0.f;
        ++kinds[kind==0?0:kind<=2?1:kind<=5?2:3];
        for(unsigned c=0;c<3;++c){currentColour[i*4+c]=float(.05+2.95*unit());previousColour[i*4+c]=float(.05+2.95*unit());const double low=.02+unit();boxLow[i*4+c]=float(low);boxHigh[i*4+c]=float(low+2*unit());}
        currentColour[i*4+3]=1;previousColour[i*4+3]=float(unit());boxLow[i*4+3]=boxHigh[i*4+3]=1;
        const unsigned count=1+next()%64;age[i]=next()%8==0?-float(count):float(count);
        // Two classes of tests texel: equal openness (a = r <= 100/255: no camera term) or the camera term open (a = 1, r >= 101/255).
        // A pixel of the first class then never takes a smaller screen value from its neighbour than its camera value, so the
        // box programs' own-texel gate (a > r) covers every pixel where the composition's b exceeds its a.
        const bool cameraOpen=next()%2;const unsigned rk=cameraOpen?101+next()%154:next()%101,ak=cameraOpen?255:rk,codes[4]={0,1,254,255};
        tests[i*4+0]=static_cast<unsigned char>(rk);tests[i*4+1]=static_cast<unsigned char>(next()%256);tests[i*4+2]=static_cast<unsigned char>(codes[next()%4]);tests[i*4+3]=static_cast<unsigned char>(ak);}
    auto upload=[&](unsigned which,const void* data,UINT bytesPerTexel,bool half){D3DLOCKED_RECT lock{};check("hold identity lock",input[which]->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<S;++y){char* row=static_cast<char*>(lock.pBits)+y*lock.Pitch;
            if(half){const float* f=static_cast<const float*>(data)+y*S*4;for(UINT x=0;x<S;++x)for(UINT c=0;c<4;++c){const unsigned short h=toHalf(f[x*4+c]);std::memcpy(row+x*8+c*2,&h,2);}}
            else std::memcpy(row,static_cast<const char*>(data)+y*S*bytesPerTexel,S*bytesPerTexel);}
        check("hold identity unlock",input[which]->UnlockRect(0));};
    // A8R8G8B8 memory order is B, G, R, A.
    auto bgra=[](const std::vector<unsigned char>& rgba){std::vector<unsigned char> out(rgba.size());for(size_t i=0;i<rgba.size();i+=4){out[i]=rgba[i+2];out[i+1]=rgba[i+1];out[i+2]=rgba[i];out[i+3]=rgba[i+3];}return out;};
    upload(Current,currentColour.data(),8,true);upload(Depth,depth.data(),4,false);upload(Previous,previousColour.data(),8,true);upload(PreviousDepth,depth.data(),4,false);
    upload(Motion,motion.data(),16,false);upload(Age,age.data(),4,false);{const auto t=bgra(tests);upload(Tests,t.data(),4,false);}upload(BoxLow,boxLow.data(),8,true);upload(BoxHigh,boxHigh.data(),8,true);
    Com<IDirect3DTexture9> colourOut[2],ageOut[2];Com<IDirect3DSurface9> colourSurface[2],ageSurface[2];
    for(unsigned k=0;k<2;++k){check("hold identity colour target",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&colourOut[k].p,nullptr));check("hold identity colour surface",colourOut[k]->GetSurfaceLevel(0,&colourSurface[k].p));
        check("hold identity age target",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&ageOut[k].p,nullptr));check("hold identity age surface",ageOut[k]->GetSurfaceLevel(0,&ageSurface[k].p));}
    // The resolve's closest-depth dilation (clamped addressing; the centre's depth, or 1 on the sentinel, wins ties; a neighbour
    // must be valid and strictly closer, scanned row by row): the texel whose tests values join this pixel's own.
    std::vector<UINT> nearIndex(S*S);
    for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const UINT i=y*S+x;float nearest=depth[i]<=-.5f?1.f:depth[i];UINT at=i;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const UINT qx=UINT(std::min(std::max(int(x)+dx,0),int(S)-1)),qy=UINT(std::min(std::max(int(y)+dy,0),int(S)-1));const float q=depth[qy*S+qx];
            if(q>=0&&q<nearest){nearest=q;at=qy*S+qx;}}
        nearIndex[i]=at;}
    auto ownS=[&](UINT i){return std::min(tests[i*4],tests[nearIndex[i]*4]);};auto ownC=[&](UINT i){return std::min(tests[i*4+3],tests[nearIndex[i]*4+3]);};
    for(const float k:{0.f,.5f})for(const float strength:{0.f,1.f}){
        // The per-pixel composition, as the y draw writes it with a one-pixel window (camera program; c5.zw = 0, 1: far weight on, filter off).
        std::vector<unsigned char> composedMask(S*S*4);
        for(UINT i=0;i<S*S;++i){const double rt=ownS(i)/255.,gt=tests[i*4+1]/255.,at=ownC(i)/255.;const unsigned code=tests[i*4+2];const bool flag=code>=254,cls=code==1||code==255;
            double b=flag?at:0;if(strength>0&&cls)b=std::max(b,double(strength)*at);
            composedMask[i*4+0]=0;composedMask[i*4+1]=static_cast<unsigned char>(std::lround(gt*255));composedMask[i*4+2]=static_cast<unsigned char>(std::lround(b*255));composedMask[i*4+3]=static_cast<unsigned char>(std::lround((flag?rt:0)*255));}
        {const auto t=bgra(composedMask);upload(Composed,t.data(),4,false);}
        const float c0[8][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1},{1.f/S,1.f/S,float(ju),float(jv)},{.97f,9,.9f,1},{.0001f,.02f,65000,.000001f},{1,0,0,2}};
        const float c22[4]={k,0,0,0},c24[8]={1,.985f,farLo,1.f/(farHi-farLo),1e30f,0,1,1},c11[4]={strength,0,1,float(oracleHoldFrames)};
        for(unsigned which=0;which<2;++which){ // 0 the hold program, 1 the camera program on the composed mask
            helper.target(colourSurface[which].p);check("hold identity RT1",d->SetRenderTarget(1,ageSurface[which].p));check("hold identity CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
            for(UINT slot=0;slot<13;++slot){for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,slot==11?D3DTEXF_LINEAR:D3DTEXF_POINT},{D3DSAMP_MAGFILTER,slot==11?D3DTEXF_LINEAR:D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},
                    {D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})check("hold identity sampler",d->SetSamplerState(slot,p.first,p.second));}
            IDirect3DTexture9* const bind[13]={input[Current].p,input[Depth].p,input[Previous].p,input[PreviousDepth].p,input[Motion].p,nullptr,nullptr,input[Age].p,which?input[Composed].p:input[Tests].p,input[BoxLow].p,input[BoxHigh].p,input[Previous].p,nullptr};
            for(UINT slot=0;slot<13;++slot)check("hold identity texture",d->SetTexture(slot,bind[slot]));
            check("hold identity constants",d->SetPixelShaderConstantF(0,&c0[0][0],8));check("hold identity c22",d->SetPixelShaderConstantF(22,c22,1));check("hold identity c24",d->SetPixelShaderConstantF(24,c24,2));check("hold identity c11",d->SetPixelShaderConstantF(11,c11,1));
            check("hold identity PS",d->SetPixelShader(which?composed.p:held.p));
            check("hold identity Begin",d->BeginScene());helper.quad(0,0,S,S,0,0);check("hold identity End",d->EndScene());
            check("hold identity unbind RT1",d->SetRenderTarget(1,nullptr));}
        for(UINT slot=0;slot<13;++slot)check("hold identity unbind",d->SetTexture(slot,nullptr));
        const auto heldColour=helper.read(colourOut[0].p),refColour=helper.read(colourOut[1].p),heldAge=helper.read(ageOut[0].p),refAge=helper.read(ageOut[1].p);
        unsigned colourDiffers=0,ageDiffers=0,blended=0,countMismatch=0;
        for(UINT i=0;i<S*S;++i){const unsigned code=tests[i*4+2];const float rt=ownS(i)/255.f,at=ownC(i)/255.f;
            if(std::memcmp(&heldColour[i*4],&refColour[i*4],4*sizeof(float))!=0)++colourDiffers;
            const double fraction=fresh_code(code>=254,at),ref=refAge[i*4];(void)rt;const float expected=float(ref>=0?ref+fraction:ref-fraction);
            if(heldAge[i*4]!=expected)++ageDiffers;
            if(std::floor(std::fabs(double(heldAge[i*4])))!=std::fabs(ref))++countMismatch;
            if(std::fabs(ref)>1)++blended;}
        std::printf("REGION_HOLD_IDENTITY k=%.2f strength=%.2f pixels=%u sentinel=%u rest=%u moving=%u unrouted_geometry=%u blended=%u colour_differs=%u age_differs=%u count_differs=%u\n",
            double(k),double(strength),S*S,kinds[0],kinds[1],kinds[2],kinds[3],blended,colourDiffers,ageDiffers,countMismatch);
        ++numeric_checks;require(blended>S*S/4&&colourDiffers==0&&ageDiffers==0&&countMismatch==0,"holds reading 0: the hold program is the camera program on the 1x1 composition, colour bit for bit, the age its count plus the encoded holds");}
    check("hold identity RT0",d->SetRenderTarget(0,helper.colorSurface.p));check("hold identity PS reset",d->SetPixelShader(nullptr));
}

// ---- REGION_HOLD_STATE ----
// (LateBoxFault: temporal_thin_region_inc.h.)
// Refuses CreatePixelShader of the hold resolve's words while alive (a device that refuses the camera gate's program at creation).
struct HoldRefusal {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,const DWORD* words,IDirect3DPixelShader9** out){
        if(words==reinterpret_cast<const DWORD*>(x3m::renderer::temporal_resolve_far_camera_hold_program())){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}
        return original(d,words,out);}
    explicit HoldRefusal(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[106],sizeof original);auto fn=&hook;std::memcpy(&table[106],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~HoldRefusal(){*reinterpret_cast<void***>(device)=previous;}
};
void state_cases(EdgeScene& s,const DWORD* resolver){
    IDirect3DDevice9* const d=s.d;s.render(thin_objects(0),sentinelBackground,0,0);Output out;const FlickerConfig none{"hold-validation",0,0,.1f,.5f,false,.9f};
    auto in=flicker_inputs(s,none,0,0,true);in.caller_scene_open=false;in.thin_region_weight=.97f;in.thin_region_camera_gate=true;
    // The hold resolve refused at creation: no camera-gate path (no fallback program set), the far program and the screen gate stay.
    bool refusedPath=false;HRESULT refusedResult=S_OK;
    {TemporalPass refused;check("hold refusal initialize",refused.initialize(d,nullptr,resolver));{HoldRefusal refusal(d);check("hold refusal configure far",refused.configure_far());require(HoldRefusal::refused==1,"hold refusal: configure_far asked for the hold resolve once");}
        refusedResult=refused.camera_programs_result();
        const bool refusedRun=refused.run(in,&out)==E_INVALIDARG;
        in.thin_region_camera_gate=false;const bool screen=SUCCEEDED(refused.run(in,&out))&&out.stabiliser_mask&&!refused.diagnostics().region_hold;in.thin_region_camera_gate=true;
        refusedPath=refused.far_available()&&!refused.camera_gate_available()&&refusedResult==D3DERR_OUTOFVIDEOMEMORY&&refused.configure_sentinel()==E_FAIL&&refusedRun&&screen;}
    TemporalPass pass;check("hold initialize",pass.initialize(d,nullptr,resolver));
    require(pass.run(in,&out)==E_INVALIDARG,"camera gate without configure_far is refused");
    check("hold configure far",pass.configure_far());require(pass.camera_gate_available()&&pass.camera_programs_result()==S_OK&&!pass.sentinel_available(),"configure_far creates the camera mask, the hold resolve and its box; the separable twins only with configure_sentinel");
    check("hold configure sentinel",pass.configure_sentinel());check("hold configure sentinel is idempotent",pass.configure_sentinel());require(pass.sentinel_available(),"configure_sentinel creates the separable box's twins");
    for(unsigned bad:{0u,65u}){in.thin_region_hold_frames=bad;require(pass.run(in,&out)==E_INVALIDARG,"a hold length outside 1..64 is refused");}in.thin_region_hold_frames=oracleHoldFrames;
    require(SUCCEEDED(pass.run(in,&out))&&out.stabiliser_mask&&out.age&&pass.diagnostics().region_hold,"camera-gate run publishes the tests target and the age target");
    require(SUCCEEDED(pass.run(in,&out))&&out.used_history&&pass.diagnostics().region_hold,"camera-gate run continues the history");
    const float junk[4]={9,8,7,6};for(UINT reg:{0u,4u,5u,6u,10u,11u,22u,24u,25u})check("hold hostile constant",d->SetPixelShaderConstantF(reg,junk,1));
    for(UINT slot:{0u,7u,8u,9u,10u,11u})check("hold hostile texture",d->SetTexture(slot,s.wave.p));
    check("hold hostile s8 min",d->SetSamplerState(8,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("hold hostile s11 min",d->SetSamplerState(11,D3DSAMP_MINFILTER,D3DTEXF_POINT));check("hold hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
    {Snapshot before(d);check("hold hostile run",pass.run(in,&out));before.equals(d,"hold run restores c0..c7, c11, c22, c24, c25, samplers 0 and 7..12, RT1 and COLORWRITEENABLE1");}
    in.sentinel_strength=.7f;
    {Snapshot before(d);check("hold sentinel hostile run",pass.run(in,&out));before.equals(d,"hold run with the separable box restores state");require(pass.diagnostics().region_hold,"hold with the sentinel stabiliser");}
    // Draws of a hold run with the separable box: tests (1), rows (2), columns (3), resolve (4).
    for(unsigned call:{2u,4u}){Output failed;{Fault fault(d,call);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid,"failed box / resolve draw of a hold run publishes nothing");}
        check("hold recovery",pass.run(in,&out));require(out.color&&!out.used_history&&pass.diagnostics().region_hold,"after a failed hold run the resolve restarts without history");}
    pass.before_reset();pass.after_reset(S_OK);check("hold after Reset",pass.run(in,&out));require(out.color&&!out.used_history&&pass.camera_gate_available()&&pass.diagnostics().region_hold,"Reset keeps the camera-gate programs and restarts the history");
    check("hold continues after Reset",pass.run(in,&out));
    // Leaving the camera gate restarts the history (the screen gate's far program reads whole counts); coming back keeps it
    // (whole counts read as no hold). Mask targets (line_mask_targets): one on a camera-gate run, two on the screen gate.
    const unsigned masksCamera=pass.line_mask_targets();
    in.thin_region_camera_gate=false;check("screen gate",pass.run(in,&out));const bool restarted=!out.used_history&&!pass.diagnostics().region_hold;const unsigned masksScreen=pass.line_mask_targets();
    check("screen gate again",pass.run(in,&out));const bool continued=out.used_history;
    in.thin_region_camera_gate=true;check("camera gate again",pass.run(in,&out));const bool kept=out.used_history&&pass.diagnostics().region_hold;const unsigned masksOn=pass.line_mask_targets();
    // 16 taps: the camera gate has no 16-tap program; a camera-gate run is refused, 5 taps run it again.
    check("hold taps16",pass.configure_history_taps(16));const bool taps16=!pass.camera_gate_available()&&pass.run(in,&out)==E_INVALIDARG;
    check("hold taps5",pass.configure_history_taps(5));check("hold taps5 run",pass.run(in,&out));const bool taps5=pass.camera_gate_available()&&pass.diagnostics().region_hold&&pass.diagnostics().history_taps==5;
    // Reset: every default-pool target goes; the first camera-gate run after it allocates one mask target.
    pass.before_reset();const unsigned masksReset=pass.line_mask_targets();pass.after_reset(S_OK);check("hold run after second Reset",pass.run(in,&out));const unsigned masksAfterReset=pass.line_mask_targets();
    // Box-target creation failure on a camera-gate run: the thin region off for the session (no fallback program set; this
    // input has no far stabiliser of its own, so the plain resolve): no mask published, the next run neither retries the
    // boxes nor draws the camera gate and continues the history; one mask target stays from the refused run's allocation.
    pass.before_reset();pass.after_reset(S_OK);bool boxRefused=false;
    {LateBoxFault fault(d,2);check("hold run with the box targets refused",pass.run(in,&out));boxRefused=LateBoxFault::refused>0&&pass.camera_gate_failed()&&pass.camera_gate_result()==D3DERR_OUTOFVIDEOMEMORY&&!pass.diagnostics().region_hold&&out.color&&!out.stabiliser_mask&&!out.age;}
    {LateBoxFault fault(d,0);check("hold run after the refused box targets",pass.run(in,&out));boxRefused=boxRefused&&LateBoxFault::refused==0&&!pass.diagnostics().region_hold&&out.used_history&&!out.stabiliser_mask;}
    const unsigned masksBoxRefused=pass.line_mask_targets();
    pass.before_reset();pass.after_reset(S_OK);check("hold re-armed after Reset",pass.run(in,&out));const bool rearmed=pass.diagnostics().region_hold&&pass.line_mask_targets()==1;
    std::printf("REGION_HOLD_STATE refused_path=%u refused_create=%08lx screen_restarts=%u screen_continues=%u camera_keeps=%u taps16_refused=%u taps5_camera=%u masks_camera=%u masks_screen=%u masks_on=%u masks_at_reset=%u masks_after_reset=%u box_refused_region_off=%u masks_box_refused=%u rearmed=%u\n",
        unsigned(refusedPath),(unsigned long)refusedResult,unsigned(restarted),unsigned(continued),unsigned(kept),unsigned(taps16),unsigned(taps5),masksCamera,masksScreen,masksOn,masksReset,masksAfterReset,unsigned(boxRefused),masksBoxRefused,unsigned(rearmed));
    ++numeric_checks;require(refusedPath,"hold resolve refused at creation: no camera-gate program, a camera-gate run and configure_sentinel refused, the screen gate runs");
    ++numeric_checks;require(restarted&&continued&&kept&&taps16&&taps5,"leaving the camera gate restarts the history once, coming back keeps it; 16 taps refuse a camera-gate run");
    ++numeric_checks;require(masksCamera==1&&masksScreen==2&&masksOn==1&&masksReset==0&&masksAfterReset==1&&masksBoxRefused==1&&rearmed,
        "mask targets: one on a camera-gate run (the second released), two on the screen gate, none across Reset, one after it (the refused box targets allocate no second)");
    ++numeric_checks;require(boxRefused,"box targets refused on a camera-gate run: the thin region off for the session (plain resolve, no mask, history kept), no retry until Reset");
    for(UINT slot:{0u,1u,2u,3u,4u,6u,7u,8u,9u,10u,11u,12u})check("hold unbind",d->SetTexture(slot,nullptr));
    s.target(s.colorSurface.p);
}

// ---- thin-region rows with the hold ----
void thin_rows(EdgeScene& s,const DWORD* resolver){
    const LineConfig base{"thin-base",0,0},on97{"thin-region-0.97",0,0,0,0,.97f,1},hold97{"thin-region-0.97-camera-gate-hold",0,0,0,0,.97f,1,true};
    const double bound=.0006/(1-.97);
    for(double drift:{0.,.12,.3}){thinDrift=drift;thinMoveFrom=~0u;const auto baseRun=thin_sequence(s,resolver,base,thinFrames),run=thin_sequence(s,resolver,hold97,thinFrames);
        double baseRms=0,baseP2p=0,rms=0,p2p=0;thin_ripple(baseRun,baseRms,baseP2p);thin_ripple(run,rms,p2p);
        const Oracle o=oracle(run,hold97);const double maskError=hold_tests_error(run);unsigned squareDiffers=0,squarePixels=0;
        for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=20;x+3<S;++x){++squarePixels;squareDiffers+=std::memcmp(&run.output[n][(y*S+x)*4],&baseRun.output[n][(y*S+x)*4],4*sizeof(float))!=0;}
        std::printf("THIN_REGION_HOLD drift=%.2f config=%s oracle_error=%.6f age_oracle_error=%.6f tests_mask_error=%.6f shard_rms_codes=%.3f shard_p2p_codes=%.1f rms_ratio=%.4f square_px=%u square_differs=%u\n",
            drift,hold97.name,o.colour,o.age,maskError,255*rms,255*p2p,rms/baseRms,squarePixels,squareDiffers);
        metric("region hold: shader matches the 2-D CPU oracle with the holds within the FP16 bound",o.colour,0,bound);
        metric("region hold: age target (count and holds) matches the CPU oracle",o.age,0,0);
        metric("region hold: the published tests target equals the CPU tests draw",maskError,0,.5/255);
        ++numeric_checks;require(squarePixels>0&&squareDiffers==0,"region hold: plain silhouette (x >= 20) bit-identical to the plain resolve");
        if(drift==0){++numeric_checks;require(rms<=.35*baseRms&&p2p<=.5*baseP2p,"region hold, static shards: ripple <= 0.35 x and peak-to-peak <= 0.5 x the installed resolve");}}
    thinDrift=0;
    // Motion starts after 64 static frames (0.4 px/frame): against the plain resolve, the design's ghost bound (0.04 above the plain resolve's trail from 8 frames on) and the 24-frame release.
    {thinDrift=.4;thinMoveFrom=64;const auto baseRun=thin_sequence(s,resolver,base,thinFrames),run=thin_sequence(s,resolver,hold97,thinFrames);
        double first=0,late=0,trail=0,baseTrail=0;UINT lateX=0,lateY=0,trailX=0,trailY=0,trailN=0;
        for(unsigned n=65;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x<20;++x){const double e=std::fabs(px(run.output[n],x,y)-px(baseRun.output[n],x,y));if(n==65)first=std::max(first,e);if(n>=65+24&&e>late){late=e;lateX=x;lateY=y;}
            if(px(run.depth[n],x,y)<=-.5f&&n>=65+8){const double t=std::fabs(double(px(run.output[n],x,y))-.25);if(t>trail){trail=t;trailX=x;trailY=y;trailN=n;}baseTrail=std::max(baseTrail,std::fabs(double(px(baseRun.output[n],x,y))-.25));}}
        // (No oracle row here: the scene hooks give the shards one velocity for the whole run, not the 64 static frames first.)
        std::printf("THIN_REGION_HOLD_MOTION_START first_frame_difference=%.6f after_24_frames=%.6f after_24_at=%u,%u background_trail=%.6f trail_at=%u,%u,%u base_background_trail=%.6f\n",first,late,lateX,lateY,trail,trailX,trailY,trailN,baseTrail);
        metric("region hold: 24 frames after motion starts the output is the installed resolve's",late,0,2./255);
        metric("region hold: from 8 frames after motion starts, uncovered background carries at most 0.04 more shard colour than the plain resolve",std::max(trail-baseTrail,0.),0,.04);
        thinMoveFrom=~0u;thinDrift=0;}
    // Camera pan at 0.5 px/frame: the hold keeps the region open under the pan; the bright mover and the stale patch.
    {thinPanX=cameraPanX=.5;const auto screenRun=thin_sequence(s,resolver,on97,thinFrames),run=thin_sequence(s,resolver,hold97,thinFrames);
        double screenRms=0,screenP2p=0,rms=0,p2p=0;thin_ripple(screenRun,screenRms,screenP2p,18,28);thin_ripple(run,rms,p2p,18,28);
        const Oracle o=oracle(run,hold97);const double maskError=hold_tests_error(run);
        std::printf("THIN_REGION_HOLD_PAN pan=0.50 oracle_error=%.6f age_oracle_error=%.6f tests_mask_error=%.6f screen_rms_codes=%.3f hold_rms_codes=%.3f hold_over_screen=%.4f screen_p2p_codes=%.1f hold_p2p_codes=%.1f\n",
            o.colour,o.age,maskError,255*screenRms,255*rms,rms/screenRms,255*screenP2p,255*p2p);
        metric("region hold, pan: shader matches the oracle",o.colour,0,bound);metric("region hold, pan: age matches the oracle",o.age,0,0);metric("region hold, pan: tests target equals the CPU tests draw",maskError,0,.5/255);
        ++numeric_checks;require(rms<=.5*screenRms&&p2p<=.6*screenP2p,"region hold, pan past HI: the held camera gate halves the shard ripple (rms <= 0.5 x, peak-to-peak <= 0.6 x the screen gate)");
        thinPatchV=2;thinPatchFrom=40;thinInjectFrame=oracleInjectFrame=100;std::copy(injectRect,injectRect+4,oracleInjectRect);oracleInjectValue=patchValue;
        const auto screenPatch=thin_sequence(s,resolver,on97,thinFrames),patch=thin_sequence(s,resolver,hold97,thinFrames);
        double excess=0,added[2]={0,0};
        for(unsigned n=61;n<100;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x)excess=std::max(excess,double(px(patch.output[n],x,y))-1);
        const FarRun* runs[2]={&screenPatch,&patch};const FarRun* clean[2]={&screenRun,&run};
        for(unsigned which=0;which<2;++which)for(int y=injectRect[1]-1;y<=injectRect[3];++y)for(int x=injectRect[0]-1;x<=injectRect[2];++x)added[which]=std::max(added[which],double(px(runs[which]->output[101],UINT(x),UINT(y))-px(clean[which]->output[101],UINT(x),UINT(y))));
        const Oracle po=oracle(patch,hold97,101);
        std::printf("THIN_REGION_HOLD_STALE pan=0.50 mover_px_per_frame=2 excess_after_mover=%.6f screen_added_frame101=%.6f hold_added_frame101=%.6f hold_over_screen=%.4f unbounded_estimate=%.4f oracle_error=%.6f age_oracle_error=%.6f\n",
            excess,added[0],added[1],added[1]/std::max(added[0],1e-9),.97*(patchValue-.25),po.colour,po.age);
        metric("region hold, pan + bright mover: no ghost above the scene's maximum after the mover left",excess,0,2./255);
        metric("region hold, pan + stale patch: shader matches the oracle on the injected history",po.colour,0,bound);
        ++numeric_checks;require(added[0]>.1&&added[1]<=2*added[0]&&added[1]<.5*.97*(patchValue-.25),"region hold, stale patch: the 7x7 box bounds it to at most 2 x the screen gate's clipped value, well below the clip-off estimate");
        thinPatchV=0;thinPatchFrom=~0u;thinInjectFrame=oracleInjectFrame=~0u;
        // k = 0.5 and a non-finite current tap (the box domain row): the per-block weighing and the box twin against the oracle.
        {thinBadTap=true;thinK=.5f;oracleK=.5;const auto bad=thin_sequence(s,resolver,hold97,thinFrames);const Oracle bo=oracle(bad,hold97);
            std::printf("THIN_REGION_HOLD_BOX_DOMAIN pan=0.50 k=0.5 oracle_error=%.6f age_oracle_error=%.6f\n",bo.colour,bo.age);
            metric("region hold, k = 0.5 and a non-finite current tap: shader matches the oracle",bo.colour,0,bound);metric("region hold, k = 0.5: age matches the oracle",bo.age,0,0);
            thinBadTap=false;thinK=0;oracleK=0;}
        thinPanX=cameraPanX=0;}
    // Stop after a pan (second review, item 9): the pan scene panning 0.5 px/frame for 64 frames, then standing still. The screen gate is
    // not held, so the region reopens the frame the camera stops: frame-to-frame rms on the shard field over the 8 frames after the
    // stop and the 24 after those, against the plain resolve (the removed dilated chain measured 0.094 x of it on both windows).
    {thinPanX=cameraPanX=.5;thinPanStop=64;constexpr unsigned frames=96;
        const auto baseRun=thin_sequence(s,resolver,base,frames),run=thin_sequence(s,resolver,hold97,frames);
        auto step=[&](const FarRun& r,unsigned from,unsigned to){double sum=0;unsigned count=0;for(unsigned n=from;n<to;++n)for(UINT y=5;y<27;++y)for(UINT x=18;x<28;++x){const double e=double(px(r.output[n],x,y))-double(px(r.output[n-1],x,y));sum+=e*e;++count;}return 255*std::sqrt(sum/count);};
        const double hold8=step(run,65,73),base8=step(baseRun,65,73),hold24=step(run,73,frames),base24=step(baseRun,73,frames);
        std::printf("THIN_REGION_HOLD_PAN_STOP pan=0.50 stop_frame=64 step_rms_codes_first8=%.3f base_first8=%.3f step_rms_codes_next24=%.3f base_next24=%.3f\n",hold8,base8,hold24,base24);
        ++numeric_checks;require(hold8<=.15*base8&&hold24<=.15*base24,"stop after a pan: the held region settles at once (step rms at most 0.15 x the plain resolve's, the 8 frames after the stop and the 24 after)");
        thinPanStop=~0u;thinPanX=cameraPanX=0;}
    // Box-open fraction under a camera pan (second review, item 8): the arm scene (shards at x 2..11, a plain square at 21..29, sentinel
    // background) carried by a 0.5 px/frame pan for 24 frames. The share of the frame where the box programs run on the last frame:
    // the tests-texel gate alone (a > r), and the region-gated twins (a > r inside this frame's flag or the previous frame's region
    // hold at the same texel).
    {armPanX=cameraPanX=.5;constexpr unsigned frames=24;const auto run=thin_sequence(s,resolver,hold97,frames);
        unsigned testsOnly=0,gated=0;constexpr UINT N=S*S;const auto& tests=run.mask.back();const auto& previousAge=run.age[frames-2];
        for(UINT i=0;i<N;++i){const float r=tests[i*4],b=tests[i*4+2],a=tests[i*4+3];const bool cameraAdds=a>r;
            testsOnly+=cameraAdds;gated+=cameraAdds&&(b>.5f||held_region(previousAge[i*4]));}
        std::printf("THIN_REGION_HOLD_BOX_OPEN pan=0.50 frames=%u pixels=%u tests_gate_alone=%.4f region_gated=%.4f\n",frames,N,double(testsOnly)/N,double(gated)/N);
        ++numeric_checks;require(gated<=testsOnly&&gated<testsOnly,"box twins under a pan: the region gate runs the box on fewer pixels than the tests gate alone");
        armPanX=cameraPanX=0;}
    // Sentinel facets (the stabiliser S = 0.7 on the separable box's hold twins) at rest and under a 2 px/frame reversing vertical pan.
    {thinSentinel=true;LineConfig holdOff=hold97,holdOn=hold97;holdOff.name="sentinel-0-hold";holdOn.name="sentinel-0.7-hold";holdOn.sentS=.7f;
        struct PanCase{const char* mode;double pan;bool alternating;unsigned lag;};const PanCase pans[]={{"rest",0,false,1},{"reversing",2,true,2}};
        for(const PanCase& pc:pans){cameraPanVertical=pc.alternating;cameraPanAlternates=pc.alternating;cameraPanSpeed=pc.pan;cameraPanY=0;oracleSkipCeiling=pc.pan<1?0:unsigned(std::ceil(pc.pan)+1)*(S-6)*thinFrames;
            const auto baseRun=thin_sequence(s,resolver,holdOff,thinFrames),run=thin_sequence(s,resolver,holdOn,thinFrames);const Oracle o=oracle(run,holdOn);
            auto flicker=[&](const FarRun& r){double sum=0;unsigned count=0;for(unsigned n=thinFrames-thinAnalysed;n<thinFrames;++n)for(UINT y=8;y<28;++y)for(UINT x=4;x<28;++x){const double e=double(px(r.output[n],x,y))-double(px(r.output[n-pc.lag],x,y));sum+=e*e;++count;}return std::sqrt(sum/count);};
            const double baseRms=flicker(baseRun),rms=flicker(run);
            std::printf("THIN_REGION_HOLD_SENTINEL pan=%.2f pan_mode=%s strength=0.70 oracle_error=%.6f age_oracle_error=%.6f oracle_skipped_px=%u base_flicker_codes=%.3f flicker_codes=%.3f flicker_ratio=%.4f\n",pc.pan,pc.mode,o.colour,o.age,oracleSkipped,255*baseRms,255*rms,rms/baseRms);
            metric("region hold, sentinel stabiliser: shader matches the oracle",o.colour,0,bound);metric("region hold, sentinel stabiliser: age matches the oracle",o.age,0,0);
            ++numeric_checks;require(rms<.6*baseRms,"region hold, sentinel facets: flicker below 0.6 x the S = 0 hold run");}
        cameraPanVertical=cameraPanAlternates=false;cameraPanSpeed=cameraPanY=0;oracleSkipCeiling=0;thinSentinel=false;}
    // Fade owner (X3M_FADE_RT2_OWNER; fade-rt2-ownership.md sections 4 and 7): the sentinel scene at rest with the stabiliser (0.7)
    // and the hold, a far routed square that is a masked fade row (routed on the depth sentinel) until frame 32 and an RT2 owner
    // (depth 0.999) from it, against the same square masked throughout and owner throughout. A routed valid-depth square's
    // corners are FRAGMENTED against the sentinel (a diagonal 7-tap line clips a corner: two class changes), so an owner's
    // corners hold the region in steady state like any geometry; the class change itself may open it once per pixel and its
    // extra openness (hold values differing from the always-owner run) lasts at most L + 1 frames. The output and the age match
    // the CPU oracle with the holds (the thin-region bounds), and the frames before the switch are the always-masked run's.
    {thinSentinel=true;LineConfig holdOn=hold97;holdOn.name="fade-owner-0.7-hold";holdOn.sentS=.7f;constexpr unsigned frames=64,from=32;
        sentinelOwnerFrom=frames;const auto masked=thin_sequence(s,resolver,holdOn,frames);sentinelOwnerFrom=0;const auto always=thin_sequence(s,resolver,holdOn,frames);
        sentinelOwnerFrom=from;const auto run=thin_sequence(s,resolver,holdOn,frames);
        const Oracle o=oracle(run,holdOn);const double maskError=hold_tests_error(run);
        auto hold=[](float age){const double v=std::fabs(double(age));if(v>65)return 0.;const double held=(v-std::floor(v))*65536;return std::round(held-128*std::floor(held/128));};
        bool classes=true;unsigned before=0,preDiffers=0,openings=0,maxOpenings=0,extraFrames=0,lateDiffers=0,steadyHeld=0,maxHold=0;
        for(UINT y=11;y<21;++y)for(UINT x=5;x<15;++x){unsigned pixelOpenings=0;const bool square=x>=6&&x<14&&y>=12&&y<20;
            for(unsigned n=0;n<frames;++n){const float d=px(run.depth[n],x,y),a=px(run.motion[n],x,y,3);
                if(square)classes=classes&&a==1.f&&(n<from?d<=-.5f:d==ownerDepth);
                if(n<from){++before;preDiffers+=std::memcmp(&run.output[n][(y*S+x)*4],&masked.output[n][(y*S+x)*4],4*sizeof(float))!=0||px(run.age[n],x,y)!=px(masked.age[n],x,y);}
                const double h=hold(px(run.age[n],x,y)),previous=n?hold(px(run.age[n-1],x,y)):0.;maxHold=std::max(maxHold,unsigned(h));
                if(h>previous){++pixelOpenings;++openings;}
                if(n>from+oracleHoldFrames)lateDiffers+=h!=hold(px(always.age[n],x,y));}
            if(hold(px(always.age[frames-1],x,y))>0)++steadyHeld;
            maxOpenings=std::max(maxOpenings,pixelOpenings);}
        for(unsigned n=from;n<frames;++n){bool extra=false;for(UINT y=11;y<21;++y)for(UINT x=5;x<15;++x)extra=extra||hold(px(run.age[n],x,y))!=hold(px(always.age[n],x,y));extraFrames+=extra;}
        std::printf("THIN_REGION_HOLD_FADE_OWNER frames=%u switch_frame=%u strength=0.70 oracle_error=%.6f age_oracle_error=%.6f tests_mask_error=%.6f pre_switch_px_frames=%u pre_switch_differs=%u region_openings=%u max_openings_per_px=%u extra_open_frames=%u late_hold_differs=%u steady_held_px=%u max_hold=%u hold_frames=%u\n",
            frames,from,o.colour,o.age,maskError,before,preDiffers,openings,maxOpenings,extraFrames,lateDiffers,steadyHeld,maxHold,oracleHoldFrames);
        require(classes,"fade owner: the square is routed on the sentinel before the switch and holds its depth after it");
        metric("fade owner: shader matches the oracle with the holds",o.colour,0,bound);metric("fade owner: age matches the oracle",o.age,0,0);
        metric("fade owner: the published tests target equals the CPU tests draw",maskError,0,.5/255);
        ++numeric_checks;require(preDiffers==0,"fade owner: before the switch the run is the always-masked run bit for bit");
        ++numeric_checks;require(maxOpenings<=1&&extraFrames<=oracleHoldFrames+1&&lateDiffers==0,"fade owner: the class change opens the region at most once per pixel, its extra hold lasts at most L + 1 frames");
        sentinelOwnerFrom=~0u;thinSentinel=false;}
}
} // namespace region_hold
void region_hold_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("REGION_HOLD_CASES");EdgeScene s(d,compiler);
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    struct Hooks{Hooks(){line_velocity=thin_velocity;line_velocity_x=thin_velocity_x;farD0=.98f;farInv=200;}
        ~Hooks(){line_velocity=line_velocity_default;line_velocity_x=line_velocity_x_default;thinDrift=0;thinMoveFrom=~0u;thinBadTap=false;thinK=0;oracleK=0;thinPanX=cameraPanX=thinPatchV=0;thinPatchFrom=thinInjectFrame=oracleInjectFrame=~0u;farD0=farInv=0;
            cameraPanAlternates=cameraPanVertical=false;cameraPanSpeed=cameraPanY=0;thinSentinel=false;oracleSkipCeiling=0;thinPanStop=~0u;armPanX=0;sentinelOwnerFrom=~0u;}} hooks;
    region_hold::identity_cases(s,compiler);
    region_hold::state_cases(s,resolver);
    region_hold::thin_rows(s,resolver);
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
