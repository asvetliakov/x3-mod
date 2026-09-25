// Thin-region source (FrameInputs::thin_region_source, X3M_TAA_THIN_REGION_SOURCE; docs/architecture/
// taa-thin-geometry-alternatives.md section 3.2), lattice mode. A 64 x 32 lane (A32B32G32R32F RT2: .r device depth with the -1
// sentinel, .a = 1 - thin where the route voted, 1 on other geometry, -1 on the fill): on the screen gate folded into the
// tests draw with the thin-vote twin, on the camera gate read by the folded resolve itself (the mask fold,
// docs/architecture/taa-mask-fold.md), per pixel class:
//   U  unvoted struts over the sky (1 px wide, 4 px apart: every 7-tap line across them changes class twice: fragmented);
//   W  voted struts over the sky (fragmented and voted);
//   V  voted bars over a panel at almost the same depth (0.25 against 0.26: no class change, never fragmented);
//   S  sky and P panel controls beyond the screen gate's 5-px grow of any flag.
// Expected flag (screen gate: tests-target b >= 254/255, b <= 1/255 otherwise; camera gate: the region hold of the age target
// is L, the pixel flagged this frame, else 0 (static classes: never flagged before either)):
//   both   U W V       screen  U W   (the vote unread; screen gate only: a camera-gate run is refused)       vote  W V   (the search skipped)
// and the resolve's weight: after 40 frames of one colour and one frame of another, a flagged pixel keeps 0.97 of its
// history (the thin-region weight, clip relaxed) and an unflagged one none (clipped to the uniform new colour).
// Identities (every published target, byte for byte, every frame): screen = the same run without the vote (the plain
// tests program; screen gate); vote without the vote (no thin_vote input; on the screen gate also the twin refused at
// creation) = that plain run with the drawn source reported as both. Every compared run starts from the fixture's hostile
// state and restores it exactly; a source outside the enum, and screen on the camera gate, refuse the run and restore it
// too. Reset: a vote run after before_reset / Reset / after_reset draws the same flags (screen gate: tests target; camera
// gate: age target) as before it. (The timing moved to FOLD_TIMING: the camera gate has no tests draw.)
using x3m::renderer::ThinRegionSource;
using x3m::renderer::thin_region_source_name;
namespace thin_source {
constexpr UINT SW=64,SH=32;
enum Class { Sky, Panel, U, Wv, V };
Class classAt(UINT x,UINT y){
    if(y>=2&&y<=9&&x%4==0&&x>=4&&x<=28)return U;
    if(y>=2&&y<=9&&x%4==0&&x>=36&&x<=48)return Wv;
    if(y<18)return Sky;
    if(y>=22&&y<=29&&(x==8||x==9||x==16||x==17||x==24||x==25||x==32||x==33))return V;
    return Panel;}
float depthOf(Class c){return c==Sky?-1.f:c==Panel?.26f:.25f;}
float alphaOf(Class c){return c==Sky?-1.f:(c==Wv||c==V)?0.f:1.f;}
// The test pixels of each class: U struts x <= 20 (16 px from W), all W and V, S x 58..62 rows 2..9, P x 40..60 rows 22..29:
// beyond the screen gate's 11x11 grow (5 px) of every flag another source may set (the sky gaps of the W struts reach x = 50).
bool tested(UINT x,UINT y,Class c){
    if(c==U)return x<=20;
    if(c==Wv||c==V)return true;
    if(c==Sky)return x>=58&&x<=62&&y>=2&&y<=9;
    return x>=40&&x<=60&&y>=22&&y<=29;}
struct Scene {
    IDirect3DDevice9* d;Com<IDirect3DTexture9> lane,motion,colorA,colorB;
    template<class F> void upload(IDirect3DTexture9* target,D3DFORMAT format,UINT pixel,UINT w,UINT h,F texel){
        Com<IDirect3DTexture9> staging;check("source staging",d->CreateTexture(w,h,1,0,format,D3DPOOL_SYSTEMMEM,&staging.p,nullptr));
        D3DLOCKED_RECT lock{};check("source staging lock",staging->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)texel(x,y,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*pixel);
        check("source staging unlock",staging->UnlockRect(0));check("source upload",d->UpdateTexture(staging.p,target));}
    // uniform: every pixel one class (the timing content); else the class map above.
    Scene(IDirect3DDevice9* device,UINT w,UINT h,int uniform=-1):d(device){
        auto target=[&](D3DFORMAT format,Com<IDirect3DTexture9>& t,const char* label){check(label,d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&t.p,nullptr));};
        target(D3DFMT_A32B32G32R32F,lane,"source lane");target(D3DFMT_A32B32G32R32F,motion,"source motion");
        target(D3DFMT_A16B16G16R16F,colorA,"source colour A");target(D3DFMT_A16B16G16R16F,colorB,"source colour B");
        auto cls=[&](UINT x,UINT y){return uniform>=0?Class(uniform):classAt(x,y);};
        upload(lane.p,D3DFMT_A32B32G32R32F,16,w,h,[&](UINT x,UINT y,char* p){const Class c=cls(x,y);const float z=depthOf(c),vz=z>=0?1.f/(1.0001f-z):-1.f,v[4]={z,.5f,vz,alphaOf(c)};std::memcpy(p,v,16);});
        // Camera path everywhere (alpha 0, the sky's sentinel class never set): a static camera, every gate open.
        upload(motion.p,D3DFMT_A32B32G32R32F,16,w,h,[&](UINT x,UINT y,char* p){const float m[4]={(x+.5f)/float(w),(y+.5f)/float(h),depthOf(cls(x,y)),0.f};std::memcpy(p,m,16);});
        for(int k=0;k<2;++k)upload(k?colorB.p:colorA.p,D3DFMT_A16B16G16R16F,8,w,h,[&](UINT,UINT,char* p){const unsigned short v[4]={toHalf(k?.2f:.8f),toHalf(k?.2f:.8f),toHalf(k?.2f:.8f),toHalf(1)};std::memcpy(p,v,8);});}
};
std::vector<unsigned char> bytes(IDirect3DDevice9* d,IDirect3DTexture9* texture){
    D3DSURFACE_DESC desc{};check("source level desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level,read;check("source level",texture->GetSurfaceLevel(0,&level.p));
    const UINT pixel=desc.Format==D3DFMT_A16B16G16R16F?8:4;
    check("source readback surface",d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read.p,nullptr));check("source readback",d->GetRenderTargetData(level.p,read.p));
    D3DLOCKED_RECT lock{};check("source readback lock",read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(desc.Width*desc.Height*pixel);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(&out[y*desc.Width*pixel],static_cast<char*>(lock.pBits)+y*lock.Pitch,desc.Width*pixel);
    check("source readback unlock",read->UnlockRect());return out;}
// Refuses CreatePixelShader of the thin-vote twin while alive (configure_thin_vote fails; the plain programs remain).
struct TwinRefusal {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,const DWORD* words,IDirect3DPixelShader9** out){
        namespace r=x3m::renderer;
        if(words==reinterpret_cast<const DWORD*>(r::temporal_line_mask_depth_thin_program())){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}
        return original(d,words,out);}
    explicit TwinRefusal(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[106],sizeof original);auto fn=&hook;std::memcpy(&table[106],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~TwinRefusal(){*reinterpret_cast<void***>(device)=previous;}
};
void setup(IDirect3DDevice9* d,TemporalPass& pass,const DWORD* resolver,bool refuseTwin){
    check("source initialize",pass.initialize(d,nullptr,resolver,nullptr,nullptr,reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program())));
    check("source configure far",pass.configure_far());
    if(refuseTwin){TwinRefusal refusal(d);const HRESULT hr=pass.configure_thin_vote();++numeric_checks;require(FAILED(hr)&&TwinRefusal::refused==1,"thin source: a refused twin leaves configure_thin_vote failed");}
    else{check("source configure thin vote",pass.configure_thin_vote());require(pass.thin_vote_available(),"thin source: the twin created");}}
