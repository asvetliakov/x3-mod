// Shared 2-D oracle of the far-stabiliser and thin-region cases of
// temporal_pass_fixture.cpp (lattice mode), and the pass timings. Included after
// temporal_flicker_inc.h: uses EdgeScene, FlickerRun, halton, metric, Snapshot.
// (The line-filter cases, docs/architecture/taa-lattice-crawl.md section 9, went
// with the option on 2026-09-23, cleanup batch 6.)
constexpr double lineDrift=.3;
constexpr float lineDepth=.99f,squareDepth=.98f;
struct LineConfig { const char* name; float thin,wmax; float farW=0,farA=0,thinW=0,relax=1; bool camera=false; float sentS=0,sentE=1,emisE=0; bool hold=false; }; // sentS / sentE: the sentinel stabiliser (temporal-integration.md), camera gate only; emisE: the thin region's emissive vote (thin-glow-lines.md 8.3 R3)
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
// History reconstruction the oracle models (docs/architecture/taa-high-resolution.md S3): 5 (the default programs) or 16 (the 16-tap twins).
unsigned oracleHistoryTaps=5;
// A' (resolve.hlsl X3M_REGION_HOLD; LineConfig::hold): the camera gate's L-frame peak hold (closureHold) and the hold fraction of the
// age count, (h + 128 code) / 65536 with code = q (L + 1) + t; L = oracleHoldFrames (FrameInputs::thin_region_hold_frames). The closed
// quarters k = floor(4.5 - 4 open) in float32 as the program computes them (never near a floor boundary for UNORM8 openness).
unsigned oracleHoldFrames=8;
float closure_hold(float own,double held,double& code){const double period=oracleHoldFrames+1.,q=std::floor((held+.5)/period),t=held-period*q,carried=t>0?q:0,k=std::floor(4.5f-4*own);
    code=k>=carried?period*k+(k>0?double(oracleHoldFrames):0.):held-1;return float(1-.25*carried);}
double hold_code(double h,double codeC){return (h+128*codeC)/65536;}
double fresh_code(bool flagged,float a){double c;closure_hold(a,0,c);return hold_code(flagged?double(oracleHoldFrames):0.,c);}
bool held_region(double age){const double v=std::fabs(age);const double held=v<=65?(v-std::floor(v))*65536:0;return held-128*std::floor(held/128)>0;} // the previous region hold at a texel
bool hold_class(float code){return code>.5f/255&&(code<1.5f/255||code>254.5f/255);} // the tests draw's sentinel-class code (1/255 or 1)
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
// Emissive vote of the thin region exactly as line_mask_ps.hlsl casts it (thin-glow-lines.md 8.3 R3, clamped addressing): a ROUTED
// pixel (motion alpha 1) of valid depth whose own Rec.709 luma exceeds E and whose 3x3 luma minimum is below that luma / 3.
// A tap that is not finite (NaN, or |L| above the resolve's 65000 limit) counts as 0 at the centre and as the limit as a
// neighbour, so it can neither vote itself nor lower the minimum that admits its neighbours.
constexpr double emissiveFinite=65000;
bool emissive_vote(const std::vector<float>& current,const std::vector<float>& depth,const std::vector<float>& motion,int x,int y,float E){
    constexpr int S=int(EdgeScene::S);auto clampx=[](int v){return UINT(std::min(std::max(v,0),S-1));};
    auto luma=[&](int qx,int qy,double bad){const UINT cx=clampx(qx),cy=clampx(qy);const double l=.2126*double(px(current,cx,cy,0))+.7152*double(px(current,cx,cy,1))+.0722*double(px(current,cx,cy,2));
        return (l==l&&std::fabs(l)<=emissiveFinite)?std::max(l,0.):bad;};
    const float d=depth_clamped(depth,x,y);if(!(d>=0&&d<=1))return false;
    if(px(motion,clampx(x),clampx(y),3)!=1.f)return false;
    const double centre=luma(x,y,0);if(!(centre>double(E)))return false;
    double lowest=emissiveFinite;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)lowest=std::min(lowest,luma(x+dx,y+dy,emissiveFinite));
    return lowest*3<centre;}
