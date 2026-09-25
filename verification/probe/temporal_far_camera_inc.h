// Far weight under a camera pan (docs/architecture/taa-mask-fold.md section 4.2 addendum "far weight on the camera gate";
// run327 "sparkles"), lattice mode. Included after temporal_region_hold_inc.h: uses held_region (temporal_line_inc.h), halton,
// EdgeScene and the metric helpers.
//   FAR_CAMERA_PAN: a far (farw 1), routed, uniform-depth strip (512 x 16) carrying world-static bright lines (period 16 px,
//   0.05 / 2.0; width 1 px, and 0.4 px for a line the jittered sample hits in some frames only), sampled at the jittered raster
//   position, static 64 frames and then moving 32: a pure camera yaw of 10 (integer), 10.5 (half-texel) or 8.25 px/frame with
//   the lines world-static (their motion vectors carry the yaw); and, moving from frame 0, a far mover (camera static, lines
//   moving 10 px/frame on screen) and a co-moving object (camera yawing 10 px/frame, lines screen-static). Uniform depth: the
//   fragmented-depth search never flags, so every pixel is on the plain 3x3-clip path (region_px counts the hold fraction's
//   region bits). Programs: the camera-gate resolve with the far weight 0.985 on its camera gate (X3M_TAA_FAR_GATE=camera, the
//   default), on the screen speed gate (=screen, c11.x = 1) and with the far weight off (its base 0.9), and the far program alone
//   (the screen-gate path: thin region off), 0.985. screen_program_diff: the camera-gate resolve on the screen gate against
//   the far program, largest output difference (the two are the same far arithmetic on this strip). Per run, over the last 16 frames and x >= 352 (every
//   history chain reaches back into the static phase):
//     sparkles: the run327 triage's one-frame sparkle (verification/results/run327-run83b-sparkles/sparkles.py) on codes
//       c = 255 (Y / (1 + Y))^(1 / 2.2): c_n(x) above the 1x3 maximum of frame n - 1 and of frame n + 1 at the same content
//       (the whole-pixel shift, the 1x3 maximum absorbing the sub-pixel residual) by more than 6 codes; spike = the largest
//       such margin, count = the pixels above 6;
//     profile_rms: the rms of each sample about the mean of its content-phase class ((x - displacement) mod 16 in quarter
//       pixels): the drift of the converging profile, not a one-frame event;
//     peak: the mean over frames of the brightest pixel (the lines' resolved height; the resampling softening);
//     off_diff: the largest output difference against the far-off run; rest_content_diff (integer speeds): the largest
//       difference against the rest run at the same content.
//   Asserted on the camera-gate program: no pixel in the thin region; the integer yaw's largest one-frame margin equal to rest's
//   within 0.1 code, the fractional yaws' not above rest's + 1 code (the 0.4 px line carries the discrimination: 14.2 codes at
//   rest, 57.9 on the screen gate at the integer yaw); the integer
//   yaw bit-identical to rest in the content frame; the mover bit-identical to the far weight off (the weight closes for
//   content moving against the camera path); the co-moving lines bit-identical to rest (screen-static: the camera openness is
//   the larger of the screen and the camera-relative openness); the far weight acting at rest and under the integer yaw; on the
//   screen gate, every row bit-identical to the far program and the integer yaw's sparkle margin above 40 codes on the 0.4 px
//   line, above rest's on the 1 px line (the gate before 2026-09-25: the sparkles return).
//   Since 2026-09-25 every program here runs with the far clip off (FrameInputs::far_clip = kFarClipOff, the clip these pins were taken
//   with): the far program has no far clip, so the screen-gate identity needs it off; the far clip is FAR_JITTER_LINE's
//   (temporal_far_jitter_line_inc.h).
namespace far_camera {
void far_camera_pan_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("FAR_CAMERA_PAN_CASES");constexpr UINT W=512,H=16,P=16;constexpr unsigned frames=96,moveFrom=64,window=16;constexpr UINT X0=352,X1=W-16,Y0=2,Y1=H-2;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer; // every row prints before a failed metric throws
    constexpr float lineDepth=.99992f,background=.05f,bright=2.f,period=16; // farw = saturate((.99992 - .9995) * 2500) = 1
    EdgeScene s(d,compiler); // the edge scene's state setup, flat shader and quad; its own 32x32 targets are not used
    Com<IDirect3DTexture9> color,depth,motion;Com<IDirect3DSurface9> colorSurface,depthSurface,motionSurface;
    check("fc color",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("fc color surface",color->GetSurfaceLevel(0,&colorSurface.p));
    check("fc depth",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));check("fc depth surface",depth->GetSurfaceLevel(0,&depthSurface.p));
    check("fc motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));check("fc motion surface",motion->GetSurfaceLevel(0,&motionSurface.p));
    Com<ID3DXBuffer> a,b;Com<IDirect3DPixelShader9> linesPS,motionPS;
    // c.x = jitter + displacement (content x = pixel centre - c.x), c.y background, c.z line value, c.w period; e.x the line width:
    // the line is [7.31, 7.31 + width) of each period.
    compile(compiler,"float4 c:register(c0);float4 e:register(c1);float4 main(float2 vpos:VPOS):COLOR0{float p=floor(vpos.x)+0.5-c.x;float m=p-c.w*floor(p/c.w)-7.31;float v=m>=0&&m<e.x?c.z:c.y;return float4(v,v,v,1);}","ps_3_0",&a.p);
    compile(compiler,"float4 j:register(c0);float4 k:register(c1);float4 main(float2 vpos:VPOS):COLOR0{return float4((floor(vpos)+0.5-j.xy-j.zw)*k.xy,k.z,1);}","ps_3_0",&b.p);
    check("fc lines PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&linesPS.p));check("fc motion PS",d->CreatePixelShader(static_cast<DWORD*>(b->GetBufferPointer()),&motionPS.p));
    auto target=[&](IDirect3DSurface9* rt,IDirect3DPixelShader9* ps){s.target(rt);D3DVIEWPORT9 vp{0,0,W,H,0,1};check("fc viewport",d->SetViewport(&vp));check("fc PS",d->SetPixelShader(ps));};
    auto read=[&](IDirect3DTexture9* texture){D3DSURFACE_DESC desc{};check("fc desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level,sys;check("fc level",texture->GetSurfaceLevel(0,&level.p));
        check("fc readback surface",d->CreateOffscreenPlainSurface(W,H,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr));check("fc validation-only readback",d->GetRenderTargetData(level.p,sys.p));D3DLOCKED_RECT lock{};check("fc lock",sys->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<float> out(W*H);for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const char* p=static_cast<const char*>(lock.pBits)+y*lock.Pitch;float v;if(desc.Format==D3DFMT_R32F)std::memcpy(&v,p+x*4,4);else{unsigned short h;std::memcpy(&h,p+x*8,2);v=halfFloat(h);}out[y*W+x]=v;} // the R channel
        check("fc unlock",sys->UnlockRect());return out;};
    struct Run{std::vector<std::vector<float>> output,age;std::vector<double> shift;};
    enum Program{Camera,CameraScreen,CameraFarOff,ScreenFar};constexpr unsigned programs=4;
    // yaw: the camera path's px/frame from `from`; move: the lines' own screen px/frame beyond the yaw (a far mover); comove: the
    // lines stay screen-static while the camera yaws (their motion vectors are 0, the camera path carries the yaw).
    auto sequence=[&](Program program,float width,double yaw,double move,bool comove,unsigned from){TemporalPass pass;check("fc initialize",pass.initialize(d,nullptr,resolver));
        check("fc configure far",pass.configure_far());require(pass.far_available()&&pass.camera_gate_available(),"fc far and camera-gate programs available");
        Run run;double disp=0;
        for(unsigned n=0;n<frames;++n){const unsigned index=n%P+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
            const bool moving=n>=from;const double v=moving?(comove?0:yaw+move):0;disp+=v;run.shift.push_back(v);
            target(colorSurface.p,linesPS.p);check("fc Begin",d->BeginScene());{const float c[8]={float(jx+disp),background,bright,period,width,0,0,0};check("fc lines constant",d->SetPixelShaderConstantF(0,c,2));}s.quad(0,0,W,H,0,0);check("fc End",d->EndScene());
            target(motionSurface.p,motionPS.p);check("fc motion Begin",d->BeginScene());{const float j[4]={float(jx),float(jy),float(v),0},k[4]={1.f/W,1.f/H,lineDepth,0};check("fc motion j",d->SetPixelShaderConstantF(0,j,1));check("fc motion k",d->SetPixelShaderConstantF(1,k,1));}s.quad(0,0,W,H,0,0);check("fc motion End",d->EndScene());
            target(depthSurface.p,s.flat.p);check("fc depth Begin",d->BeginScene());{const float c[4]={lineDepth,0,0,0};check("fc depth constant",d->SetPixelShaderConstantF(0,c,1));}s.quad(0,0,W,H,0,0);check("fc depth End",d->EndScene());
            FrameInputs in;in.color=color.p;in.current_depth=depth.p;in.motion=motion.p;in.width=W;in.height=H;in.epoch=1;
            const float path[16]={1,0,0,float(moving?-2*yaw/W:0),0,1,0,0,0,0,1,0,0,0,0,1};std::copy(path,path+16,in.clip_to_previous);
            in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=.9f;in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;
            in.sentinel_camera=true;in.sentinel_strict_sky=true;in.sky_history_exit_px=.25f;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
            in.far_weight=program==CameraFarOff?0.f:.985f;in.far_d0=.9995f;in.far_inv=1.f/(.9999f-.9995f);in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
            if(program!=ScreenFar){in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;}
            in.far_camera_gate=program!=CameraScreen;
            in.far_clip=x3::temporal::kFarClipOff; // the 3x3 clip (X3M_TAA_FAR_CLIP=3x3): this scene pins the far weight's gate against the far program; the far clip's rows are FAR_JITTER_LINE's
            Output out;check("fc Begin resolve",d->BeginScene());check("fc resolve",pass.run(in,&out));check("fc End resolve",d->EndScene());
            require(out.color&&out.age&&pass.diagnostics().history_valid&&out.used_history==(n>0),"fc history follows the sequence");
            run.output.push_back(read(out.color));run.age.push_back(read(out.age));}
        return run;};
    auto code=[](double y){y=std::max(y,0.);return 255*std::pow(y/(1+y),1/2.2);};
    struct Numbers{double spike=0,profile=0,peak=0,offDiff=0,restDiff=-1,screenDiff=-1;unsigned sparkles=0,region=0;};
    auto numbers=[&](const Run& r,const Run* off,const Run& rest){Numbers m;std::vector<double> at(frames+1,0);for(unsigned n=0;n<frames;++n)at[n+1]=at[n]+r.shift[n];
        // Sparkles (frames with a frame after them).
        for(unsigned n=frames-window-1;n+1<frames;++n){const int back=int(std::lround(at[n+1]-at[n])),ahead=int(std::lround(at[n+2]-at[n+1]));
            for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){double before=0,after=0;
                for(int dx=-1;dx<=1;++dx){before=std::max(before,code(r.output[n-1][y*W+UINT(int(x)-back+dx)]));after=std::max(after,code(r.output[n+1][y*W+UINT(int(x)+ahead+dx)]));}
                const double c=code(r.output[n][y*W+x]),margin=std::min(c-before,c-after);m.spike=std::max(m.spike,margin);if(margin>6)++m.sparkles;}}
        // Profile drift about the content-phase class means, the peak and the region bits (last 16 frames).
        std::vector<double> sum(64,0);std::vector<unsigned> count(64,0);
        auto key=[&](unsigned n,UINT x){const double c=std::fmod(double(x)-at[n+1],16.);return unsigned(std::lround((c<0?c+16:c)*4))%64u;};
        for(unsigned n=frames-window;n<frames;++n)for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){const unsigned q=key(n,x);sum[q]+=r.output[n][y*W+x];++count[q];}
        double squares=0;unsigned total=0;
        for(unsigned n=frames-window;n<frames;++n){double top=0;
            for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){const double v=r.output[n][y*W+x],e=v-sum[key(n,x)]/count[key(n,x)];squares+=e*e;++total;top=std::max(top,v);if(held_region(r.age[n][y*W+x]))++m.region;}
            m.peak+=top/window;}
        m.profile=std::sqrt(squares/total);
        if(off)for(unsigned n=0;n<frames;++n)for(UINT i=0;i<W*H;++i)m.offDiff=std::max(m.offDiff,double(std::fabs(r.output[n][i]-off->output[n][i])));
        // Integer speeds only: the same content in the rest run sits at x - displacement (content frame).
        bool integral=true;for(unsigned n=0;n<=frames;++n)integral=integral&&std::fabs(at[n]-std::round(at[n]))<1e-9;
        if(integral){m.restDiff=0;for(unsigned n=frames-window;n<frames;++n){const int s0=int(std::lround(at[n+1]));
            for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x)m.restDiff=std::max(m.restDiff,double(std::fabs(r.output[n][y*W+x]-rest.output[n][y*W+UINT(int(x)-s0)])));}}
        return m;};
    // from: the first moving frame. The mover and co-moving rows move from frame 0, so no history accumulated at rest under the
    // far weight is carried into their window and the far-off run is their exact oracle.
    struct Row{const char* name;double yaw,move;bool comove;unsigned from;};
    const Row rows[]={{"rest",0,0,false,moveFrom},{"yaw10",10,0,false,moveFrom},{"yaw10.5",10.5,0,false,moveFrom},{"yaw8.25",8.25,0,false,moveFrom},{"mover10",0,10,false,0},{"comove10",10,0,true,0}};
    constexpr unsigned R=sizeof rows/sizeof rows[0],YAW10=1,MOVER=4,COMOVE=5;
    const float widths[2]={1.f,.4f};
    const char* names[programs]={"far_camera","far_camera_screen_gate","far_camera_far_off","far_screen"};
    for(const float width:widths){Numbers result[programs][R];std::vector<Run> restRuns(programs);
        for(unsigned k=0;k<R;++k){Run runs[programs];for(unsigned p=0;p<programs;++p)runs[p]=sequence(Program(p),width,rows[k].yaw,rows[k].move,rows[k].comove,rows[k].from);
            if(!k)for(unsigned p=0;p<programs;++p)restRuns[p]=runs[p];
            for(unsigned p=0;p<programs;++p){Numbers& q=result[p][k];q=numbers(runs[p],p==Camera||p==CameraScreen?&runs[CameraFarOff]:nullptr,restRuns[p]);
                if(p==CameraScreen){q.screenDiff=0;for(unsigned n=0;n<frames;++n)for(UINT i=0;i<W*H;++i)q.screenDiff=std::max(q.screenDiff,double(std::fabs(runs[p].output[n][i]-runs[ScreenFar].output[n][i])));}
                std::printf("FAR_CAMERA_PAN program=%s width=%.1f row=%s yaw_px=%.2f move_px=%.2f comove=%u sparkles=%u spike_codes=%.3f profile_rms=%.6f peak=%.6f peak_rest=%.6f off_diff=%.6f rest_content_diff=%.6f screen_program_diff=%.6f region_px=%u\n",
                    names[p],double(width),rows[k].name,rows[k].yaw,rows[k].move,unsigned(rows[k].comove),q.sparkles,q.spike,q.profile,q.peak,result[p][0].peak,q.offDiff,q.restDiff,q.screenDiff,q.region);}}
        const Numbers* cam=result[Camera];const std::string prefix="far camera pan, camera-gate program, width "+std::to_string(width).substr(0,3)+": ";
        unsigned region=0;for(unsigned p=0;p<3;++p)for(unsigned k=0;k<R;++k)region+=result[p][k].region;
        metric((prefix+"no pixel of the uniform-depth strip in the thin region (plain 3x3-clip path)").c_str(),double(region),0,0);
        metric((prefix+"yaw10: largest one-frame margin equal to rest's (|yaw - rest|, within 0.1 code)").c_str(),std::fabs(cam[YAW10].spike-cam[0].spike),0,.1);
        for(unsigned k=2;k<=3;++k)metric((prefix+rows[k].name+": largest one-frame margin not above rest's + 1 code (max(yaw - rest - 1, 0))").c_str(),std::max(cam[k].spike-cam[0].spike-1,0.),0,0);
        metric((prefix+"integer yaw 10 px/frame bit-identical to rest in the content frame").c_str(),cam[YAW10].restDiff,0,0);
        metric((prefix+"far mover 10 px/frame bit-identical to the far weight off (the weight closes)").c_str(),cam[MOVER].offDiff,0,0);
        metric((prefix+"co-moving 10 px/frame (screen-static) bit-identical to rest").c_str(),cam[COMOVE].restDiff,0,0);
        metric((prefix+"the far weight acts at rest and under the integer yaw (differs from far off; min(diff, 0.001))").c_str(),std::min(std::min(cam[0].offDiff,cam[YAW10].offDiff),.001),.001,0);
        const Numbers* scr=result[CameraScreen];double screenDiff=0;for(unsigned k=0;k<R;++k)screenDiff=std::max(screenDiff,scr[k].screenDiff);
        metric((prefix+"screen gate (X3M_TAA_FAR_GATE=screen): every row bit-identical to the far program's screen speed gate").c_str(),screenDiff,0,0);
        if(width<1)metric((prefix+"screen gate: the integer yaw's sparkle margin above 40 codes (the gate before 2026-09-25; min(margin, 40))").c_str(),std::min(scr[YAW10].spike,40.),40,0);
        else metric((prefix+"screen gate: the integer yaw's margin above rest's (the gate before 2026-09-25; min(yaw - rest, 0.1))").c_str(),std::min(scr[YAW10].spike-scr[0].spike,.1),.1,0);}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
}
using far_camera::far_camera_pan_cases;
