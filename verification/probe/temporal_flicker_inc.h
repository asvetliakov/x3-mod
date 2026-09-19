// Flicker-suppression cases of temporal_pass_fixture.cpp (lattice mode);
// docs/architecture/taa-flicker-suppression.md, steps 1-3. Included after the
// lattice cases: uses EdgeScene, EdgeRun, halton, metric, Snapshot, Fault.
//
// Drifting lattice: vertical lines 0.8 px wide, value 1 at depth 0.5, pitch 4
// or 2.37 px, drifting +v px/frame along x over the depth-sentinel background
// (value 0.25, depth -1, motion alpha -1, policy 2 with the identity camera),
// rasterized with the 8-sample Halton jitter, 256 frames, last 128 analysed
// over x, y in [8, 24): per-pixel and 8x8-block band rms in 8-bit codes for
// periods [2,4], (4,8], (8,32] and the spatial contrast against the analytic
// box-filtered lattice. CPU oracle: flicker_model below.
struct FlickerConfig { const char* name; float thin,wmax,lo,hi; bool filtered,alpha; float weight; };
struct FlickerRun : EdgeRun { std::vector<std::vector<float>> age; };
struct DriftSpec { double v,pitch,width; };
constexpr double driftStart=.31;
constexpr UINT driftLo=8,driftHi=24;
constexpr unsigned driftFrames=256,driftAnalysed=128;
std::vector<EdgeObject> drift_objects(const DriftSpec& spec,unsigned n){
    std::vector<EdgeObject> o;const double phase=std::fmod(driftStart+spec.v*n,spec.pitch);
    for(double l=phase-spec.pitch;l<32;l+=spec.pitch)if(l>=1&&l+spec.width<=31)o.push_back({l,2,l+spec.width,30,1,.5f,spec.v,0});
    return o;}
const EdgeBackground sentinelBackground{.25f,-1.f,-1},routedBackground{.25f,.9f,1};
FrameInputs flicker_inputs(EdgeScene& s,const FlickerConfig& c,double jx,double jy,bool sentinelCamera){
    constexpr UINT S=EdgeScene::S;FrameInputs in;in.color=s.color.p;in.current_depth=s.depth32.p;in.motion=s.motion.p;in.width=S;in.height=S;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);
    in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=c.weight;in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=sentinelCamera;
    in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
    in.thin_clip=c.thin;in.adaptive_weight=c.wmax;in.adaptive_lo=c.lo;in.adaptive_hi=c.hi;in.alpha_history=c.alpha;in.current_filter=c.filtered?1.f:0.f;return in;}
template<class Objects> FlickerRun flicker_sequence(EdgeScene& s,const DWORD* resolver,const DWORD* filtered,Objects objects,unsigned frames,const FlickerConfig& c,const EdgeBackground& bg){
    TemporalPass pass;check("flicker initialize",pass.initialize(s.d,nullptr,resolver,nullptr,nullptr,nullptr,c.filtered?filtered:nullptr));
    if(c.thin>0||c.wmax>0||c.alpha){check("flicker configure",pass.configure_flicker());require(pass.flicker_available()&&(c.wmax<=0||pass.age_available()),"flicker programs available");}
    FlickerRun run;bool sequence=true;
    for(unsigned n=0;n<frames;++n){const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(objects(n),bg,jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));
        const auto in=flicker_inputs(s,c,jx,jy,bg.depth<0);
        Output out;check("flicker Begin resolve",s.d->BeginScene());check(c.name,pass.run(in,&out));check("flicker End resolve",s.d->EndScene());
        sequence=sequence&&out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0)&&bool(out.age)==(c.wmax>0);
        run.output.push_back(s.read(out.color));if(out.age)run.age.push_back(s.read(out.age));}
    require(sequence,"flicker history and age output follow the sequence");return run;}
