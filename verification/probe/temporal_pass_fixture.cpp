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
unsigned short toHalfTruncate(float f){ // IEEE binary32 -> binary16, toward zero (no subnormal inputs expected).
    unsigned bits;std::memcpy(&bits,&f,4);const unsigned sign=(bits>>16)&0x8000;const int halfExponent=int((bits>>23)&255)-127+15;
    if(f==0||halfExponent<=0)return static_cast<unsigned short>(sign);
    return static_cast<unsigned short>(sign|(unsigned(halfExponent)<<10)|((bits&0x7fffff)>>13));}
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
        // Fixed-function texture-stage state of stage 0 reaches the resolve quad's TEXCOORD0 (pre-transformed vertices, no VS): a texture transform and a remapped coordinate index must neither change the output nor survive the run.
        D3DMATRIX tex{};tex.m[0][0]=.5f;tex.m[1][1]=.5f;tex.m[2][2]=1;tex.m[3][3]=1;tex.m[2][0]=.25f;tex.m[2][1]=.25f;check("hostile texture transform",d->SetTransform(D3DTS_TEXTURE0,&tex));
        check("hostile TSS transform flags",d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2));check("hostile TSS coordinate index",d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,1));
        float constants[32];for(UINT i=0;i<32;++i)constants[i]=i*.125f+3;check("hostile PS constants",d->SetPixelShaderConstantF(0,constants,8));check("hostile VS constants",d->SetVertexShaderConstantF(0,constants,8));
        for(UINT n=0;n<20;++n){UINT slot=n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16;check("hostile texture",d->SetTexture(slot,sentinel.p));for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},{D3DSAMP_MIPFILTER,D3DTEXF_LINEAR},{D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP},{D3DSAMP_ADDRESSV,D3DTADDRESS_MIRROR},{D3DSAMP_SRGBTEXTURE,TRUE},{D3DSAMP_MAXMIPLEVEL,1}})check("hostile sampler",d->SetSamplerState(slot,p.first,p.second));}
    }
    FrameInputs inputs(){FrameInputs in;in.color=color.p;in.depth_snapshot=depth.p;in.width=W;in.height=H;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);in.motion_policy=MotionPolicy::KnownCameraOnly;in.reactive_policy=x3m::renderer::ReactivePolicy::KnownNonReactive;in.weight=.5f;in.history_allowed=true;in.caller_queries_idle=true;return in;}
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
    // History (0.5 even / 0.75 odd) differs from the current checker (0.25 / 1)
    // so own-position (0.375), neighbor (0.5) and rejected (0.25) are distinct:
    // the previous jitter is packed but never applied (history is unjittered).
    f.upload(.5f,.75f);in=f.inputs();in.camera_cut=true;f.run(pass,in,"warmup for previous jitter");in.camera_cut=false;f.upload(.25f,1);in.previous_jitter[0]=1;auto jitter=f.run(pass,in,"previous jitter ignored");f.sample(jitter,.375f,.5f,"camera path ignores previous jitter");
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
    pass.invalidate();require(!pass.diagnostics().history_valid,"explicit invalidate");pass.before_reset();require(pass.run(in,&missing)==E_INVALIDARG&&pass.diagnostics().reset_pending,"before_reset refuses runs until after_reset");
}
using x3m::renderer::ReactivePolicy;
struct Particle {RECT rect;float rgb;float alpha=0;};
struct ReactiveScene {
    Fixture& f;IDirect3DDevice9* d;
    Com<IDirect3DTexture9> color,mask;Com<IDirect3DSurface9> colorSurface,maskSurface;
    Com<IDirect3DPixelShader9> textured,constant;
    ReactiveScene(Fixture& fixture,Compiler compiler):f(fixture),d(f.d){
        check("reactive color",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("reactive color surface",color->GetSurfaceLevel(0,&colorSurface.p));
        check("reactive mask",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&mask.p,nullptr));check("reactive mask surface",mask->GetSurfaceLevel(0,&maskSurface.p));
        Com<ID3DXBuffer> a,b;compile(compiler,"sampler2D source:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(source,uv);}","ps_3_0",&a.p);
        compile(compiler,"float4 color:register(c0);float4 main():COLOR0{return color;}","ps_3_0",&b.p);
        check("reactive base PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&textured.p));check("reactive particle PS",d->CreatePixelShader(static_cast<DWORD*>(b->GetBufferPointer()),&constant.p));
    }
    ~ReactiveScene(){for(UINT i=0;i<7;++i)d->SetTexture(i,nullptr);d->SetRenderTarget(0,f.rt[0].p);d->SetPixelShader(nullptr);}
    void quad(const RECT& r,float z){struct V{float x,y,z,rhw,u,v;};
        const V v[]={{float(r.left)-.5f,float(r.top)-.5f,z,1,float(r.left)/W,float(r.top)/H},
            {float(r.right)-.5f,float(r.top)-.5f,z,1,float(r.right)/W,float(r.top)/H},
            {float(r.left)-.5f,float(r.bottom)-.5f,z,1,float(r.left)/W,float(r.bottom)/H},
            {float(r.right)-.5f,float(r.bottom)-.5f,z,1,float(r.right)/W,float(r.bottom)/H}};
        check("original reactive raster",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));
    }
    void render(std::initializer_list<Particle> particles,float a=.25f,float b=1,float opaque_depth=.5f){
        f.upload(a,b);
        for(UINT n=0;n<20;++n)check("particle unbind",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
        check("particle MRT",d->SetRenderTarget(1,nullptr));check("particle DS",d->SetDepthStencilSurface(f.depthSurface.p));check("particle RT",d->SetRenderTarget(0,colorSurface.p));
        D3DVIEWPORT9 vp{0,0,W,H,0,1};check("particle VP",d->SetViewport(&vp));
        check("particle stream freq0",d->SetStreamSourceFreq(0,1));check("particle stream freq1",d->SetStreamSourceFreq(1,1));
        check("particle VS",d->SetVertexShader(nullptr));check("particle FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check("particle TSS index",d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));check("particle TSS transform",d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));check("particle IB",d->SetIndices(nullptr));
        for(auto state:{D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})check("particle state disable",d->SetRenderState(state,FALSE));
        for(auto p:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_VERTEXBLEND,D3DVBF_DISABLE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_WRAP0,0}})check("particle render state",d->SetRenderState(p.first,p.second));
        for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})check("particle sampler",d->SetSamplerState(0,p.first,p.second));
        check("particle clear",d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));check("particle Begin",d->BeginScene());
        check("base texture",d->SetTexture(0,f.color.p));check("base shader",d->SetPixelShader(textured.p));quad({0,0,W,H},opaque_depth);
        check("particle no depth writes",d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));check("particle constant shader",d->SetPixelShader(constant.p));
        check("actual source-color blend",d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCCOLOR));check("actual inverse-source-color blend",d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCCOLOR));check("actual additive blend op",d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));check("particle blend enabled",d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        for(auto p:particles){const float rgb[]={p.rgb,p.rgb,p.rgb,p.alpha};check("particle RGB not alpha",d->SetPixelShaderConstantF(0,rgb,1));quad(p.rect,.5f);}
        // Independent binary visible coverage: same submitted rectangles/depth,
        // depth test retained, no blending, and no inference from source alpha.
        check("mask target",d->SetRenderTarget(0,maskSurface.p));check("mask no blend",d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));check("mask clear",d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        const float marked[]={1,0,0,0};check("mask coverage value",d->SetPixelShaderConstantF(0,marked,1));
        for(auto p:particles)quad(p.rect,.5f);
        check("particle End",d->EndScene());
    }
    FrameInputs inputs(){auto in=f.inputs();in.color=color.p;in.reactive=mask.p;in.reactive_policy=ReactivePolicy::RequiredMask;return in;}
    Output run(TemporalPass& pass,FrameInputs in,const char* label,bool valid=true){
        f.hostile();Snapshot before(d);check("reactive caller Begin",d->BeginScene());Output out;check(label,pass.run(in,&out));check("reactive caller End",d->EndScene());before.equals(d,label);
        require(out.color&&out.depth&&pass.diagnostics().history_valid==valid,"reactive atomic output validity");
        require(bool(out.reactive)==(in.reactive_policy==ReactivePolicy::RequiredMask),"reactive snapshot policy");return out;
    }
};
void reactive_sample(IDirect3DDevice9* d,IDirect3DTexture9* texture,UINT x,UINT y,float expected,const char* label,UINT component=0){
    require(texture!=nullptr,"sample texture exists");D3DSURFACE_DESC desc{};check("reactive sample desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> source,read;check("reactive sample source",texture->GetSurfaceLevel(0,&source.p));check("reactive sample readback",d->CreateOffscreenPlainSurface(W,H,desc.Format,D3DPOOL_SYSTEMMEM,&read.p,nullptr));check("reactive test-only readback",d->GetRenderTargetData(source.p,read.p));D3DLOCKED_RECT lock{};check("reactive read lock",read->LockRect(&lock,nullptr,D3DLOCK_READONLY));float actual;
    if(desc.Format==D3DFMT_R32F)std::memcpy(&actual,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,4);
    else{unsigned short h;std::memcpy(&h,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8+component*2,2);actual=halfFloat(h);}
    check("reactive read unlock",read->UnlockRect());bool okay=std::isfinite(actual)&&std::fabs(actual-expected)<=.002f;++numeric_checks;
    std::printf("SAMPLE %s actual=%.9f expected=%.9f %s\n",label,actual,expected,okay?"PASS":"FAIL");if(!okay)throw std::runtime_error(label);
}
void reactive_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("REACTIVE_CASES");Fixture f(d,compiler);ReactiveScene scene(f,compiler);TemporalPass pass;check("reactive initialize",pass.initialize(d,decoder,resolver));
    const Particle a{{7,7,10,10},.5f,0},b{{7,7,10,10},.75f,0},right{{10,7,13,10},.5f,0},tap{{9,7,10,10},.5f,0};
    auto in=scene.inputs();scene.render({},.75f,.75f);scene.run(pass,in,"reactive warmup");
    scene.render({a});reactive_sample(d,scene.color.p,8,8,.375f,"actual SRC_COLOR particle RGB with zero source alpha");reactive_sample(d,scene.color.p,8,8,1,"particle preserves destination alpha",3);
    auto born=scene.run(pass,in,"particle born current coverage");reactive_sample(d,born.color,8,8,.375f,"birth rejects old unmarked color");reactive_sample(d,born.reactive,8,8,1,"current mask copied to owned history");
    scene.render({});reactive_sample(d,born.reactive,8,8,1,"owned mask survives caller target reuse");auto gone=scene.run(pass,in,"particle disappears previous coverage");reactive_sample(d,gone.color,8,8,.25f,"disappearance rejects stale RGB");
    in.camera_cut=true;scene.render({a});scene.run(pass,in,"movement prior coverage");in.camera_cut=false;scene.render({right});auto moved=scene.run(pass,in,"particle moves");reactive_sample(d,moved.color,8,8,.25f,"movement old position rejects history");reactive_sample(d,moved.color,11,8,.75f,"movement new position rejects history");
    in.camera_cut=true;scene.render({a,b});auto ab=scene.run(pass,in,"particle order AB");reactive_sample(d,ab.color,8,8,.65625f,"noncommutative RGB blend AB");in.camera_cut=false;scene.render({b,a});auto ba=scene.run(pass,in,"particle order BA");reactive_sample(d,ba.color,8,8,.5625f,"reordering uses actual current RGB");
    in.camera_cut=true;scene.render({},.75f,.75f,.25f);scene.run(pass,in,"opaque occlusion warmup");in.camera_cut=false;scene.render({a},.25f,1,.25f);reactive_sample(d,scene.mask.p,8,8,0,"opaque depth occludes reactive contributor");auto opaque=scene.run(pass,in,"opaque occlusion retains stable accumulation");reactive_sample(d,opaque.color,8,8,.5f,"occluded particle does not invalidate opaque history");
    in.camera_cut=true;scene.render({},4,8);scene.run(pass,in,"HDR prior");in.camera_cut=false;scene.render({a},4,8);auto hdr=scene.run(pass,in,"HDR reactive color");reactive_sample(d,hdr.color,8,8,2.25f,"HDR exceeds one with reactive rejection");
    in.camera_cut=true;scene.render({tap},.75f,.75f);scene.run(pass,in,"history lookup prior mask");in.camera_cut=false;scene.render({});f.uploadMotion(1);in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;
    auto correspondence=[&](float u){D3DLOCKED_RECT lock{};check("reactive motion lock",f.motion->LockRect(0,&lock,nullptr,0));const float value[]={u,8.5f/H,.5f,1};std::memcpy(static_cast<char*>(lock.pBits)+8*lock.Pitch+8*16,value,16);check("reactive motion unlock",f.motion->UnlockRect(0));};
    correspondence(9.5f/W);auto object=scene.run(pass,in,"object motion looks up previous reactive coverage");reactive_sample(d,object.color,8,8,.25f,"previous mask follows object correspondence");
    in=scene.inputs();in.camera_cut=true;scene.render({tap},.75f,.75f);scene.run(pass,in,"footprint mask warmup");in.camera_cut=false;scene.render({});in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;correspondence(9.f/W);
    auto footprint=scene.run(pass,in,"positive-weight reactive history tap");reactive_sample(d,footprint.color,8,8,.25f,"whole bilinear footprint rejects contaminated tap");
    in=scene.inputs();in.camera_cut=true;scene.render({tap},.75f,.75f);scene.run(pass,in,"zero-weight mask warmup");in.camera_cut=false;scene.render({});in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;correspondence(8.5f/W);
    auto zero=scene.run(pass,in,"zero-weight masked neighbor");reactive_sample(d,zero.color,8,8,.5f,"zero-weight reactive tap does not reject");
    // Mask values are canonicalized independently of application alpha or sign.
    Com<IDirect3DTexture9> values;check("mask values",d->CreateTexture(W,H,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&values.p,nullptr));
    IDirect3DTexture9* lastSnapshot=nullptr;
    for(float value:{-1.f,NAN,INFINITY,.25f}){D3DLOCKED_RECT lock{};check("mask value lock",values->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&value,4);check("mask value unlock",values->UnlockRect(0));in=scene.inputs();in.reactive=values.p;auto invalid=scene.run(pass,in,"nonzero or invalid mask values conservative");lastSnapshot=invalid.reactive;reactive_sample(d,invalid.color,8,8,.25f,"invalid mask uses current RGB");reactive_sample(d,invalid.reactive,8,8,1,"invalid mask canonicalized to one");}
    values.p->Release();values.p=nullptr;reactive_sample(d,lastSnapshot,8,8,1,"owned snapshot survives caller mask release");
    in=scene.inputs();f.hostile();Snapshot missingBefore(d);Output failed;in.reactive=nullptr;require(pass.run(in,&failed)==E_INVALIDARG&&!failed.color&&!failed.depth&&!failed.reactive&&!pass.diagnostics().history_valid,"required missing mask fails closed");missingBefore.equals(d,"missing mask preserves state");
    in=scene.inputs();in.reactive=f.color.p;require(pass.run(in,&failed)==E_INVALIDARG&&!failed.reactive,"wrong mask format refused");
    Com<IDirect3DTexture9> wrongSize;check("wrong mask dimensions",d->CreateTexture(W+1,H,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&wrongSize.p,nullptr));in.reactive=wrongSize.p;require(pass.run(in,&failed)==E_INVALIDARG&&!failed.reactive,"wrong mask dimensions refused");
    in=scene.inputs();in.reactive_policy=static_cast<ReactivePolicy>(99);require(pass.run(in,&failed)==E_INVALIDARG&&!failed.reactive,"unknown reactive policy refused");
    in=scene.inputs();scene.render({});auto recovered=scene.run(pass,in,"missing mask recovery");require(!recovered.used_history,"missing mask invalidated history");reactive_sample(d,recovered.color,8,8,.25f,"missing mask recovery current only");
    in.reactive=recovered.reactive;require(pass.run(in,&failed)==E_INVALIDARG&&!failed.reactive,"owned mask cannot alias current input");
    in=scene.inputs();f.hostile();Snapshot snapshotBefore(d);check("snapshot fault Begin",d->BeginScene());{Fault fault(d,3);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!failed.depth&&!failed.reactive&&!pass.diagnostics().history_valid,"third mask-copy draw failure rejects entire history set");}check("snapshot fault End",d->EndScene());snapshotBefore.equals(d,"third draw failure restores state");
    auto afterCopyFailure=scene.run(pass,in,"mask-copy failure recovery");require(!afterCopyFailure.used_history,"mask-copy failure invalidates old history");
    f.hostile();Snapshot restorationBefore(d);check("reactive restore fault Begin",d->BeginScene());{Fault fault(d,0,true,false);require(pass.run(in,&failed)==E_FAIL&&pass.diagnostics().operation==S_OK&&!failed.color&&!failed.depth&&!failed.reactive&&!pass.diagnostics().history_valid,"restore failure rejects color depth and mask");}check("reactive restore fault End",d->EndScene());restorationBefore.equals(d,"reactive restoration failure remaining state");
    in=scene.inputs();in.reactive_policy=ReactivePolicy::Unavailable;in.reactive=nullptr;scene.render({},.75f,.75f);auto unavailable=scene.run(pass,in,"unavailable coverage current only",false);require(!unavailable.used_history,"unavailable cannot consume history");scene.render({});auto unavailableAgain=scene.run(pass,in,"unavailable repeated current only",false);reactive_sample(d,unavailableAgain.color,8,8,.25f,"unknown coverage cannot accumulate");
    in=scene.inputs();auto restored=scene.run(pass,in,"required policy restored");require(!restored.used_history,"Required Unavailable Required invalidates history");
    in.reactive_policy=ReactivePolicy::KnownNonReactive;in.reactive=nullptr;auto known=scene.run(pass,in,"explicit known nonreactive transition");require(!known.used_history&&!known.reactive,"Required to Known changes history policy");
    in=scene.inputs();auto required=scene.run(pass,in,"required after known");require(!required.used_history&&required.reactive,"Known to Required demands owned mask history");
    pass.before_reset();require(pass.run(in,&failed)==E_INVALIDARG&&!failed.reactive,"reactive reset releases runtime");
}
// Step-2 route inputs: 8-bit main surface copied to FP16 scratch, direct R32F
// depth with the -1 sentinel, cut flag, motion-path jitter and Reset continuity.
std::vector<unsigned char> readback(IDirect3DDevice9* d,IDirect3DSurface9* source,D3DFORMAT format,UINT pixel){Com<IDirect3DSurface9> read;check("route readback surface",d->CreateOffscreenPlainSurface(W,H,format,D3DPOOL_SYSTEMMEM,&read.p,nullptr));check("route validation-only readback",d->GetRenderTargetData(source,read.p));D3DLOCKED_RECT lock{};check("route readback lock",read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> bytes(W*H*pixel);for(UINT y=0;y<H;++y)std::memcpy(&bytes[y*W*pixel],static_cast<char*>(lock.pBits)+y*lock.Pitch,W*pixel);check("route readback unlock",read->UnlockRect());return bytes;}
std::vector<unsigned char> readback(IDirect3DDevice9* d,IDirect3DTexture9* texture){D3DSURFACE_DESC desc{};check("route level desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level;check("route level",texture->GetSurfaceLevel(0,&level.p));return readback(d,level.p,desc.Format,desc.Format==D3DFMT_R32F?4:desc.Format==D3DFMT_A8R8G8B8?4:8);}
template<class T> T at(const std::vector<unsigned char>& bytes,size_t index){T value;std::memcpy(&value,&bytes[index*sizeof(T)],sizeof value);return value;}
struct RouteScene {
    Fixture& f;IDirect3DDevice9* d;
    Com<IDirect3DSurface9> main8;Com<IDirect3DTexture9> bytes8,exact16,depth32;Com<IDirect3DPixelShader9> textured;
    std::vector<unsigned char> reference; // bytes last drawn into main8, B G R A per pixel
    RouteScene(Fixture& fixture,Compiler compiler):f(fixture),d(f.d){
        // The main target is a plain render-target surface, deliberately not a texture level.
        check("main 8-bit target",d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&main8.p,nullptr));
        check("8-bit source",d->CreateTexture(W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&bytes8.p,nullptr));
        check("exact FP16 twin",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&exact16.p,nullptr));
        check("R32F current depth",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth32.p,nullptr));
        Com<ID3DXBuffer> a;compile(compiler,"sampler2D source:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(source,uv);}","ps_3_0",&a.p);
        check("route textured PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&textured.p));
    }
    ~RouteScene(){for(UINT i=0;i<7;++i)d->SetTexture(i,nullptr);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,f.rt[0].p);d->SetPixelShader(nullptr);}
    void plain(IDirect3DSurface9* rt,IDirect3DSurface9* ds,bool depth){
        for(UINT n=0;n<20;++n)check("route unbind",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
        check("route MRT",d->SetRenderTarget(1,nullptr));check("route DS",d->SetDepthStencilSurface(ds));check("route RT",d->SetRenderTarget(0,rt));
        D3DVIEWPORT9 vp{0,0,W,H,0,1};check("route VP",d->SetViewport(&vp));
        check("route freq0",d->SetStreamSourceFreq(0,1));check("route freq1",d->SetStreamSourceFreq(1,1));
        check("route VS",d->SetVertexShader(nullptr));check("route FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check("route TSS index",d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));check("route TSS transform",d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));check("route IB",d->SetIndices(nullptr));
        for(auto state:{D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})check("route disable",d->SetRenderState(state,FALSE));
        for(auto p:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZENABLE,depth?TRUE:FALSE},{D3DRS_ZWRITEENABLE,depth?TRUE:FALSE},{D3DRS_ZFUNC,D3DCMP_ALWAYS},{D3DRS_VERTEXBLEND,D3DVBF_DISABLE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_WRAP0,0}})check("route render state",d->SetRenderState(p.first,p.second));
        for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})check("route sampler",d->SetSamplerState(0,p.first,p.second));
        check("route PS",d->SetPixelShader(textured.p));
    }
    void quad(const RECT& r,float z){struct V{float x,y,z,rhw,u,v;};
        const V v[]={{float(r.left)-.5f,float(r.top)-.5f,z,1,float(r.left)/W,float(r.top)/H},{float(r.right)-.5f,float(r.top)-.5f,z,1,float(r.right)/W,float(r.top)/H},
            {float(r.left)-.5f,float(r.bottom)-.5f,z,1,float(r.left)/W,float(r.bottom)/H},{float(r.right)-.5f,float(r.bottom)-.5f,z,1,float(r.right)/W,float(r.bottom)/H}};
        check("route raster",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));}
    // Every 8-bit code appears in R; G and B differ per pixel, so a gamma curve
    // or channel swap would be caught.
    void fill(unsigned seed){reference.assign(W*H*4,0);D3DLOCKED_RECT lock{};check("lock 8-bit",bytes8->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){unsigned char* p=&reference[(y*W+x)*4];const unsigned r=(y*W+x+seed)&255;p[2]=static_cast<unsigned char>(r);p[1]=static_cast<unsigned char>(255-r);p[0]=static_cast<unsigned char>((x*y*7+seed*3)&255);p[3]=255;
            std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,p,4);}
        check("unlock 8-bit",bytes8->UnlockRect(0));
        plain(main8.p,nullptr,false);check("fill Begin",d->BeginScene());check("fill texture",d->SetTexture(0,bytes8.p));quad({0,0,W,H},.5f);check("fill End",d->EndScene());
        require(readback(d,main8.p,D3DFMT_A8R8G8B8,4)==reference,"8-bit main target holds the exact bytes");}
    // The FP16 twin holds FP16(v/255) of the same bytes under the backend's
    // detected conversion rule (round to nearest even or truncation toward zero).
    void twin(bool truncate){D3DLOCKED_RECT half{};check("lock twin",exact16->LockRect(0,&half,nullptr,0));
        for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const unsigned char* p=&reference[(y*W+x)*4];unsigned short px[4];for(UINT c=0;c<4;++c){const float v=p[c<3?2-c:3]/255.f;px[c]=truncate?toHalfTruncate(v):toHalf(v);}std::memcpy(static_cast<char*>(half.pBits)+y*half.Pitch+x*8,px,8);}
        check("unlock twin",exact16->UnlockRect(0));}
    // Same synthetic depth into the native D24X8 snapshot (rasterized) and the R32F texture (CPU model).
    void depths(std::initializer_list<std::pair<RECT,float>> rects,float clear){
        f.clear(clear);plain(f.rt[0].p,f.depthSurface.p,true);check("depth Begin",d->BeginScene());for(auto& r:rects)quad(r.first,r.second);check("depth End",d->EndScene());
        depth([&](UINT x,UINT y){float z=clear;for(auto& r:rects)if(int(x)>=r.first.left&&int(x)<r.first.right&&int(y)>=r.first.top&&int(y)<r.first.bottom)z=r.second;return z;});}
    template<class F> void depth(F value){Com<IDirect3DTexture9> staging;check("depth staging",d->CreateTexture(W,H,1,0,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging.p,nullptr));D3DLOCKED_RECT lock{};check("lock staging",staging->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const float z=value(x,y);std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&z,4);}check("unlock staging",staging->UnlockRect(0));check("depth upload",d->UpdateTexture(staging.p,depth32.p));}
    FrameInputs inputs(){auto in=f.inputs();in.color=nullptr;in.color_surface=main8.p;in.depth_snapshot=nullptr;in.current_depth=depth32.p;return in;}
    Output run(TemporalPass& pass,FrameInputs in,const char* label){f.hostile();Snapshot before(d);check("route caller Begin",d->BeginScene());Output out;check(label,pass.run(in,&out));check("route caller End",d->EndScene());before.equals(d,label);require(out.color&&out.depth&&out.color_surface&&pass.diagnostics().history_valid,"route atomic outputs valid");return out;}
};
void route_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("ROUTE_CASES");Fixture f(d,compiler);RouteScene s(f,compiler);Output failed;
    // 8-bit copy path against the direct FP16 path: bit-exact on the first frame
    // and through accumulation; the copy is linear (no sRGB) up to FP16 rounding.
    TemporalPass p8,p16;check("initialize 8-bit path",p8.initialize(d,decoder,resolver));check("initialize FP16 path",p16.initialize(d,decoder,resolver));
    s.depth([](UINT,UINT){return .5f;});s.fill(0);
    auto in8=s.inputs();auto in16=s.inputs();in16.color=s.exact16.p;in16.color_surface=nullptr;
    auto a8=s.run(p8,in8,"8-bit copy first frame");
    {Com<IDirect3DSurface9> level;check("resolved level",a8.color->GetSurfaceLevel(0,&level.p));require(level.p==a8.color_surface,"resolved surface is level 0 of resolved color");}
    // The backend's UNORM8-to-FP16 conversion may round to nearest even or
    // truncate toward zero; both stay within one FP16 ulp of v/255 and neither
    // is a gamma curve (code 63 would become 0.0497 instead of 0.247 under sRGB
    // decoding). One rule must explain every sample.
    auto h8=readback(d,a8.color);unsigned nearest=0,truncated=0,alpha=0;float worst=0;
    for(UINT i=0;i<W*H;++i){const unsigned char* p=&s.reference[i*4];for(UINT c=0;c<3;++c){const unsigned short actual=at<unsigned short>(h8,i*4+c);const float v=p[2-c]/255.f;
            nearest+=actual==toHalf(v);truncated+=actual==toHalfTruncate(v);worst=std::max(worst,std::fabs(halfFloat(actual)-v));}alpha+=at<unsigned short>(h8,i*4+3)==toHalf(1);}
    const bool truncate=truncated==W*H*3;
    std::printf("COPY rule=%s nearest=%u truncated=%u of %u max_abs_error=%.9f alpha_one=%u of %u\n",truncate?"truncate":"nearest",nearest,truncated,W*H*3,worst,alpha,W*H);
    ++numeric_checks;require((truncate||nearest==W*H*3)&&worst<1.f/2048&&alpha==W*H,"8-bit copy is v/255 within one FP16 ulp for all 256 codes, no gamma");
    s.twin(truncate);auto a16=s.run(p16,in16,"FP16 direct first frame");
    ++numeric_checks;require(h8==readback(d,a16.color)&&readback(d,a8.depth)==readback(d,a16.depth),"8-bit copy path equals direct FP16 path bit-exactly");
    check("copy back",d->StretchRect(a8.color_surface,nullptr,s.main8.p,nullptr,D3DTEXF_POINT));++numeric_checks;require(readback(d,s.main8.p,D3DFMT_A8R8G8B8,4)==s.reference,"copy-back round trip restores the exact 8-bit bytes");
    s.fill(97);s.twin(truncate);auto b8=s.run(p8,in8,"8-bit copy accumulation");auto b16=s.run(p16,in16,"FP16 direct accumulation");require(b8.used_history&&b16.used_history,"both paths accumulate");
    ++numeric_checks;require(readback(d,b8.color)==readback(d,b16.color),"accumulated 8-bit copy path equals direct FP16 path bit-exactly");
    // Input validation: exactly one color and one depth input.
    auto in=s.inputs();in.color=s.exact16.p;require(p8.run(in,&failed)==E_INVALIDARG&&!failed.color,"both color inputs refused");
    in=s.inputs();in.color_surface=nullptr;require(p8.run(in,&failed)==E_INVALIDARG,"no color input refused");
    in=s.inputs();in.depth_snapshot=f.depth.p;require(p8.run(in,&failed)==E_INVALIDARG,"both depth inputs refused");
    in=s.inputs();in.current_depth=nullptr;require(p8.run(in,&failed)==E_INVALIDARG,"no depth input refused");
    in=s.inputs();in.color_surface=a8.color_surface;require(p8.run(in,&failed)==E_INVALIDARG,"resolved surface cannot alias the color input");
    in=s.inputs();in.current_depth=a8.depth;require(p8.run(in,&failed)==E_INVALIDARG,"owned depth history cannot alias current depth");
    {Com<IDirect3DTexture9> wrong;check("wrong depth format",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&wrong.p,nullptr));in=s.inputs();in.current_depth=wrong.p;require(p8.run(in,&failed)==E_INVALIDARG,"wrong current depth format refused");}
    // Direct R32F depth equals the decoded D24X8 snapshot on one synthetic scene.
    TemporalPass pd,pr;check("initialize decoded path",pd.initialize(d,decoder,resolver));check("initialize R32F path",pr.initialize(d,decoder,resolver));
    s.depths({{{4,4,12,12},.25f}},.5f);f.upload(.75f,.75f);auto ind=f.inputs();auto inr=f.inputs();inr.depth_snapshot=nullptr;inr.current_depth=s.depth32.p;
    s.run(pd,ind,"decoded first frame");s.run(pr,inr,"R32F first frame");
    s.depths({{{4,4,8,12},.25f}},.5f);f.upload(.25f,1);auto od=s.run(pd,ind,"decoded accumulation");auto orf=s.run(pr,inr,"R32F accumulation");
    reactive_sample(d,orf.color,2,8,.5f,"R32F outside quad accumulates");reactive_sample(d,orf.color,6,8,.5f,"R32F inside stable quad accumulates");reactive_sample(d,orf.color,10,8,.25f,"R32F depth change rejects history");
    reactive_sample(d,orf.depth,6,8,.25f,"R32F depth history holds the copied quad depth");
    ++numeric_checks;require(readback(d,od.color)==readback(d,orf.color),"R32F path color equals decoded D24X8 path bit-exactly");
    {auto dd=readback(d,od.depth),dr=readback(d,orf.depth);float worst=0;for(UINT i=0;i<W*H;++i)worst=std::max(worst,std::fabs(at<float>(dd,i)-at<float>(dr,i)));std::printf("DEPTH decoded_vs_r32f_max_error=%.10f\n",worst);++numeric_checks;require(worst<=2.f/16777215.f,"decoded depth within two D24 steps of R32F depth");}
    // Reactive derived from the depth sentinel: no mask texture, no third draw.
    using x3m::renderer::ReactivePolicy;TemporalPass ps;check("initialize sentinel path",ps.initialize(d,decoder,resolver));
    s.depth([](UINT x,UINT){return x<8?-1.f:.5f;});f.upload(.75f,.75f);in=f.inputs();in.depth_snapshot=nullptr;in.current_depth=s.depth32.p;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;
    auto s1=s.run(ps,in,"sentinel first frame");require(!s1.used_history&&!s1.reactive,"sentinel policy establishes history without a mask");
    f.upload(.25f,1);auto s2=s.run(ps,in,"sentinel accumulation");require(s2.used_history,"sentinel policy accumulates");
    reactive_sample(d,s2.color,4,8,.25f,"sentinel current pixel resolves current-only");reactive_sample(d,s2.depth,4,8,-1.f,"sentinel copied into depth history");
    reactive_sample(d,s2.color,10,8,.5f,"opaque pixel accumulates beside sentinel");
    f.uploadMotion(0);{D3DLOCKED_RECT ml{};check("lock sentinel correspondence",f.motion->LockRect(0,&ml,nullptr,0));const float corr[4]={4.5f/W,8.5f/H,.5f,1};std::memcpy(static_cast<char*>(ml.pBits)+8*ml.Pitch+8*16,corr,16);check("unlock sentinel correspondence",f.motion->UnlockRect(0));}
    in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;auto s3=s.run(ps,in,"sentinel history tap");reactive_sample(d,s3.color,8,8,.25f,"previous sentinel tap contributes nothing");reactive_sample(d,s3.color,10,8,.375f,"opaque correspondence keeps accumulating");
    in=f.inputs();in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;require(ps.run(in,&failed)==E_INVALIDARG&&!failed.color,"sentinel policy requires direct R32F depth");
    in=f.inputs();in.depth_snapshot=nullptr;in.current_depth=s.depth32.p;auto known=s.run(ps,in,"sentinel to known transition");require(!known.used_history,"policy transition invalidates history");
    in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;auto back=s.run(ps,in,"known to sentinel transition");require(!back.used_history,"transition back invalidates history");
    // Route cut verdict rejects history for the frame, then accumulation resumes.
    f.upload(.75f,.75f);s.run(ps,in,"before cut");f.upload(.25f,1);in.cut=true;auto cut=s.run(ps,in,"route cut");require(!cut.used_history,"cut rejects history");reactive_sample(d,cut.color,10,8,.25f,"cut frame is current-only");
    in.cut=false;auto resumed=s.run(ps,in,"after cut");require(resumed.used_history,"accumulation resumes after cut");
    // Jitter on the resolve's two paths: history (unjittered grid) is read at
    // the content's previous unjittered UV (the producer's RG, or the camera
    // reprojection of the jitter-corrected position) plus the CURRENT jitter,
    // i.e. at "pixel center minus velocity"; the previous jitter is never
    // applied. History (0.5 even / 0.75 odd) differs from the current checker
    // (0.25 / 1) at every pixel, so own-position (0.375), neighbor (0.5) and
    // rejected (0.25) lookups are distinct.
    TemporalPass pj;check("initialize jitter path",pj.initialize(d,decoder,resolver));f.clear(.5f);f.uploadMotion(1);
    auto checker=[&](){in=f.inputs();in.camera_cut=true;f.upload(.5f,.75f);f.run(pj,in,"jitter warmup");in.camera_cut=false;f.upload(.25f,1);in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;};
    auto correspondence=[&](float u){D3DLOCKED_RECT ml{};check("lock jitter correspondence",f.motion->LockRect(0,&ml,nullptr,0));const float corr[4]={u,8.5f/H,.5f,1};std::memcpy(static_cast<char*>(ml.pBits)+8*ml.Pitch+8*16,corr,16);check("unlock jitter correspondence",f.motion->UnlockRect(0));};
    checker();correspondence(8.5f/W);in.previous_jitter[0]=1;auto ignored=f.run(pj,in,"motion previous jitter");f.sample(ignored,.375f,.5f,"motion path ignores previous jitter");
    checker();correspondence(8.5f/W);in.current_jitter[0]=1;auto current=f.run(pj,in,"motion current jitter");f.sample(current,.5f,.5f,"motion path adds the current jitter to the producer UV");
    checker();correspondence(7.5f/W);in.current_jitter[0]=1;in.previous_jitter[0]=-1;auto stationary=f.run(pj,in,"stationary content under jitter");f.sample(stationary,.375f,.5f,"static content (RG = own center minus current jitter) lands on its own texel center");
    checker();correspondence(9.5f/W);in.previous_jitter[0]=1;auto verbatim=f.run(pj,in,"unjittered producer UV");f.sample(verbatim,.5f,.5f,"producer RG selects the neighbor, previous jitter not added");
    checker();in.motion_policy=MotionPolicy::KnownCameraOnly;in.motion=nullptr;in.current_jitter[0]=1;auto camera=f.run(pj,in,"camera current jitter");f.sample(camera,.375f,.5f,"camera path restores the current jitter after reprojection (static camera lands on its own texel center)");
    checker();in.motion_policy=MotionPolicy::KnownCameraOnly;in.motion=nullptr;in.previous_jitter[0]=1;auto camera_ignored=f.run(pj,in,"camera previous jitter");f.sample(camera_ignored,.375f,.5f,"camera path ignores previous jitter");
}
// Stationary stability: a static synthetic scene (flat backdrop, a bright
// rectangle with fractional edges, an integer-aligned bright square and a
// bilinear-textured ramp, all on one surface at depth 0.5) is rasterized with
// the route's per-frame Halton jitter over four 16-phase periods and resolved
// with the default weight and the producer's stationary correspondence
// (previous unjittered UV of the content = own center minus the current
// jitter). History is the accumulated output on the unjittered grid and is
// read at that UV plus the current jitter, so every history tap lands on a
// texel center (f = 0 up to float rounding of the jitter, about 1e-6 px): the
// output must be stable across phases, converge at the fractional edges to
// the jitter-sampled coverage (supersampling) and keep the square's centroid
// in place. The old convention (previous jitter added to the lookup) moved the
// taps by the jitter difference every frame: oscillation plus bilinear blur.
double halton(unsigned index,unsigned base){double f=1,r=0;while(index){f/=base;r+=f*(index%base);index/=base;}return r;}
void metric(const char* label,double actual,double expected,double tolerance){const bool okay=std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance;++numeric_checks;std::printf("SAMPLE %s actual=%.9f expected=%.9f tolerance=%.9f %s\n",label,actual,expected,tolerance,okay?"PASS":"FAIL");if(!okay)throw std::runtime_error(label);}
struct StationaryScene {
    static constexpr UINT S=32,PHASES=16,FRAMES=64;
    // Area coordinates: pixel i spans [i, i+1). The fractional edges keep every
    // jittered raster edge at least 0.03 px from a pixel center (no fill-rule ties).
    static constexpr double EDGE_L=4.28,EDGE_T=4.37,EDGE_R=12.72,EDGE_B=12.59,SQ_L=20,SQ_T=6,SQ_R=23,SQ_B=9,RAMP_L=4,RAMP_T=18,RAMP_R=28,RAMP_B=28;
    IDirect3DDevice9* d;
    Com<IDirect3DTexture9> color,depth32,motion,ramp;Com<IDirect3DSurface9> colorSurface;
    Com<IDirect3DPixelShader9> flat,textured;
    StationaryScene(IDirect3DDevice9* device,Compiler compiler):d(device){
        check("stationary color",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("stationary color surface",color->GetSurfaceLevel(0,&colorSurface.p));
        check("stationary depth",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth32.p,nullptr));
        check("stationary motion",d->CreateTexture(S,S,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&motion.p,nullptr));
        check("stationary ramp",d->CreateTexture(64,64,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&ramp.p,nullptr));
        Com<ID3DXBuffer> a,b;compile(compiler,"float4 color:register(c0);float4 main():COLOR0{return color;}","ps_3_0",&a.p);
        compile(compiler,"sampler2D source:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(source,uv);}","ps_3_0",&b.p);
        check("stationary flat PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&flat.p));check("stationary textured PS",d->CreatePixelShader(static_cast<DWORD*>(b->GetBufferPointer()),&textured.p));
        // One depth (0.5) everywhere: the edges are color edges on one surface, so depth rejection never fires and coverage can accumulate.
        {Com<IDirect3DTexture9> staging;check("stationary depth staging",d->CreateTexture(S,S,1,0,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging.p,nullptr));D3DLOCKED_RECT lock{};check("lock stationary depth",staging->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const float z=.5f;std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&z,4);}check("unlock stationary depth",staging->UnlockRect(0));check("stationary depth upload",d->UpdateTexture(staging.p,depth32.p));}
        // Horizontal ramp 64/255..190/255 (2.67 texels per pixel over the 24-px quad), constant down the columns.
        {D3DLOCKED_RECT lock{};check("lock ramp",ramp->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<64;++y)for(UINT x=0;x<64;++x){const unsigned char v=static_cast<unsigned char>(64+2*x);const unsigned char px[4]={v,v,v,255};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,px,4);}check("unlock ramp",ramp->UnlockRect(0));}
    }
    // Stationary correspondence exactly as the route's producer emits it: the
    // jittered sample at pixel p shows content at p - j, whose previous
    // unjittered texture-center UV is (p + 0.5 - j)/S; expected depth 0.5, valid.
    void correspondence(double jx,double jy){D3DLOCKED_RECT lock{};check("lock stationary motion",motion->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const float v[4]={float((x+.5-jx)/S),float((y+.5-jy)/S),.5f,1};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,v,16);}check("unlock stationary motion",motion->UnlockRect(0));}
    ~StationaryScene(){for(UINT i=0;i<7;++i)d->SetTexture(i,nullptr);d->SetPixelShader(nullptr);}
    // Pre-transformed pixel space: pixel i's center is at i, so the area edge e
    // lies at e-0.5. The jitter displaces the geometry by +j, so pixel i then
    // sees the unjittered scene at i+0.5-j, exactly like the route's jittered rows.
    void quad(double left,double top,double right,double bottom,double jx,double jy){struct V{float x,y,z,rhw,u,v;};
        const float l=float(left-.5+jx),t=float(top-.5+jy),r=float(right-.5+jx),b=float(bottom-.5+jy);
        const V v[]={{l,t,.5f,1,0,0},{r,t,.5f,1,1,0},{l,b,.5f,1,0,1},{r,b,.5f,1,1,1}};
        check("stationary raster",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));}
    void render(double jx,double jy){
        for(UINT n=0;n<20;++n)check("stationary unbind",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
        check("stationary MRT",d->SetRenderTarget(1,nullptr));check("stationary DS",d->SetDepthStencilSurface(nullptr));check("stationary RT",d->SetRenderTarget(0,colorSurface.p));
        D3DVIEWPORT9 vp{0,0,S,S,0,1};check("stationary VP",d->SetViewport(&vp));
        check("stationary freq0",d->SetStreamSourceFreq(0,1));check("stationary freq1",d->SetStreamSourceFreq(1,1));
        check("stationary VS",d->SetVertexShader(nullptr));check("stationary FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check("stationary TSS index",d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));check("stationary TSS transform",d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));check("stationary IB",d->SetIndices(nullptr));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})check("stationary disable",d->SetRenderState(state,FALSE));
        for(auto p:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_VERTEXBLEND,D3DVBF_DISABLE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_WRAP0,0}})check("stationary render state",d->SetRenderState(p.first,p.second));
        for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})check("stationary sampler",d->SetSamplerState(0,p.first,p.second));
        check("stationary Begin",d->BeginScene());check("stationary flat PS bind",d->SetPixelShader(flat.p));
        const float backdrop[4]={.25f,.25f,.25f,1},bright[4]={1,1,1,1};
        check("backdrop color",d->SetPixelShaderConstantF(0,backdrop,1));quad(0,0,S,S,0,0); // unjittered: every pixel in every phase
        check("bright color",d->SetPixelShaderConstantF(0,bright,1));quad(EDGE_L,EDGE_T,EDGE_R,EDGE_B,jx,jy);quad(SQ_L,SQ_T,SQ_R,SQ_B,jx,jy);
        check("ramp bind",d->SetTexture(0,ramp.p));check("stationary textured PS bind",d->SetPixelShader(textured.p));quad(RAMP_L,RAMP_T,RAMP_R,RAMP_B,jx,jy);
        check("stationary End",d->EndScene());
    }
    std::vector<float> read(IDirect3DTexture9* texture){Com<IDirect3DSurface9> level,sys;check("stationary level",texture->GetSurfaceLevel(0,&level.p));check("stationary readback surface",d->CreateOffscreenPlainSurface(S,S,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&sys.p,nullptr));check("stationary validation-only readback",d->GetRenderTargetData(level.p,sys.p));D3DLOCKED_RECT lock{};check("stationary lock",sys->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(S*S*4);
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)for(UINT c=0;c<4;++c){unsigned short h;std::memcpy(&h,static_cast<const char*>(lock.pBits)+y*lock.Pitch+x*8+c*2,2);out[(y*S+x)*4+c]=halfFloat(h);}
        check("stationary unlock",sys->UnlockRect());return out;}
};
void stationary_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("STATIONARY_CASES");StationaryScene s(d,compiler);TemporalPass pass;check("stationary initialize",pass.initialize(d,decoder,resolver));
    constexpr UINT S=StationaryScene::S,N=StationaryScene::FRAMES,P=StationaryScene::PHASES;constexpr float w=.9f;
    std::vector<std::vector<float>> current(N),output(N);double pjx=0,pjy=0,modelError=0;
    for(UINT n=0;n<N;++n){
        const unsigned index=n%P+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(jx,jy);s.correspondence(jx,jy);current[n]=s.read(s.color.p);
        FrameInputs in;in.color=s.color.p;in.current_depth=s.depth32.p;in.motion=s.motion.p;in.width=S;in.height=S;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.previous_jitter[0]=float(pjx);in.previous_jitter[1]=float(pjy);in.weight=w;
        in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::KnownNonReactive;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        Output out;check("stationary Begin resolve",d->BeginScene());check("stationary resolve",pass.run(in,&out));check("stationary End resolve",d->EndScene());
        require(out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0),"stationary history follows the sequence");
        output[n]=s.read(out.color);pjx=jx;pjy=jy;
        // One-step oracle: the shader's arithmetic on the actual current image and
        // the actual previous output, with the history tap at this pixel's own
        // center (f = 0; a jitter-difference offset would blend the neighbors and
        // fail at every edge and ramp pixel) and the 3x3 clamp of the current frame.
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)for(UINT c=0;c<3;++c){const float cur=current[n][(y*S+x)*4+c];float expected=cur;
            if(n){float lo=cur,hi=cur;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int nx=std::min(std::max(int(x)+dx,0),int(S)-1),ny=std::min(std::max(int(y)+dy,0),int(S)-1);const float v=current[n][(UINT(ny)*S+UINT(nx))*4+c];lo=std::min(lo,v);hi=std::max(hi,v);}
                const float old=std::min(std::max(output[n-1][(y*S+x)*4+c],lo),hi);expected=halfFloat(toHalf(cur+w*(old-cur)));}
            modelError=std::max(modelError,double(std::fabs(output[n][(y*S+x)*4+c]-expected)));}
    }
    // (a) Stability across phases in the last period. Interior (flat regions and
    // the integer-aligned square): identical. Fractional-edge ring: the raw sample
    // toggles, so the EMA moves by at most (1-w) per frame. Bilinear ramp: the
    // jitter shifts the sampled texture by up to 0.94 px (0.02 in value), so the
    // EMA moves by at most 0.002 plus FP16 rounding.
    auto ring=[](UINT x,UINT y){return ((x==4||x==12)&&y>=4&&y<=12)||((y==4||y==12)&&x>=4&&x<=12);};
    auto rampq=[](UINT x,UINT y){return x>=4&&x<28&&y>=18&&y<28;};
    double interiorDelta=0,ringDelta=0,rampDelta=0;
    for(UINT n=N-P;n<N;++n)for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)for(UINT c=0;c<3;++c){const double delta=std::fabs(output[n][(y*S+x)*4+c]-output[n-1][(y*S+x)*4+c]);
        (ring(x,y)?ringDelta:rampq(x,y)?rampDelta:interiorDelta)=std::max(ring(x,y)?ringDelta:rampq(x,y)?rampDelta:interiorDelta,delta);}
    // (b) Edge convergence: over one full period the EMA's mean equals the mean
    // of the raw jittered samples (DC gain one, transient 0.9^48 < 0.01), which
    // is the 16-phase estimate of the analytic coverage (quantized to 1/16 and
    // biased by the Halton set's mean offset, hence the 0.1 bound). The ramp's
    // period mean likewise equals the supersampled texture mean.
    auto periodMean=[&](const std::vector<std::vector<float>>& images,UINT x,UINT y){double sum=0;for(UINT n=N-P;n<N;++n)sum+=images[n][(y*S+x)*4];return sum/P;};
    struct EdgePixel{UINT x,y;double coverage;const char* label;};
    const EdgePixel edges[]={{4,8,1-.28,"left"},{12,8,.72,"right"},{8,4,1-.37,"top"},{8,12,.59,"bottom"},{4,4,(1-.28)*(1-.37),"corner"}};
    double dcError=0,analyticError=0,rampDc=0;
    for(auto e:edges){const double resolved=(periodMean(output,e.x,e.y)-.25)/.75,sampled=(periodMean(current,e.x,e.y)-.25)/.75;
        dcError=std::max(dcError,std::fabs(resolved-sampled));analyticError=std::max(analyticError,std::fabs(resolved-e.coverage));
        std::printf("EDGE %s x=%u y=%u resolved_coverage=%.4f sampled_coverage=%.4f analytic_coverage=%.4f\n",e.label,e.x,e.y,resolved,sampled,e.coverage);}
    for(UINT y=18;y<28;++y)for(UINT x=4;x<28;++x)rampDc=std::max(rampDc,std::fabs(periodMean(output,x,y)-periodMean(current,x,y)));
    // (c) No drift: the centroid of the bright square (backdrop subtracted) over
    // a 9x9 window stays at its unjittered position (21, 7) in every frame.
    double drift=0;
    for(UINT n=0;n<N;++n){double mass=0,mx=0,my=0;for(UINT y=3;y<12;++y)for(UINT x=17;x<26;++x){const double v=output[n][(y*S+x)*4]-.25;mass+=v;mx+=v*x;my+=v*y;}
        drift=mass>3?std::max(drift,std::hypot(mx/mass-21,my/mass-7)):INFINITY;}
    // Every number is computed before any check so a failing resolve still reports all of them.
    std::printf("STATIONARY frames=%u phases=%u weight=%.2f oracle_error=%.6f interior_delta=%.6f edge_delta=%.6f ramp_delta=%.6f edge_dc_error=%.4f edge_analytic_error=%.4f ramp_dc_error=%.4f drift_px=%.6f\n",N,P,double(w),modelError,interiorDelta,ringDelta,rampDelta,dcError,analyticError,rampDc,drift);
    metric("stationary one-step oracle max error over 64 frames",modelError,0,.002);
    metric("stationary interior max delta between consecutive phases",interiorDelta,0,1./255);
    metric("stationary fractional-edge max delta between consecutive phases",ringDelta,0,(1-double(w))+.002);
    metric("stationary bilinear ramp max delta between consecutive phases",rampDelta,0,.01);
    metric("stationary edge coverage equals the jitter-sampled coverage over a period",dcError,0,.01);
    metric("stationary edge coverage converges to the analytic coverage",analyticError,0,.1);
    metric("stationary ramp period mean equals the supersampled texture mean",rampDc,0,.01);
    metric("stationary square centroid drift max over 64 frames (px)",drift,0,.05);
}
void reset_continuity(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS& pp,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("RESET_CONTINUITY");TemporalPass pass;check("continuity initialize",pass.initialize(d,decoder,resolver));Output out;
    {Fixture f(d,compiler);auto in=f.inputs();f.run(pass,in,"before reset frame");f.upload(.25f,1);auto b=f.run(pass,in,"before reset accumulation");require(b.used_history,"history before reset");
        pass.before_reset();require(pass.diagnostics().reset_pending&&!pass.diagnostics().history_valid,"before_reset releases history and pends");
        require(pass.run(in,&out)==E_INVALIDARG&&!out.color,"run refused while reset pending");
        pass.after_reset(E_FAIL);require(pass.diagnostics().reset_pending&&pass.run(in,&out)==E_INVALIDARG,"failed Reset keeps refusing");}
    check("Reset",d->Reset(&pp));std::puts("RESET PASS");pass.after_reset(S_OK);require(!pass.diagnostics().reset_pending,"after_reset clears pending");
    {Fixture f(d,compiler);auto in=f.inputs();auto a=f.run(pass,in,"after reset first frame");require(!a.used_history,"reset invalidates history");f.sample(a,.75f,.5f,"after reset");
        f.upload(.25f,1);auto b=f.run(pass,in,"after reset accumulation");require(b.used_history,"history rebuilt after reset without initialize");f.sample(b,.5f,.5f,"after reset blend");}
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3TemporalPassFixture";RegisterClassA(&cls);HWND window=CreateWindowA(cls.lpszClassName,"X3 temporal production module",WS_OVERLAPPEDWINDOW,90,90,128,128,nullptr,nullptr,cls.hInstance,nullptr);
    // Optional fifth argument "stationary-only": run just the stationary
    // stability scene (one generation), used to record the negative proof
    // against a resolve that applies the previous jitter.
    const bool stationaryOnly=argc==5&&std::strcmp(argv[4],"stationary-only")==0;
    try{if((argc!=4&&!stationaryOnly)||!window)throw std::runtime_error("usage: temporal_pass_fixture.exe <D3DX> <decoder> <resolve> [stationary-only]");Module runtime("d3d9.dll"),d3dx(argv[1]);auto compiler=symbol<Compiler>(d3dx.h,"D3DXCompileShader");Com<ID3DXBuffer> dc,rc;compile(compiler,file(argv[2]),"ps_3_0",&dc.p);compile(compiler,file(argv[3]),"ps_3_0",&rc.p);auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime.h,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Create9");D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;Com<IDirect3DDevice9> d;check("CreateDevice",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&d.p));
        if(stationaryOnly){stationary_cases(d.p,compiler,static_cast<DWORD*>(dc->GetBufferPointer()),static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u stationary_only=1\n",numeric_checks);result=0;}
        else for(unsigned generation=0;generation<2;++generation){auto* decoder=static_cast<DWORD*>(dc->GetBufferPointer());auto* resolver=static_cast<DWORD*>(rc->GetBufferPointer());cases(d.p,compiler,decoder,resolver,generation);reactive_cases(d.p,compiler,decoder,resolver);route_cases(d.p,compiler,decoder,resolver);stationary_cases(d.p,compiler,decoder,resolver);if(!generation)reset_continuity(d.p,pp,compiler,decoder,resolver);}
        if(!stationaryOnly){std::printf("RESULT PASS numerical=%u state_restorations=%u generations=2\n",numeric_checks,state_checks);result=0;}
    }catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());}if(window)DestroyWindow(window);UnregisterClassA(cls.lpszClassName,cls.hInstance);return result;}