FrameInputs inputs(const Scene& s,UINT w,UINT h,bool camera,ThinRegionSource source,bool vote,bool second){
    FrameInputs in{};in.color=second?s.colorB.p:s.colorA.p;in.current_depth=s.lane.p;in.motion=s.motion.p;in.width=w;in.height=h;in.epoch=1;in.weight=.9f;
    std::copy(identity,identity+16,in.clip_to_previous);in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;
    in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=false;
    in.thin_region_weight=.97f;in.thin_region_camera_gate=camera;in.thin_vote=vote;in.thin_region_source=source;return in;}
struct Result{HRESULT hr=S_OK;ThinRegionSource drawn=ThinRegionSource::Both;bool twin=false;const char* reason="";std::array<std::vector<unsigned char>,4> out;};
// frames runs of one sequence (colour A, the last one colour B); the hostile state before the last run, restored exactly.
Result sequence(IDirect3DDevice9* d,Fixture& f,TemporalPass& pass,const Scene& s,bool camera,ThinRegionSource source,bool vote,unsigned frames,const char* label){
    Result r;Output o;
    for(unsigned n=0;n<frames;++n){
        const bool last=n+1==frames;FrameInputs in=inputs(s,SW,SH,camera,source,vote,last&&frames>1);
        // Every frame from the hostile state: the pass's state block is created under it (see the ledger: this runtime's
        // Capture did not refresh stream offsets captured at the block's creation; measured on the fixture, not a change here).
        f.hostile();if(last){Snapshot before(d);r.hr=pass.run(in,&o);before.equals(d,label);}else check(label,pass.run(in,&o));
        if(FAILED(r.hr))return r;
        r.drawn=pass.diagnostics().thin_region_source;r.twin=pass.diagnostics().thin_vote;r.reason=pass.diagnostics().thin_vote_reason;}
    require(o.color&&o.depth&&o.age&&(camera?!o.stabiliser_mask:o.stabiliser_mask!=nullptr),"thin source: a thin-region run publishes the colour, depth and age targets and, on the screen gate, the mask");
    r.out={bytes(d,o.color),bytes(d,o.depth),bytes(d,o.age),camera?std::vector<unsigned char>():bytes(d,o.stabiliser_mask)};return r;}