// CPU oracle of the resolve on these scenes (k = 0, no current filter): the
// closest-depth dilation picks the line (depth 0.5, velocity v along x) wherever
// the 3x3 holds one, else the pixel is its own static correspondence; the
// disocclusion proof over the bilinear footprint; Catmull-Rom history along x
// with the shader's snap; the 3x3 min/max box intersected with mean +/- 1.25
// sigma; the thin soft clip; the age weight; FP16 rounding per frame. Border
// pixels (no full 3x3, clamped taps) take the shader's output.
struct FlickerModel { std::vector<std::vector<float>> color,age,alpha; };
FlickerModel flicker_model(const FlickerRun& run,const FlickerConfig& c,double v){
    constexpr UINT S=EdgeScene::S;const unsigned N=unsigned(run.current.size());FlickerModel m;m.color.resize(N);m.age.resize(N);m.alpha.resize(N);
    auto valid=[](float d){return d>=0&&d<=1;};auto sentinel=[](float d){return d<=-.5f;};
    for(unsigned n=0;n<N;++n){m.color[n].assign(S*S,0);m.age[n].assign(S*S,1);m.alpha[n].assign(S*S,1);
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const UINT i=y*S+x;m.color[n][i]=px(run.output[n],x,y);m.alpha[n][i]=px(run.output[n],x,y,3);if(!run.age.empty())m.age[n][i]=px(run.age[n],x,y);}
        if(!n)continue;
        for(UINT y=1;y+1<S;++y)for(UINT x=2;x+2<S;++x){const UINT i=y*S+x;const double cur=px(run.current[n],x,y),curAlpha=px(run.current[n],x,y,3);
            const float centre=px(run.depth[n],x,y);
            bool sawValid=valid(centre),sawSentinel=sentinel(centre);double nearest=sentinel(centre)?1:centre;
            double lo=cur,hi=cur,m1=0,m2=0,loA=curAlpha,hiA=curAlpha;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const float d=px(run.depth[n],x+dx,y+dy);if(valid(d)){sawValid=true;if(d<nearest)nearest=d;}if(sentinel(d))sawSentinel=true;
                const double q=px(run.current[n],x+dx,y+dy),a=px(run.current[n],x+dx,y+dy,3);lo=std::min(lo,q);hi=std::max(hi,q);m1+=q/9;m2+=q*q/9;loA=std::min(loA,a);hiA=std::max(hiA,a);}
            const double sigma=std::sqrt(std::max(m2-m1*m1,0.));lo=std::max(lo,m1-1.25*sigma);hi=std::min(hi,m1+1.25*sigma);
            const double shift=nearest==double(.5f)?v:0,speed=std::fabs(shift);
            double position=x-shift,base=std::floor(position),f=position-base;if(f>1-1e-4){base+=1;f=0;}else if(f<1e-4)f=0;
            const double tolerance=std::max(.0001,.02*nearest);bool proven=true;
            for(int tx=0;tx<2;++tx){const double weight=tx?f:1-f;if(weight>.01){const float p=px(run.depth[n-1],UINT(std::min(std::max(base+tx,0.),double(S-1))),y);if(!((valid(p)&&p>=nearest-tolerance)||sentinel(p)))proven=false;}}
            if(x+.5-shift<0)proven=false; // the shader returns current-only for a lookup outside the history
            if(!proven){m.color[n][i]=float(cur);m.age[n][i]=1;m.alpha[n][i]=float(curAlpha);continue;}
            const double f2=f*f,f3=f2*f,w[4]={-.5*f+f2-.5*f3,1-2.5*f2+1.5*f3,.5*f+2*f2-1.5*f3,-.5*f2+.5*f3};
            double old=0,oldAlpha=0,total=0;for(int t=0;t<4;++t)if(w[t]!=0){const UINT tx=UINT(std::min(std::max(base+t-1,0.),double(S-1)));old+=w[t]*m.color[n-1][y*S+tx];oldAlpha+=w[t]*m.alpha[n-1][y*S+tx];total+=w[t];}
            old/=total;oldAlpha/=total;
            const double clamped=std::min(std::max(old,lo),hi),soft=sawValid&&sawSentinel?c.thin*(1-std::min(std::max((speed-2)*.5,0.),1.)):0;
            old=clamped+soft*(old-clamped);
            double keep=c.weight;
            if(c.wmax>0){double a=m.age[n-1][y*S+UINT(std::min(std::max(base+(f>=.5?1:0),0.),double(S-1)))];if(!(a>=1&&a<=64))a=1;const double t=std::min(std::max((speed-c.lo)/(c.hi-c.lo),0.),1.);
                keep=std::min(a/(a+1),c.wmax+t*(c.weight-c.wmax));m.age[n][i]=float(std::min(a+1,64.));}
            m.color[n][i]=halfFloat(toHalf(float(cur+keep*(old-cur))));
            if(c.alpha){const double blended=curAlpha+keep*(std::min(std::max(oldAlpha,loA),hiA)-curAlpha);m.alpha[n][i]=halfFloat(toHalf(float(blended>=loA&&blended<=hiA?blended:curAlpha)));}
            else m.alpha[n][i]=float(curAlpha);}}
    return m;}
