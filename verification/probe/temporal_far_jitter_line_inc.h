// Far clip (docs/architecture/taa-mask-fold.md section 4.2 addendum "far clip"; run327 / run332 rest sparkles on the fog-band
// plants), lattice mode. Included after temporal_far_camera_inc.h: uses halton, EdgeScene, held_region and the metric helpers.
//   FAR_JITTER_LINE: a far (farw 1 on the production far ramp F0 / F1 60 / 68 at 5120 px: the gate x3::temporal::far_gate of the
//   run327 projection, the strip at view z 100,000 units (20 km)), routed, uniform-depth strip (512 x 16) of dark surface (0.05)
//   carrying world-static bright lines (2.0, period 16 px) of width 1 px and 0.4 px, slanted 0.2 px per row (a plant edge: a
//   neighbouring row samples the line in the jitter phases that miss this row), sampled at the jittered raster position under the
//   production 8-phase jitter, on the camera-gate resolve (thin region 0.97 camera gate, far weight 0.985 on the camera gate: the
//   launcher defaults) with the far clip at its default (7x7 wherever farw * openC > 0) and off (3x3, the clip before 2026-09-25). Rows: rest
//   (96 frames), and static 64 frames then a pure camera yaw of 10 / 10.5 / 8.25 px/frame for 32 (the lines world-static: openC
//   1, so the far weight and the far clip stay on under the pan; taa-thin-classification.md section 7). Per run,
//   over the last 16 frames (two jitter cycles), x in [352, 496), rows [4, 12) (the 7x7 box stays off the clamped border):
//     sparkles / spike_codes: the run327 one-frame sparkle of FAR_CAMERA_PAN (codes c = 255 (Y / (1 + Y))^(1 / 2.2), c_n above
//       the 1x3 maximum of frames n - 1 and n + 1 at the same content by more than 6 codes; spike = the largest margin);
//     rest_delta_codes (rest only, else -1): the largest frame-to-frame |c_n - c_(n-1)| at a pixel;
//     dim_min / dim_max / dim_p10 (rest only): code(mean output) - code(mean input) over the frames, on the line pixels (mean
//       input above 1.5 x the surface), the line's dimming in codes;
//     peak: the mean over frames of the brightest pixel; region_px: pixels whose hold fraction carries a region bit.
//   Mode 2 runs the pan rows with the far weight on the screen speed gate (X3M_TAA_FAR_GATE=screen, opt-in) and the 7x7 far clip:
//   the witness for the camera default (at the base weight 0.9 a sampled sub-pixel line leaks 10 % per frame, which no clip
//   removes; the camera gate holds 0.985 under a world-static pan, a 1.5 % leak).
//   Asserted: no pixel in the thin region; with the far clip (7x7, camera gate) every row's spike and the rest delta at most 6 codes and the
//   rest dimming within 3 codes on both widths; without it (3x3) the 0.4 px line's rest spike and rest delta above 6 codes (the
//   sparkle the far clip removes), and on the screen gate the 0.4 px line at yaw 10.5 above 6 codes. verification/results/far-clip-7x7/far_jitter_line_model.py is the host model of these rows.
namespace far_jitter_line {
void far_jitter_line_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("FAR_JITTER_LINE_CASES");constexpr UINT W=512,H=16;constexpr unsigned frames=96,moveFrom=64,window=16,phases=8;constexpr UINT X0=352,X1=W-16,Y0=4,Y1=H-4;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer; // every row prints before a failed metric throws
    constexpr float background=.05f,bright=2.f,period=16,slope=.2f;
    // The far gate of the run327 projection (camera_state p00 / p22 / p32) at 5120 px on the production footprints, and the strip's depth at
    // view z 100,000 units (inside the farw = 1 band, which starts at about 87,000 units).
    constexpr float p00=.4999979f,p22=1.00000298f,p32=-6.00001812f;float gateD0=0,gateInv=0;
    require(x3::temporal::far_gate(p00,p22,p32,5120,60.f,68.f,gateD0,gateInv),"fjl far gate of the production footprints");
    const float stripDepth=float(double(p22)+double(p32)/100000.);
    require((stripDepth-gateD0)*gateInv>=1.f,"fjl strip depth inside the farw = 1 band");
    EdgeScene s(d,compiler); // the edge scene's state setup, flat shader and quad; its own 32x32 targets are not used
    Com<IDirect3DTexture9> color,depth,motion;Com<IDirect3DSurface9> colorSurface,depthSurface,motionSurface;
    check("fjl color",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("fjl color surface",color->GetSurfaceLevel(0,&colorSurface.p));
    check("fjl depth",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));check("fjl depth surface",depth->GetSurfaceLevel(0,&depthSurface.p));
    check("fjl motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));check("fjl motion surface",motion->GetSurfaceLevel(0,&motionSurface.p));
    Com<ID3DXBuffer> a,b;Com<IDirect3DPixelShader9> linesPS,motionPS;
    // c.x = jitter x + displacement, c.y surface, c.z line value, c.w period; e.x the line width, e.y the slope (px per row), e.z the
    // jitter y: the line is [7.31 + slope * content y, + width) of each period, content (x, y) = pixel centre - (c.x, e.z).
    compile(compiler,"float4 c:register(c0);float4 e:register(c1);float4 main(float2 vpos:VPOS):COLOR0{float p=floor(vpos.x)+0.5-c.x;float q=p-e.y*(floor(vpos.y)+0.5-e.z);float m=q-c.w*floor(q/c.w)-7.31;float v=m>=0&&m<e.x?c.z:c.y;return float4(v,v,v,1);}","ps_3_0",&a.p);
    compile(compiler,"float4 j:register(c0);float4 k:register(c1);float4 main(float2 vpos:VPOS):COLOR0{return float4((floor(vpos)+0.5-j.xy-j.zw)*k.xy,k.z,1);}","ps_3_0",&b.p);
    check("fjl lines PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&linesPS.p));check("fjl motion PS",d->CreatePixelShader(static_cast<DWORD*>(b->GetBufferPointer()),&motionPS.p));
    auto target=[&](IDirect3DSurface9* rt,IDirect3DPixelShader9* ps){s.target(rt);D3DVIEWPORT9 vp{0,0,W,H,0,1};check("fjl viewport",d->SetViewport(&vp));check("fjl PS",d->SetPixelShader(ps));};
    auto read=[&](IDirect3DTexture9* texture){D3DSURFACE_DESC desc{};check("fjl desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level,sys;check("fjl level",texture->GetSurfaceLevel(0,&level.p));
        check("fjl readback surface",d->CreateOffscreenPlainSurface(W,H,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr));check("fjl validation-only readback",d->GetRenderTargetData(level.p,sys.p));D3DLOCKED_RECT lock{};check("fjl lock",sys->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<float> out(W*H);for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const char* p=static_cast<const char*>(lock.pBits)+y*lock.Pitch;float v;if(desc.Format==D3DFMT_R32F)std::memcpy(&v,p+x*4,4);else{unsigned short h;std::memcpy(&h,p+x*8,2);v=halfFloat(h);}out[y*W+x]=v;} // the R channel
        check("fjl unlock",sys->UnlockRect());return out;};
    struct Run{std::vector<std::vector<float>> input,output,age;std::vector<double> shift;};
    auto sequence=[&](float farClip,float width,double yaw,bool cameraGate){TemporalPass pass;check("fjl initialize",pass.initialize(d,nullptr,resolver));
        check("fjl configure far",pass.configure_far());require(pass.far_available()&&pass.camera_gate_available(),"fjl far and camera-gate programs available");
        Run run;double disp=0;
        for(unsigned n=0;n<frames;++n){const unsigned index=n%phases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
            const bool moving=n>=moveFrom;const double v=moving?yaw:0;disp+=v;run.shift.push_back(v);
            target(colorSurface.p,linesPS.p);check("fjl Begin",d->BeginScene());{const float c[8]={float(jx+disp),background,bright,period,width,slope,float(jy),0};check("fjl lines constant",d->SetPixelShaderConstantF(0,c,2));}s.quad(0,0,W,H,0,0);check("fjl End",d->EndScene());
            target(motionSurface.p,motionPS.p);check("fjl motion Begin",d->BeginScene());{const float j[4]={float(jx),float(jy),float(v),0},k[4]={1.f/W,1.f/H,stripDepth,0};check("fjl motion j",d->SetPixelShaderConstantF(0,j,1));check("fjl motion k",d->SetPixelShaderConstantF(1,k,1));}s.quad(0,0,W,H,0,0);check("fjl motion End",d->EndScene());
            target(depthSurface.p,s.flat.p);check("fjl depth Begin",d->BeginScene());{const float c[4]={stripDepth,0,0,0};check("fjl depth constant",d->SetPixelShaderConstantF(0,c,1));}s.quad(0,0,W,H,0,0);check("fjl depth End",d->EndScene());
            FrameInputs in;in.color=color.p;in.current_depth=depth.p;in.motion=motion.p;in.width=W;in.height=H;in.epoch=1;
            const float path[16]={1,0,0,float(moving?-2*yaw/W:0),0,1,0,0,0,0,1,0,0,0,0,1};std::copy(path,path+16,in.clip_to_previous);
            in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=.9f;in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;
            in.sentinel_camera=true;in.sentinel_strict_sky=true;in.sky_history_exit_px=.25f;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
            in.far_weight=.985f;in.far_d0=gateD0;in.far_inv=gateInv;in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
            in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;in.thin_region_hold_frames=phases;in.far_camera_gate=cameraGate;in.far_clip=farClip;
            Output out;check("fjl Begin resolve",d->BeginScene());check("fjl resolve",pass.run(in,&out));check("fjl End resolve",d->EndScene());
            require(out.color&&out.age&&pass.diagnostics().history_valid&&out.used_history==(n>0),"fjl history follows the sequence");
            run.input.push_back(read(color.p));run.output.push_back(read(out.color));run.age.push_back(read(out.age));}
        return run;};
    auto code=[](double y){y=std::max(y,0.);return 255*std::pow(y/(1+y),1/2.2);};
    struct Numbers{double spike=0,restDelta=-1,dimMin=0,dimMax=0,dimP10=0,peak=0;unsigned sparkles=0,region=0,linePx=0;};
    auto numbers=[&](const Run& r,bool rest){Numbers m;std::vector<double> at(frames+1,0);for(unsigned n=0;n<frames;++n)at[n+1]=at[n]+r.shift[n];
        for(unsigned n=frames-window-1;n+1<frames;++n){const int back=int(std::lround(at[n+1]-at[n])),ahead=int(std::lround(at[n+2]-at[n+1]));
            for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){double before=0,after=0;
                for(int dx=-1;dx<=1;++dx){before=std::max(before,code(r.output[n-1][y*W+UINT(int(x)-back+dx)]));after=std::max(after,code(r.output[n+1][y*W+UINT(int(x)+ahead+dx)]));}
                const double c=code(r.output[n][y*W+x]),margin=std::min(c-before,c-after);m.spike=std::max(m.spike,margin);if(margin>6)++m.sparkles;}}
        for(unsigned n=frames-window;n<frames;++n){double top=0;
            for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){top=std::max(top,double(r.output[n][y*W+x]));if(held_region(r.age[n][y*W+x]))++m.region;}
            m.peak+=top/window;}
        if(rest){m.restDelta=0;std::vector<double> dims;
            for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){double in=0,out=0;
                for(unsigned n=frames-window;n<frames;++n){in+=r.input[n][y*W+x];out+=r.output[n][y*W+x];m.restDelta=std::max(m.restDelta,std::fabs(code(r.output[n][y*W+x])-code(r.output[n-1][y*W+x])));}
                in/=window;out/=window;if(in>1.5*background)dims.push_back(code(out)-code(in));}
            require(!dims.empty(),"fjl line pixels in the window");std::sort(dims.begin(),dims.end());m.linePx=unsigned(dims.size());
            m.dimMin=dims.front();m.dimMax=dims.back();m.dimP10=dims[dims.size()/10];}
        return m;};
    struct Row{const char* name;double yaw;};
    const Row rows[]={{"rest",0},{"yaw10",10},{"yaw10.5",10.5},{"yaw8.25",8.25}};constexpr unsigned R=sizeof rows/sizeof rows[0];
    const float widths[2]={1.f,.4f};const float clips[2]={x3::temporal::kFarClipThreshold,x3::temporal::kFarClipOff};const char* clipNames[3]={"7x7","3x3","7x7"};const char* gateNames[3]={"camera","camera","screen"};
    std::printf("FAR_JITTER_LINE_GATE far_d0=%.9f far_inv=%.3f line_depth=%.9f farw=%.6f\n",double(gateD0),double(gateInv),double(stripDepth),double(std::min(std::max((stripDepth-gateD0)*gateInv,0.f),1.f)));
    // Modes: 0 the 7x7 far clip, 1 the 3x3 clip, both on the camera gate of the far weight (the default); 2 the 7x7 far clip on the
    // screen gate (X3M_TAA_FAR_GATE=screen, opt-in), pan rows only (at rest both gates are fully open: the rest row is mode 0's).
    for(const float width:widths){Numbers result[3][R];unsigned region=0;
        for(unsigned c=0;c<3;++c)for(unsigned k=c==2?1:0;k<R;++k){Numbers& q=result[c][k];q=numbers(sequence(clips[c%2],width,rows[k].yaw,c!=2),rows[k].yaw==0);region+=q.region;
            const Numbers& rest=result[c==2?0:c][0];
            std::printf("FAR_JITTER_LINE clip=%s gate=%s width=%.1f row=%s yaw_px=%.2f sparkles=%u spike_codes=%.3f spike_rest=%.3f rest_delta_codes=%.3f dim_min=%.3f dim_max=%.3f dim_p10=%.3f line_px=%u peak=%.6f peak_rest=%.6f region_px=%u\n",
                clipNames[c],gateNames[c],double(width),rows[k].name,rows[k].yaw,q.sparkles,q.spike,rest.spike,q.restDelta,q.dimMin,q.dimMax,q.dimP10,q.linePx,q.peak,rest.peak,q.region);}
        const std::string prefix="far jitter line, width "+std::to_string(width).substr(0,3)+": ";
        metric((prefix+"no pixel of the uniform-depth strip in the thin region (the far clip's path)").c_str(),double(region),0,0);
        for(unsigned k=0;k<R;++k)metric((prefix+"7x7 far clip, "+rows[k].name+": largest one-frame margin at most 6 codes (max(spike - 6, 0))").c_str(),std::max(result[0][k].spike-6,0.),0,0);
        metric((prefix+"7x7 far clip, rest: largest frame-to-frame delta at most 6 codes (max(delta - 6, 0))").c_str(),std::max(result[0][0].restDelta-6,0.),0,0);
        metric((prefix+"7x7 far clip, rest: line dimming within 3 codes (max(|dim| - 3, 0))").c_str(),std::max(std::max(-result[0][0].dimMin,result[0][0].dimMax)-3,0.),0,0);
        if(width<1){metric((prefix+"3x3 clip, rest: one-frame margin above 6 codes (the sparkle the far clip removes; 1 = above)").c_str(),result[1][0].spike>6?1.:0.,1,0);
            metric((prefix+"3x3 clip, rest: frame-to-frame delta above 6 codes (1 = above)").c_str(),result[1][0].restDelta>6?1.:0.,1,0);
            // Why the far weight's gate defaults to camera: on the screen gate the far weight drops to the base weight under the pan
            // and the sampled line leaks 10 % per frame, which the 7x7 does not remove (the other five screen rows are informational).
            metric((prefix+"7x7 far clip on the screen gate, yaw10.5: one-frame margin above 6 codes (the camera gate's job; 1 = above)").c_str(),result[2][2].spike>6?1.:0.,1,0);}}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
}
using far_jitter_line::far_jitter_line_cases;