std::array<unsigned,4> differ(const Result& a,const Result& b){std::array<unsigned,4> n{};for(unsigned k=0;k<4;++k){n[k]=a.out[k].size()!=b.out[k].size();for(size_t i=0;i<a.out[k].size()&&i<b.out[k].size();++i)n[k]+=a.out[k][i]!=b.out[k][i];}return n;}
// Per class over its test pixels: the flag as a byte (screen gate: the tests-target b, A8R8G8B8 byte 0; camera gate: 255 where
// the age target's region hold is L = FrameInputs::thin_region_hold_frames (8), the pixel flagged this frame, else 0) and the
// colour retention (out - B) / (A - B).
struct Stats{unsigned pixels=0,bmin=255,bmax=0;double rmin=1e9,rmax=-1e9;};
unsigned age_flag(const Result& r,UINT i){float age;std::memcpy(&age,&r.out[2][i*4],4);const double v=std::fabs(double(age));const double held=v<=65?(v-std::floor(v))*65536:0;
    return std::lround(held-128*std::floor(held/128))==8?255u:0u;}
std::array<Stats,5> stats(const Result& r,bool camera){std::array<Stats,5> st{};
    for(UINT y=0;y<SH;++y)for(UINT x=0;x<SW;++x){const Class c=classAt(x,y);if(!tested(x,y,c))continue;auto& t=st[c];++t.pixels;
        const unsigned b=camera?age_flag(r,y*SW+x):r.out[3][(y*SW+x)*4];t.bmin=std::min(t.bmin,b);t.bmax=std::max(t.bmax,b);
        unsigned short h;std::memcpy(&h,&r.out[0][(y*SW+x)*8],2);const double ret=(double(halfFloat(h))-double(halfFloat(toHalf(.2f))))/(double(halfFloat(toHalf(.8f)))-double(halfFloat(toHalf(.2f))));
        t.rmin=std::min(t.rmin,ret);t.rmax=std::max(t.rmax,ret);}
    return st;}
