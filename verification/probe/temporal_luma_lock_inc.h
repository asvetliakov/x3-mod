// Luminance lock (docs/architecture/taa-luminance-lock.md sections 5, 10, 11 and 12), lattice mode. Included after
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
//   shading-step runs multiply the surround by 0.8 or 0.6 from frame 30 (under the pan; the gated pair on the screen gate:
//   carried locks, no refresh).
//   Runs: blanket (the current program: far weight on every far pixel, 7x7 far clip), base (far weight 0, 3x3 clip: the
//   ordinary-hull path), base_box / base_nostrut_box (base with the 7x7 far clip on the pan frames: the l = 0 path of section
//   12), lock (16, 0.25, 3, release 0.65, camera gate), lock_rho05 (RHO 0.5), lock_always (gate always), lock_yramp,
//   lock_diag (the pan (0.37, 0.23) px/frame), lock_bar (1-px bars at [s + 8, s + 9)), lock_fast / lock_yramp_fast (the pan at
//   73.37 px/frame), lock_off / lock_off_3x3 (no creation; the 7x7 / the 3x3 under the pan) against blanket_base (the blanket
//   program at the base weight), lock_mover_pan (the mover from frame 22, under the pan), lock_step20 / lock_step40 (screen
//   gate) and their _camera twins, identity (16, 1, 32 on the scene without struts) against base_nostrut_box, blanket_slanted /
//   lock_slanted. The strut's true pixels at displacement disp: the cell of s + 0.315 + disp (its centre) per row.
//   Rows (the runner gates them; the fixture prints numbers only, plus the structural requires of LUMA_LOCK_STATE):
//     LUMA_LOCK_FORM: first frame with >= 0.9 of the strut pixels locked (lane .b > 0), from frame 0 and from the cut (48);
//     LUMA_LOCK_PLATE: plate lock share, the largest over every frame and over the rest frames, per run (x and y ramps);
//     LUMA_LOCK_PLATE_SHARP: plate gradient energy (codes) under the pan (frames 24-39) and at rest (8-15) against base and
//       base_box; the plate interior (no locked pixel within 3 px) whose colour differs from base (every frame, rest) and
//       from base_box;
//     LUMA_LOCK_STRUT_RIPPLE: rms of each strut pixel's code about its mean over frames 8-15, lock against blanket;
//     LUMA_LOCK_CARRY: lock share of the strut's true pixels on every pan frame (16-39): min, mean, each frame; the locked
//       pixel-frames carrying a y offset;
//     LUMA_LOCK_CARRY_BAND: locked pixels per strut and row near the struts during the pan (max), and the share of strut-region
//       pixels more than 2 px from a true pixel that are locked (max); the locked pixels by offset -2..2 from the true pixel,
//       those claimed through the margin (|a| >= 0.5), the per-strut count of each pan frame;
//     LUMA_LOCK_RESUME: lock share of the true pixels at the stop (frame 40) and two frames after it (41);
//     LUMA_LOCK_CHATTER: the largest share of strut pixels whose lock toggles between consecutive rest frames after formation
//       (3-15, 42-47 on the resumed pixels, 51-55);
//     LUMA_LOCK_MOVER: locks on the pixels the mover uncovers, at their uncover frame; the strut column's re-formation delay;
//     LUMA_LOCK_SLANTED: the slanted line's covered pixels: lock share at rest and the share of their ripple energy (the std of
//       their input over frames 8-15) on the pixels locked at frame 15; its ripple against blanket;
//     LUMA_LOCK_SHADING: the true pixels' lock share at frames 29-33 and 39 of the step runs;
//     LUMA_LOCK_LANE: rest mode (b < 128) at rest, motion mode (b >= 128) under the pan, locked offsets within 0.75 px, the strut
//       residual's sign alternation at rest, the class / sign bits against the rest-mode residual;
//     LUMA_LOCK_IDENTITY: colour, age and depth bytes of identity against base_nostrut_box over all 72 frames (and the colour
//       against base_nostrut);
//     LUMA_LOCK_PAN_CREATE, LUMA_LOCK_PLATE_PAN, LUMA_LOCK_CLIP_PAN, LUMA_LOCK_MOVER_PAN: the section-12 rows (at each block);
//     LUMA_LOCK_STATE: refusals, the program / camera / lane reasons, the lane refusal and Reset, a failed draw, the hostile
//       state of RT3, CWE3, s13 and c14 / c15 restored.
namespace luma_lock {
constexpr UINT W=512,H=16;constexpr unsigned frames=72,phases=8,panFrom=16,panTo=40,cutAt=48,moverFrom=56,stepAt=30;
constexpr float plateEnd=248,strutOffset=8.115f,strutWidth=.4f,strutPeriod=16,panSpeed=.37f,panSpeedY=.23f,panFast=73.37f;
constexpr float clip7=x3::temporal::kFarClipThreshold,clip3=x3::temporal::kFarClipOff;
constexpr unsigned moverPanFrom=22; // the mover under the pan: screen x 355 + (n - 22), over the strut at 362.7 + 0.37 (n - 22)
constexpr UINT Y0=4,Y1=12,PX0=24,PX1=232,SX0=264,SX1=489,moverStrut=360,moverLeft0=355;
using x3::temporal::LumaLockGate;
// panX / panY: the pan's px/frame (frames 16-39); width / offset: the struts' (a 1-px bar at offset 8: texel-aligned at
// rest); moverAt: the mover's first frame; panBox: the reference of the l = 0 path under section 12 (d), the 7x7 far clip on
// the pan frames and the 3x3 at rest.
struct Config{const char* name;unsigned lockFrames;float rho,tau;LumaLockGate gate;float farWeight,farClip;bool struts;float slope;bool plateY;float step;
    float panX=panSpeed,panY=0,width=strutWidth,offset=strutOffset;unsigned moverAt=moverFrom;bool panBox=false;};
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
    // c0 = (jitter x + displacement, jitter y + y displacement, plate base, ramp per px), c1 = (strut luma, offset, period, width), c2 = (strut slope
    // per row, mover left pixel, mover width, mover luma), c3 = (the plate's end (content x), 1: the ramp along y, the surround's
    // factor, 0).
    compile(compiler,"float4 a:register(c0);float4 b:register(c1);float4 e:register(c2);float4 g:register(c3);"
        "float4 main(float2 vpos:VPOS):COLOR0{float px=floor(vpos.x);float cx=px+0.5-a.x;float cy=floor(vpos.y)+0.5-a.y;float v=a.z*g.z;"
        "if(cx<g.x){float r=g.y>0.5?cy:cx;float t=r-16*floor(r/16);v=a.z+a.w*abs(t-8);}else{float q=cx-e.x*cy;float m=q-b.z*floor(q/b.z)-b.y;if(m>=0&&m<b.w)v=b.x;}"
        "if(px>=e.y&&px<e.y+e.z)v=e.w;return float4(v,v,v,1);}","ps_3_0",&ca.p);
    // c0 = (jitter x, jitter y, this frame's camera motion x, y px), c1 = (1/W, 1/H, strip depth, 0), c2 = (mover left, width,
    // px/frame, depth).
    compile(compiler,"float4 j:register(c0);float4 k:register(c1);float4 m:register(c2);"
        "float4 main(float2 vpos:VPOS):COLOR0{float px=floor(vpos.x);bool mv=px>=m.x&&px<m.x+m.y;float2 v=mv?float2(m.z,0):j.zw;"
        "return float4((floor(vpos)+0.5-j.xy-v)*k.xy,mv?m.w:k.z,1);}","ps_3_0",&cm.p);
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
    // Lane bytes (BGRA): b = lifetime + 32 class + 64 sign + 128 in motion mode, g, r, a = the reference.
    auto mode=[](const std::vector<unsigned char>& lane,UINT i){return lane[i*4]>=128?1u:0u;};
    auto life=[](const std::vector<unsigned char>& lane,UINT i){return unsigned(lane[i*4])&31u;};
    auto classBit=[](const std::vector<unsigned char>& lane,UINT i){return (unsigned(lane[i*4])>>5)&1u;};
    auto signBit=[](const std::vector<unsigned char>& lane,UINT i){return (unsigned(lane[i*4])>>6)&1u;};
    auto laneR=[](const std::vector<unsigned char>& lane,UINT i){return int(lane[i*4+2])-128;};
    auto laneG=[](const std::vector<unsigned char>& lane,UINT i){return int(lane[i*4+1])-128;};
    auto code=[](double y){y=std::max(y,0.);return 255*std::pow(y/(1+y),1/2.2);};
    auto moverLeft=[](const Config& c,unsigned n){return n>=c.moverAt?float(moverLeft0+(n-c.moverAt)):-1e4f;};
    auto panFrame=[](unsigned n){return n>=panFrom&&n<panTo;};
    auto frameInputs=[&](const Config& c,unsigned n,double jx,double jy,double v,double vy){FrameInputs in;in.color=color.p;in.current_depth=depth.p;in.motion=motion.p;in.width=W;in.height=H;in.epoch=1;
        const float path[16]={1,0,0,float(-2*v/W),0,1,0,float(2*vy/H),0,0,1,0,0,0,0,1};std::copy(path,path+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=x3::temporal::kHistoryWeightDefault;in.motion_policy=MotionPolicy::PerPixel;
        in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=true;in.sentinel_strict_sky=true;in.sky_history_exit_px=.25f;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        in.far_weight=c.farWeight;in.far_d0=gateD0;in.far_inv=gateInv;in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
        in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;in.thin_region_hold_frames=phases;in.far_camera_gate=true;in.far_clip=c.panBox?(panFrame(n)?clip7:clip3):c.farClip;
        in.luma_lock_frames=c.lockFrames;in.luma_lock_rho=c.rho;in.luma_lock_tau=c.tau;in.luma_lock_gate=c.gate;in.cut=n==cutAt;return in;};
    // Draws the three inputs of frame n (jitter, the camera displacement (disp, dispY) of the content, this frame's camera
    // motion (v, vy)).
    auto draw=[&](const Config& c,unsigned n,double jx,double jy,double disp,double dispY,double v,double vy){
        target(colorSurface.p,contentPS.p);check("ll Begin",d->BeginScene());
        {const float k[16]={float(jx+disp),float(jy+dispY),.5f,.1f,c.struts?1.5f:.5f,c.offset,strutPeriod,c.width,c.slope,moverLeft(c,n),3,.2f,plateEnd,c.plateY?1.f:0.f,n>=stepAt?c.step:1.f,0};
            check("ll content constants",d->SetPixelShaderConstantF(0,k,4));}
        s.quad(0,0,W,H,0,0);check("ll End",d->EndScene());
        target(motionSurface.p,motionPS.p);check("ll motion Begin",d->BeginScene());
        {const float k[12]={float(jx),float(jy),float(v),float(vy),1.f/W,1.f/H,stripDepth,0,moverLeft(c,n),3,1,.5f};check("ll motion constants",d->SetPixelShaderConstantF(0,k,3));}
        s.quad(0,0,W,H,0,0);check("ll motion End",d->EndScene());
        target(depthSurface.p,depthPS.p);check("ll depth Begin",d->BeginScene());
        {const float k[8]={stripDepth,0,0,0,moverLeft(c,n),3,1,.5f};check("ll depth constants",d->SetPixelShaderConstantF(0,k,2));}
        s.quad(0,0,W,H,0,0);check("ll depth End",d->EndScene());};
    auto sequence=[&](const Config& c){TemporalPass pass;check("ll initialize",pass.initialize(d,nullptr,resolver));check("ll configure far",pass.configure_far());
        if(c.lockFrames)check("ll configure lock",pass.configure_luma_lock());
        require(pass.camera_gate_available()&&(!c.lockFrames||pass.luma_lock_available()),"ll camera gate (and the lock) available");
        Run run;double disp=0,dispY=0;
        for(unsigned n=0;n<frames;++n){const unsigned index=n%phases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
            const double v=panFrame(n)?c.panX:0,vy=panFrame(n)?c.panY:0;if(n==cutAt)disp=dispY=0;disp+=v;dispY+=vy;run.disp.push_back(disp);
            draw(c,n,jx,jy,disp,dispY,v,vy);FrameInputs in=frameInputs(c,n,jx,jy,v,vy);Output out;
            check("ll Begin resolve",d->BeginScene());check("ll resolve",pass.run(in,&out));check("ll End resolve",d->EndScene());
            require(out.color&&out.age&&out.depth&&out.used_history==(n>0&&n!=cutAt)&&bool(out.luma_lock)==(c.lockFrames>0)&&pass.diagnostics().luma_lock==(c.lockFrames>0),"ll history and the lock lane follow the sequence");
            run.color.push_back(raw(out.color));run.age.push_back(raw(out.age));run.depth.push_back(raw(out.depth));run.lane.push_back(out.luma_lock?raw(out.luma_lock):std::vector<unsigned char>());
            const auto input=raw(color.p);std::vector<float> in16(W*H);for(UINT i=0;i<W*H;++i)in16[i]=float(red(input,i));run.input.push_back(in16);}
        return run;};
    constexpr LumaLockGate camera=LumaLockGate::Camera;
    const Config blanket{"blanket",0,.25f,2,camera,.985f,clip7,true,0,false,1},base{"base",0,.25f,2,camera,0.f,clip3,true,0,false,1},
        lockA{"lock",16,.25f,3,camera,.985f,clip7,true,0,false,1},lockB{"lock_rho05",16,.5f,3,camera,.985f,clip7,true,0,false,1},
        lockAlways{"lock_always",16,.25f,3,LumaLockGate::Always,.985f,clip7,true,0,false,1},lockY{"lock_yramp",16,.25f,3,camera,.985f,clip7,true,0,true,1},
        step20{"lock_step20",16,.25f,3,LumaLockGate::Screen,.985f,clip7,true,0,false,.8f},step40{"lock_step40",16,.25f,3,LumaLockGate::Screen,.985f,clip7,true,0,false,.6f},
        baseFlat{"base_nostrut",0,.25f,2,camera,0.f,clip3,false,0,false,1},identity{"identity",16,1.f,32,camera,.985f,clip7,false,0,false,1},
        blanketSlanted{"blanket_slanted",0,.25f,2,camera,.985f,clip7,true,.2f,false,1},lockSlanted{"lock_slanted",16,.25f,3,camera,.985f,clip7,true,.2f,false,1};
    // Section 12: the l = 0 references with the pan's 7x7 (base_box, base_nostrut_box), the bar, the fast pans, the diagonal
    // pan, creation off with the 7x7 / 3x3 under the pan against the blanket at the base weight, the mover under the pan.
    const Config baseBox{"base_box",0,.25f,2,camera,0.f,clip3,true,0,false,1,panSpeed,0,strutWidth,strutOffset,moverFrom,true},
        baseFlatBox{"base_nostrut_box",0,.25f,2,camera,0.f,clip3,false,0,false,1,panSpeed,0,strutWidth,strutOffset,moverFrom,true},
        lockBar{"lock_bar",16,.25f,3,camera,.985f,clip7,true,0,false,1,panSpeed,0,1.f,8.f},
        lockFast{"lock_fast",16,.25f,3,camera,.985f,clip7,true,0,false,1,panFast},lockYFast{"lock_yramp_fast",16,.25f,3,camera,.985f,clip7,true,0,true,1,panFast},
        lockDiag{"lock_diag",16,.25f,3,camera,.985f,clip7,true,0,false,1,panSpeed,panSpeedY},
        lockOff{"lock_off",16,.25f,3,LumaLockGate::Off,.985f,clip7,true,0,false,1},lockOff3{"lock_off_3x3",16,.25f,3,LumaLockGate::Off,.985f,clip3,true,0,false,1},
        blanketBase{"blanket_base",0,.25f,2,camera,float(x3::temporal::kHistoryWeightDefault),clip7,true,0,false,1},
        lockMoverPan{"lock_mover_pan",16,.25f,3,camera,.985f,clip7,true,0,false,1,panSpeed,0,strutWidth,strutOffset,moverPanFrom};
    const Run rBlanket=sequence(blanket),rBase=sequence(base),rLock=sequence(lockA),rLockB=sequence(lockB),rAlways=sequence(lockAlways),rY=sequence(lockY),
        rBaseBox=sequence(baseBox),rDiag=sequence(lockDiag);
    // The strut pixels at rest (columns 264 + 16 k, rows 4..11) and the strut's true pixels (the cell of its centre) at disp.
    std::vector<UINT> strutRest;for(UINT x=SX0;x<SX1;x+=16)for(UINT y=Y0;y<Y1;++y)strutRest.push_back(y*W+x);
    auto truePx=[&](double disp){std::vector<UINT> out;for(UINT s0=SX0-24;s0<SX1-8;s0+=16){const UINT x=UINT(std::floor(double(s0)+8.315+disp));for(UINT y=Y0;y<Y1;++y)out.push_back(y*W+x);}return out;};
    const size_t struts=truePx(0).size()/(Y1-Y0); // every strut of the surround (content x 248.115 onwards: the first enters the band region under the pan)
    auto share=[&](const Run& r,unsigned n,const std::vector<UINT>& px){unsigned locked=0;for(UINT i:px)locked+=life(r.lane[n],i)>0;return px.empty()?0.:double(locked)/px.size();};
    std::vector<UINT> plate;for(UINT y=Y0;y<Y1;++y)for(UINT x=PX0;x<PX1;++x)plate.push_back(y*W+x);
    auto rest=[](unsigned n){return n<panFrom||(n>=panTo&&n<cutAt)||(n>cutAt&&n<moverFrom);};
    for(const Run* r:{&rLock,&rLockB,&rAlways,&rY,&rDiag}){const char* name=r==&rLock?lockA.name:r==&rLockB?lockB.name:r==&rAlways?lockAlways.name:r==&rY?lockY.name:lockDiag.name;
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
            {char b[16];std::snprintf(b,sizeof b,"%s%.2f",first.empty()?"":",",v);first+=b;}}
        // The y transport: locked pan pixel-frames of the strut region whose carried offset has a y part (lane g != 128).
        unsigned yOffsets=0;for(unsigned n=panFrom;n<panTo;++n)for(UINT y=Y0;y<Y1;++y)for(UINT x=SX0-8;x<W-8;++x){const UINT i=y*W+x;yOffsets+=life(r->lane[n],i)>0&&laneG(r->lane[n],i)!=0;}
        std::printf("LUMA_LOCK_CARRY run=%s pan_px_per_frame=%.2f pan_y_px_per_frame=%.2f frames=%u-%u share_min=%.4f share_mean=%.4f locked_y_offsets=%u frames_16_39=%s\n",name,double(panSpeed),r==&rDiag?double(panSpeedY):0.,panFrom,panTo-1,carryMin,carryMean,yOffsets,first.c_str());
        // CARRY_BAND: the two parts of the strut region during the pan: the locked pixels within 2 px of a true pixel, per strut
        // and row, and the locked share of the pixels farther away (with where they are at the worst frame: x offset from the
        // nearest true pixel, and the frame); the rest halo (locked pixels within 2 px per strut and row at frame 15).
        double perStrut=0,perStrutMean=0,offMax=0;unsigned offFrame=0,dxHist[5]{},margin=0;std::string offWhere,band;
        auto nearest=[&](const std::vector<UINT>& tp,UINT y,UINT x){int best=1<<20;for(UINT t:tp)if(t/W==y&&std::abs(int(t%W)-int(x))<std::abs(best))best=int(x)-int(t%W);return best;};
        for(unsigned n=panFrom;n<panTo;++n){const auto tp=truePx(r->disp[n]);unsigned nearLocked=0,off=0,offLocked=0;std::string where;
            for(UINT y=Y0;y<Y1;++y)for(UINT x=SX0-8;x<W-8;++x){const UINT i=y*W+x;const bool l=life(r->lane[n],i)>0;const int dx=nearest(tp,y,x);
                if(std::abs(dx)<=2){nearLocked+=l;if(l){++dxHist[dx+2];margin+=std::abs(laneR(r->lane[n],i))>=64;}}
                else{++off;offLocked+=l;if(l&&where.size()<120){char b[24];std::snprintf(b,sizeof b,"%s%u:%+d",where.empty()?"":",",x,dx);where+=b;}}}
            const double v=double(nearLocked)/(struts*(Y1-Y0));perStrut=std::max(perStrut,v);perStrutMean+=v/(panTo-panFrom);{char b[16];std::snprintf(b,sizeof b,"%s%.2f",band.empty()?"":",",v);band+=b;}
            const double o=off?double(offLocked)/off:0.;if(o>offMax){offMax=o;offFrame=n;offWhere=where;}}
        unsigned halo=0;for(UINT y=Y0;y<Y1;++y)for(UINT x=SX0-8;x<W-8;++x){const int dx=nearest(truePx(0),y,x);if(std::abs(dx)<=2)halo+=life(r->lane[15],y*W+x)>0;}
        std::printf("LUMA_LOCK_CARRY_BAND run=%s locked_per_strut_max=%.4f locked_per_strut_mean=%.4f off_strut_share_max=%.4f off_frame=%u off_px=%s rest_halo_per_strut=%.4f dx_m2_to_p2=%u,%u,%u,%u,%u margin_claims=%u per_frame=%s\n",
            name,perStrut,perStrutMean,offMax,offFrame,offWhere.empty()?"-":offWhere.c_str(),double(halo)/(struts*(Y1-Y0)),dxHist[0],dxHist[1],dxHist[2],dxHist[3],dxHist[4],margin,band.c_str());
        const auto resumed=truePx(r->disp[panTo]);
        std::printf("LUMA_LOCK_RESUME run=%s disp=%.2f share_frame40=%.4f share_frame41=%.4f share_frame47=%.4f\n",name,r->disp[panTo],share(*r,panTo,resumed),share(*r,panTo+1,resumed),share(*r,cutAt-1,resumed));
        double chatter=0;for(unsigned n=3;n<moverFrom;++n){if(!((n<panFrom)||(n>=panTo+2&&n<cutAt)||(n>=cutAt+3)))continue;const auto& px=n>=panTo&&n<cutAt?resumed:strutRest;unsigned toggles=0;
            for(UINT i:px){toggles+=(life(r->lane[n],i)>0)!=(life(r->lane[n-1],i)>0);}
            chatter=std::max(chatter,double(toggles)/px.size());}
        std::printf("LUMA_LOCK_CHATTER run=%s frames=3-15,42-47,51-55 toggle_share_max=%.4f\n",name,chatter);
        unsigned uncovered=0,lockedAtUncover=0;int reform=-1;
        for(unsigned u=moverFrom+1;u<frames;++u){const float l0=moverLeft(lockA,u-1),l1=moverLeft(lockA,u);
            for(UINT x=UINT(l0);x<UINT(l0)+3;++x){if(float(x)>=l1&&float(x)<l1+3)continue;
                for(UINT y=Y0;y<Y1;++y){++uncovered;lockedAtUncover+=life(r->lane[u],y*W+x)>0;}
                if(x==moverStrut){int worst=0;for(UINT y=Y0;y<Y1;++y){int delay=-1;for(unsigned n=u;n<frames&&delay<0;++n)if(life(r->lane[n],y*W+x)>0)delay=int(n-u);worst=delay<0||worst<0?-1:std::max(worst,delay);}reform=worst;}}}
        std::printf("LUMA_LOCK_MOVER run=%s uncovered_px=%u locked_at_uncover=%u strut_x=%u reform_delay_frames=%d\n",name,uncovered,lockedAtUncover,moverStrut,reform);}
    // PLATE_SHARP (lock against base) and the interior identity; STRUT_RIPPLE (lock against blanket).
    auto energy=[&](const Run& r,unsigned from,unsigned to){double e=0;unsigned count=0;for(unsigned n=from;n<to;++n)for(UINT y=Y0;y+1<Y1;++y)for(UINT x=PX0+1;x+1<PX1;++x){
        const double c=code(red(r.color[n],y*W+x)),cx=code(red(r.color[n],y*W+x+1)),cy=code(red(r.color[n],(y+1)*W+x));e+=(cx-c)*(cx-c)+(cy-c)*(cy-c);++count;}return e/count;};
    for(const Run* r:{&rLock,&rLockB}){const char* name=r==&rLock?lockA.name:lockB.name;unsigned interior=0,differs=0,differsRest=0,differsBox=0;
        for(unsigned n=0;n<frames;++n)for(UINT y=Y0;y<Y1;++y)for(UINT x=PX0;x<PX1;++x){bool nearLock=false;
            for(int dy=-3;dy<=3&&!nearLock;++dy)for(int dx=-3;dx<=3&&!nearLock;++dx){const int yy=int(y)+dy,xx=int(x)+dx;if(yy>=0&&yy<int(H)&&life(r->lane[n],UINT(yy)*W+UINT(xx))>0)nearLock=true;}
            if(nearLock)continue;
            ++interior;const UINT i=y*W+x;
            if(std::memcmp(&r->color[n][i*8],&rBase.color[n][i*8],8)!=0){++differs;if(rest(n))++differsRest;}
            differsBox+=std::memcmp(&r->color[n][i*8],&rBaseBox.color[n][i*8],8)!=0;}
        const double ePan=energy(*r,24,panTo),eBasePan=energy(rBase,24,panTo),eRest=energy(*r,8,panFrom),eBaseRest=energy(rBase,8,panFrom),eBoxPan=energy(rBaseBox,24,panTo);
        // _box: against base_box, the l = 0 path of section 12 (the 7x7 under the pan, the 3x3 at rest); the base fields are the
        // section-11 reference (the 3x3 under the pan too).
        std::printf("LUMA_LOCK_PLATE_SHARP run=%s e_pan=%.4f e_pan_base=%.4f e_pan_ratio=%.6f e_pan_box=%.4f e_pan_ratio_box=%.6f e_rest=%.4f e_rest_base=%.4f e_rest_ratio=%.6f interior_px_frames=%u interior_differs=%u interior_differs_rest=%u interior_differs_box=%u\n",
            name,ePan,eBasePan,ePan/eBasePan,eBoxPan,ePan/eBoxPan,eRest,eBaseRest,eRest/eBaseRest,interior,differs,differsRest,differsBox);}
    auto ripple=[&](const Run& r,const std::vector<UINT>& px,unsigned from,unsigned to){double sum=0;for(UINT i:px){double mean=0;for(unsigned n=from;n<to;++n)mean+=code(red(r.color[n],i))/(to-from);
        for(unsigned n=from;n<to;++n){const double e=code(red(r.color[n],i))-mean;sum+=e*e/(to-from);}}return std::sqrt(sum/px.size());};
    {const double lock=ripple(rLock,strutRest,8,panFrom),ref=ripple(rBlanket,strutRest,8,panFrom),lockB=ripple(rLockB,strutRest,8,panFrom),base=ripple(rBase,strutRest,8,panFrom);
        std::printf("LUMA_LOCK_STRUT_RIPPLE frames=8-15 strut_px=%zu lock_rms_codes=%.4f blanket_rms_codes=%.4f ratio=%.6f lock_rho05_ratio=%.6f base_rms_codes=%.4f\n",
            strutRest.size(),lock,ref,lock/ref,lockB/ref,base);}
    // SHADING: the surround x 0.8 / x 0.6 from frame 30, under the pan. The gated runs (lock_step20 / lock_step40) take the
    // screen gate, so the pan carries the locks without refresh or creation and the row isolates the release rule (section 11);
    // the _camera runs take the default gate (section 12), where a released strut re-locks on its next flip (info).
    const Config step20Camera{"lock_step20_camera",16,.25f,3,camera,.985f,clip7,true,0,false,.8f},
        step40Camera{"lock_step40_camera",16,.25f,3,camera,.985f,clip7,true,0,false,.6f};
    for(const Config* c:{&step20,&step40,&step20Camera,&step40Camera}){const Run r=sequence(*c);
        auto at=[&](unsigned n){return share(r,n,truePx(r.disp[n]));};
        std::printf("LUMA_LOCK_SHADING run=%s factor=%.2f share_frame29=%.4f share_frame30=%.4f share_frame31=%.4f share_frame32=%.4f share_frame33=%.4f share_frame39=%.4f\n",
            c->name,double(c->step),at(stepAt-1),at(stepAt),at(stepAt+1),at(stepAt+2),at(stepAt+3),at(panTo-1));}
    // LANE: rest mode at rest and motion mode under the pan (x 8..W-9: the leading edge is current-only), carried offsets of the
    // locked pixels within 0.75 px, the strut residual alternating in sign at rest (frames 3..15).
    // Section 12: the class / sign bits of b agree with the rest-mode residual in r (class: |e| >= max(TAU, 1) = 3; sign: e < 0).
    {unsigned restChecked=0,restMotion=0,panChecked=0,panRest=0,offsets=0,offsetsOut=0,alternations=0,notAlternating=0,bitsChecked=0,bitsWrong=0;
        for(unsigned n=1;n<cutAt;++n)for(UINT y=1;y+1<H;++y)for(UINT x=8;x+8<W;++x){const UINT i=y*W+x;
            if(!mode(rLock.lane[n],i)){const int e=laneR(rLock.lane[n],i);++bitsChecked;bitsWrong+=classBit(rLock.lane[n],i)!=unsigned(std::abs(e)>=3)||signBit(rLock.lane[n],i)!=unsigned(e<0&&std::abs(e)>=3);}
            if(n<panFrom||n>=panTo){++restChecked;restMotion+=mode(rLock.lane[n],i);}
            else{++panChecked;panRest+=1-mode(rLock.lane[n],i);if(life(rLock.lane[n],i)>0){++offsets;offsetsOut+=std::abs(laneR(rLock.lane[n],i))>=96||std::abs(laneG(rLock.lane[n],i))>=96;}}}
        for(unsigned n=3;n<panFrom;++n)for(UINT i:strutRest){++alternations;notAlternating+=!(laneR(rLock.lane[n],i)*laneR(rLock.lane[n-1],i)<0);}
        std::printf("LUMA_LOCK_LANE rest_px_frames=%u rest_in_motion_mode=%u pan_px_frames=%u pan_in_rest_mode=%u locked_offsets=%u offsets_beyond_three_quarters=%u strut_residuals=%u not_alternating=%u bits_checked=%u bits_wrong=%u\n",
            restChecked,restMotion,panChecked,panRest,offsets,offsetsOut,alternations,notAlternating,bitsChecked,bitsWrong);}
    // IDENTITY: no pixel reaches tau (RHO 1, TAU 32; no struts): l = 0 and no candidate anywhere.
    // Section 12: against base_nostrut_box (the 7x7 on the pan frames, the 3x3 at rest: the l = 0 path now); the _3x3 field is
    // the colour against base_nostrut (the 3x3 under the pan too), informational.
    {const Run a=sequence(baseFlatBox),b=sequence(identity),a3=sequence(baseFlat);unsigned colour=0,age=0,depthBytes=0,locked=0,colour3=0;
        for(unsigned n=0;n<frames;++n){for(UINT i=0;i<W*H;++i){colour+=std::memcmp(&a.color[n][i*8],&b.color[n][i*8],8)!=0;age+=std::memcmp(&a.age[n][i*4],&b.age[n][i*4],4)!=0;
            depthBytes+=std::memcmp(&a.depth[n][i*4],&b.depth[n][i*4],4)!=0;locked+=life(b.lane[n],i)>0;colour3+=std::memcmp(&a3.color[n][i*8],&b.color[n][i*8],8)!=0;}}
        std::printf("LUMA_LOCK_IDENTITY frames=%u px=%u colour_differs=%u age_differs=%u depth_differs=%u locked_px_frames=%u colour_differs_3x3=%u\n",frames,W*H,colour,age,depthBytes,locked,colour3);}
    // SLANTED: the FAR_JITTER_LINE line (0.4 px, 0.2 px per row) at rest: the pixels whose input shows it in some phase of the
    // cycle (frames 8..15), their lock share, the share of their ripple energy (input std over the cycle) on the pixels locked at
    // frame 15, and their output ripple against blanket.
    {const Run a=sequence(blanketSlanted),b=sequence(lockSlanted);std::vector<UINT> covered;for(UINT y=Y0;y<Y1;++y)for(UINT x=SX0-8;x<SX1+8;++x){bool hit=false;for(unsigned n=8;n<panFrom;++n)hit=hit||b.input[n][y*W+x]>1.f;if(hit)covered.push_back(y*W+x);}
        double mean=0,minimum=1,all=0,onLocked=0;for(unsigned n=8;n<panFrom;++n){const double v=share(b,n,covered);mean+=v/(panFrom-8);minimum=std::min(minimum,v);}
        for(UINT i:covered){double m=0,sq=0;for(unsigned n=8;n<panFrom;++n){m+=b.input[n][i]/8.;sq+=double(b.input[n][i])*b.input[n][i]/8.;}const double sd=std::sqrt(std::max(sq-m*m,0.));all+=sd;if(life(b.lane[15],i)>0)onLocked+=sd;}
        int form=-1;for(unsigned n=0;n<panFrom&&form<0;++n)if(share(b,n,covered)>=.9)form=int(n);
        std::printf("LUMA_LOCK_SLANTED covered_px=%zu share_mean_8_15=%.4f share_min_8_15=%.4f energy_on_locked=%.4f form=%d lock_rms_codes=%.4f blanket_rms_codes=%.4f ratio=%.6f\n",covered.size(),mean,minimum,all>0?onLocked/all:0.,form,ripple(b,covered,8,panFrom),ripple(a,covered,8,panFrom),ripple(b,covered,8,panFrom)/ripple(a,covered,8,panFrom));}
    // Section 12 rows. PAN_CREATE: 1-px bars at [s + 8, s + 9) (texel-aligned at rest: every phase samples them, no flip), the
    // pan from 16 sweeps them across texel boundaries. Per bar the followed pixel (the cell of s + 8.5 + disp, row Y0) gives the
    // first residual flip (the lane's class bit set on consecutive frames with opposite sign bits) and the first frame from it
    // at which >= 0.9 of the bar's pixels (every row) are locked; then the share of all bars' pixels from the last formation
    // to the pan's end (carried), at the stop (40) and at 41 (resumed); the rest share (0-15).
    {const Run r=sequence(lockBar);
        auto barPx=[&](double disp){std::vector<UINT> out;for(UINT s0=SX0-24;s0<SX1-8;s0+=16){const UINT x=UINT(std::floor(double(s0)+8.5+disp));for(UINT y=Y0;y<Y1;++y)out.push_back(y*W+x);}return out;};
        const UINT rows=Y1-Y0;const size_t bars=barPx(0).size()/rows;double restMax=0;for(unsigned n=0;n<panFrom;++n)restMax=std::max(restMax,share(r,n,barPx(r.disp[n])));
        int delayMax=-1,flipFirst=-1,formLast=-1;unsigned noFlip=0,unformed=0;
        for(size_t b=0;b<bars;++b){int flip=-1,form=-1;
            for(unsigned n=panFrom;n<panTo&&form<0;++n){const auto now=barPx(r.disp[n]),before=barPx(r.disp[n-1]);const UINT i=now[b*rows],j=before[b*rows];
                if(flip<0&&classBit(r.lane[n],i)&&classBit(r.lane[n-1],j)&&signBit(r.lane[n],i)!=signBit(r.lane[n-1],j))flip=int(n);
                if(flip>=0){unsigned locked=0;for(UINT y=0;y<rows;++y)locked+=life(r.lane[n],now[b*rows+y])>0;if(locked>=.9*rows)form=int(n);}}
            if(flip<0){++noFlip;continue;}
            flipFirst=flipFirst<0?flip:std::min(flipFirst,flip);
            if(form<0){++unformed;continue;}
            delayMax=std::max(delayMax,form-flip);formLast=std::max(formLast,form);}
        double carried=1;for(unsigned n=formLast<0?panTo:unsigned(formLast);n<panTo;++n)carried=std::min(carried,share(r,n,barPx(r.disp[n])));
        const auto stopped=barPx(r.disp[panTo]);
        std::printf("LUMA_LOCK_PAN_CREATE bars=%zu rest_share_max=%.4f first_flip=%d form_last=%d delay_max=%d no_flip=%u unformed=%u carried_min=%.4f share_frame40=%.4f share_frame41=%.4f\n",
            bars,restMax,flipFirst,formLast,delayMax,noFlip,unformed,carried,share(r,panTo,stopped),share(r,panTo+1,stopped));}
    // PLATE_PAN: the x and y ramps under the 0.37 (lock, lock_yramp) and the 73.37 px/frame pans: the plate lock share, the
    // largest over the pan frames and over every frame.
    {const Run f=sequence(lockFast),fy=sequence(lockYFast);
        for(const Run* r:{&rLock,&rY,&f,&fy}){const char* name=r==&rLock?lockA.name:r==&rY?lockY.name:r==&f?lockFast.name:lockYFast.name;
            double panMax=0,allMax=0;unsigned worst=0;for(unsigned n=0;n<frames;++n){const double v=share(*r,n,plate);if(v>allMax){allMax=v;worst=n;}if(panFrame(n))panMax=std::max(panMax,v);}
            // Where they sit at the worst frame: the locked columns (x, up to 16) and the mean age count of the locked pixels.
            std::string where;double ageSum=0;unsigned count=0;for(UINT x=PX0;x<PX1;++x){bool any=false;for(UINT y=Y0;y<Y1;++y){const UINT i=y*W+x;if(life(r->lane[worst],i)>0){any=true;float a;std::memcpy(&a,&r->age[worst][i*4],4);ageSum+=std::floor(std::fabs(a));++count;}}
                if(any&&where.size()<96){char b[16];std::snprintf(b,sizeof b,"%s%u",where.empty()?"":",",x);where+=b;}}
            std::printf("LUMA_LOCK_PLATE_PAN run=%s ramp=%s pan_px_per_frame=%.2f share_pan_max=%.4f share_max=%.4f worst_frame=%u locked_x=%s locked_age_mean=%.2f\n",name,r==&rY||r==&fy?"y":"x",
                r==&f||r==&fy?double(panFast):double(panSpeed),panMax,allMax,worst,where.empty()?"-":where.c_str(),count?ageSum/count:0.);}}
    // CLIP_PAN: creation off (no lock anywhere): the struts' followed pixels (the true pixel and its two neighbours per row) over
    // the pan frames 24-39, rms of the code about each one's mean, with the 7x7 under the pan (lock_off) and with the 3x3
    // (lock_off_3x3, the far clip off) against the blanket program at the base weight (blanket_base: the 7x7 clip class of
    // run333-352 on every far pixel, the weight of an unlocked pixel).
    {const Run off=sequence(lockOff),off3=sequence(lockOff3),ref=sequence(blanketBase);
        auto panRipple=[&](const Run& r){double sum=0;unsigned count=0;const size_t n0=truePx(0).size();
            for(size_t k=0;k<n0;++k)for(int dx=-1;dx<=1;++dx){double mean=0,square=0;
                for(unsigned n=24;n<panTo;++n){const double c=code(red(r.color[n],UINT(int(truePx(r.disp[n])[k])+dx)));mean+=c/(panTo-24);square+=c*c/(panTo-24);}
                sum+=std::max(square-mean*mean,0.);++count;}
            return std::sqrt(sum/count);};
        unsigned locked=0;for(unsigned n=0;n<frames;++n)for(UINT i=0;i<W*H;++i)locked+=life(off.lane[n],i)>0;
        const double box=panRipple(off),clip3r=panRipple(off3),blanketBaseR=panRipple(ref);
        std::printf("LUMA_LOCK_CLIP_PAN frames=24-39 px=%zu box_rms_codes=%.4f clip3_rms_codes=%.4f blanket_base_rms_codes=%.4f box_ratio=%.6f clip3_ratio=%.6f locked_px_frames=%u\n",
            3*truePx(0).size(),box,clip3r,blanketBaseR,box/blanketBaseR,clip3r/blanketBaseR,locked);}
    // MOVER_PAN: the 3-px mover (1 px/frame on screen, 0.63 against the pan) from frame 22 under the pan, over the strut at
    // content 360.115: locked pixels under the mover (every covered pixel-frame, 22-39), locks at uncover, the strut's lock share
    // on the last frame before the mover's 3x3 reaches it (the lock the cover releases; one frame later the resolve's dilation
    // already follows the mover beside it) and its re-formation after the mover left it.
    {const Run r=sequence(lockMoverPan);unsigned covered=0,coveredLocked=0,uncovered=0,lockedAtUncover=0;
        for(unsigned n=moverPanFrom;n<panTo;++n){const float l=moverLeft(lockMoverPan,n),lp=moverLeft(lockMoverPan,n-1);
            for(UINT x=UINT(l);x<UINT(l)+3;++x)for(UINT y=Y0;y<Y1;++y){++covered;coveredLocked+=life(r.lane[n],y*W+x)>0;}
            if(n>moverPanFrom)for(UINT x=UINT(lp);x<UINT(lp)+3;++x){if(float(x)>=l&&float(x)<l+3)continue;for(UINT y=Y0;y<Y1;++y){++uncovered;lockedAtUncover+=life(r.lane[n],y*W+x)>0;}}}
        auto strutAt=[&](unsigned n){std::vector<UINT> px;const UINT x=UINT(std::floor(352+8.315+r.disp[n]));for(UINT y=Y0;y<Y1;++y)px.push_back(y*W+x);return px;};
        auto coveredAt=[&](unsigned n){const float l=moverLeft(lockMoverPan,n),x=float(strutAt(n)[0]%W);return x>=l&&x<l+3;};
        int firstCover=-1,lastCover=-1,reform=-1,before=-1;for(unsigned n=moverPanFrom;n<panTo;++n)if(coveredAt(n)){if(firstCover<0)firstCover=int(n);lastCover=int(n);}
        for(unsigned n=moverPanFrom;firstCover>0&&n<unsigned(firstCover);++n)if(float(strutAt(n)[0]%W)>=moverLeft(lockMoverPan,n)+4)before=int(n);
        if(lastCover>=0)for(unsigned n=unsigned(lastCover)+1;n<panTo&&reform<0;++n)if(share(r,n,strutAt(n))>=.9)reform=int(n)-lastCover;
        std::printf("LUMA_LOCK_MOVER_PAN frames=%u-%u covered_px_frames=%u covered_locked=%u uncovered_px=%u locked_at_uncover=%u strut_first_cover=%d strut_before_frame=%d strut_share_before=%.4f strut_last_cover=%d strut_reform_frames=%d\n",
            moverPanFrom,panTo-1,covered,coveredLocked,uncovered,lockedAtUncover,firstCover,before,before>=0?share(r,unsigned(before),strutAt(unsigned(before))):-1.,lastCover,reform);}
    // STATE: the pass at rest on the strut scene (frame 2 inputs; each run below starts from the hostile state).
    {const unsigned n=2;const double jx=halton(n%phases+1,2)-.5,jy=halton(n%phases+1,3)-.5;draw(lockA,n,jx,jy,0,0,0,0);FrameInputs in=frameInputs(lockA,n,jx,jy,0,0);in.caller_scene_open=false;Output out;
        TemporalPass pass;check("ll state initialize",pass.initialize(d,nullptr,resolver));check("ll state configure far",pass.configure_far());
        const bool noProgram=SUCCEEDED(pass.run(in,&out))&&!out.luma_lock&&!pass.diagnostics().luma_lock&&std::strcmp(pass.diagnostics().luma_lock_reason,"program")==0;
        check("ll state configure lock",pass.configure_luma_lock());const bool available=pass.luma_lock_available();
        FrameInputs bad=in;bad.luma_lock_frames=32;const bool refusedT=pass.run(bad,&out)==E_INVALIDARG;bad=in;bad.luma_lock_rho=1.5f;const bool refusedRho=pass.run(bad,&out)==E_INVALIDARG;
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
        std::printf("LUMA_LOCK_STATE no_program=%u available=%u refused_t32=%u refused_rho=%u refused_tau=%u refused_far0=%u first=%u rt3_restored=%u off_camera=%u camera_restarts=%u continues=%u not_requested=%u requested_restarts=%u failed_draw=%u reset=%u lane_refused=%u rearmed=%u\n",
            unsigned(noProgram),unsigned(available),unsigned(refusedT),unsigned(refusedRho),unsigned(refusedTau),unsigned(refusedFar),unsigned(first),unsigned(rt3Restored),unsigned(offCamera),unsigned(restarted),unsigned(continued),
            unsigned(notRequested),unsigned(requestedAgain),unsigned(failedDraw),unsigned(reset),unsigned(laneRefused),unsigned(rearmed));
        require(noProgram&&available&&refusedT&&refusedRho&&refusedTau&&refusedFar&&first&&rt3Restored&&offCamera&&restarted&&continued&&notRequested&&requestedAgain&&failedDraw&&reset&&laneRefused&&rearmed,
            "luma lock state: refusals, reasons, lane lifetime, failed draw, Reset and the lane refusal");}
}
}
using luma_lock::luma_lock_cases;