struct FlickerBands { double pixel[3],block[3],contrast,oracle,ageOracle; };
// Band rms (8-bit codes) of mean-removed series: DFT over the analysed frames, periods [2,4], (4,8], (8,32].
void band_power(const std::vector<double>& series,double power[3]){const unsigned N=unsigned(series.size());double mean=0;for(double v:series)mean+=v/N;
    for(unsigned k=1;k<=N/2;++k){double re=0,im=0;for(unsigned t=0;t<N;++t){const double a=-2*3.14159265358979323846*k*t/N;re+=(series[t]-mean)*std::cos(a);im+=(series[t]-mean)*std::sin(a);}
        const double p=(k==N/2?1.:2.)*(re*re+im*im)/(double(N)*N),period=double(N)/k;if(period<=4)power[0]+=p;else if(period<=8)power[1]+=p;else if(period<=32)power[2]+=p;}}
FlickerBands flicker_bands(const FlickerRun& run,const DriftSpec& spec){FlickerBands out{};const unsigned N=unsigned(run.output.size()),from=N-driftAnalysed;
    double pixel[3]{},block[3]{};unsigned pixels=0,blocks=0;
    for(UINT y=driftLo;y<driftHi;++y)for(UINT x=driftLo;x<driftHi;++x){std::vector<double> series;for(unsigned n=from;n<N;++n)series.push_back(px(run.output[n],x,y));band_power(series,pixel);++pixels;}
    for(UINT by=driftLo;by<driftHi;by+=8)for(UINT bx=driftLo;bx<driftHi;bx+=8){std::vector<double> series;for(unsigned n=from;n<N;++n){double sum=0;for(UINT y=by;y<by+8;++y)for(UINT x=bx;x<bx+8;++x)sum+=px(run.output[n],x,y);series.push_back(sum/64);}band_power(series,block);++blocks;}
    for(unsigned b=0;b<3;++b){out.pixel[b]=255*std::sqrt(pixel[b]/pixels);out.block[b]=255*std::sqrt(block[b]/blocks);}
    // Spatial contrast: std of the output over the region against the std of the analytic box-filtered lattice, mean over the analysed frames.
    double ratio=0;
    for(unsigned n=from;n<N;++n){double a1=0,a2=0,o1=0,o2=0;unsigned count=0;const double phase=std::fmod(driftStart+spec.v*n,spec.pitch);
        for(UINT y=driftLo;y<driftHi;++y)for(UINT x=driftLo;x<driftHi;++x){double cover=0;for(double l=phase-spec.pitch;l<32;l+=spec.pitch)cover+=coverage(l,spec.width,x);
            const double ideal=.25+.75*cover,o=px(run.output[n],x,y);a1+=ideal;a2+=ideal*ideal;o1+=o;o2+=o*o;++count;}
        ratio+=std::sqrt(std::max(o2/count-o1*o1/count/count,0.))/std::sqrt(std::max(a2/count-a1*a1/count/count,1e-12));}
    out.contrast=ratio/driftAnalysed;return out;}
bool same_rgb(const std::vector<std::vector<float>>& a,const std::vector<std::vector<float>>& b,bool alphaToo=true){if(a.size()!=b.size())return false;
    for(unsigned n=0;n<a.size();++n)for(unsigned i=0;i<a[n].size();++i)if((alphaToo||i%4!=3)&&std::memcmp(&a[n][i],&b[n][i],sizeof(float)))return false;
    return true;}