float thin_region_strength(const std::vector<float>& depth,int x,int y,bool camera=false,const std::vector<float>* motion=nullptr,float sentS=0,const std::vector<float>* current=nullptr,float emisE=0){bool any=false;float closure=0;
    const bool vote=current&&motion&&emisE>0;
    for(int dy=-8;dy<=8;++dy)for(int dx=-8;dx<=8;++dx){if(std::abs(dx)<=5&&std::abs(dy)<=5)any=any||fragmented(depth,x+dx,y+dy)||(vote&&emissive_vote(*current,depth,*motion,x+dx,y+dy,emisE));const float d=depth_clamped(depth,x+dx,y+dy);const bool geometry=d>=0&&d<=1;
        const double vx=geometry?line_velocity_x(d):cameraPanX,vy=geometry?line_velocity(d):0,screen=std::hypot(vx,vy),relative=std::hypot(vx-cameraPanX,vy),speed=camera?std::min(screen,relative):screen;
        closure=std::max(closure,quantise8((speed-double(farLo))/(double(farHi)-double(farLo))));}
    float strength=any?1-closure:0;
    if(camera&&motion&&sentS>0&&depth_clamped(depth,x,y)<=-.5f&&px(*motion,UINT(x),UINT(y),3)==-1.f)strength=std::max(strength,sentS*(1-closure));
    return strength;}
float far_gate_weight_raw(float depth){if(!(depth>=0&&depth<=1))return 0;return std::min(std::max((depth-farD0)*farInv,0.f),1.f);}
float far_gate_weight(float depth){if(!(depth>=0&&depth<=1))return 0;const float w=std::min(std::max((depth-farD0)*farInv,0.f),1.f);return float(std::lround(w*255.f))/255.f;} // as the A8R8G8B8 mask stores it

