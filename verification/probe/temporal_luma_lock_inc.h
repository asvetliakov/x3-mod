// Luminance lock (docs/architecture/taa-luminance-lock.md sections 5 and 10), lattice mode. Included after
// temporal_far_jitter_line_inc.h: uses halton, EdgeScene, Fault, Snapshot and the far gate of the production footprints.
//   A 512 x 16 far strip (farw 1: the run327 projection's gate on the production F0 / F1 60 / 68 at 5120 px, the strip at view
//   z 100,000 units), routed, uniform depth, on the camera-gate resolve (thin region 0.97 on the camera gate, hold 8, base
//   weight 0.85, far weight 0.985 on the camera gate) under the production 8-phase jitter. Content (point-sampled at the
//   jittered position): content x < 248 a plate of luma 0.5 with a texture ramp of 0.1 per px (a triangle wave of period
//   16 px, 0.5 .. 1.3) along x (or along y: the y-ramp run); beyond it a flat 0.5 surround with vertical struts of luma 1.5
//   and width 0.4 px every 16 px at [s + 0.115, s + 0.515), so that pixel s samples them on the even jitter phases only
//   (coverage flips every frame); the slanted runs tilt them 0.2 px per row (the FAR_JITTER_LINE line). Frames 0-15 rest,
//   16-39 a world-static camera pan of 0.37 px/frame (above HI 0.25: the screen gate closed; 8.88 px in all), 40-47 rest
//   where the pan stopped, 48 a cut (the camera back at 0, history restart), 49-55 rest, 56-71 a 3-px mover of luma 0.2 at
//   depth 0.5 (near: a disocclusion) moving +1 px/frame across the strut at x = 360 (it covers it at frames 59-61). The
//   shading-step runs multiply the surround by 0.8 or 0.6 from frame 30 (under the pan: carried locks, no refresh).
//   Runs: blanket (the current program: far weight on every far pixel, 7x7 far clip), base (far weight 0, 3x3 clip: the
//   ordinary-hull path), lock (16, 0.25, 3, release 0.65), lock_rho05 (RHO 0.5), lock_always (gate always), lock_yramp,
//   lock_step20 / lock_step40, identity (16, 1, 32 on the scene without struts) against base_nostrut, blanket_slanted /
//   lock_slanted. The strut's true pixels at displacement disp: the cell of s + 0.315 + disp (its centre) per row.
//   Rows (the runner gates them; the fixture prints numbers only, plus the structural requires of LUMA_LOCK_STATE):
//     LUMA_LOCK_FORM: first frame with >= 0.9 of the strut pixels locked (lane .b > 0), from frame 0 and from the cut (48);
//     LUMA_LOCK_PLATE: plate lock share, the largest over every frame and over the rest frames, per run (x and y ramps);
//     LUMA_LOCK_PLATE_SHARP: plate gradient energy (codes) under the pan (frames 24-39) and at rest (8-15) against base; the
//       plate interior (no locked pixel within 3 px) whose colour differs from base, over every frame and at rest;
//     LUMA_LOCK_STRUT_RIPPLE: rms of each strut pixel's code about its mean over frames 8-15, lock against blanket;
//     LUMA_LOCK_CARRY: lock share of the strut's true pixels on every pan frame (16-39): min, mean, the first eight;
//     LUMA_LOCK_CARRY_BAND: locked pixels per strut and row near the struts during the pan (max), and the share of strut-region
//       pixels more than 2 px from a true pixel that are locked (max);
//     LUMA_LOCK_RESUME: lock share of the true pixels at the stop (frame 40) and two frames after it (41);
//     LUMA_LOCK_CHATTER: the largest share of strut pixels whose lock toggles between consecutive rest frames after formation
//       (3-15, 42-47 on the resumed pixels, 51-55);
//     LUMA_LOCK_MOVER: locks on the pixels the mover uncovers, at their uncover frame; the strut column's re-formation delay;
//     LUMA_LOCK_SLANTED: the slanted line's covered pixels: lock share at rest and the share of their ripple energy (the std of
//       their input over frames 8-15) on the pixels locked at frame 15; its ripple against blanket;
//     LUMA_LOCK_SHADING: the true pixels' lock share at frames 29, 30 and 31 of the two step runs;
//     LUMA_LOCK_LANE: rest mode (b < 128) at rest, motion mode (b >= 128) under the pan, locked offsets within 0.75 px, the strut
//       residual's sign alternation at rest;
//     LUMA_LOCK_IDENTITY: colour, age and depth bytes of identity against base_nostrut over all 72 frames;
//     LUMA_LOCK_STATE: refusals, the program / camera / lane reasons, the lane refusal and Reset, a failed draw, the hostile
//       state of RT3, CWE3, s13 and c14 / c15 restored.
namespace luma_lock {
constexpr UINT W=512,H=16;constexpr unsigned frames=72,phases=8,panFrom=16,panTo=40,cutAt=48,moverFrom=56,stepAt=30;
constexpr float plateEnd=248,strutOffset=8.115f,strutWidth=.4f,strutPeriod=16,panSpeed=.37f;
constexpr UINT Y0=4,Y1=12,PX0=24,PX1=232,SX0=264,SX1=489,moverStrut=360,moverLeft0=355;
struct Config{const char* name;unsigned lockFrames;float rho,tau;bool always;float farWeight,farClip;bool struts;float slope;bool plateY;float step;};
struct Run{std::vector<std::vector<unsigned char>> color,age,depth,lane;std::vector<std::vector<float>> input;std::vector<double> disp;};
// Refuses CreateTexture of an A8R8G8B8 render target (the lock lanes on a camera-gate run) while alive.
struct LaneFault{
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
        if(format==D3DFMT_A8R8G8B8&&(usage&D3DUSAGE_RENDERTARGET)){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}
        return original(d,w,h,levels,usage,format,pool,out,shared);}
    explicit LaneFault(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[23],sizeof original);auto fn=&hook;std::memcpy(&table[23],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~LaneFault(){*reinterpret_cast<void***>(device)=previous;}
};
void luma_lock_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("LUMA_LOCK_CASES");
    constexpr float p00=.4999979f,p22=1.00000298f,p32=-6.00001812f;float gateD0=0,gateInv=0;
    require(x3::temporal::far_gate(p00,p22,p32,5120,60.f,68.f,gateD0,gateInv),"luma lock far gate of the production footprints");
    const float stripDepth=float(double(p22)+double(p32)/100000.);
    require((stripDepth-gateD0)*gateInv>=1.f,"luma lock strip depth inside the farw = 1 band");
    EdgeScene s(d,compiler);
    Com<IDirect3DTexture9> color,depth,motion;Com<IDirect3DSurface9> colorSurface,depthSurface,motionSurface;
    check("ll color",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("ll color surface",color->GetSurfaceLevel(0,&colorSurface.p));
    check("ll depth",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));check("ll depth surface",depth->GetSurfaceLevel(0,&depthSurface.p));
    check("ll motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));check("ll motion surface",motion->GetSurfaceLevel(0,&motionSurface.p));
    Com<ID3DXBuffer> ca,cm,cd;Com<IDirect3DPixelShader9> contentPS,motionPS,depthPS;
    // c0 = (jitter x + displacement, jitter y, plate base, ramp per px), c1 = (strut luma, offset, period, width), c2 = (strut slope
    // per row, mover left pixel, mover width, mover luma), c3 = (the plate's end (content x), 1: the ramp along y, the surround's
    // factor, 0).
    compile(compiler,"float4 a:register(c0);float4 b:register(c1);float4 e:register(c2);float4 g:register(c3);"
        "float4 main(float2 vpos:VPOS):COLOR0{float px=floor(vpos.x);float cx=px+0.5-a.x;float cy=floor(vpos.y)+0.5-a.y;float v=a.z*g.z;"
        "if(cx<g.x){float r=g.y>0.5?cy:cx;float t=r-16*floor(r/16);v=a.z+a.w*abs(t-8);}else{float q=cx-e.x*cy;float m=q-b.z*floor(q/b.z)-b.y;if(m>=0&&m<b.w)v=b.x;}"
        "if(px>=e.y&&px<e.y+e.z)v=e.w;return float4(v,v,v,1);}","ps_3_0",&ca.p);
    // c0 = (jitter x, jitter y, camera displacement px, 0), c1 = (1/W, 1/H, strip depth, 0), c2 = (mover left, width, px/frame, depth).
    compile(compiler,"float4 j:register(c0);float4 k:register(c1);float4 m:register(c2);"
        "float4 main(float2 vpos:VPOS):COLOR0{float px=floor(vpos.x);bool mv=px>=m.x&&px<m.x+m.y;float v=mv?m.z:j.z;"
        "return float4((floor(vpos)+0.5-j.xy-float2(v,0))*k.xy,mv?m.w:k.z,1);}","ps_3_0",&cm.p);
    compile(compiler,"float4 k:register(c0);float4 m:register(c1);"
        "float4 main(float2 vpos:VPOS):COLOR0{float px=floor(vpos.x);float z=px>=m.x&&px<m.x+m.y?m.w:k.x;return float4(z,z,z,z);}","ps_3_0",&cd.p);
    check("ll content PS",d->CreatePixelShader(static_cast<DWORD*>(ca->GetBufferPointer()),&contentPS.p));
    check("ll motion PS",d->CreatePixelShader(static_cast<DWORD*>(cm->GetBufferPointer()),&motionPS.p));
    check("ll depth PS",d->CreatePixelShader(static_cast<DWORD*>(cd->GetBufferPointer()),&depthPS.p));
    auto target=[&](IDirect3DSurface9* rt,IDirect3DPixelShader9* ps){s.target(rt);D3DVIEWPORT9 vp{0,0,W,H,0,1};check("ll viewport",d->SetViewport(&vp));check("ll PS",d->SetPixelShader(ps));};
    auto raw=[&](IDirect3DTexture9* texture){D3DSURFACE_DESC desc{};check("ll desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level,sys;check("ll level",texture->GetSurfaceLevel(0,&level.p));
        check("ll readback surface",d->CreateOffscreenPlainSurface(W,H,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr));check("ll validation-only readback",d->GetRenderTargetData(level.p,sys.p));
        const UINT bpp=desc.Format==D3DFMT_A16B16G16R16F?8:4;D3DLOCKED_RECT lock{};check("ll lock",sys->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<unsigned char> out(W*H*bpp);for(UINT y=0;y<H;++y)std::memcpy(&out[y*W*bpp],static_cast<const char*>(lock.pBits)+y*lock.Pitch,W*bpp);
        check("ll unlock",sys->UnlockRect());return out;};
    auto red=[](const std::vector<unsigned char>& fp16,UINT i){unsigned short h;std::memcpy(&h,&fp16[i*8],2);return double(halfFloat(h));};
    // Lane bytes (BGRA): b = lifetime + 128 in motion mode, g, r, a = the reference.
    auto mode=[](const std::vector<unsigned char>& lane,UINT i){return lane[i*4]>=128?1u:0u;};
    auto life=[](const std::vector<unsigned char>& lane,UINT i){const unsigned b=lane[i*4];return b>=128?b-128:b;};
    auto laneR=[](const std::vector<unsigned char>& lane,UINT i){return int(lane[i*4+2])-128;};
    auto laneG=[](const std::vector<unsigned char>& lane,UINT i){return int(lane[i*4+1])-128;};
    auto code=[](double y){y=std::max(y,0.);return 255*std::pow(y/(1+y),1/2.2);};
    auto moverLeft=[](unsigned n){return n>=moverFrom?float(moverLeft0+(n-moverFrom)):-1e4f;};
    auto frameInputs=[&](const Config& c,unsigned n,double jx,double jy,double v){FrameInputs in;in.color=color.p;in.current_depth=depth.p;in.motion=motion.p;in.width=W;in.height=H;in.epoch=1;
        const float path[16]={1,0,0,float(-2*v/W),0,1,0,0,0,0,1,0,0,0,0,1};std::copy(path,path+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=x3::temporal::kHistoryWeightDefault;in.motion_policy=MotionPolicy::PerPixel;
        in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=true;in.sentinel_strict_sky=true;in.sky_history_exit_px=.25f;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        in.far_weight=c.farWeight;in.far_d0=gateD0;in.far_inv=gateInv;in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
        in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;in.thin_region_hold_frames=phases;in.far_camera_gate=true;in.far_clip=c.farClip;
        in.luma_lock_frames=c.lockFrames;in.luma_lock_rho=c.rho;in.luma_lock_tau=c.tau;in.luma_lock_always=c.always;in.cut=n==cutAt;return in;};
    // Draws the three inputs of frame n (jitter, the camera displacement `disp` of the content, this frame's camera motion v).
    auto draw=[&](const Config& c,unsigned n,double jx,double jy,double disp,double v){
        target(colorSurface.p,contentPS.p);check("ll Begin",d->BeginScene());
        {const float k[16]={float(jx+disp),float(jy),.5f,.1f,c.struts?1.5f:.5f,strutOffset,strutPeriod,strutWidth,c.slope,moverLeft(n),3,.2f,plateEnd,c.plateY?1.f:0.f,n>=stepAt?c.step:1.f,0};
            check("ll content constants",d->SetPixelShaderConstantF(0,k,4));}
        s.quad(0,0,W,H,0,0);check("ll End",d->EndScene());
        target(motionSurface.p,motionPS.p);check("ll motion Begin",d->BeginScene());
        {const float k[12]={float(jx),float(jy),float(v),0,1.f/W,1.f/H,stripDepth,0,moverLeft(n),3,1,.5f};check("ll motion constants",d->SetPixelShaderConstantF(0,k,3));}
        s.quad(0,0,W,H,0,0);check("ll motion End",d->EndScene());
        target(depthSurface.p,depthPS.p);check("ll depth Begin",d->BeginScene());
        {const float k[8]={stripDepth,0,0,0,moverLeft(n),3,1,.5f};check("ll depth constants",d->SetPixelShaderConstantF(0,k,2));}
        s.quad(0,0,W,H,0,0);check("ll depth End",d->EndScene());};
    auto sequence=[&](const Config& c){TemporalPass pass;check("ll initialize",pass.initialize(d,nullptr,resolver));check("ll configure far",pass.configure_far());
        if(c.lockFrames)check("ll configure lock",pass.configure_luma_lock());
        require(pass.camera_gate_available()&&(!c.lockFrames||pass.luma_lock_available()),"ll camera gate (and the lock) available");
        Run run;double disp=0;
        for(unsigned n=0;n<frames;++n){const unsigned index=n%phases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
            const double v=n>=panFrom&&n<panTo?panSpeed:0;if(n==cutAt)disp=0;disp+=v;run.disp.push_back(disp);
            draw(c,n,jx,jy,disp,v);FrameInputs in=frameInputs(c,n,jx,jy,v);Output out;
            check("ll Begin resolve",d->BeginScene());check("ll resolve",pass.run(in,&out));check("ll End resolve",d->EndScene());
            require(out.color&&out.age&&out.depth&&out.used_history==(n>0&&n!=cutAt)&&bool(out.luma_lock)==(c.lockFrames>0)&&pass.diagnostics().luma_lock==(c.lockFrames>0),"ll history and the lock lane follow the sequence");
            run.color.push_back(raw(out.color));run.age.push_back(raw(out.age));run.depth.push_back(raw(out.depth));run.lane.push_back(out.luma_lock?raw(out.luma_lock):std::vector<unsigned char>());
            const auto input=raw(color.p);std::vector<float> in16(W*H);for(UINT i=0;i<W*H;++i)in16[i]=float(red(input,i));run.input.push_back(in16);}
        return run;};
    constexpr float clip7=x3::temporal::kFarClipThreshold,clip3=x3::temporal::kFarClipOff;
    const Config blanket{"blanket",0,.25f,2,false,.985f,clip7,true,0,false,1},base{"base",0,.25f,2,false,0.f,clip3,true,0,false,1},
        lockA{"lock",16,.25f,3,false,.985f,clip7,true,0,false,1},lockB{"lock_rho05",16,.5f,3,false,.985f,clip7,true,0,false,1},
        lockAlways{"lock_always",16,.25f,3,true,.985f,clip7,true,0,false,1},lockY{"lock_yramp",16,.25f,3,false,.985f,clip7,true,0,true,1},
        step20{"lock_step20",16,.25f,3,false,.985f,clip7,true,0,false,.8f},step40{"lock_step40",16,.25f,3,false,.985f,clip7,true,0,false,.6f},
        baseFlat{"base_nostrut",0,.25f,2,false,0.f,clip3,false,0,false,1},identity{"identity",16,1.f,32,false,.985f,clip7,false,0,false,1},
        blanketSlanted{"blanket_slanted",0,.25f,2,false,.985f,clip7,true,.2f,false,1},lockSlanted{"lock_slanted",16,.25f,3,false,.985f,clip7,true,.2f,false,1};
    const Run rBlanket=sequence(blanket),rBase=sequence(base),rLock=sequence(lockA),rLockB=sequence(lockB),rAlways=sequence(lockAlways),rY=sequence(lockY);
    // The strut pixels at rest (columns 264 + 16 k, rows 4..11) and the strut's true pixels (the cell of its centre) at disp.
    std::vector<UINT> strutRest;for(UINT x=SX0;x<SX1;x+=16)for(UINT y=Y0;y<Y1;++y)strutRest.push_back(y*W+x);
    auto truePx=[&](double disp){std::vector<UINT> out;for(UINT s0=SX0-24;s0<SX1-8;s0+=16){const UINT x=UINT(std::floor(double(s0)+8.315+disp));for(UINT y=Y0;y<Y1;++y)out.push_back(y*W+x);}return out;};
    const size_t struts=truePx(0).size()/(Y1-Y0); // every strut of the surround (content x 248.115 onwards: the first enters the band region under the pan)
    auto share=[&](const Run& r,unsigned n,const std::vector<UINT>& px){unsigned locked=0;for(UINT i:px)locked+=life(r.lane[n],i)>0;return px.empty()?0.:double(locked)/px.size();};
    std::vector<UINT> plate;for(UINT y=Y0;y<Y1;++y)for(UINT x=PX0;x<PX1;++x)plate.push_back(y*W+x);
    auto rest=[](unsigned n){return n<panFrom||(n>=panTo&&n<cutAt)||(n>cutAt&&n<moverFrom);};
    for(const Run* r:{&rLock,&rLockB,&rAlways,&rY}){const char* name=r==&rLock?lockA.name:r==&rLockB?lockB.name:r==&rAlways?lockAlways.name:lockY.name;
        int form=-1,formCut=-1;for(unsigned n=0;n<panFrom&&form<0;++n)if(share(*r,n,strutRest)>=.9)form=int(n);
        for(unsigned n=cutAt;n<moverFrom&&formCut<0;++n)if(share(*r,n,strutRest)>=.9)formCut=int(n-cutAt);
        std::printf("LUMA_LOCK_FORM run=%s strut_px=%zu form=%d form_after_cut=%d share_frame2=%.4f share_frame15=%.4f\n",name,strutRest.size(),form,formCut,share(*r,2,strutRest),share(*r,15,strutRest));
        double plateMax=0,plateRest=0,plateMean=0;for(unsigned n=0;n<frames;++n){const double v=share(*r,n,plate);plateMax=std::max(plateMax,v);plateMean+=v/frames;if(rest(n))plateRest=std::max(plateRest,v);}
        // Where the plate locks sit: locked pixels per row at the frame of the largest share.
        std::string rows;{unsigned worst=0;double top=-1;for(unsigned n=0;n<frames;++n){const double v=share(*r,n,plate);if(v>top){top=v;worst=n;}}
            for(UINT y=Y0;y<Y1;++y){unsigned c=0;for(UINT x=PX0;x<PX1;++x)c+=life(r->lane[worst],y*W+x)>0;char b[16];std::snprintf(b,sizeof b,"%s%u",rows.empty()?"":",",c);rows+=b;}
            char b[16];std::snprintf(b,sizeof b,"@%u",worst);rows+=b;}
        std::printf("LUMA_LOCK_PLATE run=%s ramp=%s plate_px=%zu share_max=%.4f share_rest_max=%.4f share_mean=%.4f locked_per_row_4_11=%s\n",name,r==&rY?"y":"x",plate.size(),plateMax,plateRest,plateMean,rows.c_str());
        double carryMin=1,carryMean=0;std::string first;for(unsigned n=panFrom;n<panTo;++n){const double v=share(*r,n,truePx(r->disp[n]));carryMin=std::min(carryMin,v);carryMean+=v/(panTo-panFrom);
            if(n<panFrom+8){char b[16];std::snprintf(b,sizeof b,"%s%.2f",first.empty()?"":",",v);first+=b;}}
        std::printf("LUMA_LOCK_CARRY run=%s pan_px_per_frame=%.2f frames=%u-%u share_min=%.4f share_mean=%.4f first8=%s\n",name,double(panSpeed),panFrom,panTo-1,carryMin,carryMean,first.c_str());
        // CARRY_BAND: the two parts of the strut region during the pan: the locked pixels within 2 px of a true pixel, per strut
        // and row, and the locked share of the pixels farther away (with where they are at the worst frame: x offset from the
        // nearest true pixel, and the frame); the rest halo (locked pixels within 2 px per strut and row at frame 15).
        double perStrut=0,perStrutMean=0,offMax=0;unsigned offFrame=0;std::string offWhere;
        auto nearest=[&](const std::vector<UINT>& tp,UINT y,UINT x){int best=1<<20;for(UINT t:tp)if(t/W==y&&std::abs(int(t%W)-int(x))<std::abs(best))best=int(x)-int(t%W);return best;};
        for(unsigned n=panFrom;n<panTo;++n){const auto tp=truePx(r->disp[n]);unsigned nearLocked=0,off=0,offLocked=0;std::string where;
            for(UINT y=Y0;y<Y1;++y)for(UINT x=SX0-8;x<W-8;++x){const UINT i=y*W+x;const bool l=life(r->lane[n],i)>0;const int dx=nearest(tp,y,x);
                if(std::abs(dx)<=2)nearLocked+=l;
                else{++off;offLocked+=l;if(l&&where.size()<120){char b[24];std::snprintf(b,sizeof b,"%s%u:%+d",where.empty()?"":",",x,dx);where+=b;}}}
            const double v=double(nearLocked)/(struts*(Y1-Y0));perStrut=std::max(perStrut,v);perStrutMean+=v/(panTo-panFrom);
            const double o=off?double(offLocked)/off:0.;if(o>offMax){offMax=o;offFrame=n;offWhere=where;}}
        unsigned halo=0;for(UINT y=Y0;y<Y1;++y)for(UINT x=SX0-8;x<W-8;++x){const int dx=nearest(truePx(0),y,x);if(std::abs(dx)<=2)halo+=life(r->lane[15],y*W+x)>0;}
        std::printf("LUMA_LOCK_CARRY_BAND run=%s locked_per_strut_max=%.4f locked_per_strut_mean=%.4f off_strut_share_max=%.4f off_frame=%u off_px=%s rest_halo_per_strut=%.4f\n",
            name,perStrut,perStrutMean,offMax,offFrame,offWhere.empty()?"-":offWhere.c_str(),double(halo)/(struts*(Y1-Y0)));
        const auto resumed=truePx(r->disp[panTo]);
        std::printf("LUMA_LOCK_RESUME run=%s disp=%.2f share_frame40=%.4f share_frame41=%.4f share_frame47=%.4f\n",name,r->disp[panTo],share(*r,panTo,resumed),share(*r,panTo+1,resumed),share(*r,cutAt-1,resumed));
        double chatter=0;for(unsigned n=3;n<moverFrom;++n){if(!((n<panFrom)||(n>=panTo+2&&n<cutAt)||(n>=cutAt+3)))continue;const auto& px=n>=panTo&&n<cutAt?resumed:strutRest;unsigned toggles=0;
            for(UINT i:px){toggles+=(life(r->lane[n],i)>0)!=(life(r->lane[n-1],i)>0);}
            chatter=std::max(chatter,double(toggles)/px.size());}
        std::printf("LUMA_LOCK_CHATTER run=%s frames=3-15,42-47,51-55 toggle_share_max=%.4f\n",name,chatter);
        unsigned uncovered=0,lockedAtUncover=0;int reform=-1;
        for(unsigned u=moverFrom+1;u<frames;++u){const float l0=moverLeft(u-1),l1=moverLeft(u);
            for(UINT x=UINT(l0);x<UINT(l0)+3;++x){if(float(x)>=l1&&float(x)<l1+3)continue;
                for(UINT y=Y0;y<Y1;++y){++uncovered;lockedAtUncover+=life(r->lane[u],y*W+x)>0;}
                if(x==moverStrut){int worst=0;for(UINT y=Y0;y<Y1;++y){int delay=-1;for(unsigned n=u;n<frames&&delay<0;++n)if(life(r->lane[n],y*W+x)>0)delay=int(n-u);worst=delay<0||worst<0?-1:std::max(worst,delay);}reform=worst;}}}
        std::printf("LUMA_LOCK_MOVER run=%s uncovered_px=%u locked_at_uncover=%u strut_x=%u reform_delay_frames=%d\n",name,uncovered,lockedAtUncover,moverStrut,reform);}
    // PLATE_SHARP (lock against base) and the interior identity; STRUT_RIPPLE (lock against blanket).
    auto energy=[&](const Run& r,unsigned from,unsigned to){double e=0;unsigned count=0;for(unsigned n=from;n<to;++n)for(UINT y=Y0;y+1<Y1;++y)for(UINT x=PX0+1;x+1<PX1;++x){
        const double c=code(red(r.color[n],y*W+x)),cx=code(red(r.color[n],y*W+x+1)),cy=code(red(r.color[n],(y+1)*W+x));e+=(cx-c)*(cx-c)+(cy-c)*(cy-c);++count;}return e/count;};
    for(const Run* r:{&rLock,&rLockB}){const char* name=r==&rLock?lockA.name:lockB.name;unsigned interior=0,differs=0,differsRest=0;
        for(unsigned n=0;n<frames;++n)for(UINT y=Y0;y<Y1;++y)for(UINT x=PX0;x<PX1;++x){bool nearLock=false;
            for(int dy=-3;dy<=3&&!nearLock;++dy)for(int dx=-3;dx<=3&&!nearLock;++dx){const int yy=int(y)+dy,xx=int(x)+dx;if(yy>=0&&yy<int(H)&&life(r->lane[n],UINT(yy)*W+UINT(xx))>0)nearLock=true;}
            if(nearLock)continue;
            ++interior;const UINT i=y*W+x;
            if(std::memcmp(&r->color[n][i*8],&rBase.color[n][i*8],8)!=0){++differs;if(rest(n))++differsRest;}}
        const double ePan=energy(*r,24,panTo),eBasePan=energy(rBase,24,panTo),eRest=energy(*r,8,panFrom),eBaseRest=energy(rBase,8,panFrom);
        std::printf("LUMA_LOCK_PLATE_SHARP run=%s e_pan=%.4f e_pan_base=%.4f e_pan_ratio=%.6f e_rest=%.4f e_rest_base=%.4f e_rest_ratio=%.6f interior_px_frames=%u interior_differs=%u interior_differs_rest=%u\n",
            name,ePan,eBasePan,ePan/eBasePan,eRest,eBaseRest,eRest/eBaseRest,interior,differs,differsRest);}
    auto ripple=[&](const Run& r,const std::vector<UINT>& px,unsigned from,unsigned to){double sum=0;for(UINT i:px){double mean=0;for(unsigned n=from;n<to;++n)mean+=code(red(r.color[n],i))/(to-from);
        for(unsigned n=from;n<to;++n){const double e=code(red(r.color[n],i))-mean;sum+=e*e/(to-from);}}return std::sqrt(sum/px.size());};
    {const double lock=ripple(rLock,strutRest,8,panFrom),ref=ripple(rBlanket,strutRest,8,panFrom),lockB=ripple(rLockB,strutRest,8,panFrom),base=ripple(rBase,strutRest,8,panFrom);
        std::printf("LUMA_LOCK_STRUT_RIPPLE frames=8-15 strut_px=%zu lock_rms_codes=%.4f blanket_rms_codes=%.4f ratio=%.6f lock_rho05_ratio=%.6f base_rms_codes=%.4f\n",
            strutRest.size(),lock,ref,lock/ref,lockB/ref,base);}
    // SHADING: the surround x 0.8 / x 0.6 from frame 30, under the pan (carried locks, no refresh).
    for(const Config* c:{&step20,&step40}){const Run r=sequence(*c);
        auto at=[&](unsigned n){return share(r,n,truePx(r.disp[n]));};
        std::printf("LUMA_LOCK_SHADING run=%s factor=%.2f share_frame29=%.4f share_frame30=%.4f share_frame31=%.4f share_frame32=%.4f share_frame33=%.4f share_frame39=%.4f\n",
            c->name,double(c->step),at(stepAt-1),at(stepAt),at(stepAt+1),at(stepAt+2),at(stepAt+3),at(panTo-1));}
    // LANE: rest mode at rest and motion mode under the pan (x 8..W-9: the leading edge is current-only), carried offsets of the
    // locked pixels within 0.75 px, the strut residual alternating in sign at rest (frames 3..15).
    {unsigned restChecked=0,restMotion=0,panChecked=0,panRest=0,offsets=0,offsetsOut=0,alternations=0,notAlternating=0;
        for(unsigned n=1;n<cutAt;++n)for(UINT y=1;y+1<H;++y)for(UINT x=8;x+8<W;++x){const UINT i=y*W+x;
            if(n<panFrom||n>=panTo){++restChecked;restMotion+=mode(rLock.lane[n],i);}
            else{++panChecked;panRest+=1-mode(rLock.lane[n],i);if(life(rLock.lane[n],i)>0){++offsets;offsetsOut+=std::abs(laneR(rLock.lane[n],i))>=96||std::abs(laneG(rLock.lane[n],i))>=96;}}}
        for(unsigned n=3;n<panFrom;++n)for(UINT i:strutRest){++alternations;notAlternating+=!(laneR(rLock.lane[n],i)*laneR(rLock.lane[n-1],i)<0);}
        std::printf("LUMA_LOCK_LANE rest_px_frames=%u rest_in_motion_mode=%u pan_px_frames=%u pan_in_rest_mode=%u locked_offsets=%u offsets_beyond_three_quarters=%u strut_residuals=%u not_alternating=%u\n",
            restChecked,restMotion,panChecked,panRest,offsets,offsetsOut,alternations,notAlternating);}
    // IDENTITY: no pixel reaches tau (RHO 1, TAU 32; no struts): l = 0 and no candidate anywhere.
    {const Run a=sequence(baseFlat),b=sequence(identity);unsigned colour=0,age=0,depthBytes=0,locked=0;
        for(unsigned n=0;n<frames;++n){for(UINT i=0;i<W*H;++i){colour+=std::memcmp(&a.color[n][i*8],&b.color[n][i*8],8)!=0;age+=std::memcmp(&a.age[n][i*4],&b.age[n][i*4],4)!=0;
            depthBytes+=std::memcmp(&a.depth[n][i*4],&b.depth[n][i*4],4)!=0;locked+=life(b.lane[n],i)>0;}}
        std::printf("LUMA_LOCK_IDENTITY frames=%u px=%u colour_differs=%u age_differs=%u depth_differs=%u locked_px_frames=%u\n",frames,W*H,colour,age,depthBytes,locked);}
    // SLANTED: the FAR_JITTER_LINE line (0.4 px, 0.2 px per row) at rest: the pixels whose input shows it in some phase of the
    // cycle (frames 8..15), their lock share, the share of their ripple energy (input std over the cycle) on the pixels locked at
    // frame 15, and their output ripple against blanket.
    {const Run a=sequence(blanketSlanted),b=sequence(lockSlanted);std::vector<UINT> covered;for(UINT y=Y0;y<Y1;++y)for(UINT x=SX0-8;x<SX1+8;++x){bool hit=false;for(unsigned n=8;n<panFrom;++n)hit=hit||b.input[n][y*W+x]>1.f;if(hit)covered.push_back(y*W+x);}
        double mean=0,minimum=1,all=0,onLocked=0;for(unsigned n=8;n<panFrom;++n){const double v=share(b,n,covered);mean+=v/(panFrom-8);minimum=std::min(minimum,v);}
        for(UINT i:covered){double m=0,sq=0;for(unsigned n=8;n<panFrom;++n){m+=b.input[n][i]/8.;sq+=double(b.input[n][i])*b.input[n][i]/8.;}const double sd=std::sqrt(std::max(sq-m*m,0.));all+=sd;if(life(b.lane[15],i)>0)onLocked+=sd;}
        int form=-1;for(unsigned n=0;n<panFrom&&form<0;++n)if(share(b,n,covered)>=.9)form=int(n);
        std::printf("LUMA_LOCK_SLANTED covered_px=%zu share_mean_8_15=%.4f share_min_8_15=%.4f energy_on_locked=%.4f form=%d lock_rms_codes=%.4f blanket_rms_codes=%.4f ratio=%.6f\n",covered.size(),mean,minimum,all>0?onLocked/all:0.,form,ripple(b,covered,8,panFrom),ripple(a,covered,8,panFrom),ripple(b,covered,8,panFrom)/ripple(a,covered,8,panFrom));}
    // STATE: the pass at rest on the strut scene (frame 2 inputs; each run below starts from the hostile state).
    {const unsigned n=2;const double jx=halton(n%phases+1,2)-.5,jy=halton(n%phases+1,3)-.5;draw(lockA,n,jx,jy,0,0);FrameInputs in=frameInputs(lockA,n,jx,jy,0);in.caller_scene_open=false;Output out;
        TemporalPass pass;check("ll state initialize",pass.initialize(d,nullptr,resolver));check("ll state configure far",pass.configure_far());
        const bool noProgram=SUCCEEDED(pass.run(in,&out))&&!out.luma_lock&&!pass.diagnostics().luma_lock&&std::strcmp(pass.diagnostics().luma_lock_reason,"program")==0;
        check("ll state configure lock",pass.configure_luma_lock());const bool available=pass.luma_lock_available();
        FrameInputs bad=in;bad.luma_lock_frames=65;const bool refusedT=pass.run(bad,&out)==E_INVALIDARG;bad=in;bad.luma_lock_rho=1.5f;const bool refusedRho=pass.run(bad,&out)==E_INVALIDARG;
        bad=in;bad.luma_lock_tau=33;const bool refusedTau=pass.run(bad,&out)==E_INVALIDARG;bad=in;bad.far_weight=0;const bool refusedFar=pass.run(bad,&out)==E_INVALIDARG;
        check("ll state first",pass.run(in,&out));const bool first=out.luma_lock&&!out.used_history&&std::strcmp(pass.diagnostics().luma_lock_reason,"lock")==0;
        // Hostile RT3, COLORWRITEENABLE3, s13 texture and sampler, c14 / c15: restored.
        Com<IDirect3DSurface9> hostile;check("ll hostile RT3 surface",d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&hostile.p,nullptr));
        const float junk[8]={9,8,7,6,5,4,3,2};check("ll hostile c14",d->SetPixelShaderConstantF(14,junk,2));check("ll hostile s13",d->SetTexture(13,s.wave.p));
        check("ll hostile s13 filter",d->SetSamplerState(13,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("ll hostile RT3",d->SetRenderTarget(3,hostile.p));check("ll hostile CWE3",d->SetRenderState(D3DRS_COLORWRITEENABLE3,5));
        bool rt3Restored=false;{Snapshot before(d);check("ll hostile run",pass.run(in,&out));before.equals(d,"luma lock run restores RT3, COLORWRITEENABLE3, s13 and c14 / c15");
            IDirect3DSurface9* rt3=nullptr;DWORD cwe3=0;check("ll RT3 after",d->GetRenderTarget(3,&rt3));check("ll CWE3 after",d->GetRenderState(D3DRS_COLORWRITEENABLE3,&cwe3));rt3Restored=rt3==hostile.p&&cwe3==5&&out.used_history&&out.luma_lock;if(rt3)rt3->Release();}
        check("ll unbind hostile RT3",d->SetRenderTarget(3,nullptr));check("ll CWE3 reset",d->SetRenderState(D3DRS_COLORWRITEENABLE3,15));check("ll s13 reset",d->SetTexture(13,nullptr));
        // Off the camera gate: no lock (reason), the lane pair released; back on: the history restarts (a lane it did not write).
        in.thin_region_camera_gate=false;check("ll screen gate",pass.run(in,&out));const bool offCamera=!out.luma_lock&&std::strcmp(pass.diagnostics().luma_lock_reason,"no_camera_gate")==0;
        in.thin_region_camera_gate=true;check("ll camera again",pass.run(in,&out));const bool restarted=out.luma_lock&&!out.used_history;check("ll continues",pass.run(in,&out));const bool continued=out.used_history&&out.luma_lock;
        // Not requested: the lane released, the program without the lock; requested again: the history restarts.
        in.luma_lock_frames=0;check("ll not requested",pass.run(in,&out));const bool notRequested=!out.luma_lock&&out.used_history&&std::strcmp(pass.diagnostics().luma_lock_reason,"not_requested")==0;
        in.luma_lock_frames=16;check("ll requested again",pass.run(in,&out));const bool requestedAgain=out.luma_lock&&!out.used_history;
        // A failed resolve draw (draw 2: box, resolve) publishes nothing; the next run restarts.
        bool failedDraw=false;{Output failed;{Fault fault(d,2);failedDraw=pass.run(in,&failed)==E_FAIL&&!failed.color&&!failed.luma_lock;}check("ll recovery",pass.run(in,&out));failedDraw=failedDraw&&out.luma_lock&&!out.used_history;}
        // Reset: the lanes go with the histories; the next run allocates them and restarts.
        pass.before_reset();pass.after_reset(S_OK);check("ll after Reset",pass.run(in,&out));const bool reset=out.luma_lock&&!out.used_history&&pass.diagnostics().luma_lock;
        // The lane pair refused (not a lost device): the lock off until Reset, the run without it; no retry; Reset re-arms.
        pass.before_reset();pass.after_reset(S_OK);bool laneRefused=false;
        {LaneFault fault(d);check("ll run with the lanes refused",pass.run(in,&out));laneRefused=LaneFault::refused>0&&pass.luma_lock_failed()&&pass.luma_lock_result()==D3DERR_OUTOFVIDEOMEMORY&&!out.luma_lock&&std::strcmp(pass.diagnostics().luma_lock_reason,"target")==0&&pass.diagnostics().region_hold;
            const unsigned before=LaneFault::refused;check("ll run after the refusal",pass.run(in,&out));laneRefused=laneRefused&&LaneFault::refused==before&&out.used_history&&!out.luma_lock;}
        pass.before_reset();pass.after_reset(S_OK);check("ll re-armed",pass.run(in,&out));const bool rearmed=out.luma_lock&&!pass.luma_lock_failed();
        std::printf("LUMA_LOCK_STATE no_program=%u available=%u refused_t65=%u refused_rho=%u refused_tau=%u refused_far0=%u first=%u rt3_restored=%u off_camera=%u camera_restarts=%u continues=%u not_requested=%u requested_restarts=%u failed_draw=%u reset=%u lane_refused=%u rearmed=%u\n",
            unsigned(noProgram),unsigned(available),unsigned(refusedT),unsigned(refusedRho),unsigned(refusedTau),unsigned(refusedFar),unsigned(first),unsigned(rt3Restored),unsigned(offCamera),unsigned(restarted),unsigned(continued),
            unsigned(notRequested),unsigned(requestedAgain),unsigned(failedDraw),unsigned(reset),unsigned(laneRefused),unsigned(rearmed));
        require(noProgram&&available&&refusedT&&refusedRho&&refusedTau&&refusedFar&&first&&rt3Restored&&offCamera&&restarted&&continued&&notRequested&&requestedAgain&&failedDraw&&reset&&laneRefused&&rearmed,
            "luma lock state: refusals, reasons, lane lifetime, failed draw, Reset and the lane refusal");}
}
}
using luma_lock::luma_lock_cases;
