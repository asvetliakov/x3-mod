// Line-filter cases of temporal_pass_fixture.cpp (lattice mode);
// docs/architecture/taa-lattice-crawl.md section 9. Included after
// temporal_flicker_inc.h: uses EdgeScene, FlickerRun, halton, metric, Snapshot.
//
// Scene: three lines 0.7 px thick, 16 degrees off the x axis (one 1-px column
// quad per x in [2, 18), each 0.2867 px lower than the last: the staircase the
// game's rasteriser makes of a sub-pixel strut), vertical pitch 5 px, value 1 at
// game-like depth 0.99, drifting +0.3 px/frame along y and wrapping by the
// pitch; a static 8x8 square (value 1, depth 0.98) at x in [21, 29) as the
// ordinary silhouette. Background: the depth sentinel (alpha -1, policy 2 with
// the identity camera) or routed static geometry farther than the lines (depth
// 0.999, alpha 1). 8-sample Halton jitter, 96 frames, the last 64 analysed.
constexpr double lineSlope=.2867,linePitch=5,lineThick=.7,lineDrift=.3,lineStart=4.13;
constexpr unsigned lineFrames=96,lineAnalysed=64;
constexpr float lineDepth=.99f,squareDepth=.98f;
std::vector<EdgeObject> line_objects(unsigned n){std::vector<EdgeObject> o;const double phase=std::fmod(lineStart+lineDrift*n,linePitch);
    for(double top=phase-linePitch;top<32;top+=linePitch)for(unsigned x=2;x<18;++x){const double t=top+lineSlope*(x-2);if(t>=2&&t+lineThick<=30)o.push_back({double(x),t,double(x+1),t+lineThick,1,lineDepth,0,lineDrift});}
    o.push_back({21,12,29,20,1,squareDepth,0,0});return o;}
struct LineConfig { const char* name; bool configure; float A,thin,wmax; unsigned width=1; float farW=0,farA=0,thinW=0,relax=1; bool camera=false; float sentS=0,sentE=1; }; // sentS / sentE: the sentinel stabiliser (temporal-integration.md), camera gate only
// Far stabiliser gate of the far cases (temporal_far_inc.h) and the scene hooks the shared oracle uses.
float farD0=0,farInv=0,farLo=x3::temporal::kFarSpeedLo,farHi=x3::temporal::kFarSpeedHi;
double line_velocity_default(double nearest){return nearest==double(lineDepth)?lineDrift:0;}
// Camera pan of the thin-region pan scene (temporal_thin_region_inc.h; taa-lattice-crawl.md section 32.1): the camera translates by
// (cameraPanX, 0) px per frame (clip_to_previous, followed by the sentinel background under policy 2) and, by default, so does
// every routed object (line_velocity_x); an independent mover overrides the x velocity of its depth. 0 = the static camera.
double cameraPanX=0;
// cameraPanAlternates (sentinel-stabiliser scene only): the pan keeps its speed and reverses every frame (+ on odd frames), so
// no history leaves the 32 px frame; the oracle then sets cameraPanX per frame.
// cameraPanVertical: that pan runs along y (cameraPanY; the facets of the sentinel scene vary along y, so it moves content).
bool cameraPanAlternates=false,cameraPanVertical=false;double cameraPanSpeed=0,cameraPanY=0;
// Pixels line_model left to the shader because the history footprint leaves the frame (fast pans): counted per call, and a
// call that skips more than oracleSkipCeiling throws, so no case loses oracle coverage silently. Every case but the
// sentinel-stabiliser pans keeps the ceiling at 0.
unsigned oracleSkipped=0,oracleSkipCeiling=0;
double camera_pan_at(unsigned n){return cameraPanAlternates?(n%2?cameraPanSpeed:-cameraPanSpeed):cameraPanX;}
double line_velocity_x_default(double){return cameraPanX;}
double (*line_velocity_x)(double)=line_velocity_x_default;
// Stale-history injection of the oracle (the fixture draws the same patch into the pass's history after frame oracleInjectFrame).
unsigned oracleInjectFrame=~0u;int oracleInjectRect[4]{};float oracleInjectValue=0;
// k of the resolve's luminance weighting in the oracle (grey scenes: luma = the value) and its finite-colour rule (|v| <= 65000):
// a non-finite current pixel is black and current-only, a non-finite neighbour leaves the 3x3 statistics and the 7x7 box.
double oracleK=0;
double oracle_weigh(double v){return oracleK>0?v/(1+oracleK*std::max(v,0.)):v;}
double oracle_unweigh(double v){return oracleK>0?v/std::max(1-oracleK*std::max(v,0.),1./65504):v;}
bool oracle_finite(double v){return std::fabs(v)<=65000;}
float quantise8(double v){return float(std::lround(std::min(std::max(v,0.),1.)*255.))/255.f;} // as the A8R8G8B8 mask stores it
// Thin-region gate exactly as line_mask_ps.hlsl computes it (clamped addressing): b = (11x11 maximum of FRAGMENTED) * (1 - 17x17 maximum
// of the 8-bit speed closure); FRAGMENTED = some 7-tap line through the pixel changes depth class at least twice; the closure uses the
// pixel's own motion: (line_velocity_x, line_velocity) of its depth where routed geometry, the camera path (cameraPanX, 0) on the
// sentinel background. camera (line_mask_camera_ps.hlsl): the closure speed is min(screen speed, |own motion - camera path|).
float depth_clamped(const std::vector<float>& depth,int x,int y){constexpr int S=int(EdgeScene::S);return px(depth,UINT(std::min(std::max(x,0),S-1)),UINT(std::min(std::max(y,0),S-1)));}
bool depth_class_change(float a,float b){auto valid=[](float d){return d>=0&&d<=1;};auto background=[&](float q,float d){return q<=-.5f||(valid(q)&&(1-q)*1.1f<1-d);};return (valid(a)&&background(b,a))||(valid(b)&&background(a,b));}
bool fragmented(const std::vector<float>& depth,int x,int y){const int dirs[4][2]={{1,0},{0,1},{1,1},{1,-1}};
    for(auto& k:dirs){unsigned changes=0;for(int t=-3;t<3;++t)changes+=depth_class_change(depth_clamped(depth,x+t*k[0],y+t*k[1]),depth_clamped(depth,x+(t+1)*k[0],y+(t+1)*k[1]));if(changes>=2)return true;}return false;}