// 2-D CPU oracle of the resolve on this scene at k = 0 (interior pixels [3, S-3); the rest take the shader's output):
// closest-depth dilation (content moves by (line_velocity_x, line_velocity) of the closest depth; the camera path by
// (cameraPanX, 0)), the disocclusion proof over the 2x2 footprint, Catmull-Rom history with the snap (the 5-tap form: the 4x4
// without its corners, renormalised; oracleHistoryTaps 16: the full 4x4), the 3x3 clip, the thin
// soft clip and age weight of the variants, the camera gate's 7x7 box clip by the share of the strength the camera term added,
// and the far stabiliser's exp(-A d^2) current sample by the far gate. FP16 rounding per frame. The 5-tap form weighs each of
// its five blocks (the texels one bilinear fetch blends) before the sum. c.hold (A'): the gates are composed per pixel from the
// published tests target of each frame (run.mask[n]: r screen openness, g far weight, b flag / class code, a camera openness)
// and the holds carried in the fraction of the model's own age (hold_code), exactly as resolve.hlsl X3M_REGION_HOLD does; the
// box term applies only where the box programs opened (camera openness above screen openness, or the class with S > 0).
FlickerModel line_model(const FlickerRun& run,const LineConfig& c,const std::vector<std::vector<float>>* masks=nullptr){constexpr UINT S=EdgeScene::S;const unsigned N=unsigned(run.current.size());FlickerModel m;m.color.resize(N);m.age.resize(N);
    auto valid=[](float d){return d>=0&&d<=1;};auto sentinel=[](float d){return d<=-.5f;};const double w=.9;
    oracleSkipped=0;
    for(unsigned n=0;n<N;++n){if(cameraPanAlternates)(cameraPanVertical?cameraPanY:cameraPanX)=camera_pan_at(n);m.color[n].assign(S*S,0);m.age[n].assign(S*S,1);const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){m.color[n][y*S+x]=px(run.output[n],x,y);if(!run.age.empty())m.age[n][y*S+x]=px(run.age[n],x,y);}
        if(n&&n-1==oracleInjectFrame)for(int y=oracleInjectRect[1];y<oracleInjectRect[3];++y)for(int x=oracleInjectRect[0];x<oracleInjectRect[2];++x)m.color[n-1][UINT(y)*S+UINT(x)]=oracleInjectValue; // after frame n-1 was modelled
        if(!n)continue;
        const bool hold=c.hold&&c.camera&&c.thinW>0;
        if(hold&&(!masks||masks->size()!=N))throw std::runtime_error("line_model: a hold run passes its tests target of every frame");
        for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){const UINT i=y*S+x;const double cur=px(run.current[n],x,y);const float centre=px(run.depth[n],x,y);
            const float testR=hold?px((*masks)[n],x,y,0):0,testB=hold?px((*masks)[n],x,y,2):0,testA=hold?px((*masks)[n],x,y,3):0;const bool flagged=testB>.5f;
            const float fresh=hold?float(1+fresh_code(flagged,testA)):1.f; // what a current-only return writes
            if(!oracle_finite(cur)){m.color[n][i]=0;m.age[n][i]=fresh;continue;}
            const double wcur=oracle_weigh(cur);unsigned finiteCount=0;
            bool sawValid=valid(centre),sawSentinel=sentinel(centre);double nearest=sentinel(centre)?1:centre,lo=wcur,hi=wcur,m1=0,m2=0,sum=0,total=0;int nearX=0,nearY=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const float d=px(run.depth[n],x+dx,y+dy);if(valid(d)){sawValid=true;if(d<nearest){nearest=d;nearX=dx;nearY=dy;}}if(sentinel(d))sawSentinel=true;
                const double raw=px(run.current[n],x+dx,y+dy);if(!oracle_finite(raw))continue;
                const double q=oracle_weigh(raw);++finiteCount;lo=std::min(lo,q);hi=std::max(hi,q);m1+=q;m2+=q*q;const double g=std::exp(-double(c.farA)*((dx-jx)*(dx-jx)+(dy-jy)*(dy-jy)));sum+=q*g;total+=g;}
            m1/=finiteCount;m2/=finiteCount;const double sigma=std::sqrt(std::max(m2-m1*m1,0.));lo=std::max(lo,m1-1.25*sigma);hi=std::min(hi,m1+1.25*sigma);
            const double vx=line_velocity_x(nearest),vy=line_velocity(nearest),speed=std::hypot(vx,vy);
            auto snap=[](double position,double& base,double& f){base=std::floor(position);f=position-base;if(f>1-1e-4){base+=1;f=0;}else if(f<1e-4)f=0;};
            double bx,fx,by,fy;snap(x-vx,bx,fx);snap(y-vy,by,fy);
            if(bx<1||by<1||bx+2>=S||by+2>=S){++oracleSkipped;continue;} // history footprint outside the frame (fast pans only): the shader's output stands
            // (A' leaves those pixels' age to the shader too: m.age was initialised from the shader's output above.)
            const double tolerance=std::max(.0001,.02*nearest);bool proven=true;
            for(int ty=0;ty<2;++ty)for(int tx=0;tx<2;++tx){const double weight=(tx?fx:1-fx)*(ty?fy:1-fy);if(weight>.01){const float p=px(run.depth[n-1],UINT(bx+tx),UINT(by+ty));if(!((valid(p)&&p>=nearest-tolerance)||sentinel(p)))proven=false;}}
            if(!proven){m.color[n][i]=float(cur);m.age[n][i]=fresh;continue;}
            auto keys=[](double f,double* cr){const double f2=f*f,f3=f2*f;cr[0]=-.5*f+f2-.5*f3;cr[1]=1-2.5*f2+1.5*f3;cr[2]=.5*f+2*f2-1.5*f3;cr[3]=-.5*f2+.5*f3;};
            double crx[4],cry[4];keys(fx,crx);keys(fy,cry);
            double old=0,weights=0;
            if(oracleHistoryTaps==16){for(int t=0;t<4;++t)for(int u=0;u<4;++u){const double cr=crx[u]*cry[t];if(cr!=0){const double tap=m.color[n-1][UINT(by+t-1)*S+UINT(bx+u-1)];if(oracle_finite(tap)){old+=cr*oracle_weigh(tap);weights+=cr;}}}
                old/=weights;}
            else{ // S3: the 5-tap bilinear form is the 4x4 filter without its four corner texels, renormalised. Since A' each of the
                // five blocks (per axis {0}, {1, 2}, {3}; the corners dropped) is weighed before the sum; a non-finite contributing texel
                // or result refuses the lookup (current only). At rest (f = 0) that is the point read of the one texel.
                bool finite=true;const int block[4]={0,1,1,2};
                for(int P=0;P<3;++P)for(int Q=0;Q<3;++Q){if(P!=1&&Q!=1)continue;double value=0,mass=0;
                    for(int t=0;t<4;++t)for(int u=0;u<4;++u){if(block[u]!=P||block[t]!=Q)continue;const double cr=crx[u]*cry[t];if(cr!=0){const double tap=m.color[n-1][UINT(by+t-1)*S+UINT(bx+u-1)];finite=finite&&oracle_finite(tap);value+=cr*tap;mass+=cr;}}
                    if(mass!=0){old+=mass*oracle_weigh(value/mass);weights+=mass;}}
                old/=weights;if(!finite||!oracle_finite(old)){m.color[n][i]=float(cur);m.age[n][i]=fresh;continue;}}
            const bool farOn=c.farW>0||c.farA>0||c.thinW>0; // the far program has no 3x3 sentinel soft clip
            const UINT ageIndex=UINT(by+(fy>=.5?1:0))*S+UINT(bx+(fx>=.5?1:0));
            double gate=0,screenGate=0,holdFraction=0,heldCount=0;bool boxOpen=true;
            if(hold){const double stored=m.age[n-1][ageIndex],magnitude=std::fabs(stored);const double held=magnitude<=65?(magnitude-std::floor(magnitude))*65536:0;heldCount=std::floor(magnitude);
                const double heldC=std::floor(held/128);const double regionHold=flagged?double(oracleHoldFrames):std::max(held-128*heldC-1,0.);
                const UINT bx_=UINT(int(x)+nearX),by_=UINT(int(y)+nearY); // the resolve's dilation: the nearest-depth neighbour's tests texel
                const float ownS=std::min(testR,px((*masks)[n],bx_,by_,0)),ownC=std::min(testA,px((*masks)[n],bx_,by_,3));
                double codeC;const float openC=std::min(ownC,closure_hold(ownC,heldC,codeC)),openS=std::min(ownS,openC);
                const bool region=regionHold>0,sentinelTerm=c.sentS>0&&hold_class(testB);
                gate=std::max(double(region?openC:0.f),double(sentinelTerm?c.sentS*openC:0.f));screenGate=region?openS:0.f;
                holdFraction=hold_code(regionHold,codeC);boxOpen=sentinelTerm||(testA>testR&&(flagged||held_region(m.age[n-1][i])));} // the box twins' gate: same texel, last frame's region
            else{gate=c.thinW>0?quantise8(thin_region_strength(run.depth[n],int(x),int(y),c.camera,run.motion.empty()?nullptr:&run.motion[n],c.sentS)):0;screenGate=c.camera?quantise8(thin_region_strength(run.depth[n],int(x),int(y),false)):gate;}
            const double clamped=std::min(std::max(old,lo),hi),soft=farOn?screenGate*c.relax:sawValid&&sawSentinel?c.thin*(1-std::min(std::max((speed-2)*.5,0.),1.)):0;
            double boxTerm=0;
            if(c.camera&&gate>screenGate&&boxOpen){ // the strength the camera term added takes the history clipped to the 7x7 box of the current colour (clamped addressing)
                double boxLo=wcur,boxHi=wcur,innerLo=wcur,innerHi=wcur,rawMax=0;for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){const double raw=px(run.current[n],UINT(std::min(std::max(int(x)+dx,0),int(S)-1)),UINT(std::min(std::max(int(y)+dy,0),int(S)-1)));if(!oracle_finite(raw))continue;const double q=oracle_weigh(raw);boxLo=std::min(boxLo,q);boxHi=std::max(boxHi,q);rawMax=std::max(rawMax,std::max(raw,0.));
                    if(std::abs(dx)<=1&&std::abs(dy)<=1){innerLo=std::min(innerLo,q);innerHi=std::max(innerHi,q);}}
                if(c.sentS>0&&c.sentE>0&&rawMax>double(c.sentE)&&sentinel(centre)){boxLo=innerLo;boxHi=innerHi;} // the emitter bound: the inner 3x3
                boxTerm=(gate-screenGate)*c.relax*(std::min(std::max(old,boxLo),boxHi)-clamped);}
            old=clamped+soft*(old-clamped)+boxTerm;
            double keep=w;const double farw=hold?double(px((*masks)[n],x,y,1)):farOn?far_gate_weight(centre):0;
            if(farOn){double a=hold?heldCount:m.age[n-1][ageIndex];if(!(a>=1&&a<=64))a=1;
                const double ramp=a/(a+1),slow=1-std::min(std::max((speed-double(farLo))/(double(farHi)-double(farLo)),0.),1.);
                const double farKeep=w+(c.farW>0?double(far_gate_weight(centre))*slow*(std::min(ramp,double(c.farW))-w):0.);
                keep=gate>0?std::max(farKeep,w+gate*(std::min(ramp,double(c.thinW))-w)):farKeep;
                m.age[n][i]=float(std::min(a+1,64.)+holdFraction);}
            if(c.wmax>0){double a=m.age[n-1][ageIndex];if(!(a>=1&&a<=64))a=1;const double t=std::min(std::max((speed-.1)/.4,0.),1.);keep=std::min(a/(a+1),c.wmax+t*(w-c.wmax));m.age[n][i]=float(std::min(a+1,64.));}
            const double filterWeight=c.farA>0?farw:0.;
            const double blend=farOn?wcur+filterWeight*(sum/total-wcur):wcur;
            m.color[n][i]=halfFloat(toHalf(float(oracle_unweigh(blend+keep*(old-blend)))));}}
    if(oracleSkipped>oracleSkipCeiling)throw std::runtime_error("line_model: oracle skipped "+std::to_string(oracleSkipped)+" border pixels, ceiling "+std::to_string(oracleSkipCeiling));
    return m;}
