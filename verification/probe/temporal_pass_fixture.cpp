// Calls the production temporal runtime and actual production shader bytecode.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/temporal_pass.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
template<class T> struct Com {
    T* p=nullptr;
    ~Com(){if(p)p->Release();}
    T* operator->()const{return p;}
    Com()=default; Com(const Com&)=delete; Com& operator=(const Com&)=delete;
};
struct Module { HMODULE h; explicit Module(const char* path):h(LoadLibraryA(path)){
    if(!h)throw std::runtime_error("LoadLibrary");
    char resolved[MAX_PATH]{};
    GetModuleFileNameA(h,resolved,MAX_PATH); std::printf("MODULE %s\n",resolved);
} ~Module(){FreeLibrary(h);} };
template<class T>T symbol(HMODULE m,const char* n){FARPROC p=GetProcAddress(m,n);T t=nullptr;
    static_assert(sizeof(t)==sizeof(p));std::memcpy(&t,&p,sizeof(t));if(!t)throw std::runtime_error(n);return t;}
void check(const char* label,HRESULT hr){if(FAILED(hr)){std::printf("API FAIL %s %08lx\n",label,hr);throw std::runtime_error(label);}}
void require(bool value,const char* label){std::printf("CHECK %s %s\n",label,value?"PASS":"FAIL");if(!value)throw std::runtime_error(label);}
float halfFloat(unsigned short h){unsigned e=(h>>10)&31;float sign=h&0x8000?-1.f:1.f;
    if(e==31)return (h&1023)?NAN:sign*INFINITY;
    return sign*(e?std::ldexp(float(1024+(h&1023)),int(e)-25):std::ldexp(float(h&1023),-24));}
unsigned short toHalf(float f){ // IEEE binary32 -> binary16, round to nearest/even.
    unsigned bits;std::memcpy(&bits,&f,4);
    const unsigned sign=(bits>>16)&0x8000, exponent=(bits>>23)&255;
    unsigned mantissa=bits&0x7fffff;
    if(exponent==255)return static_cast<unsigned short>(sign|0x7c00|(mantissa?0x200:0));
    int halfExponent=int(exponent)-127+15;
    if(halfExponent>=31)return static_cast<unsigned short>(sign|0x7c00);
    if(halfExponent<=0){
        if(halfExponent<-10)return static_cast<unsigned short>(sign);
        mantissa|=0x800000;
        const unsigned shift=unsigned(14-halfExponent), remainder=mantissa&((1u<<shift)-1);
        unsigned rounded=mantissa>>shift;
        const unsigned halfway=1u<<(shift-1);
        if(remainder>halfway || (remainder==halfway && (rounded&1)))++rounded;
        return static_cast<unsigned short>(sign|rounded);
    }
    const unsigned rounded=mantissa+0xfff+((mantissa>>13)&1);
    // Addition carries a rounded mantissa into the exponent; bitwise OR does not.
    return static_cast<unsigned short>(sign|((unsigned(halfExponent)<<10)+(rounded>>13)));
}
using Compiler=decltype(&D3DXCompileShader);
void compile(Compiler c,const std::string& source,const char* target,ID3DXBuffer** code){Com<ID3DXBuffer> errors;
    HRESULT hr=c(source.c_str(),UINT(source.size()),nullptr,nullptr,"main",target,D3DXSHADER_OPTIMIZATION_LEVEL3,code,&errors.p,nullptr);
    if(errors.p)std::printf("COMPILER %s\n",static_cast<char*>(errors->GetBufferPointer()));
    check(target,hr);}