double line_velocity_default(double nearest);
double (*line_velocity)(double)=line_velocity_default;
// motion + sentS > 0 (camera): the sentinel stabiliser, max(strength, sentS * (1 - closure)) on an unrouted sentinel pixel (motion alpha -1).
float thin_region_strength(const std::vector<float>& depth,int x,int y,bool camera=false,const std::vector<float>* motion=nullptr,float sentS=0){bool any=false;float closure=0;
    for(int dy=-8;dy<=8;++dy)for(int dx=-8;dx<=8;++dx){if(std::abs(dx)<=5&&std::abs(dy)<=5)any=any||fragmented(depth,x+dx,y+dy);const float d=depth_clamped(depth,x+dx,y+dy);const bool geometry=d>=0&&d<=1;
        const double vx=geometry?line_velocity_x(d):cameraPanX,vy=geometry?line_velocity(d):0,screen=std::hypot(vx,vy),relative=std::hypot(vx-cameraPanX,vy),speed=camera?std::min(screen,relative):screen;
        closure=std::max(closure,quantise8((speed-double(farLo))/(double(farHi)-double(farLo))));}
    float strength=any?1-closure:0;
    if(camera&&motion&&sentS>0&&depth_clamped(depth,x,y)<=-.5f&&px(*motion,UINT(x),UINT(y),3)==-1.f)strength=std::max(strength,sentS*(1-closure));
    return strength;}
float far_gate_weight_raw(float depth){if(!(depth>=0&&depth<=1))return 0;return std::min(std::max((depth-farD0)*farInv,0.f),1.f);}
float far_gate_weight(float depth){if(!(depth>=0&&depth<=1))return 0;const float w=std::min(std::max((depth-farD0)*farInv,0.f),1.f);return float(std::lround(w*255.f))/255.f;} // as the A8R8G8B8 mask stores it

FlickerRun line_sequence(EdgeScene& s,const DWORD* resolver,const LineConfig& c,const EdgeBackground& bg,unsigned frames=lineFrames){
    TemporalPass pass;check("line initialize",pass.initialize(s.d,nullptr,resolver));
    if(c.thin>0||c.wmax>0){check("line configure flicker",pass.configure_flicker());require(pass.flicker_available()&&(c.wmax<=0||pass.age_available()),"line: flicker programs available");}
    if(c.configure){check("line configure",pass.configure_line_filter());check("line configure is idempotent",pass.configure_line_filter());require(pass.line_filter_available()&&(c.wmax<=0||pass.age_line_available()),"line-filter programs created on this device");}
    const FlickerConfig f{c.name,c.thin,c.wmax,.1f,.5f,false,false,.9f};FlickerRun run;bool sequence=true;
    for(unsigned n=0;n<frames;++n){const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(line_objects(n),bg,jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));
        auto in=flicker_inputs(s,f,jx,jy,bg.depth<0);in.line_filter=c.A;in.line_width=c.width;
        Output out;check("line Begin resolve",s.d->BeginScene());check(c.name,pass.run(in,&out));check("line End resolve",s.d->EndScene());
        sequence=sequence&&out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0)&&bool(out.age)==(c.wmax>0);
        run.output.push_back(s.read(out.color));if(out.age)run.age.push_back(s.read(out.age));}
    require(sequence,"line history and age output follow the sequence");return run;}
// The mask exactly as resolve.hlsl defines it: a line-like pixel in the 3x3.
bool line_like(const std::vector<float>& depth,UINT x,UINT y,unsigned width){
    auto background=[](float q,float d){return q<=-.5f||(q>=0&&q<=1&&(1-q)*1.1f<1-d);};
    const float d=px(depth,x,y);if(!(d>=0&&d<=1))return false;
    const int dirs[4][2]={{1,0},{0,1},{1,1},{1,-1}};for(auto& k:dirs){bool before=background(px(depth,x-k[0],y-k[1]),d),after=background(px(depth,x+k[0],y+k[1]),d);
        if(width==2){before=before||background(px(depth,x-2*k[0],y-2*k[1]),d);after=after||background(px(depth,x+2*k[0],y+2*k[1]),d);}if(before&&after)return true;}
    return false;}