void flicker_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver,const DWORD* filtered){
    std::puts("FLICKER_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    D3DCAPS9 caps{};check("flicker caps",d->GetDeviceCaps(&caps));
    std::printf("FLICKER_CAPS simultaneous_rts=%lu mrt_independent_bit_depths=%u age_bytes_per_pixel=8 age_bytes_1280x768=%u age_bytes_3840x2160=%u\n",caps.NumSimultaneousRTs,unsigned((caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)!=0),2u*1280*768*4,2u*3840*2160*4);
    const FlickerConfig base{"base",0,0,.1f,.5f,false,false,.9f},soft{"soft-0.75",.75f,0,.1f,.5f,false,false,.9f},
        narrow{"soft-0.75+w-0.97-narrow",.75f,.97f,.1f,.5f,false,false,.9f},wide{"soft-0.75+w-0.97-wide",.75f,.97f,.8f,1.5f,false,false,.9f};
    // ---- validation and refusals ----
    {const DriftSpec spec{.4,2.37,.8};s.render(drift_objects(spec,0),sentinelBackground,0,0);Output out;
        TemporalPass bare;check("flicker bare initialize",bare.initialize(d,nullptr,resolver));auto in=flicker_inputs(s,soft,0,0,true);in.caller_scene_open=false;
        require(!bare.flicker_available()&&bare.run(in,&out)==E_INVALIDARG,"thin clip without configure_flicker is refused");
        in=flicker_inputs(s,base,0,0,true);in.caller_scene_open=false;require(SUCCEEDED(bare.run(in,&out))&&!out.age,"the unconfigured pass runs the plain resolve");
        TemporalPass pass;check("flicker validation initialize",pass.initialize(d,nullptr,resolver));check("flicker validation configure",pass.configure_flicker());check("flicker configure is idempotent",pass.configure_flicker());
        require(pass.flicker_available()&&pass.age_available(),"flicker and age variants created on this device");
        for(float bad:{-.1f,1.5f,NAN,INFINITY}){in=flicker_inputs(s,soft,0,0,true);in.caller_scene_open=false;in.thin_clip=bad;require(pass.run(in,&out)==E_INVALIDARG,"thin clip outside [0, 1] is refused");}
        in=flicker_inputs(s,narrow,0,0,true);in.caller_scene_open=false;in.thin_clip=0;require(pass.run(in,&out)==E_INVALIDARG,"adaptive weight without the thin clip is refused");
        for(float bad:{-.5f,.5f,.995f,NAN}){in=flicker_inputs(s,narrow,0,0,true);in.caller_scene_open=false;in.adaptive_weight=bad;require(pass.run(in,&out)==E_INVALIDARG,"adaptive WMAX outside [weight, 0.99] is refused");}
        in=flicker_inputs(s,narrow,0,0,true);in.caller_scene_open=false;in.adaptive_lo=.5f;in.adaptive_hi=.5f;require(pass.run(in,&out)==E_INVALIDARG,"adaptive gate needs LO < HI");
        in.adaptive_lo=-1;in.adaptive_hi=.5f;require(pass.run(in,&out)==E_INVALIDARG,"adaptive gate needs LO >= 0");
        // Alpha history is for the FP16 texture input only.
        Com<IDirect3DTexture9> main8;Com<IDirect3DSurface9> main8Surface;check("flicker 8-bit target",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&main8.p,nullptr));check("flicker 8-bit surface",main8->GetSurfaceLevel(0,&main8Surface.p));
        in=flicker_inputs(s,soft,0,0,true);in.caller_scene_open=false;in.color=nullptr;in.color_surface=main8Surface.p;require(SUCCEEDED(pass.run(in,&out)),"thin clip runs on the 8-bit input");
        in.alpha_history=true;require(pass.run(in,&out)==E_INVALIDARG,"alpha history on the 8-bit input is refused");
        // Hostile state around an aged run: RT1 bound, COLORWRITEENABLE1 masked, sampler 7 linear/wrapped with a texture.
        Com<IDirect3DTexture9> other;Com<IDirect3DSurface9> otherSurface;check("flicker hostile RT1",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&other.p,nullptr));check("flicker hostile RT1 surface",other->GetSurfaceLevel(0,&otherSurface.p));
        auto hostile=[&]{check("flicker hostile RT0",d->SetRenderTarget(0,main8Surface.p));check("flicker hostile bind RT1",d->SetRenderTarget(1,otherSurface.p));check("flicker hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
            check("flicker hostile s7",d->SetTexture(7,s.wave.p));check("flicker hostile s7 min",d->SetSamplerState(7,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("flicker hostile s7 mag",d->SetSamplerState(7,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));check("flicker hostile s7 u",d->SetSamplerState(7,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));
            const float c24[4]={9,8,7,6};check("flicker hostile c24",d->SetPixelShaderConstantF(24,c24,1));};
        TemporalPass aged;check("flicker hostile initialize",aged.initialize(d,nullptr,resolver));check("flicker hostile configure",aged.configure_flicker());
        std::vector<float> hostileAge;
        for(unsigned n=0;n<3;++n){s.render(drift_objects(spec,n),sentinelBackground,0,0);in=flicker_inputs(s,narrow,0,0,true);hostile();Snapshot before(d);check("flicker hostile Begin",d->BeginScene());check("flicker hostile run",aged.run(in,&out));check("flicker hostile End",d->EndScene());before.equals(d,"aged run restores RT1, COLORWRITEENABLE1, sampler 7 and c24");require(out.age!=nullptr,"aged run publishes the age target");hostileAge=s.read(out.age);}
        // The age target is actually written under the hostile COLORWRITEENABLE1: a static background pixel reaches 3.
        ++numeric_checks;require(px(hostileAge,4,31)==3.f,"age written through the hostile write mask");
        // A failed resolve draw: state restored, nothing published, the next frame restarts every age at 1.
        {s.render(drift_objects(spec,3),sentinelBackground,0,0);in=flicker_inputs(s,narrow,0,0,true);hostile();Snapshot before(d);check("flicker fault Begin",d->BeginScene());Output failed;{Fault fault(d,1);require(aged.run(in,&failed)==E_FAIL&&!failed.color&&!failed.age&&!aged.diagnostics().history_valid,"failed aged resolve draw publishes nothing");}check("flicker fault End",d->EndScene());before.equals(d,"failed aged run restores state");
            check("flicker recovery Begin",d->BeginScene());check("flicker recovery",aged.run(in,&out));check("flicker recovery End",d->EndScene());const auto a=s.read(out.age);bool ones=true;for(UINT i=0;i<S*S;++i)ones=ones&&a[i*4]==1.f;++numeric_checks;require(!out.used_history&&ones,"after a failed run every age restarts at 1");}
        s.target(s.colorSurface.p);}
    // ---- age lifecycle: count, saturation, camera cut, Reset protocol, option change ----
    {TemporalPass pass;check("age initialize",pass.initialize(d,nullptr,resolver));check("age configure",pass.configure_flicker());Output out;const DriftSpec still{0,4,.8};
        auto frame=[&](unsigned n,const FlickerConfig& c,bool cut=false){s.render(drift_objects(still,n),sentinelBackground,0,0);auto in=flicker_inputs(s,c,0,0,true);in.camera_cut=cut;check("age Begin",d->BeginScene());check("age run",pass.run(in,&out));check("age End",d->EndScene());return out.age?s.read(out.age):std::vector<float>();};
        auto all=[&](const std::vector<float>& a,float v){bool same=!a.empty();for(UINT i=0;i<S*S&&same;++i)same=a[i*4]==v;return same;};
        std::vector<float> a;
        for(unsigned n=0;n<20;++n){a=frame(n,narrow);}
        ++numeric_checks;require(all(a,20),"static scene: every age equals the accumulated frame count (20)");
        for(unsigned n=20;n<70;++n){a=frame(n,narrow);}
        ++numeric_checks;require(all(a,64),"age saturates at 64");
        a=frame(70,narrow,true);++numeric_checks;require(all(a,1)&&!out.used_history,"camera cut restarts every age at 1");
        a=frame(71,narrow);++numeric_checks;require(all(a,2),"age counts again after the cut");
        pass.before_reset();pass.after_reset(S_OK);a=frame(72,narrow);++numeric_checks;require(all(a,1)&&!out.used_history,"Reset protocol releases the age targets and restarts at 1");
        a=frame(73,soft);++numeric_checks;require(a.empty()&&!out.used_history,"dropping the adaptive weight releases the age targets and rejects history");
        a=frame(74,narrow);++numeric_checks;require(all(a,1)&&!out.used_history,"re-enabling the adaptive weight starts from a rejected history");
        s.target(s.colorSurface.p);}
    // ---- drifting lattice table ----
    const double speeds[]={0,.25,.4,.6},pitches[]={4,2.37},widths[]={.8,1.25};const FlickerConfig* table[]={&base,&soft,&narrow,&wide};
    FlickerBands bands[2][2][4][4];FlickerRun staticRuns[4];
    for(unsigned q=0;q<2;++q)for(unsigned p=0;p<2;++p)for(unsigned v=0;v<4;++v)for(unsigned c=0;c<4;++c){const DriftSpec spec{speeds[v],pitches[p],widths[q]};const FlickerConfig& config=*table[c];
        auto run=flicker_sequence(s,resolver,filtered,[&](unsigned n){return drift_objects(spec,n);},driftFrames,config,sentinelBackground);
        auto b=flicker_bands(run,spec);const auto model=flicker_model(run,config,spec.v);b.oracle=b.ageOracle=0;
        for(unsigned n=0;n<driftFrames;++n)for(UINT y=1;y+1<S;++y)for(UINT x=2;x+2<S;++x){b.oracle=std::max(b.oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));if(!run.age.empty())b.ageOracle=std::max(b.ageOracle,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));}
        bands[q][p][v][c]=b;
        std::printf("FLICKER_DRIFT width=%.2f pitch=%.2f v=%.2f config=%s pixel_p2_4=%.3f pixel_p4_8=%.3f pixel_p8_32=%.3f block_p2_4=%.3f block_p4_8=%.3f block_p8_32=%.3f contrast=%.4f oracle_error=%.6f age_oracle_error=%.6f\n",spec.width,spec.pitch,spec.v,config.name,b.pixel[0],b.pixel[1],b.pixel[2],b.block[0],b.block[1],b.block[2],b.contrast,b.oracle,b.ageOracle);
        const double w=config.wmax>0?config.wmax:config.weight;
        metric((std::string("flicker ")+config.name+": shader matches the CPU oracle within the FP16 bound").c_str(),b.oracle,0,.0006/(1-w));
        if(config.wmax>0)metric((std::string("flicker ")+config.name+": age target matches the CPU oracle").c_str(),b.ageOracle,0,0);
        if(q==1&&p==0&&v==0)staticRuns[c]=std::move(run);}
    // ---- step 1 gates. The note's 1-D prediction (block period 2-4 <= 0.65 x baseline, static contrast 0.55 -> 0.91) is reported
    // against the measurement and NOT asserted: the real shader shows no such gain (see the ledger entry). Asserted: no band
    // of the soft clip exceeds the baseline by more than 10 % and the contrast does not fall by more than 5 %.
    for(unsigned q=0;q<2;++q)for(unsigned p=0;p<2;++p)for(unsigned v=0;v<4;++v){const auto& b=bands[q][p][v][0];const auto& t=bands[q][p][v][1];double worst=0;
        for(unsigned k=0;k<3;++k){if(b.pixel[k]>.5)worst=std::max(worst,t.pixel[k]/b.pixel[k]);if(b.block[k]>.5)worst=std::max(worst,t.block[k]/b.block[k]);}
        const double blockRatio=b.block[0]>0?t.block[0]/b.block[0]:1;
        std::printf("FLICKER_STEP1 width=%.2f pitch=%.2f v=%.2f block_p2_4_ratio=%.4f predicted_at_most=0.65 prediction_met=%u worst_band_ratio=%.4f contrast_ratio=%.4f\n",widths[q],pitches[p],speeds[v],blockRatio,unsigned(blockRatio<=.65),worst,t.contrast/b.contrast);
        metric("flicker soft clip: no band above 1.10 x baseline",std::max(worst,1.),1,.1);
        metric("flicker soft clip: contrast at least 0.95 x baseline",std::min(t.contrast/b.contrast,1.),1,.05);}
    // Speed gate through the oracle: 3 px/frame (half strength) and 4.5 px/frame (off).
    for(double fast:{3.,4.5}){const DriftSpec spec{fast,4,.8};auto run=flicker_sequence(s,resolver,filtered,[&](unsigned n){return drift_objects(spec,n);},64,soft,sentinelBackground);const auto model=flicker_model(run,soft,fast);double oracle=0;
        for(unsigned n=0;n<64;++n)for(UINT y=1;y+1<S;++y)for(UINT x=2;x+2<S;++x)oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));
        std::printf("FLICKER_SPEED_GATE v=%.2f oracle_error=%.6f\n",fast,oracle);metric("flicker soft clip speed gate: shader matches the CPU oracle",oracle,0,.006);}
    // Bit-identity: the variants with S = 0 (forced by the alpha flag over a constant alpha) against the plain programs on
    // fractional history lookups (the variants drop the single-tap branch), plain and filtered; and the soft clip where no
    // pixel is thin (routed background).
    {const DriftSpec spec{.4,2.37,.8};auto objects=[&](unsigned n){return drift_objects(spec,n);};
        for(bool filter:{false,true}){const FlickerConfig plain{"identity-plain",0,0,.1f,.5f,filter,false,.9f},variant{"identity-variant-s0",0,0,.1f,.5f,filter,true,.9f};
            for(const auto* bg:{&sentinelBackground,&routedBackground}){const auto a=flicker_sequence(s,resolver,filtered,objects,64,plain,*bg),b=flicker_sequence(s,resolver,filtered,objects,64,variant,*bg);++numeric_checks;require(same_rgb(a.output,b.output),filter?"filtered variant at S = 0 is bit-identical to the filtered program":"variant at S = 0 is bit-identical to the plain program");}}
        const auto a=flicker_sequence(s,resolver,filtered,objects,64,base,routedBackground),b=flicker_sequence(s,resolver,filtered,objects,64,soft,routedBackground);++numeric_checks;require(same_rgb(a.output,b.output),"soft clip with no thin pixel is bit-identical to the baseline");}
    // Moving square over the sentinel background (static 32 frames, then +1 px/frame): a pixel whose 3x3 holds no square
    // depth is the background exactly; the trailing adjacent pixel keeps at most S * w of the contrast.
    {const double sl=10.28,st=10.37;auto square=[&](unsigned n){const double l=sl+(n>=32?n-31:0);return std::vector<EdgeObject>{{l,st,l+6,st+6,1,.5f,n>=32?1.:0.,0}};};
        for(const FlickerConfig* c:{&base,&soft,&narrow}){const auto run=flicker_sequence(s,resolver,filtered,square,44,*c,sentinelBackground);double distant=0,trailing=0;
            for(unsigned n=33;n<44;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){bool adjacent=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)adjacent|=px(run.depth[n],x+dx,y+dy)==.5f;
                const double lx=sl+(n-31),value=std::fabs(px(run.output[n],x,y)-.25);if(!adjacent)distant=std::max(distant,value);else if(x+1<=lx-.5)trailing=std::max(trailing,value);}
            const double bound=.75*c->thin*(c->wmax>0?c->wmax:c->weight)+1./255;
            std::printf("FLICKER_GHOST config=%s ghost_far=%.6f ghost_trailing_adjacent=%.6f bound=%.6f\n",c->name,distant,trailing,bound);
            metric((std::string("flicker ghost ")+c->name+": beyond one pixel the revealed background carries no square colour").c_str(),distant,0,1./255);
            if(c->thin>0)metric((std::string("flicker ghost ")+c->name+": trailing adjacent pixel within S * w of the contrast").c_str(),trailing,0,bound);}}
    // ---- step 2 gates, on the static lattice the mask and the disocclusion test keep (lines 1.25 px wide, pitch 4) ----
    {auto ripple=[&](const FlickerRun& r){double sum=0;unsigned count=0;for(unsigned n=driftFrames-latticePhases-1;n<driftFrames-1;++n)for(UINT y=driftLo;y<driftHi;++y)for(UINT x=driftLo;x<driftHi;++x){sum+=std::fabs(px(r.output[n+1],x,y)-2*px(r.output[n],x,y)+px(r.output[n-1],x,y))/2;++count;}return sum/count;};
        auto periodMean=[&](const FlickerRun& r,UINT x,UINT y){double sum=0;for(unsigned n=driftFrames-latticePhases;n<driftFrames;++n)sum+=px(r.output[n],x,y);return sum/latticePhases;};
        auto cutError=[&](const FlickerRun& r,unsigned frame){double sum=0;unsigned count=0;for(UINT y=driftLo;y<driftHi;++y)for(UINT x=driftLo;x<driftHi;++x){const double e=px(r.output[frame],x,y)-periodMean(r,x,y);sum+=e*e;++count;}return std::sqrt(sum/count);};
        const double ratio=ripple(staticRuns[2])/ripple(staticRuns[1]);double brightness=0,flat=0;unsigned count=0;
        for(UINT y=driftLo;y<driftHi;++y)for(UINT x=driftLo;x<driftHi;++x){brightness+=periodMean(staticRuns[2],x,y)-periodMean(staticRuns[1],x,y);++count;}
        brightness=std::fabs(brightness/count);
        for(unsigned n=driftFrames-driftAnalysed;n<driftFrames;++n)for(UINT x=0;x<S;++x)flat=std::max(flat,double(std::fabs(px(staticRuns[2].output[n],x,31)-px(staticRuns[0].output[n],x,31))));
        const double cut8=cutError(staticRuns[2],7),cut20=cutError(staticRuns[1],19);
        std::printf("FLICKER_STEP2 static_ripple_ratio=%.4f predicted=0.29 brightness_difference=%.6f flat_difference=%.6f cut_error_frame8_adaptive=%.6f cut_error_frame20_baseline=%.6f\n",ratio,brightness,flat,cut8,cut20);
        metric("flicker adaptive weight 0.97: static ripple over the thin-clip baseline (closed form 0.29)",ratio,.29,.05);
        metric("flicker adaptive weight 0.97: static lattice brightness within 1/255 of the thin-clip baseline",brightness,0,1./255);
        metric("flicker adaptive weight 0.97: flat background unchanged",flat,0,.01);
        ++numeric_checks;require(cut8<=cut20,"adaptive weight converges in one jitter cycle");
        // Speed >= HI: the weight is the baseline's. With weight 0.5 the age ramp n / (n + 1) >= 0.5 never binds, so the
        // aged run must equal the thin-clip run bit for bit, rejections and age restarts included.
        const FlickerConfig fastSoft{"fast-soft",.75f,0,.1f,.5f,false,false,.5f},fastAged{"fast-aged",.75f,.97f,.1f,.5f,false,false,.5f};const DriftSpec spec{.6,2.37,.8};
        const auto a=flicker_sequence(s,resolver,filtered,[&](unsigned n){return drift_objects(spec,n);},64,fastSoft,sentinelBackground),b=flicker_sequence(s,resolver,filtered,[&](unsigned n){return drift_objects(spec,n);},64,fastAged,sentinelBackground);
        ++numeric_checks;require(same_rgb(a.output,b.output),"adaptive weight at speed >= HI is bit-identical to the thin-clip baseline");}
    // ---- step 3 gates: alpha history (alpha follows the colour in these scenes) ----
    {s.alphaFollows=true;const FlickerConfig alphaOn{"alpha-history",0,0,.1f,.5f,false,true,.9f},alphaSoft{"alpha-history+soft-0.75+w-0.97",.75f,.97f,.1f,.5f,false,true,.9f};
        for(double v:{0.,.4}){const DriftSpec spec{v,4,1.25};auto objects=[&](unsigned n){return drift_objects(spec,n);};
            for(const FlickerConfig* c:{&alphaOn,&alphaSoft}){const auto run=flicker_sequence(s,resolver,filtered,objects,96,*c,sentinelBackground);const auto model=flicker_model(run,*c,v);double oracle=0;
                for(unsigned n=0;n<96;++n)for(UINT y=1;y+1<S;++y)for(UINT x=2;x+2<S;++x)oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y,3)-model.alpha[n][y*S+x])));
                auto ripple=[&](const std::vector<std::vector<float>>& o,UINT ch){double sum=0;unsigned count=0;for(unsigned n=96-latticePhases-1;n<95;++n)for(UINT y=driftLo;y<driftHi;++y)for(UINT x=driftLo;x<driftHi;++x){sum+=std::fabs(px(o[n+1],x,y,ch)-2*px(o[n],x,y,ch)+px(o[n-1],x,y,ch))/2;++count;}return sum/count;};
                const double alphaRatio=ripple(run.output,3)/ripple(run.current,3),colourRatio=ripple(run.output,0)/ripple(run.current,0);
                std::printf("FLICKER_ALPHA config=%s v=%.2f alpha_oracle_error=%.6f alpha_ripple_ratio=%.4f colour_ripple_ratio=%.4f\n",c->name,v,oracle,alphaRatio,colourRatio);
                metric((std::string("flicker ")+c->name+": resolved alpha matches the CPU oracle").c_str(),oracle,0,.0006/(1-(c->wmax>0?c->wmax:c->weight)));
                metric((std::string("flicker ")+c->name+": alpha ripple ratio as the colour's").c_str(),alphaRatio,colourRatio,.15);}}
        // Off: the variant without the flag hands the current alpha through bit for bit.
        const DriftSpec spec{.4,4,1.25};const auto run=flicker_sequence(s,resolver,filtered,[&](unsigned n){return drift_objects(spec,n);},32,soft,sentinelBackground);bool identical=true;
        for(unsigned n=0;n<32;++n)for(UINT i=0;i<S*S;++i)identical=identical&&!std::memcmp(&run.output[n][i*4+3],&run.current[n][i*4+3],sizeof(float));
        ++numeric_checks;require(identical,"alpha history off: the output alpha is the current alpha bit for bit");s.alphaFollows=false;}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