bool expectedFlag(ThinRegionSource source,Class c){
    if(c==Sky||c==Panel)return false;
    if(source==ThinRegionSource::Screen)return c!=V;
    if(source==ThinRegionSource::Vote)return c!=U;
    return true;}
} // namespace thin_source

void thin_source_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver,D3DPRESENT_PARAMETERS& pp){
    using namespace thin_source;
    std::puts("THIN_SOURCE_CASES");
    const char* const names[3]={"both","screen","vote"};
    const ThinRegionSource sources[3]={ThinRegionSource::Both,ThinRegionSource::Screen,ThinRegionSource::Vote};
    constexpr unsigned frames=41; // 40 of colour A (the age count passes n / (n + 1) = 0.97), one of colour B
    {Fixture f(d,compiler);Scene s(d,SW,SH);
        for(bool camera:{true,false}){
            const char* gate=camera?"camera":"screen";
            // Plain: the same inputs without the vote (the plain fold program): the reference of the identities.
            TemporalPass plainPass;setup(d,plainPass,resolver,false);
            const Result plain=sequence(d,f,plainPass,s,camera,ThinRegionSource::Both,false,frames,"thin source plain run restores the hostile state");check("thin source plain",plain.hr);
            for(unsigned i=0;i<3;++i){
                TemporalPass pass;setup(d,pass,resolver,false);
                // Screen on the camera gate: refused (the folded resolve has no plain program), the hostile state restored.
                if(camera&&i==1){const Result r=sequence(d,f,pass,s,camera,sources[i],true,1,"thin source screen refusal restores the hostile state");
                    std::printf("THIN_SOURCE_REFUSED gate=%s source=%s hr=%08lx\n",gate,names[i],(unsigned long)r.hr);
                    ++numeric_checks;require(r.hr==E_INVALIDARG,"thin source: screen on the camera gate refuses the run");continue;}
                const Result r=sequence(d,f,pass,s,camera,sources[i],true,frames,"thin source run restores the hostile state");check("thin source run",r.hr);
                const auto st=stats(r,camera);bool ok=r.drawn==(i==1?ThinRegionSource::Screen:sources[i])&&r.twin==(i!=1)&&std::strcmp(r.reason,i==1?"screen_source":"vote")==0;
                std::printf("THIN_SOURCE gate=%s source=%s drawn=%s twin=%u reason=%s",gate,names[i],thin_region_source_name(r.drawn),unsigned(r.twin),r.reason);
                const char* labels[5]={"s","p","u","w","v"};
                for(unsigned c=0;c<5;++c){const auto& t=st[c];const bool flag=expectedFlag(sources[i],Class(c));
                    std::printf(" %s_px=%u %s_b=%u..%u %s_keep=%.4f..%.4f",labels[c],t.pixels,labels[c],t.bmin,t.bmax,labels[c],t.rmin,t.rmax);
                    // Flagged: the whole tests-target flag and 0.97 of the history kept (FP16 output: half an ulp at 0.78 is 2.4e-4
                    // of 0.6); unflagged: at most the class bit and nothing kept.
                    ok=ok&&t.pixels>0&&(flag?t.bmin>=254&&t.rmin>=.97-.002&&t.rmax<=.97+.002:t.bmax<=1&&t.rmax<=.002&&t.rmin>=-.002);}
                std::printf("\n");
                ++numeric_checks;require(ok,"thin source: each source flags exactly its classes and the resolve keeps 0.97 of their history, none elsewhere");
                if(i==1){const auto n=differ(r,plain);std::printf("THIN_SOURCE_IDENTITY gate=%s kind=screen_vs_plain drawn=%s color_bytes_differ=%u depth_bytes_differ=%u age_bytes_differ=%u mask_bytes_differ=%u\n",gate,thin_region_source_name(r.drawn),n[0],n[1],n[2],n[3]);
                    ++numeric_checks;require(!(n[0]|n[1]|n[2]|n[3]),"thin source: screen is the plain run without the vote, byte for byte");}}
            // Vote without the vote: no thin_vote input, and (screen gate) the twin refused at creation. A both run of the plain
            // program; the camera gate needs no twin (its resolve reads the vote), so only the first applies there.
            for(unsigned kind=0;kind<(camera?1u:2u);++kind){
                TemporalPass pass;setup(d,pass,resolver,kind==1);
                const Result r=sequence(d,f,pass,s,camera,ThinRegionSource::Vote,kind==1,frames,"thin source fallback restores the hostile state");check("thin source fallback",r.hr);
                const auto n=differ(r,plain);
                std::printf("THIN_SOURCE_IDENTITY gate=%s kind=%s drawn=%s reason=%s color_bytes_differ=%u depth_bytes_differ=%u age_bytes_differ=%u mask_bytes_differ=%u\n",gate,kind?"vote_twin_refused_vs_plain":"vote_no_input_vs_plain",
                    thin_region_source_name(r.drawn),r.reason,n[0],n[1],n[2],n[3]);
                ++numeric_checks;require(r.drawn==ThinRegionSource::Both&&!r.twin&&std::strcmp(r.reason,kind?"no_twin_program":"not_requested")==0&&!(n[0]|n[1]|n[2]|n[3]),
                    "thin source: vote without its twin is a both run of the plain program, byte for byte");}}
        // A source outside the enum refuses the run before any state is touched (the hostile state stays exactly).
        {TemporalPass pass;setup(d,pass,resolver,false);FrameInputs in=inputs(s,SW,SH,true,ThinRegionSource(3),true,false);f.hostile();Snapshot before(d);Output o;const HRESULT hr=pass.run(in,&o);before.equals(d,"thin source invalid restores the hostile state");
            std::printf("THIN_SOURCE_INVALID value=3 hr=%08lx published=%u\n",(unsigned long)hr,unsigned(o.color!=nullptr));
            ++numeric_checks;require(hr==E_INVALIDARG&&!o.color&&!o.stabiliser_mask,"thin source: a source outside the enum refuses the run");}
    }
    // Reset: a vote pass draws one frame, the device is Reset (every default-pool object released first), and the first frame
    // after after_reset draws the same flags (it depends on the current frame alone: the camera gate's age target of a first
    // frame is the fresh count with this frame's holds) with the vote-only source.
    {TemporalPass pass;setup(d,pass,resolver,false);std::vector<unsigned char> mask[2];ThinRegionSource drawn[2]{};
        auto frame=[&](unsigned k){Fixture f(d,compiler);Scene s(d,SW,SH);const Result r=sequence(d,f,pass,s,true,ThinRegionSource::Vote,true,1,"thin source Reset run restores the hostile state");check("thin source Reset run",r.hr);mask[k]=r.out[2];drawn[k]=r.drawn;};
        frame(0);pass.before_reset();check("thin source Reset",d->Reset(&pp));pass.after_reset(S_OK);frame(1);
        unsigned differs=mask[0].size()!=mask[1].size();for(size_t i=0;i<mask[0].size()&&i<mask[1].size();++i)differs+=mask[0][i]!=mask[1][i];
        std::printf("THIN_SOURCE_RESET drawn_before=%s drawn_after=%s age_bytes_differ=%u\n",thin_region_source_name(drawn[0]),thin_region_source_name(drawn[1]),differs);
        ++numeric_checks;require(drawn[0]==ThinRegionSource::Vote&&drawn[1]==ThinRegionSource::Vote&&!differs,"thin source: after a Reset the vote-only flags are drawn again unchanged");}
}