bool line_mask(const std::vector<float>& depth,UINT x,UINT y,unsigned width){for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(line_like(depth,x+dx,y+dy,width))return true;return false;}
// 2-D CPU oracle of the resolve on this scene at k = 0 (interior pixels [3, S-3); the rest take the shader's output):
// closest-depth dilation (content moves by (line_velocity_x, line_velocity) of the closest depth; the camera path by
// (cameraPanX, 0)), the disocclusion proof over the 2x2 footprint, 4x4 Catmull-Rom history with the snap, the 3x3 clip, the thin
// soft clip and age weight of the variants, the camera gate's 7x7 box clip by the share of the strength the camera term added,
// and the exp(-A d^2) current sample where line_mask holds. FP16 rounding per frame.
FlickerModel line_model(const FlickerRun& run,const LineConfig& c){constexpr UINT S=EdgeScene::S;const unsigned N=unsigned(run.current.size());FlickerModel m;m.color.resize(N);m.age.resize(N);
    auto valid=[](float d){return d>=0&&d<=1;};auto sentinel=[](float d){return d<=-.5f;};const double w=.9;
    oracleSkipped=0;
    for(unsigned n=0;n<N;++n){if(cameraPanAlternates)(cameraPanVertical?cameraPanY:cameraPanX)=camera_pan_at(n);m.color[n].assign(S*S,0);m.age[n].assign(S*S,1);const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){m.color[n][y*S+x]=px(run.output[n],x,y);if(!run.age.empty())m.age[n][y*S+x]=px(run.age[n],x,y);}
        if(n&&n-1==oracleInjectFrame)for(int y=oracleInjectRect[1];y<oracleInjectRect[3];++y)for(int x=oracleInjectRect[0];x<oracleInjectRect[2];++x)m.color[n-1][UINT(y)*S+UINT(x)]=oracleInjectValue; // after frame n-1 was modelled
        if(!n)continue;
        for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){const UINT i=y*S+x;const double cur=px(run.current[n],x,y);const float centre=px(run.depth[n],x,y);
            if(!oracle_finite(cur)){m.color[n][i]=0;m.age[n][i]=1;continue;}
            const double wcur=oracle_weigh(cur);unsigned finiteCount=0;
            bool sawValid=valid(centre),sawSentinel=sentinel(centre);double nearest=sentinel(centre)?1:centre,lo=wcur,hi=wcur,m1=0,m2=0,sum=0,total=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const float d=px(run.depth[n],x+dx,y+dy);if(valid(d)){sawValid=true;if(d<nearest)nearest=d;}if(sentinel(d))sawSentinel=true;
                const double raw=px(run.current[n],x+dx,y+dy);if(!oracle_finite(raw))continue;
                const double q=oracle_weigh(raw);++finiteCount;lo=std::min(lo,q);hi=std::max(hi,q);m1+=q;m2+=q*q;const double g=std::exp(-double(c.A>0?c.A:c.farA)*((dx-jx)*(dx-jx)+(dy-jy)*(dy-jy)));sum+=q*g;total+=g;}
            m1/=finiteCount;m2/=finiteCount;const double sigma=std::sqrt(std::max(m2-m1*m1,0.));lo=std::max(lo,m1-1.25*sigma);hi=std::min(hi,m1+1.25*sigma);
            const double vx=line_velocity_x(nearest),vy=line_velocity(nearest),speed=std::hypot(vx,vy);
            auto snap=[](double position,double& base,double& f){base=std::floor(position);f=position-base;if(f>1-1e-4){base+=1;f=0;}else if(f<1e-4)f=0;};
            double bx,fx,by,fy;snap(x-vx,bx,fx);snap(y-vy,by,fy);
            if(bx<1||by<1||bx+2>=S||by+2>=S){++oracleSkipped;continue;} // history footprint outside the frame (fast pans only): the shader's output stands
            const double tolerance=std::max(.0001,.02*nearest);bool proven=true;
            for(int ty=0;ty<2;++ty)for(int tx=0;tx<2;++tx){const double weight=(tx?fx:1-fx)*(ty?fy:1-fy);if(weight>.01){const float p=px(run.depth[n-1],UINT(bx+tx),UINT(by+ty));if(!((valid(p)&&p>=nearest-tolerance)||sentinel(p)))proven=false;}}
            if(!proven){m.color[n][i]=float(cur);m.age[n][i]=1;continue;}
            auto keys=[](double f,double* cr){const double f2=f*f,f3=f2*f;cr[0]=-.5*f+f2-.5*f3;cr[1]=1-2.5*f2+1.5*f3;cr[2]=.5*f+2*f2-1.5*f3;cr[3]=-.5*f2+.5*f3;};
            double crx[4],cry[4];keys(fx,crx);keys(fy,cry);
            double old=0,weights=0;for(int t=0;t<4;++t)for(int u=0;u<4;++u){const double cr=crx[u]*cry[t];if(cr!=0){const double tap=m.color[n-1][UINT(by+t-1)*S+UINT(bx+u-1)];if(oracle_finite(tap)){old+=cr*oracle_weigh(tap);weights+=cr;}}}
            old/=weights;
            const bool farOn=c.farW>0||c.farA>0||c.thinW>0; // the far program has no 3x3 sentinel soft clip
            const double gate=c.thinW>0?quantise8(thin_region_strength(run.depth[n],int(x),int(y),c.camera,run.motion.empty()?nullptr:&run.motion[n],c.sentS)):0,screenGate=c.camera?quantise8(thin_region_strength(run.depth[n],int(x),int(y),false)):gate;
            const double clamped=std::min(std::max(old,lo),hi),soft=farOn?screenGate*c.relax:sawValid&&sawSentinel?c.thin*(1-std::min(std::max((speed-2)*.5,0.),1.)):0;
            double boxTerm=0;
            if(c.camera&&gate>screenGate){ // the strength the camera term added takes the history clipped to the 7x7 box of the current colour (clamped addressing)
                double boxLo=wcur,boxHi=wcur,innerLo=wcur,innerHi=wcur,rawMax=0;for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){const double raw=px(run.current[n],UINT(std::min(std::max(int(x)+dx,0),int(S)-1)),UINT(std::min(std::max(int(y)+dy,0),int(S)-1)));if(!oracle_finite(raw))continue;const double q=oracle_weigh(raw);boxLo=std::min(boxLo,q);boxHi=std::max(boxHi,q);rawMax=std::max(rawMax,std::max(raw,0.));
                    if(std::abs(dx)<=1&&std::abs(dy)<=1){innerLo=std::min(innerLo,q);innerHi=std::max(innerHi,q);}}
                if(c.sentS>0&&c.sentE>0&&rawMax>double(c.sentE)&&sentinel(centre)){boxLo=innerLo;boxHi=innerHi;} // the emitter bound: the inner 3x3
                boxTerm=(gate-screenGate)*c.relax*(std::min(std::max(old,boxLo),boxHi)-clamped);}
            old=clamped+soft*(old-clamped)+boxTerm;
            double keep=w;const double farw=farOn?far_gate_weight(centre):0;const UINT ageIndex=UINT(by+(fy>=.5?1:0))*S+UINT(bx+(fx>=.5?1:0));
            if(farOn){double a=m.age[n-1][ageIndex];if(!(a>=1&&a<=64))a=1;
                const double ramp=a/(a+1),slow=1-std::min(std::max((speed-double(farLo))/(double(farHi)-double(farLo)),0.),1.);
                const double farKeep=w+(c.farW>0?double(far_gate_weight(centre))*slow*(std::min(ramp,double(c.farW))-w):0.);
                keep=gate>0?std::max(farKeep,w+gate*(std::min(ramp,double(c.thinW))-w)):farKeep;
                m.age[n][i]=float(std::min(a+1,64.));}
            if(c.wmax>0){double a=m.age[n-1][ageIndex];if(!(a>=1&&a<=64))a=1;const double t=std::min(std::max((speed-.1)/.4,0.),1.);keep=std::min(a/(a+1),c.wmax+t*(w-c.wmax));m.age[n][i]=float(std::min(a+1,64.));}
            const double filterWeight=std::max(c.A>0&&line_mask(run.depth[n],x,y,c.width)?1.:0.,c.farA>0?farw:0.);
            const double blend=farOn?wcur+filterWeight*(sum/total-wcur):filterWeight>0?sum/total:wcur;
            m.color[n][i]=halfFloat(toHalf(float(oracle_unweigh(blend+keep*(old-blend)))));}}
    if(oracleSkipped>oracleSkipCeiling)throw std::runtime_error("line_model: oracle skipped "+std::to_string(oracleSkipped)+" border pixels, ceiling "+std::to_string(oracleSkipCeiling));
    return m;}