using x3m::renderer::TemporalPass;
using x3m::renderer::FrameInputs;
using x3m::renderer::Output;
using x3m::renderer::MotionPolicy;
constexpr UINT W=16,H=16;
const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
unsigned numeric_checks=0,state_checks=0;
std::string file(const char* path){std::ifstream in(path);if(!in)throw std::runtime_error(path);return {std::istreambuf_iterator<char>(in),{}};}
struct Snapshot {
    std::vector<unsigned char> bytes;
    template<class T> void add(const T& value){auto* p=reinterpret_cast<const unsigned char*>(&value);bytes.insert(bytes.end(),p,p+sizeof value);}
    template<class T> void object(T* value){auto bits=reinterpret_cast<std::uintptr_t>(value);add(bits);if(value)value->Release();}
    explicit Snapshot(IDirect3DDevice9* d){
        D3DCAPS9 caps{};check("snapshot caps",d->GetDeviceCaps(&caps));
        for(UINT i=0;i<caps.NumSimultaneousRTs;++i){IDirect3DSurface9* v=nullptr;HRESULT hr=d->GetRenderTarget(i,&v);add(hr);object(v);}
        IDirect3DSurface9* ds=nullptr;HRESULT hr=d->GetDepthStencilSurface(&ds);add(hr);object(ds);
        D3DVIEWPORT9 vp{};check("snapshot viewport",d->GetViewport(&vp));add(vp);
        RECT sc{};check("snapshot scissor",d->GetScissorRect(&sc));add(sc);
        for(UINT i=0;i<caps.MaxStreams;++i){IDirect3DVertexBuffer9* vb=nullptr;UINT offset=0,stride=0,freq=0;check("snapshot stream",d->GetStreamSource(i,&vb,&offset,&stride));check("snapshot frequency",d->GetStreamSourceFreq(i,&freq));object(vb);add(offset);add(stride);add(freq);}
        IDirect3DIndexBuffer9* ib=nullptr;check("snapshot indices",d->GetIndices(&ib));object(ib);
        IDirect3DVertexDeclaration9* decl=nullptr;check("snapshot declaration",d->GetVertexDeclaration(&decl));object(decl);
        IDirect3DVertexShader9* vs=nullptr;check("snapshot VS",d->GetVertexShader(&vs));object(vs);
        IDirect3DPixelShader9* ps=nullptr;check("snapshot PS",d->GetPixelShader(&ps));object(ps);
        DWORD fvf=0;check("snapshot FVF",d->GetFVF(&fvf));add(fvf);
        for(UINT n=0;n<20;++n){UINT slot=n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16;IDirect3DBaseTexture9* t=nullptr;check("snapshot texture",d->GetTexture(slot,&t));object(t);
            for(UINT j=1;j<=13;++j){DWORD value=0;hr=d->GetSamplerState(slot,D3DSAMPLERSTATETYPE(j),&value);add(hr);add(value);}}
        const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,D3DRS_STENCILPASS,D3DRS_STENCILZFAIL,D3DRS_TWOSIDEDSTENCILMODE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_VERTEXBLEND,D3DRS_FILLMODE,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,D3DRS_MULTISAMPLEMASK,D3DRS_WRAP0,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_POINTSIZE};
        for(auto state:states){DWORD value=0;check("snapshot RS",d->GetRenderState(state,&value));add(value);}
        for(UINT stage=0;stage<8;++stage)for(UINT type=1;type<=32;++type){DWORD v=0;hr=d->GetTextureStageState(stage,D3DTEXTURESTAGESTATETYPE(type),&v);add(hr);add(v);}
        float constants[256*4]{};check("snapshot VS constants",d->GetVertexShaderConstantF(0,constants,std::min(caps.MaxVertexShaderConst,256ul)));add(constants);
        std::memset(constants,0,sizeof constants);check("snapshot PS constants",d->GetPixelShaderConstantF(0,constants,224));add(constants);
        int ints[64]{};BOOL bools[16]{};
        check("snapshot VS int",d->GetVertexShaderConstantI(0,ints,16));add(ints);check("snapshot PS int",d->GetPixelShaderConstantI(0,ints,16));add(ints);
        check("snapshot VS bool",d->GetVertexShaderConstantB(0,bools,16));add(bools);check("snapshot PS bool",d->GetPixelShaderConstantB(0,bools,16));add(bools);
        for(auto transform:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION,D3DTS_TEXTURE0}){D3DMATRIX m{};check("snapshot transform",d->GetTransform(transform,&m));add(m);}
        for(UINT i=0;i<caps.MaxUserClipPlanes;++i){float plane[4]{};check("snapshot clip plane",d->GetClipPlane(i,plane));add(plane);}
    }
    void equals(IDirect3DDevice9* d,const char* label){Snapshot after(d);++state_checks;require(bytes==after.bytes,label);}
};
struct Fault {
    using Draw=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
    using Texture=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
    static inline Draw original=nullptr;static inline Texture originalTexture=nullptr;
    static inline unsigned remaining=0,draws=0,restoreCalls=0;
    static inline bool failRestore=false,loseRestore=false;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,D3DPRIMITIVETYPE p,UINT n,const void* v,UINT s){++draws;if(remaining&&--remaining==0)return E_FAIL;return original(d,p,n,v,s);}
    static HRESULT WINAPI texture(IDirect3DDevice9* d,DWORD stage,IDirect3DBaseTexture9* value){
        if(draws>=2&&failRestore){++restoreCalls;if(restoreCalls==1)return E_FAIL;if(restoreCalls==2&&loseRestore)return D3DERR_DEVICELOST;}
        return originalTexture(d,stage,value);
    }
    Fault(IDirect3DDevice9* d,unsigned call,bool restoration=false,bool loss=false):previous(*reinterpret_cast<void***>(d)),device(d){
        std::copy(previous,previous+119,table);std::memcpy(&original,&table[83],sizeof original);std::memcpy(&originalTexture,&table[65],sizeof originalTexture);
        auto fn=&hook;std::memcpy(&table[83],&fn,sizeof fn);auto tf=&texture;std::memcpy(&table[65],&tf,sizeof tf);
        remaining=call;draws=restoreCalls=0;failRestore=restoration;loseRestore=loss;*reinterpret_cast<void***>(d)=table;
    }
    ~Fault(){*reinterpret_cast<void***>(device)=previous;}
};
struct Fixture {
    IDirect3DDevice9* d;
    Com<IDirect3DTexture9> color,depth,motion,sentinel;
    Com<IDirect3DSurface9> depthSurface,rt[2],back;
    Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
    Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DPixelShader9> ps;Com<IDirect3DVertexShader9> vs;
    Fixture(IDirect3DDevice9* device,Compiler compiler):d(device){
        check("color",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&color.p,nullptr));
        check("depth snapshot",d->CreateTexture(W,H,1,D3DUSAGE_DEPTHSTENCIL,D3DFMT_D24X8,D3DPOOL_DEFAULT,&depth.p,nullptr));check("depth surface",depth->GetSurfaceLevel(0,&depthSurface.p));
        check("motion",d->CreateTexture(W,H,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&motion.p,nullptr));
        check("sentinel",d->CreateTexture(W,H,2,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&sentinel.p,nullptr));
        for(auto& target:rt)check("sentinel RT",d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr));
        check("backbuffer",d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p));
        check("VB",d->CreateVertexBuffer(256,0,0,D3DPOOL_MANAGED,&vb.p,nullptr));check("IB",d->CreateIndexBuffer(32,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib.p,nullptr));
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        check("decl",d->CreateVertexDeclaration(elements,&declaration.p));
        Com<ID3DXBuffer> vc,pc;compile(compiler,"void main(float4 p:POSITION0,float2 u:TEXCOORD0,out float4 q:POSITION0,out float2 v:TEXCOORD0){q=p;v=u;}","vs_3_0",&vc.p);
        compile(compiler,"float4 main():COLOR0{return float4(0,1,0,1);}","ps_3_0",&pc.p);
        check("VS",d->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()),&vs.p));check("PS",d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()),&ps.p));
        clear(.5f);upload(.75f,.75f);uploadMotion(0);
    }
    ~Fixture(){for(UINT n=0;n<20;++n)d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr);d->SetRenderTarget(1,nullptr);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,back.p);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetVertexDeclaration(nullptr);d->SetStreamSource(0,nullptr,0,0);d->SetStreamSource(1,nullptr,0,0);d->SetIndices(nullptr);d->SetStreamSourceFreq(0,1);d->SetStreamSourceFreq(1,1);}
    void clear(float z){check("RT clear",d->SetRenderTarget(0,rt[0].p));check("depth clear",d->SetDepthStencilSurface(depthSurface.p));D3DVIEWPORT9 vp{0,0,W,H,0,1};check("vp clear",d->SetViewport(&vp));check("clear",d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,z,0));}
    void upload(float a,float b){D3DLOCKED_RECT lock{};check("lock color",color->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){unsigned short px[4];for(UINT c=0;c<3;++c)px[c]=toHalf((x+y)&1?b:a);px[3]=toHalf(1);std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,px,8);}check("unlock color",color->UnlockRect(0));}
    void uploadMotion(float valid){D3DLOCKED_RECT lock{};check("lock motion",motion->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){float v[4]={(x+.5f)/W,(y+.5f)/H,.5f,valid};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,v,16);}check("unlock motion",motion->UnlockRect(0));}
    void hostile(){
        check("hostile RT0",d->SetRenderTarget(0,rt[0].p));check("hostile RT1",d->SetRenderTarget(1,rt[1].p));check("hostile DS",d->SetDepthStencilSurface(depthSurface.p));
        D3DVIEWPORT9 vp{2,3,10,9,.2f,.8f};RECT rect{3,4,6,7};check("hostile VP",d->SetViewport(&vp));check("hostile scissor",d->SetScissorRect(&rect));
        check("hostile decl",d->SetVertexDeclaration(declaration.p));check("hostile VS",d->SetVertexShader(vs.p));check("hostile PS",d->SetPixelShader(ps.p));check("hostile VB0",d->SetStreamSource(0,vb.p,24,24));check("hostile VB1",d->SetStreamSource(1,vb.p,48,24));check("hostile IB",d->SetIndices(ib.p));
        check("hostile instance0",d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|3));check("hostile instance1",d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|2));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})check("hostile enable",d->SetRenderState(state,TRUE));
        for(auto p:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZFUNC,D3DCMP_NEVER},{D3DRS_ALPHAFUNC,D3DCMP_NEVER},{D3DRS_STENCILFUNC,D3DCMP_NEVER},{D3DRS_CLIPPLANEENABLE,1},{D3DRS_VERTEXBLEND,D3DVBF_1WEIGHTS},{D3DRS_CULLMODE,D3DCULL_CW},{D3DRS_FILLMODE,D3DFILL_WIREFRAME},{D3DRS_COLORWRITEENABLE,2},{D3DRS_MULTISAMPLEMASK,0},{D3DRS_WRAP0,D3DWRAP_U|D3DWRAP_V}})check("hostile RS",d->SetRenderState(p.first,p.second));
        float plane[4]={1,2,3,4};check("hostile plane",d->SetClipPlane(0,plane));
        float constants[32];for(UINT i=0;i<32;++i)constants[i]=i*.125f+3;check("hostile PS constants",d->SetPixelShaderConstantF(0,constants,8));check("hostile VS constants",d->SetVertexShaderConstantF(0,constants,8));
        for(UINT n=0;n<20;++n){UINT slot=n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16;check("hostile texture",d->SetTexture(slot,sentinel.p));for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},{D3DSAMP_MIPFILTER,D3DTEXF_LINEAR},{D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP},{D3DSAMP_ADDRESSV,D3DTADDRESS_MIRROR},{D3DSAMP_SRGBTEXTURE,TRUE},{D3DSAMP_MAXMIPLEVEL,1}})check("hostile sampler",d->SetSamplerState(slot,p.first,p.second));}
    }
    FrameInputs inputs(){FrameInputs in;in.color=color.p;in.depth_snapshot=depth.p;in.width=W;in.height=H;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);in.motion_policy=MotionPolicy::KnownCameraOnly;in.weight=.5f;in.history_allowed=true;in.caller_queries_idle=true;return in;}
    void sample(const Output& out,float color_value,float z,const char* label){
        for(UINT which=0;which<2;++which){IDirect3DTexture9* texture=which?out.depth:out.color;Com<IDirect3DSurface9> surface,readback;check("output surface",texture->GetSurfaceLevel(0,&surface.p));check("readback surface",d->CreateOffscreenPlainSurface(W,H,which?D3DFMT_R32F:D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));check("readback validation only",d->GetRenderTargetData(surface.p,readback.p));D3DLOCKED_RECT lock{};check("lock readback",readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));float actual=0;if(which)std::memcpy(&actual,static_cast<char*>(lock.pBits)+8*lock.Pitch+8*4,4);else {unsigned short h;std::memcpy(&h,static_cast<char*>(lock.pBits)+8*lock.Pitch+8*8,2);actual=halfFloat(h);}check("unlock readback",readback->UnlockRect());float expected=which?z:color_value,tolerance=which?2.f/16777215.f:.002f;bool okay=std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance;++numeric_checks;std::printf("SAMPLE %s plane=%s actual=%.9f expected=%.9f %s\n",label,which?"depth":"color",actual,expected,okay?"PASS":"FAIL");if(!okay)throw std::runtime_error(label);}
    }
    Output run(TemporalPass& pass,FrameInputs in,const char* label,bool open=true){hostile();Snapshot before(d);in.caller_scene_open=open;if(open)check("caller BeginScene",d->BeginScene());Output out;check(label,pass.run(in,&out));if(open)check("caller EndScene",d->EndScene());before.equals(d,label);require(out.color&&out.depth&&pass.diagnostics().history_valid,"atomic outputs valid");return out;}
};
void cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver,unsigned generation){
    std::printf("GENERATION %u\n",generation);Fixture f(d,compiler);TemporalPass pass;check("initialize production pass",pass.initialize(d,decoder,resolver));auto in=f.inputs();
    auto a=f.run(pass,in,"first frame");require(!a.used_history,"initial history invalid");f.sample(a,.75f,.5f,"first frame");
    f.upload(.25f,1);auto b=f.run(pass,in,"second frame owned scene",false);require(b.used_history&&b.color!=a.color&&b.depth!=a.depth,"ping pong color and depth");f.sample(b,.5f,.5f,"second frame blend");
    auto c=f.run(pass,in,"third frame");require(c.color==a.color&&c.depth==a.depth,"ping pong reuse");f.sample(c,.375f,.5f,"third frame blend");
    in.camera_cut=true;auto cut=f.run(pass,in,"camera cut");require(!cut.used_history,"cut rejects history");f.sample(cut,.25f,.5f,"camera cut");in.camera_cut=false;
    f.upload(.75f,.75f);f.run(pass,in,"warmup before failure");f.upload(.25f,1);
    f.hostile();Snapshot before(d);Output failed;check("failure BeginScene",d->BeginScene());{Fault fault(d,2);HRESULT hr=pass.run(in,&failed);require(hr==E_FAIL&&!failed.color&&!failed.depth&&!pass.diagnostics().history_valid,"second draw failure invalidates both histories");}check("failure EndScene",d->EndScene());before.equals(d,"failure state restoration");
    auto recovered=f.run(pass,in,"recover after failure");require(!recovered.used_history,"failure history not committed");f.sample(recovered,.25f,.5f,"failure recovery");
    f.clear(.75f);auto dis=f.run(pass,in,"depth disocclusion");f.sample(dis,.25f,.75f,"depth rejection");
    in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;f.uploadMotion(-1);auto motion=f.run(pass,in,"unknown per pixel motion");f.sample(motion,.25f,.75f,"unknown motion rejection");
    f.clear(.5f);in=f.inputs();in.camera_cut=true;f.upload(.25f,1);f.run(pass,in,"warmup checker for motion");in.camera_cut=false;
    // At even center, previous pixel one column right is 1.0. Matrix translation
    // +2/W maps the current center there; neighborhood permits the full range.
    in.clip_to_previous[3]=2.f/W;auto translated=f.run(pass,in,"matrix translation");f.sample(translated,.625f,.5f,"matrix routing");
    in=f.inputs();in.camera_cut=true;f.run(pass,in,"warmup checker for jitter");in.camera_cut=false;in.previous_jitter[0]=1;auto jitter=f.run(pass,in,"jitter translation");f.sample(jitter,.625f,.5f,"jitter routing");
    in=f.inputs();in.camera_cut=true;f.run(pass,in,"warmup checker for object motion");in.camera_cut=false;in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;f.uploadMotion(1);
    D3DLOCKED_RECT ml{};check("lock object correspondence",f.motion->LockRect(0,&ml,nullptr,0));float corr[4]={9.5f/W,8.5f/H,.5f,1};std::memcpy(static_cast<char*>(ml.pBits)+8*ml.Pitch+8*16,corr,16);check("unlock object correspondence",f.motion->UnlockRect(0));
    auto moved=f.run(pass,in,"object correspondence");f.sample(moved,.625f,.5f,"object routing");
    in.motion=nullptr;Output missing;require(pass.run(in,&missing)==E_INVALIDARG&&!pass.diagnostics().history_valid,"missing motion fails closed");
    in=f.inputs();in.caller_stateblock_recording=true;f.hostile();Snapshot recordBefore(d);check("BeginStateBlock caller",d->BeginStateBlock());require(pass.run(in,&missing)==E_INVALIDARG,"refuse caller state block recording");Com<IDirect3DStateBlock9> record;check("EndStateBlock caller",d->EndStateBlock(&record.p));recordBefore.equals(d,"recording refusal preserves state");
    in=f.inputs();in.caller_queries_idle=false;f.hostile();Snapshot queryBefore(d);require(pass.run(in,&missing)==E_INVALIDARG&&!pass.diagnostics().history_valid,"unknown or active queries fail closed");queryBefore.equals(d,"query refusal preserves state");
    in=f.inputs();in.motion_policy=static_cast<MotionPolicy>(99);require(pass.run(in,&missing)==E_INVALIDARG&&!pass.diagnostics().history_valid,"unknown policy enum fails closed");
    in=f.inputs();f.hostile();Snapshot restoreBefore(d);check("restore failure BeginScene",d->BeginScene());
    {Fault fault(d,0,true,false);HRESULT hr=pass.run(in,&missing);require(hr==E_FAIL&&pass.diagnostics().operation==S_OK&&pass.diagnostics().restoration==E_FAIL&&!missing.color&&!pass.diagnostics().history_valid,"restore failure prevents atomic commit");}
    check("restore failure EndScene",d->EndScene());restoreBefore.equals(d,"ordinary restore failure attempts remaining restoration");
    f.hostile();check("combined loss BeginScene",d->BeginScene());
    {Fault fault(d,0,true,true);HRESULT hr=pass.run(in,&missing);require(hr==D3DERR_DEVICELOST&&pass.diagnostics().restoration==D3DERR_DEVICELOST&&!missing.color&&!missing.depth&&!pass.diagnostics().history_valid&&Fault::restoreCalls==2,"restore loss overrides ordinary failure and stops setters");}
    // Injected loss did not lose the real backend; the test alone repairs caller
    // state. Production callers must use their normal Reset recovery protocol.
    check("combined loss EndScene",d->EndScene());f.hostile();
    in=f.inputs();in.epoch=2;auto epoch=f.run(pass,in,"new camera epoch");require(!epoch.used_history,"epoch invalidates history");f.sample(epoch,.25f,.5f,"epoch");
    in.color=epoch.color;f.hostile();Snapshot aliasBefore(d);require(pass.run(in,&missing)==E_INVALIDARG&&!missing.color&&!missing.depth&&!pass.diagnostics().history_valid,"prior output cannot alias current input");aliasBefore.equals(d,"history alias refusal preserves state");
    pass.invalidate();require(!pass.diagnostics().history_valid,"explicit invalidate");pass.before_reset();require(pass.run(in,&missing)==E_INVALIDARG,"before_reset shuts down runtime");
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3TemporalPassFixture";RegisterClassA(&cls);HWND window=CreateWindowA(cls.lpszClassName,"X3 temporal production module",WS_OVERLAPPEDWINDOW,90,90,128,128,nullptr,nullptr,cls.hInstance,nullptr);
    try{if(argc!=4||!window)throw std::runtime_error("usage: temporal_pass_fixture.exe <D3DX> <decoder> <resolve>");Module runtime("d3d9.dll"),d3dx(argv[1]);auto compiler=symbol<Compiler>(d3dx.h,"D3DXCompileShader");Com<ID3DXBuffer> dc,rc;compile(compiler,file(argv[2]),"ps_3_0",&dc.p);compile(compiler,file(argv[3]),"ps_3_0",&rc.p);auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime.h,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Create9");D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;Com<IDirect3DDevice9> d;check("CreateDevice",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&d.p));
        for(unsigned generation=0;generation<2;++generation){cases(d.p,compiler,static_cast<DWORD*>(dc->GetBufferPointer()),static_cast<DWORD*>(rc->GetBufferPointer()),generation);if(!generation){check("Reset",d->Reset(&pp));std::puts("RESET PASS");}}
        std::printf("RESULT PASS numerical=%u state_restorations=%u generations=2\n",numeric_checks,state_checks);result=0;
    }catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());}if(window)DestroyWindow(window);UnregisterClassA(cls.lpszClassName,cls.hInstance);return result;}
