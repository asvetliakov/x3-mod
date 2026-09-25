// Depth-copy fold (docs/architecture/taa-high-resolution.md S1), lattice mode. On a two- or four-channel current depth
// (the sun-shadow lane's A32B32G32R32F RT2: .r = device depth with the -1 sentinel, .g = a share, .b = .a = view z; or a
// G32R32F depth) a far-program run of the screen-gate chain writes the next depth history as COLOR1 of the mask chain's
// first draw instead of the copy draw; a camera-gate run (the mask fold, docs/architecture/taa-mask-fold.md) writes it as
// RT2 of the resolve from every current-depth format, R32F included (reason resolve_mrt). Every run starts from the
// fixture's hostile state and must restore it exactly (Snapshot, COLORWRITEENABLE1 set to 5 beforehand). Per configuration
// (the camera gate, the screen-gate thin region, the far stabiliser alone; three frames of a pan) the colour history, the
// depth history, the age target and (not on the camera gate) the final mask must be byte-identical between:
//   lane fold / G32R32F fold / lane with the screen chain's folding programs refused at creation (the copy draw; the camera
//   gate still folds) and an R32F twin holding the same .r (the StretchRect path; the camera gate folds it too), all with the
//   lane term off. The camera gate's lane-term run (c9.w = 1, .b read from s1) folds every frame (reported through `ok`).
// Negative control (camera gate): a lane whose one texel .r is one ulp up must differ from the R32F twin in the depth history.
// Faults, lane input, history warm: on the screen-gate chain the RT1 bind refused, then the fold draw failed; on the camera
// gate the RT2 bind refused, then the resolve draw failed. The run fails like any draw failure (E_FAIL, nothing published,
// history dropped), the target is unbound right after the draw attempt, the caller's state comes back, and the next run folds
// again from an empty history. Reset: the device is Reset between two runs of a lane pass (before_reset / after_reset) and
// the fold resumes byte-identical to its R32F twin.
#include <memory>
namespace depth_fold {
constexpr UINT FW=48,FH=32;
float depthAt(UINT x,UINT y){
    if(y<12)return x%4==0||y%5==0?.99991f+.00001f*float(x%3):-1.f;          // struts over the sentinel (a lattice)
    if(y<20)return x%7==3?-1.f:.9995f+.00004f*float((x*5+y)%11);             // far geometry (farw > 0) with holes
    return (x+y)%9==0?-1.f:.25f+.02f*float((x/3+y)%17);}                     // near geometry with depth steps
// Refuses CreatePixelShader of the two folding programs while alive (the copy draw path of a lane input).
struct CreateRefusal {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,const DWORD* words,IDirect3DPixelShader9** out){
        namespace r=x3m::renderer;
        if(words==reinterpret_cast<const DWORD*>(r::temporal_line_mask_depth_program())){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}
        return original(d,words,out);}
    explicit CreateRefusal(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[106],sizeof original);auto fn=&hook;std::memcpy(&table[106],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~CreateRefusal(){*reinterpret_cast<void***>(device)=previous;}
};
// Records SetRenderTarget(`slot`, ...) while alive; with `fail`, refuses the first bind of a surface other than `caller`.
struct TargetProbe {
    using Set=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
    static inline Set original=nullptr;static inline bool fail=false,refused=false;static inline IDirect3DSurface9* caller=nullptr;static inline DWORD slot=1;
    static inline std::vector<int> calls; // 1: a pass-owned bind (refused or not), 0: unbind, 2: the caller's own surface
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9* surface){
        if(index==slot){calls.push_back(!surface?0:surface==caller?2:1);if(surface&&surface!=caller&&fail&&!refused){refused=true;return E_FAIL;}}
        return original(d,index,surface);}
    TargetProbe(IDirect3DDevice9* d,IDirect3DSurface9* callerSurface,bool refuse,DWORD target=1):previous(*reinterpret_cast<void***>(d)),device(d){
        std::copy(previous,previous+119,table);std::memcpy(&original,&table[37],sizeof original);auto fn=&hook;std::memcpy(&table[37],&fn,sizeof fn);
        fail=refuse;refused=false;caller=callerSurface;slot=target;calls.clear();*reinterpret_cast<void***>(d)=table;}
    ~TargetProbe(){*reinterpret_cast<void***>(device)=previous;}
    // A pass-owned bind of the target followed by an unbind before the caller's surface comes back.
    static bool unbound_after_bind(){bool bound=false;for(int c:calls){if(c==1)bound=true;else if(c==0&&bound)return true;else if(c==2&&bound)return false;}return false;}
};
struct Scene {
    IDirect3DDevice9* d;Com<IDirect3DTexture9> color,depth32,lane,lane2,laneBad,motion;
    template<class F> void upload(IDirect3DTexture9* target,D3DFORMAT format,UINT pixel,F texel){
        Com<IDirect3DTexture9> staging;check("fold staging",d->CreateTexture(FW,FH,1,0,format,D3DPOOL_SYSTEMMEM,&staging.p,nullptr));
        D3DLOCKED_RECT lock{};check("fold staging lock",staging->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<FH;++y)for(UINT x=0;x<FW;++x)texel(x,y,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*pixel);
        check("fold staging unlock",staging->UnlockRect(0));check("fold upload",d->UpdateTexture(staging.p,target));}
    explicit Scene(IDirect3DDevice9* device):d(device){
        auto target=[&](D3DFORMAT format,Com<IDirect3DTexture9>& t,const char* label){check(label,d->CreateTexture(FW,FH,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&t.p,nullptr));};
        target(D3DFMT_A16B16G16R16F,color,"fold color");target(D3DFMT_R32F,depth32,"fold R32F depth");target(D3DFMT_A32B32G32R32F,lane,"fold lane");
        target(D3DFMT_G32R32F,lane2,"fold G32R32F depth");target(D3DFMT_A32B32G32R32F,laneBad,"fold perturbed lane");target(D3DFMT_A32B32G32R32F,motion,"fold motion");
        upload(color.p,D3DFMT_A16B16G16R16F,8,[](UINT x,UINT y,char* p){const unsigned h=(x*73856093u)^(y*19349663u);
            const unsigned short v[4]={toHalf(float(h%97)/48.f),toHalf(float(h/97%89)/60.f),toHalf(float(h/8633%83)/70.f),toHalf(1)};std::memcpy(p,v,8);});
        upload(depth32.p,D3DFMT_R32F,4,[](UINT x,UINT y,char* p){const float v=depthAt(x,y);std::memcpy(p,&v,4);});
        auto laneTexel=[](UINT x,UINT y,bool perturb,char* p){float z=depthAt(x,y);if(perturb&&x==30&&y==25)z=std::nextafter(z,2.f);
            const float w=z>=0?1.f/(1.0001f-z):-1.f,v[4]={z,.5f,w,w};std::memcpy(p,v,16);};
        upload(lane.p,D3DFMT_A32B32G32R32F,16,[&](UINT x,UINT y,char* p){laneTexel(x,y,false,p);});
        upload(laneBad.p,D3DFMT_A32B32G32R32F,16,[&](UINT x,UINT y,char* p){laneTexel(x,y,true,p);});
        upload(lane2.p,D3DFMT_G32R32F,8,[](UINT x,UINT y,char* p){const float v[2]={depthAt(x,y),.5f};std::memcpy(p,v,8);});
        // Motion: every third row routed (alpha 1, 1.5 px/frame against the camera), the sentinel unrouted (alpha -1), the rest
        // on the camera path (alpha 0).
        upload(motion.p,D3DFMT_A32B32G32R32F,16,[](UINT x,UINT y,char* p){const float u=(x+.5f)/FW,v=(y+.5f)/FH,z=depthAt(x,y);
            const float m[4]={y%3==0?u+1.5f/FW:u,v,z,y%3==0?1.f:z<0?-1.f:0.f};std::memcpy(p,m,16);});}
};
std::vector<unsigned char> bytes(IDirect3DDevice9* d,IDirect3DTexture9* texture){
    D3DSURFACE_DESC desc{};check("fold level desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level,read;check("fold level",texture->GetSurfaceLevel(0,&level.p));
    const UINT pixel=desc.Format==D3DFMT_A16B16G16R16F?8:4;
    check("fold readback surface",d->CreateOffscreenPlainSurface(FW,FH,desc.Format,D3DPOOL_SYSTEMMEM,&read.p,nullptr));check("fold readback",d->GetRenderTargetData(level.p,read.p));
    D3DLOCKED_RECT lock{};check("fold readback lock",read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(FW*FH*pixel);
    for(UINT y=0;y<FH;++y)std::memcpy(&out[y*FW*pixel],static_cast<char*>(lock.pBits)+y*lock.Pitch,FW*pixel);
    check("fold readback unlock",read->UnlockRect());return out;}
struct Case{const char* name;bool camera,negative,thin;};
FrameInputs inputs(const Scene& s,const Case& c){
    FrameInputs in{};in.color=s.color.p;in.width=FW;in.height=FH;in.epoch=1;in.weight=.9f;std::copy(identity,identity+16,in.clip_to_previous);in.clip_to_previous[3]=-2.f/FW; // 1 px/frame pan
    in.motion_policy=MotionPolicy::PerPixel;in.motion=s.motion.p;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=false;
    if(c.thin){in.thin_region_weight=.97f;in.thin_region_camera_gate=c.camera;}else{in.far_weight=.985f;in.far_filter=1;}
    in.far_d0=.9995f;in.far_inv=2000;
    if(c.camera){in.camera_depth_parallax[0]=1e-4f;in.camera_depth_parallax[3]=1.000003f;}
    return in;}
void setup(IDirect3DDevice9* d,TemporalPass& pass,const DWORD* resolver,const Case& c,bool refuse){
    // The identity copy program: a lane input requires it (the copy draw when the folding program is missing).
    auto make=[&]{check("fold initialize",pass.initialize(d,nullptr,resolver,nullptr,nullptr,reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program())));check("fold configure far",pass.configure_far());};
    if(refuse){CreateRefusal refusal(d);make();require(CreateRefusal::refused>=1&&pass.far_available(),"fold: the folding programs refused at creation leave the far programs");}else make();
    (void)c;}
// One run from the hostile state, restored exactly; the published targets as bytes (the camera gate publishes no mask).
struct Result{HRESULT hr=S_OK;bool folded=false,history=false;const char* reason="";std::array<std::vector<unsigned char>,4> out;};
Result run(IDirect3DDevice9* d,Fixture& f,TemporalPass& pass,FrameInputs in,const char* label){
    f.hostile();check("fold hostile COLORWRITEENABLE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,5));Snapshot before(d);
    Result r;Output o;r.hr=pass.run(in,&o);before.equals(d,label);
    r.folded=pass.diagnostics().depth_folded;r.reason=pass.diagnostics().depth_fold_reason;r.history=o.used_history;
    if(SUCCEEDED(r.hr)){const bool camera=pass.diagnostics().region_hold;require(o.color&&o.depth&&o.age&&(camera?!o.stabiliser_mask:o.stabiliser_mask!=nullptr),"fold: a far-program run publishes the colour, depth and age targets and, off the camera gate, the mask");
        r.out={bytes(d,o.color),bytes(d,o.depth),bytes(d,o.age),camera?std::vector<unsigned char>():bytes(d,o.stabiliser_mask)};}
    return r;}
std::array<unsigned,4> differ(const Result& a,const Result& b){std::array<unsigned,4> n{};for(unsigned k=0;k<4;++k)for(size_t i=0;i<a.out[k].size()&&i<b.out[k].size();++i)n[k]+=a.out[k][i]!=b.out[k][i];return n;}
} // namespace depth_fold

void depth_fold_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver,D3DPRESENT_PARAMETERS& pp){
    using namespace depth_fold;
    std::puts("DEPTH_FOLD_CASES");
    const Case cases[]={{"camera",true,true,true},{"thin_screen",false,false,true},{"far_only",false,false,false}};
    {Fixture f(d,compiler);Scene s(d);
        for(const auto& c:cases){
            // 0 R32F twin, 1 lane fold, 2 lane copy (fold refused: the screen chain's copy draw; the camera gate still folds),
            // 3 G32R32F fold; camera: 4 the lane term (c9.w = 1) folding, 5 the perturbed lane (negative control).
            struct Variant{const char* name;IDirect3DTexture9* depth;bool refuse,laneTerm;int against;bool folds;};
            std::vector<Variant> v={{"r32f",s.depth32.p,false,false,-1,c.camera},{"lane_fold",s.lane.p,false,false,0,true},{"lane_copy",s.lane.p,true,false,0,c.camera},{"g32r32f_fold",s.lane2.p,false,false,0,true}};
            if(c.camera)v.push_back({"lane_term_fold",s.lane.p,false,true,-1,true});
            if(c.negative)v.push_back({"negative_control",s.laneBad.p,false,false,0,true});
            std::vector<std::unique_ptr<TemporalPass>> passes;for(const auto& variant:v){passes.push_back(std::make_unique<TemporalPass>());setup(d,*passes.back(),resolver,c,variant.refuse);}
            std::vector<std::array<unsigned,4>> diff(v.size(),std::array<unsigned,4>{});std::vector<unsigned> folded(v.size(),0),history(v.size(),0);std::vector<const char*> reason(v.size(),"");
            for(unsigned frame=0;frame<3;++frame){
                std::vector<Result> r;
                for(size_t i=0;i<v.size();++i){FrameInputs in=inputs(s,c);in.current_depth=v[i].depth;if(v[i].laneTerm){in.camera_lane_parallax[0]=1e-4f;in.camera_lane_parallax[3]=1;}
                    r.push_back(run(d,f,*passes[i],in,"fold run restores the hostile state"));check("fold run",r.back().hr);folded[i]+=r.back().folded;history[i]+=r.back().history;reason[i]=r.back().reason;}
                for(size_t i=0;i<v.size();++i)if(v[i].against>=0){const auto n=differ(r[i],r[size_t(v[i].against)]);for(unsigned k=0;k<4;++k)diff[i][k]+=n[k];}}
            bool ok=true;
            for(size_t i=0;i<v.size();++i){
                const bool negative=std::strcmp(v[i].name,"negative_control")==0;
                if(v[i].against>=0)std::printf("DEPTH_FOLD case=%s run=%s against=%s reason=%s against_reason=%s frames=3 folded=%u history_frames=%u color_bytes_differ=%u depth_bytes_differ=%u age_bytes_differ=%u mask_bytes_differ=%u\n",
                    c.name,v[i].name,v[size_t(v[i].against)].name,reason[i],reason[size_t(v[i].against)],folded[i],history[i],diff[i][0],diff[i][1],diff[i][2],diff[i][3]);
                ok=ok&&folded[i]==(v[i].folds?3u:0u)&&history[i]==2&&std::strcmp(reason[i],c.camera?"resolve_mrt":v[i].folds?"lane_mrt":v[i].refuse?"program":"r32f_depth")==0;
                if(v[i].against>=0&&!negative)ok=ok&&!diff[i][0]&&!diff[i][1]&&!diff[i][2]&&!diff[i][3];
                if(negative){++numeric_checks;require(diff[i][1]>0,"depth fold negative control: a one-ulp lane change reaches the depth history and fails the identity");}}
            ++numeric_checks;require(ok,"depth fold: the lane and G32R32F runs fold every frame, the refused and R32F runs do not, and every output is byte-identical to its twin");}
        // Faults, lane input, history warm: the screen-gate chain's fold (RT1 of its first mask draw) and the camera gate's
        // (RT2 of the resolve, the second draw after the box; the lane term on).
        for(unsigned fault=0;fault<4;++fault){
            const bool camera=fault>=2;const Case& c=camera?cases[0]:cases[1];
            TemporalPass pass;setup(d,pass,resolver,c,false);FrameInputs in=inputs(s,c);in.current_depth=s.lane.p;if(camera){in.camera_lane_parallax[0]=1e-4f;in.camera_lane_parallax[3]=1;}
            check("fold fault warm",run(d,f,pass,in,"fold fault warm run restores the hostile state").hr);
            Result failed;bool unbound=false,reached=false;const bool bind=fault%2==0;
            {std::unique_ptr<Fault> draw;if(!bind)draw=std::make_unique<Fault>(d,camera?2:1); // the folding draw: the screen chain's first mask draw, the camera gate's resolve
                TargetProbe probe(d,camera?nullptr:f.rt[1].p,bind,camera?2:1);failed=run(d,f,pass,in,bind?"fold target refusal restores the caller's targets":"fold draw failure restores the caller's targets");
                unbound=TargetProbe::unbound_after_bind();reached=bind?TargetProbe::refused:Fault::draws>=(camera?2u:1u);}
            const auto diagnostics=pass.diagnostics();
            const Result recovered=run(d,f,pass,in,"fold recovery restores the hostile state");
            static const char* const kinds[4]={"rt1_bind","fold_draw","rt2_bind","resolve_draw"};
            std::printf("DEPTH_FOLD_FAULT kind=%s reached=%u hr=%08lx operation=%08lx history_valid=%u folded=%u target_unbound_after_bind=%u recovered_hr=%08lx recovered_folded=%u recovered_history=%u\n",
                kinds[fault],unsigned(reached),(unsigned long)failed.hr,(unsigned long)diagnostics.operation,unsigned(diagnostics.history_valid),unsigned(failed.folded),unsigned(unbound),
                (unsigned long)recovered.hr,unsigned(recovered.folded),unsigned(recovered.history));
            ++numeric_checks;require(reached&&failed.hr==E_FAIL&&diagnostics.operation==E_FAIL&&!diagnostics.history_valid&&!failed.folded&&unbound&&recovered.hr==S_OK&&recovered.folded&&!recovered.history,
                "fold: a refused target bind or a failed folding draw fails the run, unbinds the target, restores the caller and folds again next run");}
    }
    // Reset with a lane input: both passes run two frames, the device is Reset (every default-pool object released first),
    // and after after_reset the lane pass folds again from an empty history, byte-identical to its R32F twin.
    {const Case& c=cases[0];TemporalPass pass[2];for(auto& p:pass)setup(d,p,resolver,c,false);
        auto frames=[&](const char* phase,unsigned& folded,unsigned& history,std::array<unsigned,4>& diff){Fixture f(d,compiler);Scene s(d);
            for(unsigned frame=0;frame<2;++frame){Result r[2];
                for(unsigned i=0;i<2;++i){FrameInputs in=inputs(s,c);in.current_depth=i?s.lane.p:s.depth32.p;r[i]=run(d,f,pass[i],in,phase);check(phase,r[i].hr);}
                folded+=r[1].folded;history+=r[1].history;const auto n=differ(r[1],r[0]);for(unsigned k=0;k<4;++k)diff[k]+=n[k];}};
        unsigned folded[2]{},history[2]{};std::array<unsigned,4> diff[2]{};
        frames("fold before Reset restores the hostile state",folded[0],history[0],diff[0]);
        for(auto& p:pass)p.before_reset();
        check("fold Reset",d->Reset(&pp));for(auto& p:pass)p.after_reset(S_OK);
        frames("fold after Reset restores the hostile state",folded[1],history[1],diff[1]);
        std::printf("DEPTH_FOLD_RESET before_folded=%u before_history=%u after_folded=%u after_history=%u bytes_differ=%u,%u,%u,%u\n",folded[0],history[0],folded[1],history[1],
                    diff[0][0]+diff[1][0],diff[0][1]+diff[1][1],diff[0][2]+diff[1][2],diff[0][3]+diff[1][3]);
        ++numeric_checks;require(folded[0]==2&&history[0]==1&&folded[1]==2&&history[1]==1&&!(diff[0][0]|diff[0][1]|diff[0][2]|diff[0][3]|diff[1][0]|diff[1][1]|diff[1][2]|diff[1][3]),
                                 "fold: a device Reset drops the history and the fold resumes byte-identical to the R32F twin");}
}