void line_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("LINE_CASES");EdgeScene s(d,compiler);
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
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
        // Four passes, interleaved: plain; the far stabiliser (taps + one mask draw + the age target); the thin region; the thin
        // region with the camera gate.
        namespace r=x3m::renderer;TemporalPass passes[4];
        for(unsigned i=0;i<4;++i){check("line timing initialize",passes[i].initialize(d,nullptr,resolver));
            if(i>=1){check("line timing configure far",passes[i].configure_far());require(passes[i].camera_gate_available(),"line timing: camera-gate programs created");}}
        FrameInputs base{};base.color=color.p;base.current_depth=depth.p;base.width=W;base.height=H;base.epoch=1;base.weight=.9f;std::copy(identity,identity+16,base.clip_to_previous);base.motion_policy=MotionPolicy::KnownCameraOnly;base.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;base.history_allowed=true;base.caller_queries_idle=true;base.caller_scene_open=false;
        Com<IDirect3DTexture9> motion;Com<IDirect3DSurface9> motionSurface;check("line timing motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));check("line timing motion surface",motion->GetSurfaceLevel(0,&motionSurface.p));
        check("line timing motion target",d->SetRenderTarget(0,motionSurface.p));check("line timing motion clear",d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));check("line timing motion restore",d->SetRenderTarget(0,saved.p));check("line timing motion viewport",d->SetViewport(&vp)); // alpha 0: the camera path
        FrameInputs ins[4]={base,base,base,base};for(unsigned i=1;i<4;++i){ins[i].motion_policy=MotionPolicy::PerPixel;ins[i].motion=motion.p;}ins[2].thin_region_weight=ins[3].thin_region_weight=.97f;ins[3].thin_region_camera_gate=true;ins[1].far_weight=.985f;ins[1].far_filter=1;ins[1].far_d0=-.5f;ins[1].far_inv=1; // depth 0 everywhere: farw = 0.5
        Output out;double ms[4]{};
        for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<4;++which){check("line timing warm",passes[which].run(ins[which],&out));drain();}
        for(unsigned round=0;round<6;++round)for(unsigned step=0;step<4;++step){const unsigned which=(round+step)%4;drain();const auto start=stamp();check("line timing run",passes[which].run(ins[which],&out));drain();ms[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
        // Clear writes depth 0 (R32F from the ARGB clear colour): valid geometry everywhere.
        std::printf("LINE_TIMING width=%u height=%u content=all_geometry rounds=6 plain_ms=%.4f far_stabiliser_ms=%.4f thin_region_ms=%.4f far_delta_ms=%.4f thin_region_delta_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,ms[0],ms[1],ms[2],ms[1]-ms[0],ms[2]-ms[0]);
        // The camera gate (section 32.1) beside the thin region: on the all-geometry frame (no region: the box pass skips every
        // pixel) and on a fully fragmented frame under a 1 px/frame camera pan (rows alternate geometry and the sentinel; the
        // camera path is the correspondence, so the screen gate closes everywhere and the camera term reopens everywhere: the
        // box pass and the resolve's box clip run on every pixel, the worst case).
        const double camNoRegion[2]={ms[2],ms[3]};
        {s.target(depthSurface.p);D3DVIEWPORT9 full{0,0,W,H,0,1};check("camera timing viewport",d->SetViewport(&full));
            check("camera timing Begin",d->BeginScene());check("camera timing flat",d->SetPixelShader(s.flat.p));s.constant(-1,0,0,0);
            struct V{float x,y,z,rhw,u,v;};for(UINT y=1;y<H;y+=2){const V v[]={{-.5f,y-.5f,.5f,1,0,0},{W-.5f,y-.5f,.5f,1,1,0},{-.5f,y+.5f,.5f,1,0,1},{W-.5f,y+.5f,.5f,1,1,1}};check("camera timing stripe",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));}
            check("camera timing End",d->EndScene());check("camera timing restore",d->SetRenderTarget(0,saved.p));check("camera timing restore viewport",d->SetViewport(&vp));
            for(unsigned i=2;i<4;++i){ins[i].clip_to_previous[3]=-2.f/W;ins[i].sentinel_camera=true;ms[i]=0;}
            for(unsigned warm=0;warm<3;++warm)for(unsigned which=2;which<4;++which){check("camera timing warm",passes[which].run(ins[which],&out));drain();}
            for(unsigned round=0;round<6;++round)for(unsigned step=0;step<2;++step){const unsigned which=2+(round+step)%2;drain();const auto start=stamp();check("camera timing run",passes[which].run(ins[which],&out));drain();ms[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
            std::printf("LINE_TIMING_CAMERA width=%u height=%u rounds=6 no_region_thin_ms=%.4f no_region_camera_ms=%.4f no_region_camera_delta_ms=%.4f fragmented_pan_thin_ms=%.4f fragmented_pan_camera_ms=%.4f fragmented_pan_camera_delta_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,camNoRegion[0],camNoRegion[1],camNoRegion[1]-camNoRegion[0],ms[2],ms[3],ms[3]-ms[2]);
            // The four-channel lane as the depth input (section 32.4): the same fragmented pan frame copied into an A32B32G32R32F
            // target (.b = 1 on geometry: every valid pixel takes the lane fetch). Two camera-gate passes on that input, one
            // with the lane term (s5 bound, c9.w = 1), one without (c9 = 0, the c8 law): their difference is the extra fetch;
            // the R32F camera row above against the second one is the cost of the lane's .r copy draw itself.
            Com<IDirect3DTexture9> lane;Com<IDirect3DSurface9> laneSurface;check("lane timing texture",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&lane.p,nullptr));check("lane timing surface",lane->GetSurfaceLevel(0,&laneSurface.p));
            s.target(laneSurface.p);check("lane timing viewport",d->SetViewport(&full));check("lane timing Begin",d->BeginScene());check("lane timing PS",d->SetPixelShader(s.textured.p));check("lane timing source",d->SetTexture(0,depth.p));
            check("lane timing min",d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));check("lane timing mag",d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
            {const V v[]={{-.5f,-.5f,.5f,1,0,0},{W-.5f,-.5f,.5f,1,1,0},{-.5f,H-.5f,.5f,1,0,1},{W-.5f,H-.5f,.5f,1,1,1}};check("lane timing copy",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));}
            check("lane timing End",d->EndScene());check("lane timing unbind",d->SetTexture(0,nullptr));check("lane timing restore",d->SetRenderTarget(0,saved.p));check("lane timing restore viewport",d->SetViewport(&vp));
            TemporalPass lanePasses[2];FrameInputs laneIns[2]={ins[3],ins[3]};double laneMs[2]{};
            for(unsigned i=0;i<2;++i){check("lane timing initialize",lanePasses[i].initialize(d,nullptr,resolver,nullptr,nullptr,reinterpret_cast<const DWORD*>(r::hdr_writeback_program())));check("lane timing configure far",lanePasses[i].configure_far());laneIns[i].current_depth=lane.p;
                laneIns[i].camera_depth_parallax[0]=1e-4f;laneIns[i].camera_depth_parallax[3]=1.000003f;}
            laneIns[1].camera_lane_parallax[0]=1e-4f;laneIns[1].camera_lane_parallax[3]=1;
            for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<2;++which){check("lane timing warm",lanePasses[which].run(laneIns[which],&out));drain();}
            for(unsigned round=0;round<6;++round)for(unsigned step=0;step<2;++step){const unsigned which=(round+step)%2;drain();const auto start=stamp();check("lane timing run",lanePasses[which].run(laneIns[which],&out));drain();laneMs[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
            std::printf("LINE_TIMING_CAMERA_LANE width=%u height=%u rounds=6 r32f_camera_ms=%.4f lane_input_law_ms=%.4f lane_input_lane_ms=%.4f lane_fetch_delta_ms=%.4f lane_input_over_r32f_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,ms[3],laneMs[0],laneMs[1],laneMs[1]-laneMs[0],laneMs[0]-ms[3]);
            // Sentinel stabiliser (temporal-integration.md "Distant unrouted stations under a pan"): an all-sentinel, unrouted frame
            // (depth -1, motion alpha -1) under the same 1 px/frame pan. S = 0: no region, the box pass skips every pixel. S = 0.7: the
            // separable box (rows draw on every pixel, columns draw and the resolve's box clip on every pixel), the worst case; the
            // 49-tap program over a whole frame is fragmented_pan_camera_delta_ms of LINE_TIMING_CAMERA.
            for(auto* surface:{depthSurface.p,motionSurface.p}){s.target(surface);check("sentinel timing viewport",d->SetViewport(&full));check("sentinel timing Begin",d->BeginScene());check("sentinel timing flat",d->SetPixelShader(s.flat.p));s.constant(surface==depthSurface.p?-1.f:0.f,0,0,-1);
                const V v[]={{-.5f,-.5f,.5f,1,0,0},{W-.5f,-.5f,.5f,1,1,0},{-.5f,H-.5f,.5f,1,0,1},{W-.5f,H-.5f,.5f,1,1,1}};check("sentinel timing fill",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));check("sentinel timing End",d->EndScene());}
            check("sentinel timing restore",d->SetRenderTarget(0,saved.p));check("sentinel timing restore viewport",d->SetViewport(&vp));
            TemporalPass sentinelPasses[2];FrameInputs sentinelIns[2]={ins[3],ins[3]};double sentinelMs[2]{};sentinelIns[1].sentinel_strength=.7f;
            for(unsigned i=0;i<2;++i){check("sentinel timing initialize",sentinelPasses[i].initialize(d,nullptr,resolver));check("sentinel timing configure far",sentinelPasses[i].configure_far());if(i)check("sentinel timing configure sentinel",sentinelPasses[i].configure_sentinel());require(i==0||sentinelPasses[i].sentinel_available(),"sentinel timing: separable box programs created");}
            for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<2;++which){check("sentinel timing warm",sentinelPasses[which].run(sentinelIns[which],&out));drain();}
            for(unsigned round=0;round<6;++round)for(unsigned step=0;step<2;++step){const unsigned which=(round+step)%2;drain();const auto start=stamp();check("sentinel timing run",sentinelPasses[which].run(sentinelIns[which],&out));drain();sentinelMs[which]+=1000.*double(stamp()-start)/double(frequency.QuadPart)/6;}
            std::printf("LINE_TIMING_SENTINEL width=%u height=%u rounds=6 content=all_unrouted_sentinel_pan strength_off_ms=%.4f strength_on_separable_ms=%.4f sentinel_delta_ms=%.4f scope=cpu_wall_with_event_query_drain\n",W,H,sentinelMs[0],sentinelMs[1],sentinelMs[1]-sentinelMs[0]);}
        check("line timing restore target",d->SetRenderTarget(0,saved.p));check("line timing restore vp",d->SetViewport(&vp));}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