// Roping: per analysed frame and line, the peak of the output over the rows around the line's centre in each column;
// bead amplitude = std / mean of those peaks along the line (0 for a phase-independent reconstruction), and their mean.
void line_beads(const FlickerRun& run,double& bead,double& peak){double beadSum=0,peakSum=0;unsigned lines=0;
    for(unsigned n=lineFrames-lineAnalysed;n<lineFrames;++n){const double phase=std::fmod(lineStart+lineDrift*n,linePitch);
        for(double top=phase-linePitch;top<32;top+=linePitch){if(top<6||top+lineSlope*16>25)continue;double s1=0,s2=0;unsigned count=0;
            for(UINT x=4;x<16;++x){const double centre=top+lineSlope*(x-2)+lineThick/2-.5;const int row=int(std::lround(centre));double best=0;for(int dy=-1;dy<=1;++dy)best=std::max(best,double(px(run.output[n],x,UINT(row+dy)))-.25);s1+=best;s2+=best*best;++count;}
            const double mean=s1/count;beadSum+=std::sqrt(std::max(s2/count-mean*mean,0.))/mean;peakSum+=mean;++lines;}}
    bead=beadSum/lines;peak=peakSum/lines;}
void line_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("LINE_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    const EdgeBackground farGeometry{.25f,.999f,1};
    // ---- validation, refusals, hostile state, Reset ----
    {s.render(line_objects(0),sentinelBackground,0,0);Output out;const FlickerConfig none{"line-validation",0,0,.1f,.5f,false,false,.9f};
        TemporalPass bare;check("line bare initialize",bare.initialize(d,nullptr,resolver));auto in=flicker_inputs(s,none,0,0,true);in.caller_scene_open=false;in.line_filter=1;
        require(!bare.line_filter_available()&&bare.run(in,&out)==E_INVALIDARG,"line filter without configure_line_filter is refused");
        in.line_filter=0;require(SUCCEEDED(bare.run(in,&out)),"the unconfigured pass runs the plain resolve");
        // (any ps_3_0 program stands in for the filtered one: only its presence matters to the refusal below)
        TemporalPass pass;check("line validation initialize",pass.initialize(d,nullptr,resolver,nullptr,nullptr,nullptr,resolver));check("line validation configure",pass.configure_line_filter());
        for(float bad:{-1.f,4.5f,NAN,INFINITY}){in.line_filter=bad;require(pass.run(in,&out)==E_INVALIDARG,"line filter outside [0, 4] is refused");}
        in.line_filter=1;in.current_filter=1;require(pass.run(in,&out)==E_INVALIDARG,"line filter beside the global current filter is refused");in.current_filter=0;
        in.line_filter=4;require(SUCCEEDED(pass.run(in,&out)),"line filter 4 runs");
        for(unsigned bad:{0u,3u}){in.line_width=bad;require(pass.run(in,&out)==E_INVALIDARG,"line width other than 1 or 2 is refused");}in.line_width=2;require(SUCCEEDED(pass.run(in,&out)),"line width 2 runs");in.line_width=1;
        in.line_filter=1;in.thin_clip=.75f;require(pass.run(in,&out)==E_INVALIDARG,"line filter with the thin clip but without configure_flicker is refused");in.thin_clip=0;
        // Hostile c22/c24 and sampler 1 around a line-filtered run; the state block restores them.
        const float junk[4]={9,8,7,6};check("line hostile c22",d->SetPixelShaderConstantF(22,junk,1));check("line hostile s1",d->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("line hostile s1 u",d->SetSamplerState(1,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));
        {Snapshot before(d);check("line hostile run",pass.run(in,&out));before.equals(d,"line-filtered run restores the caller's state");}
        // A failed resolve draw publishes nothing; the next run restarts from a rejected history.
        {Output failed;{Fault fault(d,1);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid,"failed line-filtered resolve draw publishes nothing");}
            check("line recovery",pass.run(in,&out));require(out.color&&!out.used_history,"after a failed run the line-filtered resolve restarts without history");}
        pass.before_reset();pass.after_reset(S_OK);check("line after Reset",pass.run(in,&out));
        require(out.color&&!out.used_history&&pass.line_filter_available(),"Reset protocol keeps the line-filter programs and restarts the history");state_checks+=1;s.target(s.colorSurface.p);}
    const LineConfig base{"line-base",false,0,0,0},off{"line-off",true,0,0,0},a2{"line-A2",true,2,0,0},a1{"line-A1",true,1,0,0},thin{"line-A1+soft-0.75",true,1,.75f,0},aged{"line-A1+soft-0.75+w-0.97",true,1,.75f,.97f},wide{"line-A1-width-2",true,1,0,0,2};
    for(const auto* bg:{&sentinelBackground,&farGeometry}){const char* bgName=bg==&sentinelBackground?"sentinel":"far_geometry";
        const auto baseRun=line_sequence(s,resolver,base,*bg);double baseBead=0,basePeak=0;line_beads(baseRun,baseBead,basePeak);
        {const auto offRun=line_sequence(s,resolver,off,*bg);++numeric_checks;require(same_rgb(baseRun.output,offRun.output),"line filter configured but A = 0 is bit-identical to the unconfigured pass");}
        for(const LineConfig* c:{&base,&a2,&a1,&wide,&thin,&aged}){if(bg==&farGeometry&&c->thin>0)continue; // no sentinel in that scene: the thin clip is inert there
            const auto run=c==&base?baseRun:line_sequence(s,resolver,*c,*bg);const auto model=line_model(run,*c);double oracle=0,ageOracle=0,bead=0,peak=0;line_beads(run,bead,peak);
            // Mask precision, from the oracle's own mask: share of the line region masked; square silhouette pixels (both sides of each
            // edge, at least 2 px from the corners (3 px with width 2), where a diagonal has background on both sides) never masked and bit-identical to the base run.
            unsigned masked=0,region=0,edgeMasked=0,edgeDiffers=0,edgePixels=0;double offMask=0,offMaskBase=0;
            for(unsigned n=0;n<lineFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));
                if(!run.age.empty())ageOracle=std::max(ageOracle,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));
                const bool mask=line_mask(run.depth[n],x,y,c->width);if(x<18&&y>=6&&y<26){++region;masked+=mask;}
                const UINT inset=c->width; // a convex corner is line-like along the diagonal across it; with width 2 so are its two edge neighbours
                auto isEdge=[inset](UINT ex,UINT ey){return ((ex==20||ex==21||ex==28||ex==29)&&ey>=13+inset&&ey<19-inset)||((ey==11||ey==12||ey==19||ey==20)&&ex>=22+inset&&ex<28-inset);};const bool edge=isEdge(x,y);
                if(edge){++edgePixels;edgeMasked+=mask;edgeDiffers+=std::memcmp(&run.output[n][(y*S+x)*4],&baseRun.output[n][(y*S+x)*4],sizeof(float))!=0;
                    if(isEdge(x+1,y)){const double g=px(run.output[n],x+1,y)-px(run.output[n],x,y),gb=px(baseRun.output[n],x+1,y)-px(baseRun.output[n],x,y);offMask+=g*g;offMaskBase+=gb*gb;}}}
            std::printf("LINE_FILTER background=%s config=%s A=%.2f oracle_error=%.6f age_oracle_error=%.6f mask_share_line_region=%.4f silhouette_px=%u silhouette_masked=%u silhouette_differs=%u silhouette_gradient_ratio=%.6f bead_amplitude=%.4f bead_ratio=%.4f line_peak=%.4f line_peak_ratio=%.4f\n",
                bgName,c->name,c->A,oracle,ageOracle,double(masked)/region,edgePixels,edgeMasked,edgeDiffers,offMask/offMaskBase,bead,bead/baseBead,peak,peak/basePeak);
            metric((std::string("line ")+c->name+": shader matches the 2-D CPU oracle within the FP16 bound").c_str(),oracle,0,.0006/(1-(c->wmax>0?c->wmax:.9)));
            if(c->wmax>0)metric((std::string("line ")+c->name+": age target matches the CPU oracle").c_str(),ageOracle,0,0);
            ++numeric_checks;require(edgePixels>0&&edgeMasked==0,"line mask never holds on the square's silhouette");
            if(c->thin<=0){++numeric_checks;require(edgeDiffers==0&&offMask==offMaskBase,"square silhouette bit-identical to the unfiltered resolve (gradient energy unchanged)");}
            if(c->A>0){++numeric_checks;require(double(masked)/region>.5,"line mask covers the lattice region");
                if(c->thin<=0){++numeric_checks;require(bead<baseBead*.85&&peak<basePeak&&peak>basePeak*.4,"line filter lowers the roping at a bounded line-peak cost");}}}}
    // ---- pass time, 1280x768, every pixel geometry (the mask's worst case: all 72 extra depth fetches), event-query drained ----
    {LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);Com<IDirect3DQuery9> completion;check("line timing query",d->CreateQuery(D3DQUERYTYPE_EVENT,&completion.p));
        auto stamp=[](){LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;};
        auto drain=[&](){check("line timing issue",completion->Issue(D3DISSUE_END));const auto start=stamp();HRESULT hr;while((hr=completion->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){if(stamp()-start>frequency.QuadPart*10)throw std::runtime_error("line timing timeout");Sleep(0);}check("line timing completion",hr);};
        constexpr UINT W=1280,H=768;Com<IDirect3DTexture9> color,depth;Com<IDirect3DSurface9> colorSurface,depthSurface,saved;check("line timing save",d->GetRenderTarget(0,&saved.p));D3DVIEWPORT9 vp{};check("line timing viewport",d->GetViewport(&vp));
        check("line timing color",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("line timing depth",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));
        check("line timing color surface",color->GetSurfaceLevel(0,&colorSurface.p));check("line timing depth surface",depth->GetSurfaceLevel(0,&depthSurface.p));
        for(UINT n=0;n<20;++n)check("line timing unbind",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
        check("line timing color target",d->SetRenderTarget(0,colorSurface.p));check("line timing color clear",d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,128,128,128),1,0));
        check("line timing depth target",d->SetRenderTarget(0,depthSurface.p));check("line timing depth clear",d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));check("line timing restore",d->SetRenderTarget(0,saved.p));check("line timing restore viewport",d->SetViewport(&vp));
        // Four passes, interleaved: plain; the global current filter (the nine exp() taps alone); the line filter (taps + two mask
        // draws + one fetch); the far stabiliser (taps + one mask draw + the age target).
        namespace r=x3m::renderer;TemporalPass passes[6];const char* labels[6]={"plain","current_filter","line_filter","far_stabiliser","thin_region","thin_region_camera"};
        for(unsigned i=0;i<6;++i){check("line timing initialize",passes[i].initialize(d,nullptr,resolver,nullptr,nullptr,nullptr,i==1?reinterpret_cast<const DWORD*>(r::temporal_resolve_filter_program()):nullptr));
            if(i==2){check("line timing configure",passes[i].configure_line_filter());}
            if(i>=3){check("line timing configure far",passes[i].configure_far());require(passes[i].camera_gate_available(),"line timing: camera-gate programs created");}}
        FrameInputs base{};base.color=color.p;base.current_depth=depth.p;base.width=W;base.height=H;base.epoch=1;base.weight=.9f;std::copy(identity,identity+16,base.clip_to_previous);base.motion_policy=MotionPolicy::KnownCameraOnly;base.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;base.history_allowed=true;base.caller_queries_idle=true;base.caller_scene_open=false;
        Com<IDirect3DTexture9> motion;Com<IDirect3DSurface9> motionSurface;check("line timing motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));check("line timing motion surface",motion->GetSurfaceLevel(0,&motionSurface.p));
        check("line timing motion target",d->SetRenderTarget(0,motionSurface.p));check("line timing motion clear",d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));check("line timing motion restore",d->SetRenderTarget(0,saved.p));check("line timing motion viewport",d->SetViewport(&vp)); // alpha 0: the camera path
        FrameInputs ins[6]={base,base,base,base,base,base};for(unsigned i=3;i<6;++i){ins[i].motion_policy=MotionPolicy::PerPixel;ins[i].motion=motion.p;}ins[4].thin_region_weight=ins[5].thin_region_weight=.97f;ins[5].thin_region_camera_gate=true;ins[1].current_filter=1;ins[2].line_filter=1;ins[3].far_weight=.985f;ins[3].far_filter=1;ins[3].far_d0=-.5f;ins[3].far_inv=1; // depth 0 everywhere: farw = 0.5
        Output out;double ms[6]{};
        for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<6;++which){check("line timing warm",passes[which].run(ins[which],&out));drain();}
        for(unsigned round=0;round<6;++round)for(unsigned step=0;step<6;++step){const unsigned which=(round+step)%6;drain();const auto start=stamp();check("line timing run",passes[which].run(ins[which],&out));drain();ms[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
        // Clear writes depth 0 (R32F from the ARGB clear colour): valid geometry everywhere, no line-like pixel.
        std::printf("LINE_TIMING width=%u height=%u content=all_geometry rounds=6 plain_ms=%.4f current_filter_ms=%.4f line_filter_ms=%.4f far_stabiliser_ms=%.4f thin_region_ms=%.4f taps_delta_ms=%.4f line_delta_ms=%.4f far_delta_ms=%.4f thin_region_delta_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,ms[0],ms[1],ms[2],ms[3],ms[4],ms[1]-ms[0],ms[2]-ms[0],ms[3]-ms[0],ms[4]-ms[0]);(void)labels;
        // The camera gate (section 32.1) beside the thin region: on the all-geometry frame (no region: the box pass skips every
        // pixel) and on a fully fragmented frame under a 1 px/frame camera pan (rows alternate geometry and the sentinel; the
        // camera path is the correspondence, so the screen gate closes everywhere and the camera term reopens everywhere: the
        // box pass and the resolve's box clip run on every pixel, the worst case).
        const double camNoRegion[2]={ms[4],ms[5]};
        {s.target(depthSurface.p);D3DVIEWPORT9 full{0,0,W,H,0,1};check("camera timing viewport",d->SetViewport(&full));
            check("camera timing Begin",d->BeginScene());check("camera timing flat",d->SetPixelShader(s.flat.p));s.constant(-1,0,0,0);
            struct V{float x,y,z,rhw,u,v;};for(UINT y=1;y<H;y+=2){const V v[]={{-.5f,y-.5f,.5f,1,0,0},{W-.5f,y-.5f,.5f,1,1,0},{-.5f,y+.5f,.5f,1,0,1},{W-.5f,y+.5f,.5f,1,1,1}};check("camera timing stripe",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));}
            check("camera timing End",d->EndScene());check("camera timing restore",d->SetRenderTarget(0,saved.p));check("camera timing restore viewport",d->SetViewport(&vp));
            for(unsigned i=4;i<6;++i){ins[i].clip_to_previous[3]=-2.f/W;ins[i].sentinel_camera=true;ms[i]=0;}
            for(unsigned warm=0;warm<3;++warm)for(unsigned which=4;which<6;++which){check("camera timing warm",passes[which].run(ins[which],&out));drain();}
            for(unsigned round=0;round<6;++round)for(unsigned step=0;step<2;++step){const unsigned which=4+(round+step)%2;drain();const auto start=stamp();check("camera timing run",passes[which].run(ins[which],&out));drain();ms[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
            std::printf("LINE_TIMING_CAMERA width=%u height=%u rounds=6 no_region_thin_ms=%.4f no_region_camera_ms=%.4f no_region_camera_delta_ms=%.4f fragmented_pan_thin_ms=%.4f fragmented_pan_camera_ms=%.4f fragmented_pan_camera_delta_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,camNoRegion[0],camNoRegion[1],camNoRegion[1]-camNoRegion[0],ms[4],ms[5],ms[5]-ms[4]);
            // The four-channel lane as the depth input (section 32.4): the same fragmented pan frame copied into an A32B32G32R32F
            // target (.b = 1 on geometry: every valid pixel takes the lane fetch). Two camera-gate passes on that input, one
            // with the lane term (s5 bound, c9.w = 1), one without (c9 = 0, the c8 law): their difference is the extra fetch;
            // the R32F camera row above against the second one is the cost of the lane's .r copy draw itself.
            Com<IDirect3DTexture9> lane;Com<IDirect3DSurface9> laneSurface;check("lane timing texture",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&lane.p,nullptr));check("lane timing surface",lane->GetSurfaceLevel(0,&laneSurface.p));
            s.target(laneSurface.p);check("lane timing viewport",d->SetViewport(&full));check("lane timing Begin",d->BeginScene());check("lane timing PS",d->SetPixelShader(s.textured.p));check("lane timing source",d->SetTexture(0,depth.p));
            check("lane timing min",d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));check("lane timing mag",d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
            {const V v[]={{-.5f,-.5f,.5f,1,0,0},{W-.5f,-.5f,.5f,1,1,0},{-.5f,H-.5f,.5f,1,0,1},{W-.5f,H-.5f,.5f,1,1,1}};check("lane timing copy",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));}
            check("lane timing End",d->EndScene());check("lane timing unbind",d->SetTexture(0,nullptr));check("lane timing restore",d->SetRenderTarget(0,saved.p));check("lane timing restore viewport",d->SetViewport(&vp));
            TemporalPass lanePasses[2];FrameInputs laneIns[2]={ins[5],ins[5]};double laneMs[2]{};
            for(unsigned i=0;i<2;++i){check("lane timing initialize",lanePasses[i].initialize(d,nullptr,resolver,nullptr,nullptr,reinterpret_cast<const DWORD*>(r::hdr_writeback_program())));check("lane timing configure far",lanePasses[i].configure_far());laneIns[i].current_depth=lane.p;
                laneIns[i].camera_depth_parallax[0]=1e-4f;laneIns[i].camera_depth_parallax[3]=1.000003f;}
            laneIns[1].camera_lane_parallax[0]=1e-4f;laneIns[1].camera_lane_parallax[3]=1;
            for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<2;++which){check("lane timing warm",lanePasses[which].run(laneIns[which],&out));drain();}
            for(unsigned round=0;round<6;++round)for(unsigned step=0;step<2;++step){const unsigned which=(round+step)%2;drain();const auto start=stamp();check("lane timing run",lanePasses[which].run(laneIns[which],&out));drain();laneMs[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
            std::printf("LINE_TIMING_CAMERA_LANE width=%u height=%u rounds=6 r32f_camera_ms=%.4f lane_input_law_ms=%.4f lane_input_lane_ms=%.4f lane_fetch_delta_ms=%.4f lane_input_over_r32f_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,ms[5],laneMs[0],laneMs[1],laneMs[1]-laneMs[0],laneMs[0]-ms[5]);
            // Sentinel stabiliser (temporal-integration.md "Distant unrouted stations under a pan"): an all-sentinel, unrouted frame
            // (depth -1, motion alpha -1) under the same 1 px/frame pan. S = 0: no region, the box pass skips every pixel. S = 0.7: the
            // separable box (rows draw on every pixel, columns draw and the resolve's box clip on every pixel), the worst case; the
            // 49-tap program over a whole frame is fragmented_pan_camera_delta_ms of LINE_TIMING_CAMERA.
            for(auto* surface:{depthSurface.p,motionSurface.p}){s.target(surface);check("sentinel timing viewport",d->SetViewport(&full));check("sentinel timing Begin",d->BeginScene());check("sentinel timing flat",d->SetPixelShader(s.flat.p));s.constant(surface==depthSurface.p?-1.f:0.f,0,0,-1);
                const V v[]={{-.5f,-.5f,.5f,1,0,0},{W-.5f,-.5f,.5f,1,1,0},{-.5f,H-.5f,.5f,1,0,1},{W-.5f,H-.5f,.5f,1,1,1}};check("sentinel timing fill",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));check("sentinel timing End",d->EndScene());}
            check("sentinel timing restore",d->SetRenderTarget(0,saved.p));check("sentinel timing restore viewport",d->SetViewport(&vp));
            TemporalPass sentinelPasses[2];FrameInputs sentinelIns[2]={ins[5],ins[5]};double sentinelMs[2]{};sentinelIns[1].sentinel_strength=.7f;
            for(unsigned i=0;i<2;++i){check("sentinel timing initialize",sentinelPasses[i].initialize(d,nullptr,resolver));check("sentinel timing configure far",sentinelPasses[i].configure_far());if(i)check("sentinel timing configure sentinel",sentinelPasses[i].configure_sentinel());require(i==0||sentinelPasses[i].sentinel_available(),"sentinel timing: separable box programs created");}
            for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<2;++which){check("sentinel timing warm",sentinelPasses[which].run(sentinelIns[which],&out));drain();}
            for(unsigned round=0;round<6;++round)for(unsigned step=0;step<2;++step){const unsigned which=(round+step)%2;drain();const auto start=stamp();check("sentinel timing run",sentinelPasses[which].run(sentinelIns[which],&out));drain();sentinelMs[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
            std::printf("LINE_TIMING_SENTINEL width=%u height=%u rounds=6 content=all_unrouted_sentinel_pan strength_off_ms=%.4f strength_on_separable_ms=%.4f sentinel_delta_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,sentinelMs[0],sentinelMs[1],sentinelMs[1]-sentinelMs[0]);}
        check("line timing restore target",d->SetRenderTarget(0,saved.p));check("line timing restore vp",d->SetViewport(&vp));}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
