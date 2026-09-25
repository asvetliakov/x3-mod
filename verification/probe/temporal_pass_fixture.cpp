// Calls the production temporal runtime and actual production shader bytecode.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/camera_reprojection.h"
#include "../../src/renderer/hdr_writeback_program.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
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
// Production shader source, with `#include "name"` lines expanded relative to
// the including file (the generator does the same; D3DXCompileShader is given
// no include handler), so the sharpen program compiles from the same files.
// The resolve source the runner passes (argv[3], src/temporal/resolve.hlsl): REGION_HOLD_IDENTITY compiles the removed camera
// program from it (temporal_region_hold_inc.h).
std::string resolveSource;
std::string file(const char* path){std::ifstream in(path);if(!in)throw std::runtime_error(path);const std::string text{std::istreambuf_iterator<char>(in),{}};
    const std::string dir=std::string(path).substr(0,std::string(path).find_last_of("/\\")+1);std::istringstream lines(text);std::string line,out;
    while(std::getline(lines,line)){if(line.rfind("#include \"",0)==0){const auto end=line.find('"',10);if(end==std::string::npos)throw std::runtime_error("include");out+=file((dir+line.substr(10,end-10)).c_str());}else out+=line;out+='\n';}
    return out;}
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
// Consumer-only FP16 raw coverage. No emission producer or live classification.
void supplemental_cases(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS& pp,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("SUPPLEMENTAL_CASES");TemporalPass pass;check("supplemental initialize",pass.initialize(d,decoder,resolver));
    for(unsigned generation=0;generation<2;++generation){
        {
        Fixture f(d,compiler);RouteScene route(f,compiler);
        Com<IDirect3DTexture9> raw,r32,wrongSize;
        check("supplemental FP16 raw",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&raw.p,nullptr));
        check("supplemental R32F wrong raw",d->CreateTexture(W,H,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&r32.p,nullptr));
        check("supplemental wrong mask size",d->CreateTexture(W+1,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&wrongSize.p,nullptr));
        {D3DLOCKED_RECT lock{};check("required transition mask lock",r32->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<H;++y)std::memset(static_cast<char*>(lock.pBits)+y*lock.Pitch,0,W*4);check("required transition mask unlock",r32->UnlockRect(0));}
        auto coverage=[&](float value,int px=-1,int py=-1){D3DLOCKED_RECT lock{};check("supplemental raw lock",raw->LockRect(0,&lock,nullptr,0));
            for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const float v=px<0||(int(x)==px&&int(y)==py)?value:0;
                const unsigned short pixel[]={toHalf(v),toHalf(v),toHalf(v),toHalf(0)};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,pixel,8);}
            check("supplemental raw unlock",raw->UnlockRect(0));};
        auto input=[&](){auto in=f.inputs();in.depth_snapshot=nullptr;in.current_depth=route.depth32.p;in.reactive=raw.p;in.reactive_policy=ReactivePolicy::SupplementalMaskWithDepthSentinel;return in;};
        auto run=[&](FrameInputs in,const char* label,bool valid=true){
            f.hostile();Snapshot before(d);check("supplemental Begin",d->BeginScene());Output out;
            {Fault count(d,0);check(label,pass.run(in,&out));require(Fault::draws==(in.reactive?2u:1u),"one snapshot plus one color draw, no extra expansion pass");}
            check("supplemental End",d->EndScene());before.equals(d,label);
            require(out.color&&out.depth&&bool(out.reactive)==bool(in.reactive)&&pass.diagnostics().history_valid==valid,"supplemental atomic output validity");return out;};
        route.depth([](UINT x,UINT){return x<4?-1.f:.5f;});coverage(0);
        auto in=input();f.upload(.75f,.75f);auto seed=run(in,"supplemental first frame");require(!seed.used_history,"initial or reset frame has no history");
        f.upload(.25f,1);coverage(1,8,8);auto born=run(in,"supplemental current mask and sentinel");
        reactive_sample(d,born.color,8,8,.25f,"marked opaque pixel is current only");
        reactive_sample(d,born.color,7,7,.25f,"diagonal one-pixel neighbor is current only");
        reactive_sample(d,born.color,10,8,.5f,"two-pixel neighbor retains opaque history");
        reactive_sample(d,born.color,2,8,.25f,"combined policy retains current sentinel rejection");
        reactive_sample(d,born.depth,2,8,-1,"combined policy preserves sentinel depth");
        reactive_sample(d,born.color,8,8,1,"supplemental preserves current alpha",3);
        const auto mask=readback(d,born.reactive);unsigned marked=0;bool exact=true;
        for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const float expected=x>=7&&x<=9&&y>=7&&y<=9?1.f:0.f;exact=exact&&at<float>(mask,y*W+x)==expected;marked+=at<float>(mask,y*W+x)>0;}
        require(exact&&marked==9,"single raw pixel expands to exact binary 3x3 union");
        coverage(0);reactive_sample(d,born.reactive,8,8,1,"raw reuse cannot change owned expanded snapshot");
        auto gone=run(in,"complete empty mask after disappearance");
        reactive_sample(d,gone.color,7,7,.25f,"previous expanded diagonal rejects disappearance");
        reactive_sample(d,gone.color,10,8,.375f,"previous mask is not expanded a second time");
        reactive_sample(d,gone.reactive,8,8,0,"complete empty mask is valid current coverage");
        if(generation==0){
            // Real camera translation in the sentinel far plane, even with missing
            // object history alpha -1; unrelated opaque history keeps motion policy.
            in=input();in.sentinel_camera=true;in.camera_cut=true;f.upload(.5f,.75f);coverage(0);run(in,"far-plane warmup");
            in.camera_cut=false;in.clip_to_previous[3]=2.f/W;f.upload(.25f,1);f.uploadMotion(-1);in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;
            auto farResult=run(in,"combined sentinel camera reprojection");reactive_sample(d,farResult.color,2,8,.5f,"unmarked far-plane camera retains translated history");
            reactive_sample(d,farResult.color,10,8,.25f,"opaque missing motion history still rejects");
            coverage(1,2,8);auto farCurrent=run(in,"current far-plane supplemental coverage");reactive_sample(d,farCurrent.color,2,8,.25f,"current mask also rejects far-plane camera history");
            in.camera_cut=true;f.upload(.5f,.75f);coverage(1,3,8);run(in,"marked far-plane warmup");
            in.camera_cut=false;coverage(0);f.upload(.25f,1);auto farGone=run(in,"previous far-plane coverage lookup");
            reactive_sample(d,farGone.color,2,8,.25f,"previous mask rejects reprojected far-plane emission");
            in=input();in.camera_cut=true;f.upload(.75f,.75f);coverage(0);run(in,"object previous mask warmup");
            coverage(1,9,8);run(in,"object marked history");in.camera_cut=false;coverage(0);f.upload(.25f,1);f.uploadMotion(1);
            in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;
            D3DLOCKED_RECT ml{};check("supplemental motion lock",f.motion->LockRect(0,&ml,nullptr,0));const float corr[]={10.f/W,8.5f/H,.5f,1};std::memcpy(static_cast<char*>(ml.pBits)+8*ml.Pitch+6*16,corr,16);check("supplemental motion unlock",f.motion->UnlockRect(0));
            auto moved=run(in,"object nonzero history footprint");reactive_sample(d,moved.color,6,8,.25f,"previous coverage follows fractional object correspondence");
            // Bright raw emitter one pixel away changes the clipping statistics.
            // The independent legacy sentinel twin demonstrates history would blend
            // without supplemental expansion, rather than coincidentally clip out.
            TemporalPass baseline;check("bright edge baseline initialize",baseline.initialize(d,decoder,resolver));
            in=input();in.camera_cut=true;coverage(0);f.upload(.75f,.75f);run(in,"bright edge warmup");auto legacy=in;legacy.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;legacy.reactive=nullptr;
            f.run(baseline,legacy,"bright edge baseline warmup");in.camera_cut=false;coverage(1,8,8);f.upload(.25f,.25f);
            D3DLOCKED_RECT cl{};check("bright edge color lock",f.color->LockRect(0,&cl,nullptr,0));const unsigned short bright[]={toHalf(64),toHalf(64),toHalf(64),toHalf(.375f)};std::memcpy(static_cast<char*>(cl.pBits)+8*cl.Pitch+8*8,bright,8);check("bright edge color unlock",f.color->UnlockRect(0));
            auto edge=run(in,"expanded current mask prevents bright clip influence");legacy.camera_cut=false;auto unprotected=f.run(baseline,legacy,"bright edge legacy twin");
            reactive_sample(d,edge.color,7,8,.25f,"bright adjacent pixel remains current only");reactive_sample(d,unprotected.color,7,8,.5f,"without supplemental mask bright neighbor permits old energy");reactive_sample(d,edge.color,8,8,.375f,"nontrivial current alpha preserved",3);
            for(float value:{0.f,-0.f,.25f,-1.f,NAN,INFINITY,-INFINITY}){
                in=input();in.camera_cut=true;coverage(0);f.upload(.75f,.75f);run(in,"canonical values warmup");in.camera_cut=false;coverage(value);f.upload(.25f,1);auto canonical=run(in,"FP16 raw canonicalization");
                const bool safe=value==0;reactive_sample(d,canonical.reactive,8,8,safe?0.f:1.f,"zero safe nonzero nonfinite canonical coverage");reactive_sample(d,canonical.color,8,8,safe?.5f:.25f,"canonical coverage rejects conservatively");
            }
            in=input();coverage(1,0,0);auto border=run(in,"clamped border expansion");reactive_sample(d,border.reactive,1,1,1,"border diagonal expands");reactive_sample(d,border.reactive,15,15,0,"border expansion never wraps");
            // Incomplete producer coverage is the existing Unavailable contract.
            in=input();in.reactive_policy=ReactivePolicy::Unavailable;in.reactive=nullptr;f.upload(.75f,.75f);auto unavailable=run(in,"incomplete coverage",false);require(!unavailable.used_history,"incomplete cannot consume history");
            in=input();coverage(0);f.upload(.25f,1);auto complete=run(in,"complete coverage restored");require(!complete.used_history,"incomplete frame cannot seed next history");reactive_sample(d,complete.color,8,8,.25f,"completion resumes current only");
            f.upload(.5f,0);auto accumulated=run(in,"complete empty coverage accumulation");require(accumulated.used_history,"complete empty establishes usable history");reactive_sample(d,accumulated.color,8,8,.375f,"history resumes after complete frame");
            for(auto policy:{ReactivePolicy::DerivedFromDepthSentinel,ReactivePolicy::KnownNonReactive,ReactivePolicy::RequiredMask}){in=input();in.reactive_policy=policy;in.reactive=policy==ReactivePolicy::RequiredMask?r32.p:nullptr;require(!run(in,"policy transition away").used_history,"policy transition invalidates");in=input();require(!run(in,"policy transition to supplemental").used_history,"supplemental policy transition invalidates");}
            ++in.epoch;require(!run(in,"supplemental epoch change").used_history,"supplemental epoch invalidates");in.cut=true;require(!run(in,"supplemental camera cut").used_history,"supplemental cut invalidates");
            Output failed;
            auto refuse=[&](FrameInputs bad,const char* label){f.hostile();Snapshot before(d);require(pass.run(bad,&failed)==E_INVALIDARG&&!failed.color&&!failed.depth&&!failed.reactive&&!pass.diagnostics().history_valid,label);before.equals(d,label);};
            in=input();in.reactive=nullptr;refuse(in,"supplemental missing mask refuses");in=input();in.reactive=wrongSize.p;refuse(in,"supplemental wrong mask size refuses");in=input();in.reactive=r32.p;refuse(in,"supplemental rejects R32F raw format");in=input();in.reactive_policy=ReactivePolicy::RequiredMask;refuse(in,"required mask still rejects FP16 format");
            in=input();in.current_depth=nullptr;in.depth_snapshot=f.depth.p;refuse(in,"supplemental refuses decoder depth");in=input();in.caller_queries_idle=false;refuse(in,"supplemental refuses unknown queries");in=input();in.caller_stateblock_recording=true;refuse(in,"supplemental refuses recording");
            in=input();auto owned=run(in,"supplemental refusal recovery");require(!owned.used_history,"refusal recovery does not consume invalid history");in.reactive=owned.reactive;refuse(in,"supplemental rejects owned mask alias");
            for(unsigned draw:{1u,2u}){in=input();f.hostile();Snapshot before(d);check("supplemental fault Begin",d->BeginScene());{Fault fault(d,draw);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!failed.depth&&!failed.reactive&&!pass.diagnostics().history_valid,"snapshot or color fault rejects whole history set");}check("supplemental fault End",d->EndScene());before.equals(d,"supplemental draw fault restoration");require(!run(in,"supplemental draw fault recovery").used_history,"failed frame never publishes history");}
            f.hostile();Snapshot before(d);check("supplemental restore fault Begin",d->BeginScene());{Fault fault(d,0,true,false);require(pass.run(in,&failed)==E_FAIL&&pass.diagnostics().operation==S_OK&&!failed.reactive&&!pass.diagnostics().history_valid,"supplemental restoration failure prevents publication");}check("supplemental restore fault End",d->EndScene());before.equals(d,"supplemental restore failure preserves remaining state");
        }
        if(!generation){Output failed;in=input();pass.before_reset();require(pass.run(in,&failed)==E_INVALIDARG&&!failed.reactive&&pass.diagnostics().reset_pending,"supplemental before_reset refuses");pass.after_reset(E_FAIL);require(pass.run(in,&failed)==E_INVALIDARG&&pass.diagnostics().reset_pending,"supplemental failed Reset keeps refusing");}
        }
        if(!generation){check("supplemental actual Reset",d->Reset(&pp));pass.after_reset(S_OK);std::puts("SUPPLEMENTAL_RESET PASS");}
    }
}
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
    // A previous SENTINEL tap is the background behind a silhouette and is
    // accepted (clamped), not dropped (before 2026-09-12 it contributed no
    // energy, which left jittered silhouettes current-only). One flat frame
    // (0.5) first gives the sentinel pixel (4,8) a current-only history of
    // 0.5, distinct from the following frame's current 0.25 at (8,8) whose
    // correspondence points at it: the blend is 0.375; a dropped tap would
    // give 0.25. The opaque pixel (10,8) keeps accumulating: 0.5 -> 0.375.
    f.upload(.5f,.5f);s.run(ps,in,"sentinel flat frame");f.upload(.25f,1);
    f.uploadMotion(0);{D3DLOCKED_RECT ml{};check("lock sentinel correspondence",f.motion->LockRect(0,&ml,nullptr,0));const float corr[4]={4.5f/W,8.5f/H,.5f,1};std::memcpy(static_cast<char*>(ml.pBits)+8*ml.Pitch+8*16,corr,16);check("unlock sentinel correspondence",f.motion->UnlockRect(0));}
    in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;auto s3=s.run(ps,in,"sentinel history tap");reactive_sample(d,s3.color,8,8,.375f,"previous sentinel tap is accepted as background behind a silhouette");reactive_sample(d,s3.color,10,8,.375f,"opaque correspondence keeps accumulating");
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
// With deferMetrics set, a failing metric is recorded instead of thrown so every
// number of a scene group is reported; the group throws the first failure at its end.
bool deferMetrics=false;std::vector<std::string> deferredFailures;
void metric(const char* label,double actual,double expected,double tolerance){const bool okay=std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance;++numeric_checks;std::printf("SAMPLE %s actual=%.9f expected=%.9f tolerance=%.9f %s\n",label,actual,expected,tolerance,okay?"PASS":"FAIL");if(!okay){if(deferMetrics)deferredFailures.push_back(label);else throw std::runtime_error(label);}}
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
        // fail at every edge and ramp pixel) and the neighborhood clip of the
        // current frame: mean +/- 1.25 sigma of the 3x3 intersected with its min/max.
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)for(UINT c=0;c<3;++c){const float cur=current[n][(y*S+x)*4+c];float expected=cur;
            if(n){double lo=cur,hi=cur,m1=0,m2=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int nx=std::min(std::max(int(x)+dx,0),int(S)-1),ny=std::min(std::max(int(y)+dy,0),int(S)-1);const float v=current[n][(UINT(ny)*S+UINT(nx))*4+c];lo=std::min(lo,double(v));hi=std::max(hi,double(v));m1+=v/9.;m2+=double(v)*v/9.;}
                const double sigma=std::sqrt(std::max(m2-m1*m1,0.));lo=std::max(lo,m1-1.25*sigma);hi=std::min(hi,m1+1.25*sigma);
                const double old=std::min(std::max(double(output[n-1][(y*S+x)*4+c]),lo),hi);expected=halfFloat(toHalf(float(cur+w*(old-cur))));}
            modelError=std::max(modelError,double(std::fabs(output[n][(y*S+x)*4+c]-expected)));}
    }
    // (a) Stability across phases in the last period. Interior (flat regions and
    // the integer-aligned square): identical. Fractional-edge ring: the raw sample
    // toggles, so the EMA moves by at most (1-w) per frame (the variance clip
    // adds a clamp step at corner pixels whose 3x3 holds one bright sample).
    // Bilinear ramp: the jitter shifts the sampled texture by up to 0.94 px
    // (0.02 in value), so the EMA moves by at most 0.002 plus FP16 rounding.
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
    // The corner pixel is measured apart: in the phases where only it is
    // covered, its 3x3 holds one bright sample among eight dark ones and the
    // variance clip (mean + 1.25 sigma = 0.628) trims its history, a
    // deliberate bias of the clip (about 0.013 of coverage) that the linear
    // DC-gain argument does not cover; the four edge pixels keep the 0.01 bound.
    double dcError=0,cornerDcError=0,analyticError=0,rampDc=0;
    for(auto e:edges){const double resolved=(periodMean(output,e.x,e.y)-.25)/.75,sampled=(periodMean(current,e.x,e.y)-.25)/.75;
        (e.x==4&&e.y==4?cornerDcError:dcError)=std::max(e.x==4&&e.y==4?cornerDcError:dcError,std::fabs(resolved-sampled));analyticError=std::max(analyticError,std::fabs(resolved-e.coverage));
        std::printf("EDGE %s x=%u y=%u resolved_coverage=%.4f sampled_coverage=%.4f analytic_coverage=%.4f\n",e.label,e.x,e.y,resolved,sampled,e.coverage);}
    for(UINT y=18;y<28;++y)for(UINT x=4;x<28;++x)rampDc=std::max(rampDc,std::fabs(periodMean(output,x,y)-periodMean(current,x,y)));
    // Resampling blur on the stationary ramp: the horizontal gradient energy of the
    // period-mean output over that of the period-mean input (every history tap
    // sits on a texel center, so no filter blurs it: the ratio is one for any filter).
    double gradientOut=0,gradientIn=0;
    for(UINT y=19;y<27;++y)for(UINT x=6;x<25;++x){gradientOut+=std::pow(periodMean(output,x+1,y)-periodMean(output,x,y),2);gradientIn+=std::pow(periodMean(current,x+1,y)-periodMean(current,x,y),2);}
    const double rampSharpness=gradientOut/gradientIn;
    // (c) No drift: the centroid of the bright square (backdrop subtracted) over
    // a 9x9 window stays at its unjittered position (21, 7) in every frame.
    double drift=0;
    for(UINT n=0;n<N;++n){double mass=0,mx=0,my=0;for(UINT y=3;y<12;++y)for(UINT x=17;x<26;++x){const double v=output[n][(y*S+x)*4]-.25;mass+=v;mx+=v*x;my+=v*y;}
        drift=mass>3?std::max(drift,std::hypot(mx/mass-21,my/mass-7)):INFINITY;}
    // Every number is computed before any check so a failing resolve still reports all of them.
    std::printf("STATIONARY frames=%u phases=%u weight=%.2f oracle_error=%.6f interior_delta=%.6f edge_delta=%.6f ramp_delta=%.6f edge_dc_error=%.4f corner_dc_error=%.4f edge_analytic_error=%.4f ramp_dc_error=%.4f drift_px=%.6f ramp_gradient_energy_ratio=%.4f\n",N,P,double(w),modelError,interiorDelta,ringDelta,rampDelta,dcError,cornerDcError,analyticError,rampDc,drift,rampSharpness);
    metric("stationary one-step oracle max error over 64 frames",modelError,0,.002);
    metric("stationary interior max delta between consecutive phases",interiorDelta,0,1./255);
    metric("stationary fractional-edge max delta between consecutive phases",ringDelta,0,(1-double(w))+.002);
    metric("stationary bilinear ramp max delta between consecutive phases",rampDelta,0,.01);
    metric("stationary edge coverage equals the jitter-sampled coverage over a period",dcError,0,.01);
    metric("stationary corner coverage equals the jitter-sampled coverage within the variance-clip bias",cornerDcError,0,.02);
    metric("stationary edge coverage converges to the analytic coverage",analyticError,0,.1);
    metric("stationary ramp period mean equals the supersampled texture mean",rampDc,0,.01);
    metric("stationary square centroid drift max over 64 frames (px)",drift,0,.05);
    metric("stationary ramp gradient energy of the output over the input",std::min(rampSharpness,1.),1,.1);
}
// Silhouette, thin-feature and resampling scenes (resolve quality pass,
// 2026-09-12). Objects with their own depth are rasterized over a background
// that is either the -1 sentinel (nothing routed, the game's space background)
// or a routed far surface. Color, RGBA32F motion and R32F depth are drawn per
// frame with the Halton jitter by three flat draws per object, exactly the
// route's three targets: the motion program writes, from VPOS, the previous
// unjittered texture-center UV of the content at the jittered sample
// (p + 0.5 - j - v)/S, the expected depth and alpha 1 (the background's fill
// alpha is a parameter: -1 unknown as the route fills today, 0 camera path).
struct EdgeObject { double l,t,r,b; float value,depth; double vx=0,vy=0; bool scroll=false; double u0=0; bool colourOnly=false; bool noDepth=false; bool pattern=false; float phase=0; float valueB=-1.f; }; // pattern: the colour is 1 where (x + y + phase) % 3 == 0, else 0 (the exit-reset sky); valueB >= 0: the blue channel (the other two stay value) // colourOnly: blended without depth and unrouted (no motion, no depth write); noDepth: the route's fade-band draw (RT1 written with alpha 1 and its depth target, RT2 masked: the depth stays the sentinel)
struct EdgeBackground { float value,depth,alpha; };
struct EdgeScene {
    static constexpr UINT S=32,P=16;
    IDirect3DDevice9* d;
    Com<IDirect3DTexture9> color,depth32,motion,wave;Com<IDirect3DSurface9> colorSurface,depthSurface,motionSurface;
    Com<IDirect3DPixelShader9> flat,motionPS,textured,patternPS;
    bool alphaFollows=false; // colour draws write alpha = value instead of 1 (alpha-history cases)
    EdgeScene(IDirect3DDevice9* device,Compiler compiler):d(device){
        check("edge color",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("edge color surface",color->GetSurfaceLevel(0,&colorSurface.p));
        check("edge depth",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth32.p,nullptr));check("edge depth surface",depth32->GetSurfaceLevel(0,&depthSurface.p));
        check("edge motion",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));check("edge motion surface",motion->GetSurfaceLevel(0,&motionSurface.p));
        check("edge wave",d->CreateTexture(S,S,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&wave.p,nullptr));
        Com<ID3DXBuffer> a,b,c;compile(compiler,"float4 color:register(c0);float4 main():COLOR0{return color;}","ps_3_0",&a.p);
        compile(compiler,"float4 j:register(c0);float4 k:register(c1);float4 main(float2 vpos:VPOS):COLOR0{return float4((vpos+0.5-j.xy-j.zw)*k.y,k.x,k.z);}","ps_3_0",&b.p);
        compile(compiler,"sampler2D source:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(source,uv);}","ps_3_0",&c.p);
        Com<ID3DXBuffer> e;compile(compiler,"float4 c:register(c0);float4 main(float2 vpos:VPOS):COLOR0{float v=fmod(floor(vpos.x)+floor(vpos.y)+c.x,3)<0.5?c.y:c.z;return float4(v,v,v,1);}","ps_3_0",&e.p);
        check("edge flat PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&flat.p));check("edge motion PS",d->CreatePixelShader(static_cast<DWORD*>(b->GetBufferPointer()),&motionPS.p));check("edge textured PS",d->CreatePixelShader(static_cast<DWORD*>(c->GetBufferPointer()),&textured.p));check("edge pattern PS",d->CreatePixelShader(static_cast<DWORD*>(e->GetBufferPointer()),&patternPS.p));
        // Horizontal sinusoid of period 8 texels, 0.25..1, one texel per pixel, constant down the columns.
        {D3DLOCKED_RECT lock{};check("lock wave",wave->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const unsigned char v=static_cast<unsigned char>(std::lround(255*(.625+.375*std::sin(2*3.14159265358979*x/8.))));const unsigned char px[4]={v,v,v,255};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,px,4);}check("unlock wave",wave->UnlockRect(0));}
    }
    ~EdgeScene(){for(UINT i=0;i<7;++i)d->SetTexture(i,nullptr);d->SetPixelShader(nullptr);}
    void target(IDirect3DSurface9* rt){
        for(UINT n=0;n<20;++n)check("edge unbind",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
        check("edge MRT",d->SetRenderTarget(1,nullptr));check("edge DS",d->SetDepthStencilSurface(nullptr));check("edge RT",d->SetRenderTarget(0,rt));
        D3DVIEWPORT9 vp{0,0,S,S,0,1};check("edge VP",d->SetViewport(&vp));
        check("edge freq0",d->SetStreamSourceFreq(0,1));check("edge freq1",d->SetStreamSourceFreq(1,1));
        check("edge VS",d->SetVertexShader(nullptr));check("edge FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check("edge TSS index",d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));check("edge TSS transform",d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));check("edge IB",d->SetIndices(nullptr));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})check("edge disable",d->SetRenderState(state,FALSE));
        for(auto p:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_VERTEXBLEND,D3DVBF_DISABLE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_WRAP0,0}})check("edge render state",d->SetRenderState(p.first,p.second));
        for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_LINEAR},{D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP},{D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})check("edge sampler",d->SetSamplerState(0,p.first,p.second));
    }
    // Pre-transformed pixel space: pixel i's center is at i, so the area edge e lies at e-0.5; the jitter displaces the geometry by +j.
    void quad(double left,double top,double right,double bottom,double jx,double jy,double u0=0,double u1=1){struct V{float x,y,z,rhw,u,v;};
        const float l=float(left-.5+jx),t=float(top-.5+jy),r=float(right-.5+jx),b=float(bottom-.5+jy);
        const V v[]={{l,t,.5f,1,float(u0),0},{r,t,.5f,1,float(u1),0},{l,b,.5f,1,float(u0),1},{r,b,.5f,1,float(u1),1}};
        check("edge raster",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));}
    void constant(float a,float b,float c,float e,UINT slot=0){const float v[4]={a,b,c,e};check("edge constant",d->SetPixelShaderConstantF(slot,v,1));}
    void render(const std::vector<EdgeObject>& objects,const EdgeBackground& bg,double jx,double jy){
        target(colorSurface.p);check("edge Begin",d->BeginScene());
        check("edge flat bind",d->SetPixelShader(flat.p));constant(bg.value,bg.value,bg.value,alphaFollows?bg.value:1);quad(0,0,S,S,0,0);
        for(auto& o:objects){if(o.scroll){check("edge wave bind",d->SetTexture(0,wave.p));check("edge textured bind",d->SetPixelShader(textured.p));quad(o.l,o.t,o.r,o.b,jx,jy,o.u0,o.u0+(o.r-o.l)/S);check("edge wave unbind",d->SetTexture(0,nullptr));check("edge flat rebind",d->SetPixelShader(flat.p));}
            else if(o.pattern){check("edge pattern bind",d->SetPixelShader(patternPS.p));constant(o.phase,1,0,0);quad(o.l,o.t,o.r,o.b,jx,jy);check("edge flat rebind",d->SetPixelShader(flat.p));}
            else{constant(o.value,o.value,o.valueB>=0?o.valueB:o.value,alphaFollows?o.value:1);quad(o.l,o.t,o.r,o.b,jx,jy);}}
        check("edge End",d->EndScene());
        target(motionSurface.p);check("edge motion Begin",d->BeginScene());check("edge motion bind",d->SetPixelShader(motionPS.p));
        constant(float(jx),float(jy),0,0);constant(bg.depth,1.f/S,bg.alpha,0,1);quad(0,0,S,S,0,0);
        for(auto& o:objects){if(o.colourOnly)continue;constant(float(jx),float(jy),float(o.vx),float(o.vy));constant(o.depth,1.f/S,1,0,1);quad(o.l,o.t,o.r,o.b,jx,jy);}
        check("edge motion End",d->EndScene());
        target(depthSurface.p);check("edge depth Begin",d->BeginScene());check("edge depth bind",d->SetPixelShader(flat.p));
        constant(bg.depth,0,0,0);quad(0,0,S,S,0,0);
        for(auto& o:objects){if(o.colourOnly||o.noDepth)continue;constant(o.depth,0,0,0);quad(o.l,o.t,o.r,o.b,jx,jy);}
        check("edge depth End",d->EndScene());
    }
    std::vector<float> read(IDirect3DTexture9* texture){D3DSURFACE_DESC desc{};check("edge level desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level,sys;check("edge level",texture->GetSurfaceLevel(0,&level.p));check("edge readback surface",d->CreateOffscreenPlainSurface(S,S,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr));check("edge validation-only readback",d->GetRenderTargetData(level.p,sys.p));D3DLOCKED_RECT lock{};check("edge lock",sys->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(S*S*4);
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)for(UINT c=0;c<4;++c){const char* p=static_cast<const char*>(lock.pBits)+y*lock.Pitch;float v=0;
            if(desc.Format==D3DFMT_R32F){if(c==0)std::memcpy(&v,p+x*4,4);}else if(desc.Format==D3DFMT_A8R8G8B8)v=static_cast<unsigned char>(p[x*4+(c==3?3:2-c)])/255.f;else if(desc.Format==D3DFMT_A32B32G32R32F)std::memcpy(&v,p+x*16+c*4,4);else{unsigned short h;std::memcpy(&h,p+x*8+c*2,2);v=halfFloat(h);}
            out[(y*S+x)*4+c]=v;}
        check("edge unlock",sys->UnlockRect());return out;}
};
struct EdgeRun { std::vector<std::vector<float>> current,output,depth,motion,age; std::vector<double> jx,jy; };
// strictSky: FrameInputs::sentinel_strict_sky (the resolve's strict sky term c7.z with the camera path; the sweep case below).
// clipToPrevious: the camera path (row-major clip_to_previous), identity when null (the pan case below passes an NDC translation).
// adaptive > 0: the age programs (thin clip 0.7, WMAX = adaptive; the age target is read into run.age); exitPx: FrameInputs::sky_history_exit_px.
// farProgram 1: the far stabiliser program (W_FAR 0.985 under the far cases' gate d0 0.9995 .. 0.9999, the default speed gate); 2: the
// far-camera program on top (thin region 0.97, camera gate): the flown configuration. Both write the age target too.
template<class Objects> EdgeRun edge_sequence(EdgeScene& s,const DWORD* decoder,const DWORD* resolver,Objects objects,const EdgeBackground& bg,unsigned frames,bool sentinelCamera,bool perPixel,const char* label,bool strictSky=false,const float* clipToPrevious=nullptr,float adaptive=0,float exitPx=0,unsigned farProgram=0){
    constexpr UINT S=EdgeScene::S,P=EdgeScene::P;TemporalPass pass;check("edge initialize",pass.initialize(s.d,decoder,resolver));EdgeRun run;
    if(adaptive>0){check("edge configure flicker",pass.configure_flicker());require(pass.age_available(),"edge age programs available");}
    if(farProgram){check("edge configure far",pass.configure_far());require(pass.far_available()&&(farProgram<2||pass.camera_gate_available()),"edge far programs available");}
    for(unsigned n=0;n<frames;++n){
        const unsigned index=n%P+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;const auto scene=objects(n);
        s.render(scene,bg,jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));run.motion.push_back(s.read(s.motion.p));run.jx.push_back(jx);run.jy.push_back(jy);
        if(n==0){ // The motion target follows the producer contract: for every pixel the first routed object covers, RG = (p + 0.5 - j - v)/S, B = its depth, A = 1.
            const auto& m=run.motion[0];double worst=0;unsigned covered=0;const EdgeObject* first=nullptr;for(auto& o:scene)if(!o.colourOnly){first=&o;break;}require(first!=nullptr,"edge scene has a routed object");
            for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)if(run.depth[0][(y*S+x)*4]==first->depth){++covered;for(double e:{std::fabs(m[(y*S+x)*4]-(x+.5-jx-first->vx)/S),std::fabs(m[(y*S+x)*4+1]-(y+.5-jy-first->vy)/S),double(std::fabs(m[(y*S+x)*4+2]-first->depth)),double(std::fabs(m[(y*S+x)*4+3]-1))})worst=std::max(worst,e);}
            ++numeric_checks;require(covered>0&&worst<=1e-6,"edge scene motion target equals the producer contract at every covered pixel");}
        FrameInputs in;in.color=s.color.p;in.current_depth=s.depth32.p;in.motion=perPixel?s.motion.p:nullptr;in.width=S;in.height=S;in.epoch=1;const float* camera=clipToPrevious?clipToPrevious:identity;std::copy(camera,camera+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=.9f;in.motion_policy=perPixel?MotionPolicy::PerPixel:MotionPolicy::KnownCameraOnly;
        in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=sentinelCamera;in.sentinel_strict_sky=strictSky;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        if(adaptive>0){in.thin_clip=.7f;in.adaptive_weight=adaptive;}in.sky_history_exit_px=exitPx;
        if(farProgram){in.far_weight=.985f;in.far_d0=.9995f;in.far_inv=1.f/(.9999f-.9995f);in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;
            if(farProgram>1){in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;}}
        Output out;check("edge Begin resolve",s.d->BeginScene());check(label,pass.run(in,&out));check("edge End resolve",s.d->EndScene());
        require(out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0),"edge history follows the sequence");
        run.output.push_back(s.read(out.color));if(out.age)run.age.push_back(s.read(out.age));
    }
    return run;
}
float px(const std::vector<float>& image,UINT x,UINT y,UINT c=0){return image[(y*EdgeScene::S+x)*4+c];}
// Coverage of the unit interval [k, k+1) by [start, start+width).
double coverage(double start,double width,unsigned k){return std::max(0.,std::min(double(k)+1,start+width)-std::max(double(k),start));}
struct LineSpec { const char* label; bool horizontal; double start; unsigned width; UINT lo,hi,along0,along1; };
// Thin-line metrics over the last period: a cross-section index k is interior when its analytic coverage is 0 or 1 (never or always covered under the jitter) and an edge otherwise.
void thin_line_metrics(const EdgeRun& run,const LineSpec& L,bool assert,const char* mode){
    constexpr UINT P=EdgeScene::P;const unsigned N=unsigned(run.output.size());constexpr double w=.9;
    auto value=[&](const std::vector<float>& img,UINT along,UINT k){return L.horizontal?px(img,along,k):px(img,k,along);};
    double interiorDelta=0,edgeDelta=0,sumDelta=0,wobble=0,coverageError=0,total=0;std::vector<double> sums,centroids;
    auto centroidAt=[&](unsigned n){double mass=0,moment=0;for(UINT k=L.lo;k<=L.hi;++k)for(UINT a=L.along0;a<L.along1;++a){const double v=value(run.output[n],a,k)-.25;mass+=v;moment+=v*(k+.5);}return mass>0?moment/mass:INFINITY;};
    double previousCentroid=0;for(unsigned n=N-2*P;n<N-P;++n)previousCentroid+=centroidAt(n)/P;
    for(unsigned n=N-P;n<N;++n){double sum=0,mass=0,moment=0;
        for(UINT k=L.lo;k<=L.hi;++k){const double analytic=coverage(L.start,L.width,k);const bool interior=analytic<=0||analytic>=1;
            for(UINT a=L.along0;a<L.along1;++a){const double delta=std::fabs(value(run.output[n],a,k)-value(run.output[n-1],a,k));(interior?interiorDelta:edgeDelta)=std::max(interior?interiorDelta:edgeDelta,delta);
                const double v=value(run.output[n],a,k)-.25;sum+=value(run.output[n],a,k);mass+=v;moment+=v*(k+.5);}}
        sums.push_back(sum/(L.along1-L.along0));centroids.push_back(mass>0?moment/mass:INFINITY);}
    for(size_t i=1;i<sums.size();++i)sumDelta=std::max(sumDelta,std::fabs(sums[i]-sums[i-1]));
    double centroidMean=0;for(double c:centroids)centroidMean+=c/centroids.size();for(double c:centroids)wobble=std::max(wobble,std::fabs(c-centroidMean));
    const double drift=std::fabs(centroidMean-previousCentroid);
    for(UINT k=L.lo;k<=L.hi;++k){double mean=0;for(unsigned n=N-P;n<N;++n)for(UINT a=L.along0;a<L.along1;++a)mean+=value(run.output[n],a,k);mean/=P*(L.along1-L.along0);
        const double resolved=(mean-.25)/.75;total+=resolved;coverageError=std::max(coverageError,std::fabs(resolved-coverage(L.start,L.width,k)));}
    const double analyticCenter=L.start+L.width/2.;
    std::printf("THINLINE mode=%s line=%s interior_delta=%.6f edge_delta=%.6f sum_delta=%.6f drift_px=%.6f wobble_px=%.6f centroid=%.4f analytic_center=%.4f total_coverage=%.4f width=%u coverage_error=%.4f\n",mode,L.label,interiorDelta,edgeDelta,sumDelta,drift,wobble,centroidMean,analyticCenter,total,L.width,coverageError);
    if(!assert)return;
    std::string prefix=std::string("thin line ")+mode+" "+L.label+": ";
    metric((prefix+"interior max delta between consecutive phases").c_str(),interiorDelta,0,1./255);
    metric((prefix+"edge max delta between consecutive phases").c_str(),edgeDelta,0,(1-w)*.75+.002);
    metric((prefix+"cross-section brightness delta between consecutive phases").c_str(),sumDelta,0,1./255);
    // Drift is the shift of the period-mean centroid between the last two
    // periods; the per-phase wobble around that mean is the EMA ripple of the
    // toggling edge rows: each moves by at most (1-w) * contrast = 0.075 per
    // frame with a lever of one pixel per unit of line mass (0.75 for a
    // 1-px line, 1.5 with two toggling rows for a 2-px line), so 0.1 px.
    metric((prefix+"centroid drift between consecutive periods (px)").c_str(),drift,0,.05);
    metric((prefix+"centroid wobble across phases (px)").c_str(),wobble,0,.1);
    metric((prefix+"centroid at the analytic line center (px)").c_str(),centroidMean,analyticCenter,.1);
    metric((prefix+"total coverage within 20% of the line width").c_str(),total/L.width,1,.2);
    metric((prefix+"per-row coverage converges to the analytic coverage").c_str(),coverageError,0,.1);
}
void silhouette_metrics(const EdgeRun& run,double l,double t,double size,unsigned moveFrom,bool assert,const char* mode){
    constexpr UINT S=EdgeScene::S,P=EdgeScene::P;const unsigned N=unsigned(run.output.size());
    // Static phase, last static period: ring pixels (fractional coverage) and the interior.
    double rawVariance=0,resolvedVariance=0,coverageError=0,dcError=0,interiorDelta=0;unsigned ring=0;
    for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const double cx=coverage(l,size,x),cy=coverage(t,size,y),analytic=cx*cy;if(analytic<=0||analytic>=1)continue;++ring;
        double rawMean=0,outMean=0;for(unsigned n=moveFrom-P;n<moveFrom;++n){rawMean+=px(run.current[n],x,y)/P;outMean+=px(run.output[n],x,y)/P;}
        double rv=0,ov=0;for(unsigned n=moveFrom-P;n<moveFrom;++n){rv+=std::pow(px(run.current[n],x,y)-rawMean,2)/P;ov+=std::pow(px(run.output[n],x,y)-outMean,2)/P;}
        const double resolved=(outMean-.25)/.75,sampled=(rawMean-.25)/.75;
        rawVariance+=rv;resolvedVariance+=ov;coverageError=std::max(coverageError,std::fabs(resolved-analytic));dcError=std::max(dcError,std::fabs(resolved-sampled));
        if(std::fabs(resolved-analytic)>.05||std::fabs(resolved-sampled)>.02)std::printf("SILRING mode=%s x=%u y=%u resolved=%.4f sampled=%.4f analytic=%.4f raw_variance=%.4f resolved_variance=%.6f\n",mode,x,y,resolved,sampled,analytic,rv,ov);}
    for(unsigned n=moveFrom-P;n<moveFrom;++n)for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)if(coverage(l,size,x)*coverage(t,size,y)>=1)interiorDelta=std::max(interiorDelta,double(std::fabs(px(run.output[n],x,y)-px(run.output[n-1],x,y))));
    const double ratio=resolvedVariance>0?rawVariance/resolvedVariance:INFINITY;
    // Moving phase: a pixel whose 3x3 holds no square pixel in the current frame must be exactly the background (no ghost beyond the clamp); the square's interior stays bright.
    double ghost=0,interiorMin=1;
    for(unsigned n=moveFrom+1;n<N;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){bool adjacent=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)adjacent|=px(run.depth[n],x+dx,y+dy)==.5f;
        const double lx=l+(n-moveFrom+1);if(!adjacent)ghost=std::max(ghost,std::fabs(px(run.output[n],x,y)-.25));
        else if(x+.5>=lx+1.5&&x+.5<=lx+size-1.5&&y+.5>=t+1.5&&y+.5<=t+size-1.5)interiorMin=std::min(interiorMin,double(px(run.output[n],x,y)));}
    std::printf("SILHOUETTE mode=%s ring=%u raw_variance=%.6f resolved_variance=%.6f ratio=%.2f dc_error=%.4f coverage_error=%.4f interior_delta=%.6f ghost_max=%.6f moving_interior_min=%.4f\n",mode,ring,rawVariance/ring,resolvedVariance/ring,ratio,dcError,coverageError,interiorDelta,ghost,interiorMin);
    if(!assert)return;
    std::string prefix=std::string("silhouette ")+mode+": ";
    metric((prefix+"edge variance across phases reduced at least 4x").c_str(),std::min(ratio,100.),100,96);
    // The ring's period mean follows the jitter-sampled coverage up to the
    // variance clip's bias at pixels whose uncovered-phase 3x3 holds two
    // bright samples (upper bound 0.806 against a 0.79 history): about 0.03.
    metric((prefix+"edge coverage equals the jitter-sampled coverage within the variance-clip bias").c_str(),dcError,0,.05);
    metric((prefix+"edge coverage converges to the analytic coverage").c_str(),coverageError,0,.1);
    metric((prefix+"interior max delta between consecutive phases").c_str(),interiorDelta,0,1./255);
    metric((prefix+"revealed background carries no square color beyond the clamp").c_str(),ghost,0,1./255);
    metric((prefix+"moving square interior stays bright").c_str(),interiorMin,1,.1);
}
// CPU model of the resolve for the scrolling wave: history at (x - v, y) through the given 1-D filter, the variance/min-max clip of the current 3x3, the blend and FP16 rounding.
std::vector<std::vector<float>> blur_model(const EdgeRun& run,double v,bool catmullRom){
    constexpr UINT S=EdgeScene::S;constexpr float w=.9f;std::vector<std::vector<float>> out(run.output.size());
    auto sample=[&](const std::vector<float>& img,double xs,UINT y,UINT c){const double pos=xs;const int base=int(std::floor(pos));const double f=pos-base;double sum=0;
        if(catmullRom){const double w0=-.5*f+f*f-.5*f*f*f,w1=1-2.5*f*f+1.5*f*f*f,w2=.5*f+2*f*f-1.5*f*f*f,w3=-.5*f*f+.5*f*f*f;const double ws[4]={w0,w1,w2,w3};
            for(int i=0;i<4;++i)sum+=ws[i]*px(img,UINT(std::min(std::max(base-1+i,0),int(S)-1)),y,c);}
        else sum=(1-f)*px(img,UINT(std::min(std::max(base,0),int(S)-1)),y,c)+f*px(img,UINT(std::min(std::max(base+1,0),int(S)-1)),y,c);
        return sum;};
    for(unsigned n=0;n<run.output.size();++n){out[n]=run.current[n];if(!n)continue;
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)for(UINT c=0;c<3;++c){const float cur=px(run.current[n],x,y,c);double lo=cur,hi=cur,m1=0,m2=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const UINT nx=UINT(std::min(std::max(int(x)+dx,0),int(S)-1)),ny=UINT(std::min(std::max(int(y)+dy,0),int(S)-1));const float q=px(run.current[n],nx,ny,c);lo=std::min(lo,double(q));hi=std::max(hi,double(q));m1+=q/9.;m2+=double(q)*q/9.;}
            const double sigma=std::sqrt(std::max(m2-m1*m1,0.));lo=std::max(lo,m1-1.25*sigma);hi=std::min(hi,m1+1.25*sigma);
            const double old=std::min(std::max(sample(out[n-1],x-v,y,c),lo),hi);out[n][(y*S+x)*4+c]=halfFloat(toHalf(float(cur+w*(old-cur))));}}
    return out;
}
// Amplitude of the period-8 component along x over columns [x0, x1) of rows [y0, y1), averaged over the last period.
double wave_amplitude(const std::vector<std::vector<float>>& images,UINT x0,UINT x1,UINT y0,UINT y1){constexpr UINT P=EdgeScene::P;const unsigned N=unsigned(images.size());double total=0;
    for(unsigned n=N-P;n<N;++n)for(UINT y=y0;y<y1;++y){double re=0,im=0;for(UINT x=x0;x<x1;++x){re+=px(images[n],x,y)*std::cos(2*3.14159265358979*x/8);im-=px(images[n],x,y)*std::sin(2*3.14159265358979*x/8);}total+=2*std::hypot(re,im)/(x1-x0);}
    return total/(P*(y1-y0));}
void edge_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("EDGE_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    // Jittered edges stay at least 0.03 px from every sample center (no fill-rule ties) for the fractional positions used below.
    {double margin=1;for(unsigned i=1;i<=EdgeScene::P;++i){const double jx=halton(i,2)-.5,jy=halton(i,3)-.5;for(double e:{6.37,7.37,14.59,16.59,10.37,16.37}){const double v=e+jy;margin=std::min(margin,std::fabs(v-std::floor(v)-.5));}for(double e:{12.28,13.28,10.28,16.28}){const double v=e+jx;margin=std::min(margin,std::fabs(v-std::floor(v)-.5));}}
        std::printf("EDGE_MARGIN %.4f\n",margin);require(margin>=.029,"edge geometry keeps jittered edges off the sample centers");}
    // sentinelFill is the route's actual ABI for unrouted pixels (RT1 alpha -1,
    // RT2 -1): under policy 2 it must behave exactly like the alpha-0 marking.
    const EdgeBackground farBackground{.25f,.9f,1},sentinelUnknown{.25f,-1.f,-1},sentinelCamera{.25f,-1.f,0},sentinelFill{.25f,-1.f,-1};
    // (a) Thin lines: a 1-px and a 2-px horizontal line and a 1-px vertical line at fractional positions, static, 64 frames.
    auto lines=[](unsigned){return std::vector<EdgeObject>{{3,6.37,29,7.37,1,.5f},{3,14.59,29,16.59,1,.5f},{12.28,20,13.28,30,1,.5f}};};
    const LineSpec specs[]={{"1px horizontal",true,6.37,1,5,8,6,26},{"2px horizontal",true,14.59,2,13,17,6,26},{"1px vertical",false,12.28,1,11,14,22,28}};
    struct Mode{const char* name;EdgeBackground bg;bool camera,perPixel,assert;};
    const Mode modes[]={{"far-background",farBackground,false,true,true},{"sentinel-camera",sentinelCamera,true,true,true},{"sentinel-camera-fill",sentinelFill,true,true,true},{"sentinel-current-only",sentinelUnknown,false,true,false}};
    for(auto& m:modes){auto run=edge_sequence(s,decoder,resolver,lines,m.bg,64,m.camera,m.perPixel,"thin lines");for(auto& L:specs)thin_line_metrics(run,L,m.assert,m.name);}
    // (b) Silhouette: a 6x6 square at a fractional position, static for 32 frames, then moving +1 px/frame for 12 frames.
    const double sl=10.28,st=10.37;auto square=[&](unsigned n){const double l=sl+(n>=32?n-31:0);return std::vector<EdgeObject>{{l,st,l+6,st+6,1,.5f,n>=32?1.:0.,0}};};
    for(auto& m:modes){auto run=edge_sequence(s,decoder,resolver,square,m.bg,44,m.camera,m.perPixel,"silhouette");silhouette_metrics(run,sl,st,6,32,m.assert,m.name);}
    // (c) Resampling blur: a static 24x8 quad whose period-8 sinusoid scrolls 0.25 px/frame (the history is resampled at a constant 0.75 texel offset every frame); the amplitude of the period-8 component of the output over the input, shader against the CPU models of the Catmull-Rom and the previous bilinear history filter.
    const double v=.25;auto wave=[&](unsigned n){return std::vector<EdgeObject>{{4,12,28,20,1,.5f,v,0,true,-double(n)*v/S}};};
    auto run=edge_sequence(s,decoder,resolver,wave,farBackground,64,false,true,"scrolling wave");
    const auto cubic=blur_model(run,v,true),bilinear=blur_model(run,v,false);
    const double input=wave_amplitude(run.current,8,24,13,19),shader=wave_amplitude(run.output,8,24,13,19),cubicModel=wave_amplitude(cubic,8,24,13,19),bilinearModel=wave_amplitude(bilinear,8,24,13,19);
    double oracle=0;for(unsigned n=0;n<run.output.size();++n)for(UINT y=13;y<19;++y)for(UINT x=8;x<24;++x)oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-px(cubic[n],x,y))));
    std::printf("BLUR velocity=%.2f input_amplitude=%.4f shader_ratio=%.4f catmull_rom_model_ratio=%.4f bilinear_model_ratio=%.4f shader_energy_ratio=%.4f bilinear_energy_ratio=%.4f oracle_error=%.6f\n",v,input,shader/input,cubicModel/input,bilinearModel/input,std::pow(shader/input,2),std::pow(bilinearModel/input,2),oracle);
    // The CPU model and the shader differ by float rounding in the weights and
    // the clip bounds, compounded through 64 frames of fractional resampling.
    metric("scrolling wave: shader matches the Catmull-Rom CPU model",oracle,0,.01);
    metric("scrolling wave: period-8 amplitude of the output over the input (Catmull-Rom)",std::min(shader/input,1.),1,.1);
    metric("scrolling wave: Catmull-Rom keeps more amplitude than the previous bilinear filter",std::min(shader/input-bilinearModel/input,1.),1,.9);
    // (d) SETA sweep (docs/architecture/seta-motion.md; run235): a black 8x8 square at device depth 0.9997 (a station a few
    // km out) static for 8 frames, then +5 px/frame for 4 frames over a textured sky (the wave, colour only: no depth, no
    // motion, the route's sentinel fill under it), the square's motion the exact displacement. Under policy 2 a trailing sky
    // pixel reprojects onto its own previous position, where the square's depth 0.9997 >= 1 - 0.02 proves the black hull as
    // its history; the strict sky history (c7.z = 3) accepts sentinel taps only there. Far from the square both are identical.
    {const double sl2=2.37,st2=12.37;const unsigned moveFrom=8,frames=12;constexpr float squareDepth=.9997f;const double v=5;
     auto sweep=[&](unsigned n){const double l=sl2+v*(n>=moveFrom?n-moveFrom+1:0);return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,0,true},{l,st2,l+8,st2+8,0,squareDepth,n>=moveFrom?v:0.,0}};};
     EdgeRun runs[2];const char* names[2]={"loose","strict"};
     for(unsigned m=0;m<2;++m)runs[m]=edge_sequence(s,decoder,resolver,sweep,sentinelFill,frames,true,true,"seta sweep",m==1);
     for(unsigned m=0;m<2;++m){const EdgeRun& run=runs[m];double trail=0,adjacent=0,motionError=0,farDiff=0;unsigned uncovered=0;
        for(unsigned n=moveFrom;n<frames;++n){const double l=sl2+v*(n-moveFrom+1);
            for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){const std::size_t i=y*S+x;bool beside=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)beside|=px(run.depth[n],x+dx,y+dy)==squareDepth;
                const bool sky=px(run.depth[n],x,y)==-1.f,wasSquare=px(run.depth[n-1],x,y)==squareDepth;
                const double dev=std::fabs(px(run.output[n],x,y)-px(run.current[n],x,y));
                if(sky&&wasSquare&&!beside){++uncovered;trail=std::max(trail,dev);}
                else if(sky&&beside)adjacent=std::max(adjacent,dev);
                if(px(run.depth[n],x,y)==squareDepth){const double dx=(x+.5-run.jx[n])-run.motion[n][i*4]*S,dy=(y+.5-run.jy[n])-run.motion[n][i*4+1]*S;motionError=std::max(motionError,std::max(std::fabs(dx-v),std::fabs(dy)));}
                if(x+8<sl2||x>l+8+8)farDiff=std::max(farDiff,double(std::fabs(px(runs[0].output[n],x,y)-px(runs[1].output[n],x,y))));}}
        std::printf("SETA_SWEEP mode=%s uncovered_px=%u trail_max=%.6f adjacent_max=%.6f motion_error_px=%.6f far_difference=%.6f\n",names[m],uncovered,trail,adjacent,motionError,farDiff);
        require(uncovered>=100,"seta sweep uncovers a trailing band every moving frame");
        const std::string prefix=std::string("seta sweep ")+names[m]+": ";
        metric((prefix+"motion target equals the 5 px displacement at every covered pixel (px)").c_str(),motionError,0,.1);
        if(m)metric((prefix+"uncovered sky beyond the dilation band is current-only (no square history)").c_str(),trail,0,1./255);
        else metric((prefix+"uncovered sky carries the square's history under policy 2 (the smear reproduced)").c_str(),std::min(trail,1.),1,.95);
        if(m)metric((prefix+"identical to loose eight pixels and more from the square").c_str(),farDiff,0,0);
        // Band term (seta-motion.md section 4): the dilated 1-px band beside the 5 px/frame silhouette takes no hull history under strict.
        if(m)metric((prefix+"dilated band beside the 5 px/frame silhouette carries no hull history (band term)").c_str(),adjacent,0,.1);}
     // (e) Fade-band object (review finding 1): a routed 8x8 square of value 0.6 at depth 0.9997, static and depth-writing for 8
     // frames, then a fade-band draw (RT1 alpha 1 with its depth target, RT2 masked: current depth sentinel) moving +5 px/frame
     // over the textured sky. Its history taps at frame 8 land on its own depth of frame 7; strict must accept them like loose
     // (alpha 1 switches the term off), so the two runs are identical on every pixel the object routes, while the uncovered
     // sky at frame 8 (previous depth 0.9997, alpha -1 now) is the smear case again.
     auto fade=[&](unsigned n){const double l=sl2+v*(n>=moveFrom?n-moveFrom+1:0);EdgeObject o{l,st2,l+8,st2+8,.6f,squareDepth,n>=moveFrom?v:0.,0};o.noDepth=n>=moveFrom;return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,0,true},o};};
     EdgeRun fades[2];for(unsigned m=0;m<2;++m)fades[m]=edge_sequence(s,decoder,resolver,fade,sentinelFill,frames,true,true,"fade-band sweep",m==1);
     {double routedDiff=0,skyDiff=0;unsigned routed=0,accumulated=0;
      for(unsigned n=moveFrom;n<frames;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){const std::size_t i=y*S+x;const bool own=fades[1].motion[n][i*4+3]==1;
          const double diff=std::fabs(px(fades[0].output[n],x,y)-px(fades[1].output[n],x,y));
          if(own){++routed;routedDiff=std::max(routedDiff,diff);if(std::fabs(px(fades[1].output[n],x,y)-px(fades[1].current[n],x,y))>1./255)++accumulated;}
          else skyDiff=std::max(skyDiff,diff);}
      std::printf("SETA_FADE routed_px=%u strict_vs_loose_on_routed=%.6f routed_px_with_history=%u strict_vs_loose_on_sky=%.6f\n",routed,routedDiff,accumulated,skyDiff);
      require(routed>=200&&skyDiff>.05,"fade-band sweep routes its object and reproduces the sky smear beside it under loose");
      metric("seta fade-band strict: identical to loose on every pixel the fade-band draw routes (its history accepted)",routedDiff,0,0);
      metric("seta fade-band strict: the fade-band draw's pixels keep using history (pixels whose output differs from the current sample)",std::min(double(accumulated),1.),1,0);}
     // (f) Slow sweep (band term, seta-motion.md section 4): the (d) square moving +0.5 px/frame for 8 frames after 8 static
     // ones. Below bandSpeed the band keeps the geometry path and every uncovered pixel lies inside the band, so strict is
     // bit-identical to loose on every pixel and frame; the band's deviation from the current sample (the anti-aliased edge) is reported.
     {const double vs=.5;const unsigned slowFrames=16;
      auto slow=[&](unsigned n){const double l=sl2+vs*(n>=moveFrom?n-moveFrom+1:0);return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,0,true},{l,st2,l+8,st2+8,0,squareDepth,n>=moveFrom?vs:0.,0}};};
      EdgeRun slows[2];for(unsigned m=0;m<2;++m)slows[m]=edge_sequence(s,decoder,resolver,slow,sentinelFill,slowFrames,true,true,"slow sweep",m==1);
      double diff=0,adjacent[2]={0,0};
      for(unsigned n=moveFrom;n<slowFrames;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){diff=std::max(diff,double(std::fabs(px(slows[0].output[n],x,y)-px(slows[1].output[n],x,y))));
          bool beside=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)beside|=px(slows[0].depth[n],x+dx,y+dy)==squareDepth;
          if(beside&&px(slows[0].depth[n],x,y)==-1.f)for(unsigned m=0;m<2;++m)adjacent[m]=std::max(adjacent[m],double(std::fabs(px(slows[m].output[n],x,y)-px(slows[m].current[n],x,y))));}
      std::printf("SETA_SLOW velocity_px=%.2f adjacent_loose=%.6f adjacent_strict=%.6f strict_vs_loose=%.6f\n",vs,adjacent[0],adjacent[1],diff);
      metric("seta slow sweep (0.5 px/frame): strict identical to loose on every pixel (the band keeps the geometry path below bandSpeed)",diff,0,0);}
     // (g) Static ring (the strict term under the jitter, review question): the square static and routed for two periods over the
     // textured sky, loose and strict. Per-pixel temporal variance of the output over the last period by Chebyshev distance from
     // the union of the square's jittered footprints (1: the dilated band, geometry path; 2 and 3: own path). Under a static
     // camera an own-path tap is the pixel's own previous texel (sentinel depth), so strict is bit-identical to loose.
     {constexpr UINT P=EdgeScene::P;const unsigned ringFrames=2*P;const double rl=sl2+8;auto still=[&](unsigned){return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,0,true},{rl,st2,rl+8,st2+8,0,squareDepth,0,0}};};
      EdgeRun stills[2];for(unsigned m=0;m<2;++m)stills[m]=edge_sequence(s,decoder,resolver,still,sentinelFill,ringFrames,true,true,"static ring",m==1);
      std::vector<unsigned char> footprint(S*S,0);for(unsigned n=0;n<ringFrames;++n)for(UINT i=0;i<S*S;++i)if(stills[0].depth[n][i*4]==squareDepth)footprint[i]=1;
      auto distance=[&](UINT x,UINT y){for(int r=1;r<=3;++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){const int nx=int(x)+dx,ny=int(y)+dy;if(nx>=0&&ny>=0&&nx<int(S)&&ny<int(S)&&footprint[ny*S+nx])return r;}return 4;};
      double variance[2][5]={},count[5]={},diff=0;
      for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){if(footprint[y*S+x])continue;const int r=distance(x,y);count[r]+=1;
          for(unsigned m=0;m<2;++m){double mean=0,sq=0;for(unsigned n=ringFrames-P;n<ringFrames;++n){const double v=px(stills[m].output[n],x,y);mean+=v/P;sq+=v*v/P;}variance[m][r]+=std::max(sq-mean*mean,0.);}
          for(unsigned n=0;n<ringFrames;++n)diff=std::max(diff,double(std::fabs(px(stills[0].output[n],x,y)-px(stills[1].output[n],x,y))));}
      for(unsigned m=0;m<2;++m)std::printf("SETA_RING mode=%s d1_variance=%.8f d2_variance=%.8f d3_variance=%.8f sky_variance=%.8f d1_px=%.0f d2_px=%.0f d3_px=%.0f\n",names[m],variance[m][1]/count[1],variance[m][2]/count[2],variance[m][3]/count[3],variance[m][4]/count[4],count[1],count[2],count[3]);
      metric("seta static ring: strict identical to loose on every sky pixel over two periods (no ring flicker from the strict term under a static camera)",diff,0,0);}
     // (h) Pan (the band term's camera-relative gate): the camera yaws 3 px/frame past the world-static square over the textured
     // sky from frame 0. On screen the square and the sky both move +3 px/frame: the sky through the camera path (clip_to_previous
     // = an NDC x translation of -6/S: the content at p was at p - 3 px), the square through its routed motion (vx = 3). The
     // routed correspondence of the square equals the camera path of a static point at its depth (relative displacement 0), so
     // under strict the 1-px border beside it keeps the geometry path and accumulates exactly as under loose; nothing in the
     // frame differs. adjacent_* is the border's deviation from the current sample (its accumulated coverage, > 0 in both modes).
     {const double vp=3;const unsigned panFrames=8;const float pan[16]={1,0,0,float(-2*vp/S),0,1,0,0,0,0,1,0,0,0,0,1};
      auto panned=[&](unsigned n){const double l=1.37+vp*n;return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,-double(n)*vp/S,true},{l,st2,l+8,st2+8,0,squareDepth,vp,0}};};
      EdgeRun pans[2];for(unsigned m=0;m<2;++m)pans[m]=edge_sequence(s,decoder,resolver,panned,sentinelFill,panFrames,true,true,"pan",m==1,pan);
      double diff=0,adjacent[2]={0,0},skyDiff=0;unsigned skyHistory=0;
      for(unsigned n=1;n<panFrames;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){diff=std::max(diff,double(std::fabs(px(pans[0].output[n],x,y)-px(pans[1].output[n],x,y))));
          bool beside=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)beside|=px(pans[0].depth[n],x+dx,y+dy)==squareDepth;
          const bool sky=px(pans[0].depth[n],x,y)==-1.f;
          if(beside&&sky)for(unsigned m=0;m<2;++m)adjacent[m]=std::max(adjacent[m],double(std::fabs(px(pans[m].output[n],x,y)-px(pans[m].current[n],x,y))));
          if(!beside&&sky&&x>=4&&x+4<S){skyDiff=std::max(skyDiff,double(std::fabs(px(pans[1].output[n],x,y)-px(pans[1].current[n],x,y))));if(pans[1].output[n][(y*S+x)*4]!=pans[1].current[n][(y*S+x)*4])++skyHistory;}}
      std::printf("SETA_PAN velocity_px=%.2f adjacent_loose=%.6f adjacent_strict=%.6f strict_vs_loose=%.6f sky_max_deviation=%.6f sky_px_with_history=%u\n",vp,adjacent[0],adjacent[1],diff,skyDiff,skyHistory);
      require(adjacent[1]>.05,"pan: the border beside the panned square accumulates under strict (its output differs from the current sample)");
      metric("seta pan (camera 3 px/frame past a static square): strict identical to loose on every pixel (the band's camera-relative displacement is 0)",diff,0,0);}
     // (i) Fade-band draw beside a fast occluder (review finding: the alpha gate): a depth-writing square at depth 0.5 sweeping
     // +5 px/frame along the top edge of a static fade-band strip (RT1 alpha 1 with depth target 0.9997, RT2 masked) over the sky.
     // The strip's pixels next to the occluder dilate to it (band candidates with 5 px/frame of parallax) but are routed (their
     // own alpha is 1): the band term must leave them on the geometry path they take today, strict identical to loose there.
     {const unsigned fadeFrames=8;
      auto occluded=[&](unsigned n){const double l=1.37+5.*n;return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,0,true},{l,4.37,l+6,10.37,0,.5f,5,0},[&]{EdgeObject o{2.37,10.37,26.37,18.37,.6f,squareDepth,0,0};o.noDepth=true;return o;}()};};
      EdgeRun fades2[2];for(unsigned m=0;m<2;++m)fades2[m]=edge_sequence(s,decoder,resolver,occluded,sentinelFill,fadeFrames,true,true,"fade-band beside occluder",m==1);
      // The oracle is the refusal itself, not bit-identity: the strip pixels beside the occluder take its 5 px/frame motion, so their
      // taps land on sky pixels the band term refused a frame earlier (their history differs between the modes by design); without
      // the alpha gate every one of them would be current-only under strict. Neither mode may leave one current-only.
      double routedDiff=0;unsigned routed=0,besideCount=0,currentOnly[2]={0,0};
      for(unsigned n=1;n<fadeFrames;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){const std::size_t i=y*S+x;
          const bool own=fades2[1].motion[n][i*4+3]==1&&fades2[1].depth[n][i*4]==-1.f;if(!own)continue;++routed;
          bool beside=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)beside|=px(fades2[1].depth[n],x+dx,y+dy)==.5f;
          routedDiff=std::max(routedDiff,double(std::fabs(px(fades2[0].output[n],x,y)-px(fades2[1].output[n],x,y))));
          if(!beside)continue;
          ++besideCount;
          for(unsigned m=0;m<2;++m)currentOnly[m]+=fades2[m].output[n][i*4]==fades2[m].current[n][i*4]&&fades2[m].output[n][i*4+1]==fades2[m].current[n][i*4+1]&&fades2[m].output[n][i*4+2]==fades2[m].current[n][i*4+2];}
      std::printf("SETA_FADE_OCCLUDER routed_fade_px=%u beside_occluder_px=%u beside_current_only_loose=%u beside_current_only_strict=%u strict_vs_loose_on_routed=%.6f\n",routed,besideCount,currentOnly[0],currentOnly[1],routedDiff);
      // Loose leaves some of them current-only through the existing rules (their 5 px/frame taps beside the occluder); the gate's
      // oracle is that strict adds none, over at least ten pixels a missing gate would have refused.
      require(besideCount>=20&&besideCount-currentOnly[0]>=10,"fade-band strip has pixels beside the sweeping occluder every frame and loose keeps history on ten or more");
      metric("seta fade-band beside a 5 px/frame occluder: strict leaves no routed fade-band pixel beside it current-only that loose accumulates (alpha gate of the band term)",double(currentOnly[1])-double(currentOnly[0]),0,0);}
     // (j) Projective pan (review finding: the affine pan of (h) is depth-independent by construction): the route's far-plane
     // matrix for a yaw, P R P^-1 with the z row = w row x (1 - 2^-16), through the projective divide at the band's depth. The
     // yaw moves the screen centre 3 px/frame; the square (static in the world) follows the exact inverse mapping of its centre
     // each frame and its motion vector carries that displacement, the sky texture the centre's 3 px. Strict identical to loose.
     // Focal f = 16 (a narrow view: the game's 1280-px frame at f ~ 1.7 has the same order of projective non-uniformity per pixel of
     // pan): x' = (c x + f s) / (c - s x / f), y' = y / (c - s x / f); the yaw that moves the centre 3 px is 0.67 degrees and the
     // displacement varies by < 0.02 px across the 8-px square, so its uniform motion vector is the projective path to that error.
     {const unsigned panFrames=8;const double f=16,theta=std::atan(2.*3/(S*f)),c=std::cos(theta),sn=-std::sin(theta),k=1.-std::ldexp(1.,-16);
      const float proj[16]={float(c),0,0,float(f*sn),0,1,0,0,float(-sn/f*k),0,0,float(c*k),float(-sn/f),0,0,float(c)};
      std::vector<double> centres(panFrames);centres[0]=1.37+4;for(unsigned n=1;n<panFrames;++n){const double xp=(centres[n-1]+.5)*2/S-1;const double xn=(xp*c-f*sn)/(c+xp*sn/f);centres[n]=(xn+1)*S/2-.5;}
      auto projected=[&](unsigned n){const double l=centres[n]-4,v=n?centres[n]-centres[n-1]:0;return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,-double(n)*3/S,true},{l,st2,l+8,st2+8,0,squareDepth,v,0}};};
      EdgeRun projs[2];for(unsigned m=0;m<2;++m)projs[m]=edge_sequence(s,decoder,resolver,projected,sentinelFill,panFrames,true,true,"projective pan",m==1,proj);
      double diff=0,adjacent[2]={0,0};
      for(unsigned n=1;n<panFrames;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){diff=std::max(diff,double(std::fabs(px(projs[0].output[n],x,y)-px(projs[1].output[n],x,y))));
          bool beside=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)beside|=px(projs[0].depth[n],x+dx,y+dy)==squareDepth;
          if(beside&&px(projs[0].depth[n],x,y)==-1.f)for(unsigned m=0;m<2;++m)adjacent[m]=std::max(adjacent[m],double(std::fabs(px(projs[m].output[n],x,y)-px(projs[m].current[n],x,y))));}
      std::printf("SETA_PAN_PROJECTIVE focal=16 yaw_deg=%.4f centre_step_px=%.4f adjacent_loose=%.6f adjacent_strict=%.6f strict_vs_loose=%.6f\n",theta*180/3.14159265358979,centres[1]-centres[0],adjacent[0],adjacent[1],diff);
      require(adjacent[1]>.05,"projective pan: the border beside the panned square accumulates under strict");
      metric("seta projective pan (yaw 3 px/frame at the centre, focal 16, far-plane matrix form): strict identical to loose on every pixel",diff,0,0);}
     // (k) Camera path off the scale (review finding: the select's failure mode): the (h) pan with the x row scaled to 1e15, the
     // largest magnitude prepare accepts. Every own-path sky pixel lands outside the texture (current-only in both modes); the
     // band's camera reference is ~1e16 px away, finite, so the relative displacement is far above the threshold: under strict
     // the band refuses (fail closed: equal to the current sample), no output is NaN or infinite. A NaN camera path is unreachable
     // (prepare bounds the rows and every operand is finite).
     {const unsigned panFrames=8;const double vp=3;const float huge[16]={1e15f,0,0,float(-2*vp/S),0,1,0,0,0,0,1,0,0,0,0,1};
      auto panned2=[&](unsigned n){const double l=1.37+vp*n;return std::vector<EdgeObject>{{0,0,S,S,0,-1.f,0,0,true,-double(n)*vp/S,true},{l,st2,l+8,st2+8,0,squareDepth,vp,0}};};
      EdgeRun huges[2];for(unsigned m=0;m<2;++m)huges[m]=edge_sequence(s,decoder,resolver,panned2,sentinelFill,panFrames,true,true,"huge camera path",m==1,huge);
      double bandStrict=0,bandLoose=0;unsigned nonfinite=0,bandPx=0;
      for(unsigned n=1;n<panFrames;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){for(unsigned m=0;m<2;++m)for(UINT ch=0;ch<4;++ch)nonfinite+=!std::isfinite(huges[m].output[n][(y*S+x)*4+ch]);
          bool beside=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)beside|=px(huges[0].depth[n],x+dx,y+dy)==squareDepth;
          if(beside&&px(huges[0].depth[n],x,y)==-1.f){++bandPx;bandStrict=std::max(bandStrict,double(std::fabs(px(huges[1].output[n],x,y)-px(huges[1].current[n],x,y))));bandLoose=std::max(bandLoose,double(std::fabs(px(huges[0].output[n],x,y)-px(huges[0].current[n],x,y))));}}
      std::printf("SETA_HUGE_CAMERA band_px=%u band_deviation_loose=%.6f band_deviation_strict=%.6f nonfinite_outputs=%u\n",bandPx,bandLoose,bandStrict,nonfinite);
      require(bandPx>=20&&bandLoose>.05,"huge camera path: the band accumulates under loose (the routed path is unaffected)");
      metric("seta huge camera path (rows at 1e15): the band is current-only under strict (fail closed) and no output is nonfinite",bandStrict+nonfinite,0,0);}
     // (l) Exit reset (docs/architecture/seta-sky-hull-share-decay.md section 6), the age program (thin clip 0.7, WMAX 0.9: the
     // mark lives in the age target) under strict + band 3 with the exit floor 0.25 px/frame against strict alone. An 8x8 square
     // of colour (0, 0, 0.5) at depth 0.9997, routed, static for 16 frames, then +0.5 px/frame for 20 (motion = the displacement;
     // camera static: the band's parallax is 0.5) over a sky whose texel (x, y) on frame n is 1 when (x + y + n) % 3 == 0 and 0
     // otherwise, colour only (the sentinel fill under it). Every 3x3 holds all three residues, so the clip box is [0, 1] on every
     // sky pixel and never pulls a dark history (the run249 mechanism), and a sky-only history keeps B == R exactly (identical
     // channels through identical arithmetic): any hull share shows as B - R > 0 (the "cast"). Trail: a strict-sky pixel (sky, no
     // square in its 3x3) that was in the band (sky beside the square) on the previous moving frame, followed while it stays
     // strict sky. Yaw variants: the camera path an NDC x translation of -2 yaw / S (the sky content moves yaw px/frame on screen,
     // the world-static square yaw + 0.5 through its motion vector, so the band's parallax stays 0.5), which makes the strict
     // sky's history footprint the fractional Catmull-Rom one (f = 0.7 for +0.3, 0.3 for -0.3): the re-import question of the note
     // (its section 7, item 3), reported as trail_cast_max. The slow variant (0.125 px/frame, below the floor) must write no mark;
     // loose with the floor uploaded must be bit-identical to loose without it (the pass uploads the off value).
     {const unsigned exitFrames=36,exitMove=16;
      struct ExitRun{EdgeRun run;double yaw,v;bool strict;float exit;const char* name;unsigned program=0;};
      auto exitScene=[&](double sl,double v,double yaw){return [=](unsigned n){const double l=sl+v*(n>=exitMove?n-exitMove+1:0)+yaw*n;
          EdgeObject sky{0,0,S,S,0,-1.f,0,0,false,0,true};sky.pattern=true;sky.phase=float(n);
          EdgeObject square{l,st2,l+8,st2+8,0,squareDepth,(n>=exitMove?v:0.)+yaw,0};square.valueB=.5f;
          return std::vector<EdgeObject>{sky,square};};};
      auto exitSequence=[&](double sl,double v,double yaw,bool strict,float exit,const char* name,unsigned program=0){
          const float path[16]={1,0,0,float(-2*yaw/S),0,1,0,0,0,0,1,0,0,0,0,1};
          return ExitRun{edge_sequence(s,decoder,resolver,exitScene(sl,v,yaw),sentinelFill,exitFrames,true,true,"exit reset",strict,yaw!=0?path:nullptr,program?0.f:.9f,exit,program),yaw,v,strict,exit,name,program};};
      struct ExitNumbers{unsigned fresh=0,freshCurrent=0,negTotal=0,negStatic=0,negNotBand=0,bandPositive=0,hullReads=0,hullOwn=0,hullUnexplained=0;double trailCast=0,controlCast=0,controlDiff=0,diff=0,ageDiff=0;};
      auto exitNumbers=[&](const ExitRun& r,const ExitRun* other){const EdgeRun& run=r.run;ExitNumbers m;require(run.age.size()==exitFrames,"exit reset: the age target is read every frame");
          auto sky=[&](unsigned n,UINT x,UINT y){return px(run.depth[n],x,y)==-1.f;};
          auto beside=[&](unsigned n,UINT x,UINT y){bool b=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)b|=px(run.depth[n],x+dx,y+dy)==squareDepth;return b;};
          std::vector<unsigned char> footprint(S*S,0),trail(S*S,0);for(unsigned n=0;n<exitFrames;++n)for(UINT i=0;i<S*S;++i)if(run.depth[n][i*4]==squareDepth)footprint[i]=1;
          auto control=[&](UINT x,UINT y){for(int dy=-7;dy<=7;++dy)for(int dx=-7;dx<=7;++dx){const int nx=int(x)+dx,ny=int(y)+dy;if(nx>=0&&ny>=0&&nx<int(S)&&ny<int(S)&&footprint[ny*S+nx])return false;}return true;};
          for(unsigned n=1;n<exitFrames;++n){std::vector<unsigned char> next(S*S,0);
              // The count is floor(|age|) with the sign the exit mark: the far_camera program (the camera gate's A' resolve) carries
              // its region and closure holds in the fraction (resolve.hlsl X3M_REGION_HOLD); every other program writes whole counts.
              auto whole=[](float v){return std::copysign(std::floor(std::fabs(v)),v);};
              for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){const std::size_t i=y*S+x;const float age=whole(run.age[n][i*4]);const bool isSky=sky(n,x,y),isBeside=isSky&&beside(n,x,y),strictSky=isSky&&!isBeside;
                  const double cast=std::fabs(px(run.output[n],x,y,2)-px(run.output[n],x,y,0));
                  if(age<0){++m.negTotal;if(n<exitMove)++m.negStatic;if(!isBeside)++m.negNotBand;}
                  if(isBeside&&n>=exitMove&&age>=2)++m.bandPositive;
                  if(n>exitMove&&strictSky){const bool wasBeside=sky(n-1,x,y)&&beside(n-1,x,y);
                      if(wasBeside){++m.fresh;next[i]=1;if(run.output[n][i*4]==run.current[n][i*4]&&run.output[n][i*4+1]==run.current[n][i*4+1]&&run.output[n][i*4+2]==run.current[n][i*4+2])++m.freshCurrent;}
                      else if(trail[i])next[i]=1;
                      if(next[i])m.trailCast=std::max(m.trailCast,cast);}
                  if(isSky&&control(x,y)){m.controlCast=std::max(m.controlCast,cast);if(other)m.controlDiff=std::max(m.controlDiff,double(std::fabs(px(run.output[n],x,y)-px(other->run.output[n],x,y))));}
                  if(other)for(UINT c=0;c<3;++c)m.diff=std::max(m.diff,double(std::fabs(run.output[n][i*4+c]-other->run.output[n][i*4+c])));
                  if(other)m.ageDiff=std::max(m.ageDiff,double(std::fabs(run.age[n][i*4]-other->run.age[n][i*4])));
                  // A hull pixel whose own age texel was marked last frame continues the count through the sign (abs): the value
                  // written is |own| + 1 (or the x-1 texel's when the half-pixel footprint rounds that way), never a restart.
                  if(px(run.depth[n],x,y)==squareDepth&&run.age[n-1][i*4]<0){++m.hullReads;const float own=std::fabs(whole(run.age[n-1][i*4])),left=std::fabs(whole(run.age[n-1][i*4-4]));
                      if(age==own+1)++m.hullOwn;else if(age!=1&&age!=left+1)++m.hullUnexplained;}}
              trail.swap(next);}
          std::printf("SETA_EXIT mode=%s program=%s strict=%u exit_px=%.3f yaw_px=%.2f velocity_px=%.3f fresh_px=%u fresh_current_only=%u trail_cast_max=%.6f control_cast_max=%.6f control_diff=%.6f negative_px=%u negative_static=%u negative_not_band=%u band_blend_positive=%u hull_mark_reads=%u hull_mark_own=%u hull_mark_unexplained=%u output_diff=%.6f age_diff=%.6f\n",r.name,r.program==2?"far_camera":r.program?"far":"age",unsigned(r.strict),double(r.exit),r.yaw,r.v,m.fresh,m.freshCurrent,m.trailCast,m.controlCast,m.controlDiff,m.negTotal,m.negStatic,m.negNotBand,m.bandPositive,m.hullReads,m.hullOwn,m.hullUnexplained,m.diff,m.ageDiff);
          return m;};
      const ExitRun off=exitSequence(1.37,.5,0,true,0,"straight_off"),on=exitSequence(1.37,.5,0,true,.25f,"straight_on");
      const ExitNumbers mOff=exitNumbers(off,nullptr),mOn=exitNumbers(on,&off);
      // Exit off (today's strict): the trail carries the hull share (cast > 0.05 somewhere), no age is ever negative.
      require(mOff.fresh>=20&&mOff.trailCast>.05&&mOff.negTotal==0,"exit reset control: strict alone leaves the hull share in the trail and writes no negative age");
      metric("exit reset: every trail pixel is current-only on its first strict-sky frame after a band frame (fresh_current_only - fresh_px)",double(mOn.freshCurrent)-double(mOn.fresh),0,0);
      metric("exit reset: the trail carries no hull share afterwards (max B - R over trail pixels and frames, sky-only history is B == R)",mOn.trailCast,0,0);
      metric("exit reset: negative ages only on band pixels of moving frames (negative_static + negative_not_band)",double(mOn.negStatic)+double(mOn.negNotBand),0,0);
      metric("exit reset: every band pixel on the blend path of a moving frame is marked (band_blend_positive)",double(mOn.bandPositive),0,0);
      metric("exit reset: control sky (8+ px from the square) identical to strict without the floor",mOn.controlDiff,0,0);
      metric("exit reset: a hull pixel reading a marked texel continues the count through the sign (hull_mark_unexplained)",double(mOn.hullUnexplained),0,0);
      require(mOn.negTotal>=20&&mOn.hullReads>=1&&mOn.hullOwn>=1,"exit reset: marks are written and at least one hull pixel read its own marked texel and continued the count");
      // Below the floor (parallax 0.125 < 0.25): no mark, whatever the band does.
      const ExitNumbers mSlow=exitNumbers(exitSequence(1.37,.125,0,true,.25f,"slow_on"),nullptr);
      metric("exit reset: below the floor (0.125 px/frame against 0.25) no negative age is written",double(mSlow.negTotal),0,0);
      // Yaw variants (note section 7, item 3): the reset itself is unchanged (the nearest age texel is the pixel's own under
      // 0.3 px/frame); the re-import through the fractional Catmull-Rom footprint is trail_cast_max, reported for both signs.
      for(const double yaw:{.3,-.3}){const double sl=yaw>0?1.37:12.37;
          const ExitRun yawOff=exitSequence(sl,.5,yaw,true,0,yaw>0?"yaw_plus_off":"yaw_minus_off"),yawOn=exitSequence(sl,.5,yaw,true,.25f,yaw>0?"yaw_plus_on":"yaw_minus_on");
          const ExitNumbers yOff=exitNumbers(yawOff,nullptr),yOn=exitNumbers(yawOn,&yawOff);
          require(yOff.fresh>=20&&yOff.trailCast>.05&&yOff.negTotal==0,"exit reset yaw control: strict alone leaves the hull share in the trail");
          const std::string prefix=std::string("exit reset, camera yaw ")+(yaw>0?"+":"-")+"0.3 px/frame: ";
          metric((prefix+"every trail pixel is current-only on its first strict-sky frame (fresh_current_only - fresh_px)").c_str(),double(yOn.freshCurrent)-double(yOn.fresh),0,0);
          metric((prefix+"negative ages only on band pixels of moving frames").c_str(),double(yOn.negStatic)+double(yOn.negNotBand),0,0);
          metric((prefix+"a hull pixel reading a marked texel continues the count (hull_mark_unexplained)").c_str(),double(yOn.hullUnexplained),0,0);}
      // The far and far-camera programs (the flown configuration carries the term in far_camera): the same rows with the exit on,
      // straight and both yaw signs; the off-side is the adaptive rows' (the term is one shared source).
      for(unsigned program:{1u,2u}){const char* pn=program==2?"far_camera":"far";
          for(const double yaw:{0.,.3,-.3}){const double sl=yaw<0?12.37:1.37;const std::string name=std::string(pn)+(yaw>0?"_yaw_plus_on":yaw<0?"_yaw_minus_on":"_straight_on");
              const ExitNumbers f=exitNumbers(exitSequence(sl,.5,yaw,true,.25f,name.c_str(),program),nullptr);
              const std::string prefix=std::string("exit reset, ")+pn+" program"+(yaw>0?", camera yaw +0.3 px/frame":yaw<0?", camera yaw -0.3 px/frame":"")+": ";
              metric((prefix+"every trail pixel is current-only on its first strict-sky frame (fresh_current_only - fresh_px)").c_str(),double(f.freshCurrent)-double(f.fresh),0,0);
              if(yaw==0){metric((prefix+"the trail carries no hull share afterwards (max B - R over trail pixels and frames)").c_str(),f.trailCast,0,0);
                  metric((prefix+"every band pixel on the blend path of a moving frame is marked (band_blend_positive)").c_str(),double(f.bandPositive),0,0);
                  require(f.negTotal>=20&&f.hullReads>=1&&f.hullOwn>=1,"exit reset on the far programs: marks are written and a hull pixel continued its count through its own marked texel");}
              metric((prefix+"negative ages only on band pixels of moving frames").c_str(),double(f.negStatic)+double(f.negNotBand),0,0);
              metric((prefix+"a hull pixel reading a marked texel continues the count (hull_mark_unexplained)").c_str(),double(f.hullUnexplained),0,0);}}
      // Loose with the floor uploaded: the pass forces the off value; both targets bit-identical to loose without it.
      const ExitRun looseOff=exitSequence(1.37,.5,0,false,0,"loose_off"),looseOn=exitSequence(1.37,.5,0,false,.25f,"loose_on");
      const ExitNumbers mLoose=exitNumbers(looseOn,&looseOff);
      metric("exit reset: loose with the floor uploaded is bit-identical to loose without it on both targets, no negative age",mLoose.diff+mLoose.ageDiff+double(mLoose.negTotal),0,0);
      // (m) Streak over sky (docs/architecture/fog-dust-motes.md section 5.4): a dust mote's capsule is unrouted (drawn colour-only
      // after the fog: no motion, no depth), 5 px across and 24 px long, advancing 8 px/frame, on the flown configuration
      // (far_camera, strict sky, exit 0.25, sentinel stabiliser 0.7), over (l)'s flickering sky and over a dark uniform sky.
      // Asserted: no negative age anywhere; the sky 8+ rows from the streak identical to the run without it; on the dark sky no
      // trail beyond 3 px behind the tail (|out - control| <= .05 M). Second row: the streak across (l)'s moving hull and its
      // band writes no mark of its own (negative ages equal to the run without the streak). Reported (not gated): the trail
      // on the flickering sky and output/current on the overlapped and leading segments.
      // M 2 lies above the stabiliser's emitter bound (E 1: the box shrinks to the inner 3x3 beside it); M 0.8 below it.
      {const unsigned streakFrames=34;const double streakLen=24,streakStep=8;
       auto streakAt=[=](unsigned n,unsigned enter){return -streakLen+streakStep*(double(n)-double(enter));}; // left edge
       auto streakScene=[=](bool flicker,bool streak,bool hull,double top,unsigned enter,float M){return [=](unsigned n){
           EdgeObject sky{0,0,S,S,flicker?0.f:.02f,-1.f,0,0,false,0,true};sky.pattern=flicker;sky.phase=float(n);
           std::vector<EdgeObject> out{sky};
           // The sequence's motion contract needs a routed object: a static corner square, or (l)'s hull moving 0.5 px/frame.
           if(hull){const double l=1.37+.5*(n>=exitMove?n-exitMove+1:0);EdgeObject sq{l,st2,l+8,st2+8,0,squareDepth,(n>=exitMove?.5:0.),0};sq.valueB=.5f;out.push_back(sq);}
           else out.push_back(EdgeObject{1,1,3,3,.5f,squareDepth,0,0});
           const double l=streakAt(n,enter);
           if(streak&&l<S&&l+streakLen>0)out.push_back(EdgeObject{std::max(l,0.),top,std::min(l+streakLen,double(S)),top+5,M,-1.f,0,0,false,0,true});
           return out;};};
       auto streakRun=[&](bool flicker,bool streak,bool hull,double top,unsigned enter,float M=2.f){
           return edge_sequence(s,decoder,resolver,streakScene(flicker,streak,hull,top,enter,M),sentinelFill,streakFrames,true,true,"mote streak",true,nullptr,0.f,.25f,2);};
       struct StreakNumbers{unsigned negative=0,trailPx=0,overlapN=0,leadingN=0;double control=0,trail=0,overlap=0,leading=0;};
       auto streakNumbers=[&](const EdgeRun& on,const EdgeRun& off,double top,unsigned enter,const char* name,float M=2.f){StreakNumbers m;
           require(on.age.size()==streakFrames&&off.age.size()==streakFrames,"mote streak: the age target is read every frame");
           for(unsigned n=0;n<streakFrames;++n){const double l=streakAt(n,enter),lp=streakAt(n-1,enter);
               for(UINT i=0;i<S*S;++i)m.negative+=on.age[n][i*4]<0;
               for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const double d=std::fabs(px(on.output[n],x,y)-px(off.output[n],x,y)),behind=l-(x+.5);
                   if(y+8<top||y>=top+5+8)m.control=std::max(m.control,d);
                   if(y>=top&&y<top+5&&n>=enter&&behind>3){m.trail=std::max(m.trail,d);if(d>.05*M)m.trailPx=std::max(m.trailPx,unsigned(std::ceil(behind)));}}
               // Output over current on the centre row once the streak is wholly on screen: the overlapped segment [l, lp + 24) was
               // streak last frame too, the leading one [lp + 24, l + 24) was sky (history from the sky, pulled up by the clip).
               if(n>0&&l>=0&&l+streakLen<=S&&lp>=-streakLen)for(UINT x=0;x<S;++x){const double c=x+.5;const UINT y=UINT(top)+2;const double r=px(on.output[n],x,y)/px(on.current[n],x,y);
                   if(c>l+1&&c<lp+streakLen-1){m.overlap+=r;++m.overlapN;}else if(c>lp+streakLen+1&&c<l+streakLen-1){m.leading+=r;++m.leadingN;}}}
           m.overlap/=std::max(m.overlapN,1u);m.leading/=std::max(m.leadingN,1u);
           std::printf("MOTE_STREAK sky=%s value=%.2f frames=%u negative_px=%u control_diff=%.6f trail_beyond_3px_max=%.6f trail_px=%u overlap_output_over_current=%.4f overlap_px=%u leading_output_over_current=%.4f leading_px=%u\n",
                       name,double(M),streakFrames,m.negative,m.control,m.trail,m.trailPx,m.overlap,m.overlapN,m.leading,m.leadingN);
           return m;};
       const EdgeRun flickerSky=streakRun(true,false,false,13,12),darkSky=streakRun(false,false,false,13,12);
       const StreakNumbers flick=streakNumbers(streakRun(true,true,false,13,12),flickerSky,13,12,"flicker");
       const StreakNumbers dark=streakNumbers(streakRun(false,true,false,13,12),darkSky,13,12,"dark");
       const StreakNumbers dim=streakNumbers(streakRun(false,true,false,13,12,.8f),darkSky,13,12,"dark",.8f);
       metric("mote streak over the flickering sky: no negative age (the unrouted streak writes no exit mark)",double(flick.negative),0,0);
       metric("mote streak over the flickering sky: the sky 8+ rows from the streak identical to the run without it",flick.control,0,0);
       for(const StreakNumbers* m:{&dark,&dim}){const std::string prefix=std::string("mote streak (value ")+(m==&dark?"2, above":"0.8, below")+" the emitter bound) over a dark sky: ";
           metric((prefix+"no negative age").c_str(),double(m->negative),0,0);
           metric((prefix+"the sky 8+ rows from the streak identical to the run without it").c_str(),m->control,0,0);
           metric((prefix+"no trail beyond 3 px behind the tail (max |out - control| over 0.05 M)").c_str(),m->trail/(.05*(m==&dark?2.:.8)),0,1);}
       // The hull row: the streak crosses (l)'s band while the hull moves 0.5 px/frame (frames 16..33); it enters at 18.
       const EdgeRun hullAlone=streakRun(true,false,true,st2+2,18);
       const StreakNumbers hull=streakNumbers(streakRun(true,true,true,st2+2,18),hullAlone,st2+2,18,"flicker_hull");
       unsigned alone=0;for(const auto& age:hullAlone.age)for(UINT i=0;i<S*S;++i)alone+=age[i*4]<0;
       std::printf("MOTE_STREAK_HULL marks_with_streak=%u marks_without=%u\n",hull.negative,alone);
       metric("mote streak across a moving hull's band: the hull's marks equal the run without the streak",double(hull.negative)-double(alone),0,0);
       require(alone>=20,"mote streak hull row: the moving hull writes its band marks");}}}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
// ---- run 139: 1-px jittered lattice and the history weight ----
// docs/verification/motion-output.md, "Run 139": a static lattice of 1-px
// bright lines (pitch 2 px, fractional position) over a routed far background
// is rasterized with the session's 8-sample Halton jitter, so every lattice
// pixel toggles between line and background with the phase. Flicker metric as
// in the ledger: half the temporal second difference, mean over the lattice
// pixels, last period. (The filtered current sample of that run,
// resolve_filter.hlsl, was removed 2026-09-23, cleanup batch 6; the history
// weight rows stay.)
struct LatticeConfig { const char* name; float weight,k; };
constexpr unsigned latticePhases=8;
template<class Objects> EdgeRun lattice_sequence(EdgeScene& s,const DWORD* resolver,Objects objects,unsigned frames,const LatticeConfig& c){
    constexpr UINT S=EdgeScene::S;TemporalPass pass;check("lattice initialize",pass.initialize(s.d,nullptr,resolver));EdgeRun run;const EdgeBackground bg{.25f,.9f,1};
    for(unsigned n=0;n<frames;++n){const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(objects(n),bg,jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));
        FrameInputs in;in.color=s.color.p;in.current_depth=s.depth32.p;in.motion=s.motion.p;in.width=S;in.height=S;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=c.weight;in.luminance_k=c.k;in.motion_policy=MotionPolicy::PerPixel;
        in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        Output out;check("lattice Begin resolve",s.d->BeginScene());check(c.name,pass.run(in,&out));check("lattice End resolve",s.d->EndScene());
        require(out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0),"lattice history follows the sequence");
        run.output.push_back(s.read(out.color));}
    return run;}
// CPU model of the static resolve at k = 0: the blend's current colour is the
// point sample, the history is the same texel clipped to the 3x3 statistics,
// FP16 rounding per frame.
std::vector<std::vector<float>> lattice_model(const EdgeRun& run,double w){constexpr UINT S=EdgeScene::S;std::vector<std::vector<float>> out(run.current.size());
    for(unsigned n=0;n<run.current.size();++n){out[n]=run.current[n];if(!n)continue;
        for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){const double cur=px(run.current[n],x,y);double lo=cur,hi=cur,m1=0,m2=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const double q=px(run.current[n],x+dx,y+dy);lo=std::min(lo,q);hi=std::max(hi,q);m1+=q/9;m2+=q*q/9;}
            const double sigma=std::sqrt(std::max(m2-m1*m1,0.));lo=std::max(lo,m1-1.25*sigma);hi=std::min(hi,m1+1.25*sigma);
            const double old=std::min(std::max(double(px(out[n-1],x,y)),lo),hi);const float v=halfFloat(toHalf(float(cur+w*(old-cur))));
            for(UINT ch=0;ch<3;++ch)out[n][(y*S+x)*4+ch]=v;}}
    return out;}
// (m) Motion history weight (docs/architecture/taa-motion-history-weight.md section 6): a routed hull (alpha 1, depth 0.9997,
// exact motion vectors) textured with a period-4 sinusoidal stripe (0.25..1, point-sampled at the jittered raster position:
// the sample at p shows content at p - jitter - displacement), static 16 frames then translating for 32 frames under a static
// camera path (relative = the displacement), on the far-camera program with the flown settings (weight 0.9, thin region 0.97
// under the camera gate, far 0.985, strict + band 3 + exit 0.25) and on the age program (thin clip 0.7, WMAX 0.9), with the
// option 0.8,2,8 against off. The hull fills a 512x16 strip (the 32-px edge scene cannot hold a fast history: a pixel's chain
// is only S / v frames long), read at x >= 386, where every chain reaches back through all 32 moving frames into the static
// ones. Rows: rest, 1 / 5 / 12 px/frame (integer: the history tap lands on the texel grid, no resampling), 1.5 / 5.5 / 6.5 /
// 12.5 (half-texel: the Catmull-Rom resample softening the option is for; 6.5 is where the ramp binds, cap 0.8725 below the
// 0.9 base), a pan (camera yaw 12.5 px/frame with the hull world-static: its screen motion is the pan's, relative 0) and a
// co-moving hull (camera yaw 12.5 px/frame with the hull screen-static: screen motion 0, relative the yaw). Per row and mode:
// the interior Laplacian energy of the resolve over the current jittered sample (E ratio, the run254 triage's measure), the
// best Gaussian sigma on {0, .35, .5, .7, 1, 1.4, 2} against the unjittered stripe (a sinusoid blurs to its amplitude times
// exp(-2 pi^2 sigma^2 / 16); the continuous sigma_fit beside it), the mean |age|, the rms of output[n](x) - output[n - k](x -
// k v) for the smallest k with an integer k v (the flicker witness in the content frame), all over the last 16 frames, and
// the largest difference against the off run on both targets (R channel; the stripe is grey and the blend per channel).
// Asserted: rest, 1, 1.5, the pan and the co-moving rows bit-identical to off (cap 1 at or below 2 px/frame, 0 under a pan,
// 0 on a co-moving hull); the age target identical on every row (the cap changes the weight, never the count); 5 / 5.5 E
// ratio not below off's (cap 0.93 / 0.91 above the 0.9 base); 6.5 within 0.002 of a run whose cap is the constant 0.8725
// (F 0.8725, V0 0, V1 0.5: the ramp's A and B are checked at a binding point, a wrong B fails) and at least 0.01 from off;
// 12.5 E ratio at least 1.5x off's and its ripple at most 2.5x off's (the first run measured 2.06x).
void motion_weight_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("MOTION_WEIGHT_CASES");constexpr UINT W=512,H=16,P=16;constexpr unsigned frames=48,moveFrom=16;constexpr UINT X0=386,X1=W-3,Y0=2,Y1=H-2;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer; // every row prints before a failed metric throws
    constexpr float hullDepth=.9997f;constexpr double mean=.625,amp=.375,period=4,pi=3.14159265358979;
    EdgeScene s(d,compiler); // the edge scene's state setup, flat shader and quad; its own 32x32 targets are not used
    Com<IDirect3DTexture9> color,depth,motion;Com<IDirect3DSurface9> colorSurface,depthSurface,motionSurface;
    check("mw color",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&color.p,nullptr));check("mw color surface",color->GetSurfaceLevel(0,&colorSurface.p));
    check("mw depth",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));check("mw depth surface",depth->GetSurfaceLevel(0,&depthSurface.p));
    check("mw motion",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));check("mw motion surface",motion->GetSurfaceLevel(0,&motionSurface.p));
    Com<ID3DXBuffer> a,b;Com<IDirect3DPixelShader9> stripePS,motionPS;
    compile(compiler,"float4 c:register(c0);float4 main(float2 vpos:VPOS):COLOR0{float v=c.y+c.z*sin(6.28318530718*(floor(vpos.x)-c.x)/c.w);return float4(v,v,v,1);}","ps_3_0",&a.p);
    compile(compiler,"float4 j:register(c0);float4 k:register(c1);float4 main(float2 vpos:VPOS):COLOR0{return float4((floor(vpos)+0.5-j.xy-j.zw)*k.xy,k.z,1);}","ps_3_0",&b.p);
    check("mw stripe PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&stripePS.p));check("mw motion PS",d->CreatePixelShader(static_cast<DWORD*>(b->GetBufferPointer()),&motionPS.p));
    auto target=[&](IDirect3DSurface9* rt,IDirect3DPixelShader9* ps){s.target(rt);D3DVIEWPORT9 vp{0,0,W,H,0,1};check("mw viewport",d->SetViewport(&vp));check("mw PS",d->SetPixelShader(ps));};
    auto read=[&](IDirect3DTexture9* texture){D3DSURFACE_DESC desc{};check("mw desc",texture->GetLevelDesc(0,&desc));Com<IDirect3DSurface9> level,sys;check("mw level",texture->GetSurfaceLevel(0,&level.p));
        check("mw readback surface",d->CreateOffscreenPlainSurface(W,H,desc.Format,D3DPOOL_SYSTEMMEM,&sys.p,nullptr));check("mw validation-only readback",d->GetRenderTargetData(level.p,sys.p));D3DLOCKED_RECT lock{};check("mw lock",sys->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<float> out(W*H);for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const char* p=static_cast<const char*>(lock.pBits)+y*lock.Pitch;float v;if(desc.Format==D3DFMT_R32F)std::memcpy(&v,p+x*4,4);else if(desc.Format==D3DFMT_A32B32G32R32F)std::memcpy(&v,p+x*16,4);else{unsigned short h;std::memcpy(&h,p+x*8,2);v=halfFloat(h);}out[y*W+x]=v;} // the R channel
        check("mw unlock",sys->UnlockRect());return out;};
    struct Run{std::vector<std::vector<float>> current,output,age;std::vector<double> d;};
    // program 0: the age program; 1: far_camera. v: the hull's translation in px/frame from frame 16; pan: a camera yaw of that
    // many px/frame from frame 16 with the hull world-static (its motion vector carries the pan, the far-plane path too).
    // triple: F, V0, V1 of the option (null: off). comove: the camera yaws pan px/frame but the hull stays screen-static.
    auto sequence=[&](unsigned program,double v,double pan,const float* triple,bool comove=false){TemporalPass pass;check("mw initialize",pass.initialize(d,decoder,resolver));
        if(program){check("mw configure far",pass.configure_far());require(pass.far_available()&&pass.camera_gate_available(),"mw far-camera program available");}
        else{check("mw configure flicker",pass.configure_flicker());require(pass.age_available(),"mw age program available");}
        Run run;double previous=0;
        for(unsigned n=0;n<frames;++n){const unsigned index=n%P+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
            const double disp=n>=moveFrom?(v+(comove?0:pan))*(n-moveFrom+1):0,vx=disp-previous;previous=disp;run.d.push_back(disp);
            target(colorSurface.p,stripePS.p);check("mw Begin",d->BeginScene());{const float c[4]={float(jx+disp),float(mean),float(amp),float(period)};check("mw stripe constant",d->SetPixelShaderConstantF(0,c,1));}s.quad(0,0,W,H,0,0);check("mw End",d->EndScene());
            target(motionSurface.p,motionPS.p);check("mw motion Begin",d->BeginScene());{const float j[4]={float(jx),float(jy),float(vx),0},k[4]={1.f/W,1.f/H,hullDepth,0};check("mw motion j",d->SetPixelShaderConstantF(0,j,1));check("mw motion k",d->SetPixelShaderConstantF(1,k,1));}s.quad(0,0,W,H,0,0);check("mw motion End",d->EndScene());
            target(depthSurface.p,s.flat.p);check("mw depth Begin",d->BeginScene());{const float c[4]={hullDepth,0,0,0};check("mw depth constant",d->SetPixelShaderConstantF(0,c,1));}s.quad(0,0,W,H,0,0);check("mw depth End",d->EndScene());
            run.current.push_back(read(color.p));
            if(n==0){const auto m=read(motion.p);double worst=0;for(UINT x=X0;x<X1;++x)worst=std::max(worst,std::fabs(m[(Y0*W+x)]-(x+.5-jx-vx)/W));require(worst<=1e-6,"mw motion target equals the producer contract");}
            FrameInputs in;in.color=color.p;in.current_depth=depth.p;in.motion=motion.p;in.width=W;in.height=H;in.epoch=1;
            const float path[16]={1,0,0,float(n>=moveFrom?-2*pan/W:0),0,1,0,0,0,0,1,0,0,0,0,1};std::copy(path,path+16,in.clip_to_previous);
            in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=.9f;in.motion_policy=MotionPolicy::PerPixel;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;
            in.sentinel_camera=true;in.sentinel_strict_sky=true;in.sky_history_exit_px=.25f;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
            if(program){in.far_weight=.985f;in.far_d0=.9995f;in.far_inv=1.f/(.9999f-.9995f);in.far_speed_lo=x3::temporal::kFarSpeedLo;in.far_speed_hi=x3::temporal::kFarSpeedHi;in.thin_region_weight=.97f;in.thin_region_relax=1;in.thin_region_camera_gate=true;}
            else{in.thin_clip=.7f;in.adaptive_weight=.9f;}
            if(triple){in.motion_weight=triple[0];in.motion_weight_v0=triple[1];in.motion_weight_v1=triple[2];}
            Output out;check("mw Begin resolve",d->BeginScene());check("mw resolve",pass.run(in,&out));check("mw End resolve",d->EndScene());
            require(out.color&&out.age&&pass.diagnostics().history_valid&&out.used_history==(n>0),"mw history follows the sequence");
            run.output.push_back(read(out.color));run.age.push_back(read(out.age));}
        return run;};
    struct Numbers{double eRatio=0,sigma=0,ampRatio=0,sigmaFit=0,age=0,ripple=0,diff=0,ageDiff=0;unsigned shiftFrames=0;};
    auto numbers=[&](const Run& r,const Run* off,double speed){Numbers m;double eOut=0,eCur=0,best=1e30,ageSum=0,rippleSum=0;unsigned ageCount=0,rippleCount=0;
        for(unsigned k=1;k<=4&&!m.shiftFrames;++k)if(std::fabs(k*speed-std::lround(k*speed))<1e-9)m.shiftFrames=k;
        const int shift=int(std::lround(m.shiftFrames*speed));
        for(unsigned n=frames-P;n<frames;++n)for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){const auto& o=r.output[n];const auto& c=r.current[n];const std::size_t i=y*W+x;
            auto lap=[&](const std::vector<float>& img){return 4.*img[i]-img[i-1]-img[i+1]-img[i-W]-img[i+W];};
            eOut+=lap(o)*lap(o);eCur+=lap(c)*lap(c);
            if(m.shiftFrames&&int(x)-shift>=1){const double dd=o[i]-r.output[n-m.shiftFrames][y*W+(int(x)-shift)];rippleSum+=dd*dd;++rippleCount;}}
        for(double sigma:{0.,.35,.5,.7,1.,1.4,2.}){double err=0;unsigned cnt=0;const double g=std::exp(-2*pi*pi*sigma*sigma/(period*period));
            for(unsigned n=frames-P;n<frames;++n)for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){const double e=r.output[n][y*W+x]-(mean+amp*g*std::sin(2*pi*(x-r.d[n])/period));err+=e*e;++cnt;}
            err=std::sqrt(err/cnt);if(err<best){best=err;m.sigma=sigma;}}
        // The stripe's amplitude in the output, phase-free (the least-squares projection on the sine and cosine of the stripe's
        // frequency at each frame's displacement, over the amplitude drawn): the grid sigma above steps x1.4, this is continuous.
        {double amplitude=0;for(unsigned n=frames-P;n<frames;++n){double cs=0,cc=0;unsigned cnt=0;for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){const double ph=2*pi*(x-r.d[n])/period,v=r.output[n][y*W+x]-mean;cs+=v*std::sin(ph);cc+=v*std::cos(ph);++cnt;}amplitude+=2*std::sqrt(cs*cs+cc*cc)/cnt/P;}
            m.ampRatio=amplitude/amp;m.sigmaFit=m.ampRatio<1?std::sqrt(-std::log(m.ampRatio)*period*period/(2*pi*pi)):0;}
        for(UINT y=Y0;y<Y1;++y)for(UINT x=X0;x<X1;++x){ageSum+=std::fabs(r.age[frames-1][y*W+x]);++ageCount;}
        m.eRatio=eOut/eCur;m.age=ageSum/ageCount;m.ripple=rippleCount?std::sqrt(rippleSum/rippleCount):-1;
        if(off)for(unsigned n=0;n<frames;++n)for(UINT i=0;i<W*H;++i){m.diff=std::max(m.diff,double(std::fabs(r.output[n][i]-off->output[n][i])));m.ageDiff=std::max(m.ageDiff,double(std::fabs(r.age[n][i]-off->age[n][i])));}
        return m;};
    struct Row{const char* name;double v,pan;bool comove;};
    const Row rows[]={{"rest",0,0,false},{"1px",1,0,false},{"1.5px",1.5,0,false},{"5px",5,0,false},{"5.5px",5.5,0,false},{"6.5px",6.5,0,false},{"12px",12,0,false},{"12.5px",12.5,0,false},{"pan12.5",0,12.5,false},{"comove12.5",0,12.5,true}};
    constexpr unsigned R=sizeof rows/sizeof rows[0],RAMP=5,FAST=7,PAN=8,COMOVE=9;
    const float flown[3]={.8f,2,8},bound[3]={.8725f,0,.5f}; // bound: the cap is the constant F above 0.5 px/frame, the flown ramp's value at 6.5 px/frame
    for(unsigned program=0;program<2;++program){const char* pn=program?"far_camera":"age";Numbers off[R],on[R];
        for(unsigned k=0;k<R;++k){const Run rOff=sequence(program,rows[k].v,rows[k].pan,nullptr,rows[k].comove),rOn=sequence(program,rows[k].v,rows[k].pan,flown,rows[k].comove);
            const double speed=rows[k].v+(rows[k].comove?0:rows[k].pan);off[k]=numbers(rOff,nullptr,speed);on[k]=numbers(rOn,&rOff,speed);
            for(unsigned m=0;m<2;++m){const Numbers& q=m?on[k]:off[k];std::printf("MOTION_WEIGHT program=%s row=%s velocity_px=%.2f pan_px=%.2f comove=%u on=%u e_ratio=%.6f sigma=%.2f amplitude_ratio=%.4f sigma_fit=%.3f age_mean=%.3f ripple_rms=%.6f ripple_frames=%u output_diff=%.6f age_diff=%.6f\n",pn,rows[k].name,rows[k].v,rows[k].pan,unsigned(rows[k].comove),m,q.eRatio,q.sigma,q.ampRatio,q.sigmaFit,q.age,q.ripple,q.shiftFrames,q.diff,q.ageDiff);}
            if(k==RAMP){const Run rBound=sequence(program,rows[k].v,rows[k].pan,bound);const Numbers b=numbers(rBound,&rOn,speed);
                std::printf("MOTION_WEIGHT program=%s row=%s velocity_px=%.2f pan_px=%.2f comove=0 on=2 e_ratio=%.6f sigma=%.2f amplitude_ratio=%.4f sigma_fit=%.3f age_mean=%.3f ripple_rms=%.6f ripple_frames=%u output_diff=%.6f age_diff=%.6f\n",pn,rows[k].name,rows[k].v,rows[k].pan,b.eRatio,b.sigma,b.ampRatio,b.sigmaFit,b.age,b.ripple,b.shiftFrames,b.diff,b.ageDiff);
                const std::string prefix=std::string("motion weight, ")+pn+" program: ";
                // The ramp at a binding point: 0.8,2,8 at 6.5 px/frame is cap 1.01333 - 0.0033333 x 42.25 = 0.8725 in the shader's mad against the
                // constant F = 0.8725 of the bound run (last-ulp apart: a rare FP16 rounding flip of the blend, not a visible difference).
                metric((prefix+"6.5 px/frame: within 0.002 of the run whose cap is the constant 0.8725 (a wrong A or B fails)").c_str(),std::min(b.diff,.002),0,.002);
                metric((prefix+"6.5 px/frame: the ramp binds (output differs from off by at least 0.01; min(diff, 0.01))").c_str(),std::min(on[k].diff,.01),.01,0);}}
        const std::string prefix=std::string("motion weight, ")+pn+" program: ";
        metric((prefix+"rest row bit-identical to off on both targets").c_str(),on[0].diff+on[0].ageDiff,0,0);
        metric((prefix+"1 and 1.5 px/frame rows bit-identical to off (cap 1 at or below 2 px/frame)").c_str(),on[1].diff+on[1].ageDiff+on[2].diff+on[2].ageDiff,0,0);
        metric((prefix+"pan row (camera yaw 12.5 px/frame, hull world-static) bit-identical to off").c_str(),on[PAN].diff+on[PAN].ageDiff,0,0);
        metric((prefix+"co-moving row (camera yaw 12.5 px/frame, hull screen-static: relative 12.5, screen motion 0) bit-identical to off").c_str(),on[COMOVE].diff+on[COMOVE].ageDiff,0,0);
        double ageDiff=0,slowRatio=1;for(unsigned k=0;k<R;++k)ageDiff+=on[k].ageDiff;for(unsigned k:{3u,4u})slowRatio=std::min(slowRatio,on[k].eRatio/off[k].eRatio);
        metric((prefix+"age target identical to off on every row (the cap changes the weight, never the count)").c_str(),ageDiff,0,0);
        metric((prefix+"5 and 5.5 px/frame: E ratio not below off's (min(on / off, 1))").c_str(),slowRatio,1,0);
        metric((prefix+"12.5 px/frame: E ratio at least 1.5x off's (min(on / off, 1.5))").c_str(),std::min(on[FAST].eRatio/off[FAST].eRatio,1.5),1.5,0);
        // The first run measured 2.06x on the age program (the design's model said 1.45x for the random-phase alias amplitude;
        // this witness compares jitter phases two frames apart); the bound is set above it, the flight rates the trade.
        metric((prefix+"12.5 px/frame: frame-to-frame rms at most 2.5x off's (max(on / off, 2.5))").c_str(),std::max(on[FAST].ripple/off[FAST].ripple,2.5),2.5,0);
        require(on[FAST-1].diff>0&&on[FAST].diff>0&&off[FAST].eRatio<off[0].eRatio,"motion weight: the 12 and 12.5 px/frame rows differ from off and the half-texel row is softer than rest without the option");}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
void lattice_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("LATTICE_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;constexpr unsigned N=128,P=latticePhases;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    // Lines [4.31+4i, 5.31+4i) x [3, 15), i = 0..3 (isolated 1-px struts; pitch 4 is the closest to the run-139 content); a flat bright block [21, 29) x [3, 15); flat background below y = 20.
    auto lattice=[](unsigned){std::vector<EdgeObject> o;for(unsigned i=0;i<4;++i)o.push_back({4.31+4*i,3,5.31+4*i,15,1,.5f});o.push_back({21,3,29,15,1,.5f});return o;};
    {double margin=1;for(unsigned i=1;i<=P;++i){const double jx=halton(i,2)-.5,jy=halton(i,3)-.5;for(double e:{4.31,5.31,21.}){const double v=e+jx;margin=std::min(margin,std::fabs(v-std::floor(v)-.5));}{const double v=3+jy;margin=std::min(margin,std::fabs(v-std::floor(v)-.5));}}
        std::printf("LATTICE_MARGIN %.4f\n",margin);require(margin>=.029,"lattice geometry keeps jittered edges off the sample centers");}
    const LatticeConfig configs[]={{"baseline",.9f,0},{"weight-0.95",.95f,0},{"k-baseline",.9f,2.4276f},{"k-weight-0.95",.95f,2.4276f}};
    std::vector<EdgeRun> runs;std::vector<double> ripple;
    auto flicker=[&](const std::vector<std::vector<float>>& o,double& peak){double sum=0;unsigned count=0;peak=0;for(unsigned n=N-P-1;n<N-1;++n)for(UINT y=5;y<13;++y)for(UINT x=4;x<19;++x){const double v=std::fabs(px(o[n+1],x,y)-2*px(o[n],x,y)+px(o[n-1],x,y))/2;sum+=v;peak=std::max(peak,v);++count;}return sum/count;};
    double rawFlicker=0;
    for(auto& c:configs){runs.push_back(lattice_sequence(s,resolver,lattice,N,c));double peak=0,rawPeak=0;const double r=flicker(runs.back().output,peak),raw=flicker(runs.back().current,rawPeak);ripple.push_back(r);rawFlicker=raw;
        const unsigned base=c.k>0?2:0;std::printf("LATTICE config=%s weight=%.2f k=%.4f raw_flicker=%.6f flicker=%.6f peak=%.6f codes_mean=%.3f ratio=%.4f\n",c.name,c.weight,c.k,raw,r,peak,255*r,r/ripple[base]);}
    // Shader against the CPU model (interior pixels; float rounding compounds through the history).
    for(unsigned i:{0u,1u}){const auto model=lattice_model(runs[i],configs[i].weight);double oracle=0;for(unsigned n=0;n<N;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x)oracle=std::max(oracle,double(std::fabs(px(runs[i].output[n],x,y)-px(model[n],x,y))));
        std::printf("LATTICE_ORACLE config=%s error=%.6f\n",configs[i].name,oracle);metric((std::string("lattice ")+configs[i].name+": shader matches the CPU model of the resolve").c_str(),oracle,0,.0003/(1-configs[i].weight));} // FP16 rounding (half an ulp, 0.00024 below 1) accumulates with gain 1 / (1 - w)
    // Modelled ratio of the run-139 table (cap 1): 1.00/1.61 for the weight.
    metric("lattice: the resolve removes at least 80% of the raw input flicker",std::min(ripple[0]/rawFlicker,1.),0,.2);
    metric("lattice weight 0.95: 8-phase ripple over the baseline (modelled 0.62)",ripple[1]/ripple[0],.62,.15);
    metric("lattice k 2.4276 weight 0.95: ripple below the weighted baseline",std::min(ripple[3]/ripple[2],1.),0,.9);
    // Static flat regions: the block interior (2 px inside) and the background below the objects, the weight against the baseline, last period.
    {double flat=0;for(unsigned n=N-P;n<N;++n)for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const bool block=x>=23&&x<27&&y>=5&&y<13,background=y>=20&&y<30&&x>=2&&x<30;if(block||background)flat=std::max(flat,double(std::fabs(px(runs[1].output[n],x,y)-px(runs[0].output[n],x,y))));}
        std::printf("LATTICE_FLAT config=%s max_difference=%.6f\n",configs[1].name,flat);metric((std::string("lattice ")+configs[1].name+": static flat regions unchanged").c_str(),flat,0,1./255);}
    // Moving edge: the silhouette square, static 32 frames then +1 px/frame for
    // 12. A pixel whose 3x3 holds no square pixel stays the background exactly;
    // in the rows of the square's span, at columns wholly outside its jittered
    // extent (both sides), a higher weight keeps at most (w - 0.9) * contrast
    // more of the clamp-bounded one-pixel trail.
    const double sl=10.28,st=10.37;auto square=[&](unsigned n){const double l=sl+(n>=32?n-31:0);return std::vector<EdgeObject>{{l,st,l+6,st+6,1,.5f,n>=32?1.:0.,0}};};
    const LatticeConfig moving[]={{"moving-baseline",.9f,0},{"moving-weight-0.95",.95f,0}};
    std::vector<EdgeRun> moves;for(auto& c:moving)moves.push_back(lattice_sequence(s,resolver,square,44,c));
    for(unsigned i=0;i<2;++i){const double w=moving[i].weight;
        double ghost=0,added=0,interiorMin=1;
        for(unsigned n=33;n<44;++n)for(UINT y=1;y+1<S;++y)for(UINT x=1;x+1<S;++x){bool adjacent=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)adjacent|=px(moves[i].depth[n],x+dx,y+dy)==.5f;
            const double lx=sl+(n-31);if(!adjacent)ghost=std::max(ghost,std::fabs(px(moves[i].output[n],x,y)-.25));
            // Rows inside the square's span, columns wholly outside its jittered extent (leading and trailing side).
            if(y+.5>=st+1.5&&y+.5<=st+6-1.5&&(x+1<=lx-.5||x>=lx+6+.5))added=std::max(added,double(px(moves[i].output[n],x,y)-px(moves[0].output[n],x,y)));
            if(x+.5>=lx+1.5&&x+.5<=lx+6-1.5&&y+.5>=st+1.5&&y+.5<=st+6-1.5)interiorMin=std::min(interiorMin,double(px(moves[i].output[n],x,y)));}
        const double bound=(w>.9?(w-.9)*.75:0)+1./255;
        std::printf("LATTICE_MOVING config=%s ghost_far=%.6f added_outside_square=%.6f bound=%.6f moving_interior_min=%.4f\n",moving[i].name,ghost,added,bound,interiorMin);
        metric((std::string("lattice ")+moving[i].name+": revealed background beyond one pixel carries no square colour").c_str(),ghost,0,1./255);
        if(i){metric((std::string("lattice ")+moving[i].name+": colour added outside the square over the baseline within the stated bound").c_str(),added,0,bound);
            metric((std::string("lattice ")+moving[i].name+": moving square interior stays bright").c_str(),interiorMin,1,.1);}}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
// ---- camera reprojection of the far background (sentinel policy 2) ----------
// A background at infinity rendered from a camera state (the fixture's own sky
// shader: the world direction of each unjittered sample, raster pixel p at
// NDC 2(p - j)/S - 1 exactly as the resolve's camera path assumes, through the
// same row-vector, left-handed convention camera_reprojection.h documents,
// coloured by a smooth angular pattern) with the route's sentinel ABI (RT2 -1,
// RT1 alpha -1). The resolve runs under policy 2 with the far-plane matrix the
// builder makes from consecutive (P, V) pairs; the accumulated output must
// track the current render (no crawl) while the camera turns.
struct CameraSky {
    EdgeScene& s; Com<IDirect3DPixelShader9> sky;
    CameraSky(EdgeScene& scene,Compiler compiler):s(scene){
        Com<ID3DXBuffer> code;
        compile(compiler,
            "float4 j:register(c0);float4 p:register(c1);float4 r0:register(c2);float4 r1:register(c3);float4 r2:register(c4);float4 k:register(c5);"
            "float4 main(float2 vpos:VPOS):COLOR0{float2 q=(vpos-j.xy)*j.z;float2 ndc=float2(q.x*2-1,1-q.y*2);"
            "float3 d=float3((ndc.x-p.z)*p.x,(ndc.y-p.w)*p.y,1);float3 w=float3(dot(r0.xyz,d),dot(r1.xyz,d),dot(r2.xyz,d));"
            "float phi=atan2(w.x,w.z);float theta=atan2(w.y,length(w.xz));float v=k.z+k.y*(sin(k.x*phi)+sin(k.x*theta));return float4(v,v,v,1);}",
            "ps_3_0",&code.p);
        check("sky PS",s.d->CreatePixelShader(static_cast<DWORD*>(code->GetBufferPointer()),&sky.p));
    }
    // Sentinel motion/depth for the whole frame, then the sky over the colour target.
    float frequency=12; // pattern cycles per radian; period 8.38 px at the centre of the 90-degree view
    void render(const x3m::renderer::CameraState& c,double jx,double jy){
        constexpr UINT S=EdgeScene::S;
        s.render({},EdgeBackground{.25f,-1.f,-1},jx,jy);
        s.target(s.colorSurface.p);check("sky Begin",s.d->BeginScene());check("sky bind",s.d->SetPixelShader(sky.p));
        s.constant(float(jx),float(jy),1.f/S,0,0);s.constant(1.f/c.m00,1.f/c.m11,c.m20,c.m21,1);
        s.constant(c.r[0],c.r[1],c.r[2],0,2);s.constant(c.r[3],c.r[4],c.r[5],0,3);s.constant(c.r[6],c.r[7],c.r[8],0,4);
        s.constant(frequency,.2f,.5f,0,5);
        s.quad(0,0,S,S,0,0);check("sky End",s.d->EndScene());
    }
};
// Camera basis from yaw (about +Y) and pitch (about the camera's right axis), row-vector view with V's columns the basis.
x3m::renderer::CameraState camera_pose(double yaw,double pitch,float m00=1,float m11=1){
    const double right[3]={std::cos(yaw),0,-std::sin(yaw)};
    const double f0[3]={std::sin(yaw),0,std::cos(yaw)};
    const double up0[3]={0,1,0};
    const double cp=std::cos(pitch),sp=std::sin(pitch);
    double up[3],forward[3];
    for(unsigned i=0;i<3;++i){up[i]=up0[i]*cp-f0[i]*sp;forward[i]=f0[i]*cp+up0[i]*sp;}
    float projection[16]={},view[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    projection[0]=m00;projection[5]=m11;projection[10]=1.000003f;projection[11]=1;projection[14]=-6.0000184f;
    for(unsigned i=0;i<3;++i){view[i*4]=float(right[i]);view[i*4+1]=float(up[i]);view[i*4+2]=float(forward[i]);}
    x3m::renderer::CameraState c;require(x3m::renderer::camera_state_from_matrices(projection,view,c),"camera pose validates");return c;
}
enum class CameraControl { Builder, Identity, Swapped };
struct CameraRun { std::vector<std::vector<float>> current,reference,output,age; std::vector<x3m::renderer::SentinelDecision> decisions; std::vector<bool> used; };
template<class Pose> CameraRun camera_sequence(EdgeScene& s,CameraSky& sky,const DWORD* decoder,const DWORD* resolver,Pose pose,unsigned frames,CameraControl control,float cutDegrees,const char* label,bool jitter=true,float weight=.9f,float adaptive=0){
    constexpr UINT S=EdgeScene::S,P=EdgeScene::P;TemporalPass pass;check("camera initialize",pass.initialize(s.d,decoder,resolver));CameraRun run;
    // adaptive > 0: the age-weight programs (thin clip 0.7, WMAX = adaptive), whose age target marks a current-only pixel with exactly 1.
    if(adaptive>0){check("camera configure flicker",pass.configure_flicker());require(pass.age_available(),"camera age programs available");}
    x3m::renderer::CameraState previous;
    for(unsigned n=0;n<frames;++n){
        const unsigned index=n%P+1;const double jx=jitter?halton(index,2)-.5:0,jy=jitter?halton(index,3)-.5:0;
        const x3m::renderer::CameraState c=pose(n);
        sky.render(c,jx,jy);run.current.push_back(s.read(s.color.p));
        // The route's decision (X3M_FIXTURE_TAA_SENTINEL=auto) or a negative control that forces policy 2 with the wrong matrix.
        auto d=x3m::renderer::camera_sentinel_policy(x3m::renderer::SentinelMode::Auto,c,previous,cutDegrees);
        if(control!=CameraControl::Builder&&c.valid&&previous.valid&&!d.cut){
            d.policy=2;d.transform=true;
            if(control==CameraControl::Identity)std::copy(identity,identity+16,d.matrix);
            else require(x3m::renderer::camera_far_plane_reprojection(previous,c,d.matrix),"swapped control builds");
        }
        run.decisions.push_back(d);
        FrameInputs in;in.color=s.color.p;in.current_depth=s.depth32.p;in.motion=s.motion.p;in.width=S;in.height=S;in.epoch=1;
        std::copy(d.matrix,d.matrix+16,in.clip_to_previous);
        in.current_jitter[0]=float(jx);in.current_jitter[1]=float(jy);in.weight=weight;in.motion_policy=MotionPolicy::PerPixel;
        in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=d.policy==2;in.cut=d.cut;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=true;
        if(adaptive>0){in.thin_clip=.7f;in.adaptive_weight=adaptive;}
        Output out;check("camera Begin resolve",s.d->BeginScene());check(label,pass.run(in,&out));check("camera End resolve",s.d->EndScene());
        require(out.color&&pass.diagnostics().history_valid,"camera sequence keeps a history");
        run.used.push_back(out.used_history);run.output.push_back(s.read(out.color));
        if(adaptive>0){require(out.age!=nullptr,"camera age target written");run.age.push_back(s.read(out.age));}
        // The unjittered render of the same camera: what the accumulated output represents.
        sky.render(c,0,0);run.reference.push_back(s.read(s.color.p));
        previous=c; // The history now holds this frame (the route does the same after a successful resolve).
    }
    return run;
}
// Sub-pixel shift of the accumulated output against the UNJITTERED render of
// the same camera, per axis, from the phase of the pattern's fundamental
// (period 8.38 px at the centre of the 90-degree view; Hann-windowed DFT over
// the interior lines, phases combined by magnitude). A phase estimate is
// invariant to the symmetric resampling blur a moving history accumulates,
// which a least-squares fit against a bilinearly shifted reference is not
// (it matched the blur with a fractional offset). `error` is the mean
// absolute difference at zero shift.
double camera_phase_shift(const std::vector<float>& a,const std::vector<float>& b,bool horizontal){
    constexpr UINT S=EdgeScene::S;constexpr UINT lo=6,hi=S-6;const double period=8.38,pi=3.14159265358979;
    double re=0,im=0;
    for(UINT line=lo;line<hi;++line){
        double ma=0,mb=0;for(UINT t=lo;t<hi;++t){ma+=horizontal?px(a,t,line):px(a,line,t);mb+=horizontal?px(b,t,line):px(b,line,t);}
        ma/=hi-lo;mb/=hi-lo;
        double ar=0,ai=0,br=0,bi=0;
        for(UINT t=lo;t<hi;++t){const double w=.5-.5*std::cos(2*pi*(t-lo+.5)/(hi-lo));const double c=std::cos(2*pi*t/period),sn=-std::sin(2*pi*t/period);
            const double va=(horizontal?px(a,t,line):px(a,line,t))-ma,vb=(horizontal?px(b,t,line):px(b,line,t))-mb;
            ar+=w*va*c;ai+=w*va*sn;br+=w*vb*c;bi+=w*vb*sn;}
        re+=ar*br+ai*bi;im+=ai*br-ar*bi; // a * conj(b)
    }
    return -std::atan2(im,re)*period/(2*pi);
}
void camera_drift(const std::vector<float>& output,const std::vector<float>& reference,double& drift,double& error,double& sx,double& sy){
    constexpr UINT S=EdgeScene::S;constexpr UINT lo=6,hi=S-6;
    sx=camera_phase_shift(output,reference,true);sy=camera_phase_shift(output,reference,false);
    drift=std::max(std::fabs(sx),std::fabs(sy));
    error=0;unsigned count=0;
    for(UINT y=lo;y<hi;++y)for(UINT x=lo;x<hi;++x){error+=std::fabs(px(output,x,y)-px(reference,x,y));++count;}
    error/=count;
}
void camera_metrics(const CameraRun& run,const CameraRun* baseline,unsigned from,const char* label,double& drift,double& error){
    drift=0;error=0;
    for(unsigned n=from;n<run.output.size();++n){double d=0,e=0,sx=0,sy=0,bx=0,by=0;
        camera_drift(run.output[n],run.reference[n],d,e,sx,sy);
        if(baseline){double bd=0,be=0;camera_drift(baseline->output[n],baseline->reference[n],bd,be,bx,by);d=std::max(std::fabs(sx-bx),std::fabs(sy-by));}
        drift=std::max(drift,d);error=std::max(error,e);
        if(n%8==0||run.output.size()<=8)std::printf("CAMERA_FRAME label=%s frame=%u shift_px=%.3f,%.3f baseline_px=%.3f,%.3f drift_px=%.4f error=%.5f used=%u policy=%u\n",label,n,sx,sy,bx,by,d,e,unsigned(run.used[n]),run.decisions[n].policy);}
    std::printf("CAMERA label=%s frames=%u from=%u drift_px=%.4f error=%.5f baseline=%u\n",label,unsigned(run.output.size()),from,drift,error,baseline!=nullptr);
}
void camera_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("CAMERA_CASES");EdgeScene s(d,compiler);CameraSky sky(s,compiler);
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    const double step=.5*3.14159265358979/180;   // 0.5 degrees per frame: 0.14 px at the centre of the 90-degree view, 0.56 px of the 28-degree one
    const auto yaw=[&](unsigned n){return camera_pose(n*step,0);};
    const auto pitch=[&](unsigned n){return camera_pose(0,n*step);};
    const auto still=[&](unsigned){return camera_pose(0,0);};
    CameraRun run;double drift=0,error=0;
    // (0) Static control: the same sky, jitter and policy with a still camera. Its
    // shift against the unjittered render is the recency-weighted mean of the
    // Halton set (the sequence's own bias, about -0.03/-0.04 px), the baseline
    // of every moving case below.
    const CameraRun still_run=camera_sequence(s,sky,decoder,resolver,still,48,CameraControl::Builder,20,"camera static");
    camera_metrics(still_run,nullptr,16,"static-builder",drift,error);
    metric("camera static: jitter-set bias of the accumulated output against the unjittered render (px)",drift,0,.15);
    metric("camera static: mean absolute error against the unjittered render",error,0,.02);
    // (a) Yaw and pitch under the route's jitter: the far background accumulates in place of the current render.
    run=camera_sequence(s,sky,decoder,resolver,yaw,48,CameraControl::Builder,20,"camera yaw");
    require(std::all_of(run.decisions.begin()+1,run.decisions.end(),[](const x3m::renderer::SentinelDecision& d){return d.policy==2&&!d.cut;}),"yaw frames take the camera path");
    require(!run.used[0]&&std::all_of(run.used.begin()+1,run.used.end(),[](bool u){return u;}),"yaw frames use history after the first");
    camera_metrics(run,&still_run,16,"yaw-builder",drift,error);
    metric("camera yaw: resolved background tracks the current render (drift px against the static control)",drift,0,.2);
    metric("camera yaw: mean absolute error against the unjittered render",error,0,.03);
    run=camera_sequence(s,sky,decoder,resolver,pitch,48,CameraControl::Builder,20,"camera pitch");
    camera_metrics(run,&still_run,16,"pitch-builder",drift,error);
    metric("camera pitch: resolved background tracks the current render (drift px against the static control)",drift,0,.2);
    metric("camera pitch: mean absolute error against the unjittered render",error,0,.03);
    // (b) Without jitter the remaining error is the resampling of the accumulated
    // history (Catmull-Rom of the sampled pattern at the fractional velocity) plus,
    // at 90 degrees, the perspective chirp of the pattern; the narrow view isolates the former.
    run=camera_sequence(s,sky,decoder,resolver,yaw,48,CameraControl::Builder,20,"camera yaw unjittered",false);
    camera_metrics(run,nullptr,16,"yaw-builder-unjittered",drift,error);
    metric("camera yaw unjittered: drift px against the unjittered render",drift,0,.1);
    sky.frequency=48;
    run=camera_sequence(s,sky,decoder,resolver,[&](unsigned n){return camera_pose(n*step,0,4,4);},48,CameraControl::Builder,20,"camera narrow yaw unjittered",false);
    camera_metrics(run,nullptr,16,"narrow-yaw-builder-unjittered",drift,error);
    metric("camera narrow yaw unjittered: drift px against the unjittered render",drift,0,.05);
    sky.frequency=12;
    // (c) One reprojection step at a time: history weight 1 makes the output the
    // reprojected history alone, so frame n is n chained lookups of frame 0.
    run=camera_sequence(s,sky,decoder,resolver,yaw,6,CameraControl::Builder,20,"camera yaw single step",false,1.f);
    camera_metrics(run,nullptr,1,"yaw-single-step",drift,error);
    metric("camera yaw: five chained reprojections of one render (drift px)",drift,0,.05);
    run=camera_sequence(s,sky,decoder,resolver,pitch,6,CameraControl::Builder,20,"camera pitch single step",false,1.f);
    camera_metrics(run,nullptr,1,"pitch-single-step",drift,error);
    metric("camera pitch: five chained reprojections of one render (drift px)",drift,0,.05);
    // (d) Negative controls under policy 2: the identity matrix (the route before the camera read) crawls, the swapped convention drifts the other way.
    double identityDrift=0,identityError=0,swappedDrift=0,swappedError=0;
    run=camera_sequence(s,sky,decoder,resolver,yaw,48,CameraControl::Identity,20,"camera identity control");
    camera_metrics(run,&still_run,16,"yaw-identity",identityDrift,identityError);
    run=camera_sequence(s,sky,decoder,resolver,yaw,48,CameraControl::Swapped,20,"camera swapped control");
    camera_metrics(run,&still_run,16,"yaw-swapped",swappedDrift,swappedError);
    metric("camera yaw: identity matrix under policy 2 crawls (drift px, at least 0.5)",std::min(identityDrift,1.),1,.5);
    metric("camera yaw: identity matrix under policy 2 crawls (mean absolute error, at least 0.05)",std::min(identityError,.1),.1,.05);
    metric("camera yaw: swapped rotation convention drifts (drift px, at least 0.5)",std::min(swappedDrift,1.),1,.5);
    // (e) Cut: a 25-degree jump exceeds the 20-degree bound; the decision falls back to policy 1 with a cut, the frame is
    // bit-identical to its render (no ghost), and the sequence accumulates again afterwards.
    const auto jump=[&](unsigned n){return camera_pose(n*step+(n>=16?25*3.14159265358979/180:0),0);};
    run=camera_sequence(s,sky,decoder,resolver,jump,40,CameraControl::Builder,20,"camera cut");
    require(run.decisions[16].cut&&run.decisions[16].policy==1&&!run.decisions[16].transform&&run.decisions[15].policy==2&&run.decisions[17].policy==2,"a 25-degree rotation is a cut and the policy falls back to 1 for that frame only");
    require(!run.used[16]&&run.used[15]&&run.used[17],"the cut frame runs current-only and history resumes");
    double ghost=0;for(UINT i=0;i<run.output[16].size();++i)ghost=std::max(ghost,double(std::fabs(run.output[16][i]-run.current[16][i])));
    metric("camera cut: the cut frame equals its render exactly (no ghost)",ghost,0,0);
    camera_metrics(run,&still_run,26,"yaw-cut-resumed",drift,error);
    metric("camera cut: tracking resumes after the cut (drift px against the static control)",drift,0,.2);
    // (f) Rotation angle reported by the decision matches the pose step.
    metric("camera yaw: reported rotation per frame (degrees)",run.decisions[5].rotation_degrees,.5,1e-3);
    metric("camera cut: reported rotation of the jump (degrees)",run.decisions[16].rotation_degrees,25.5,1e-2);
    // (g) Pan (run215): an all-sentinel frame under a 2-degree yaw. clip_to_previous gives w < 1 over the half of the frame the
    // camera turns toward; expectedDepth = z / w of the far-plane rows must stay a valid depth there whatever the GPU's division
    // rounding, or the pixel drops its history (age target == 1; the installed thin clip 0.7 / WMAX 0.97 programs). Pixels whose history is off-frame (oracle previous
    // position within 1.5 px of the border or outside) are excluded. The w > 1 half is reported as the control.
    {
        constexpr UINT S=EdgeScene::S;const double pan=2*3.14159265358979/180;
        const auto panYaw=[&](unsigned n){return camera_pose(n*pan,0);};
        run=camera_sequence(s,sky,decoder,resolver,panYaw,48,CameraControl::Builder,20,"camera pan",true,.9f,.97f);
        unsigned below=0,belowReset=0,above=0,aboveReset=0,framesHit=0;
        for(unsigned n=8;n<run.output.size();++n){
            const float* m=run.decisions[n].matrix;const auto now=panYaw(n),before=panYaw(n-1);unsigned hit=0;
            for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){
                const double nx=(x+.5)/S*2-1,ny=1-(y+.5)/S*2;double px=0,py=0;
                if(!x3m::renderer::camera_far_plane_previous_ndc(now,before,nx,ny,px,py))continue;
                const double u=(px*.5+.5)*S,v=(.5-py*.5)*S;if(u<1.5||u>S-1.5||v<1.5||v>S-1.5)continue;
                const double w=double(m[12])*nx+double(m[13])*ny+double(m[15]);if(std::fabs(w-1)<1e-4)continue;
                const bool reset=run.age[n][(y*S+x)*4]==1.f;
                if(w<1){++below;if(reset){++belowReset;++hit;}}else{++above;aboveReset+=reset;}
            }
            framesHit+=hit!=0;
        }
        std::printf("CAMERA_PAN yaw_degrees=2 frames=40 w_below_px=%u w_below_current_only=%u w_above_px=%u w_above_current_only=%u frames_hit=%u\n",below,belowReset,above,aboveReset,framesHit);
        require(below>4000&&above>4000,"pan: both halves of the frame are measured");
        metric("camera pan: current-only pixels where previousClip.w < 1 (history on frame)",belowReset,0,0);
    }
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
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
// Stage 3 of the HDR scene path: the direct FP16 input path (in.color) with
// the reversible luminance weighting (resolve.hlsl c22.x, FrameInputs::
// luminance_k). Every existing case above runs with k = 0 and must keep its
// numbers; these cases state the k > 0 expectations. A CPU model of one grey
// pixel of the resolve (3x3 statistics, clip at 1.25 sigma within the box,
// blend, weighting and inversion) gives the firefly expectation analytically.
namespace hdr {
float weigh(float v,float k){return v/(1+k*v);}   // grey: luma == v
float unweigh(float v,float k){return v/(1-k*v);}
// One grey pixel: `center` with eight `around` neighbours, accepted history `old`, weight w.
float model(float center,float around,float old,float w,float k){
    const float c=weigh(center,k),a=weigh(around,k),h=weigh(old,k);
    const float mean=(c+8*a)/9,square=(c*c+8*a*a)/9,sigma=std::sqrt(std::max(square-mean*mean,0.f));
    const float low=std::max(std::min(c,a),mean-1.25f*sigma),high=std::min(std::max(c,a),mean+1.25f*sigma);
    const float clipped=std::min(std::max(h,low),high);
    return unweigh(c+(clipped-c)*w,k);
}
template<class F> void upload16(IDirect3DTexture9* t,F pixel){D3DLOCKED_RECT lock{};check("lock hdr scene",t->LockRect(0,&lock,nullptr,0));
    for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){float v[4];pixel(x,y,v);unsigned short px[4];for(UINT c=0;c<4;++c)px[c]=toHalf(v[c]);std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,px,8);}
    check("unlock hdr scene",t->UnlockRect(0));}
// Largest half-precision ulp distance between the stored expectation and the output over RGB.
template<class F> unsigned worst_ulp(const std::vector<unsigned char>& bytes,F pixel,double* minSaturation=nullptr){unsigned worst=0;
    for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){float q[4];pixel(x,y,q);float lo=1e30f,hi=0;
        for(UINT c=0;c<3;++c){const unsigned short expected=toHalf(q[c]),actual=at<unsigned short>(bytes,(y*W+x)*4+c);worst=std::max(worst,unsigned(std::abs(int(expected)-int(actual))));
            const float value=halfFloat(actual);lo=std::min(lo,value);hi=std::max(hi,value);}
        if(minSaturation)*minSaturation=std::min(*minSaturation,double(hi/std::max(lo,1e-30f)));}
    return worst;}
}
void hdr_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver){
    std::puts("HDR_CASES");Fixture f(d,compiler);RouteScene s(f,compiler);s.depth([](UINT,UINT){return .5f;});
    Com<IDirect3DTexture9> scene;check("hdr scene",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&scene.p,nullptr));
    auto inputs=[&](float k){auto in=f.inputs();in.color=scene.p;in.depth_snapshot=nullptr;in.current_depth=s.depth32.p;in.weight=.9f;in.luminance_k=k;return in;};
    // k is validated like every other constant.
    {TemporalPass p;check("initialize hdr validation pass",p.initialize(d,decoder,resolver));Output failed;
        auto in=inputs(-1.f);require(p.run(in,&failed)==E_INVALIDARG&&!failed.color,"negative k refused");
        in=inputs(NAN);require(p.run(in,&failed)==E_INVALIDARG,"NaN k refused");
        in=inputs(70000.f);require(p.run(in,&failed)==E_INVALIDARG,"k above the FP16 range refused");}
    // (1) A bright firefly (8.0) appearing against a stationary 0.2 background:
    // the unweighted blend keeps 0.1 * 8 + 0.9 * 0.2 of it; the weighted blend
    // suppresses the flicker energy by the model's factor. Both values are the
    // FP16-stored ones (0.2 is not exact in FP16).
    const float dark=halfFloat(toHalf(.2f)),bright=8.f;
    const float ks[]={0.f,1.f,4.f};float outs[3]{};
    for(unsigned i=0;i<3;++i){TemporalPass p;check("initialize hdr firefly pass",p.initialize(d,decoder,resolver));
        hdr::upload16(scene.p,[&](UINT,UINT,float* q){q[0]=q[1]=q[2]=dark;q[3]=1;});s.run(p,inputs(ks[i]),"hdr flat first frame");
        hdr::upload16(scene.p,[&](UINT x,UINT y,float* q){const float v=x==8&&y==8?bright:dark;q[0]=q[1]=q[2]=v;q[3]=1;});
        auto out=s.run(p,inputs(ks[i]),"hdr firefly frame");require(out.used_history,"firefly frame accumulates");
        auto bytes=readback(d,out.color);outs[i]=halfFloat(at<unsigned short>(bytes,(8*W+8)*4));
        const float expected=hdr::model(bright,dark,dark,.9f,ks[i]),side=halfFloat(at<unsigned short>(bytes,(8*W+7)*4));
        std::printf("HDR_FIREFLY k=%g output=%.6f expected=%.6f energy=%.6f neighbour=%.6f\n",double(ks[i]),double(outs[i]),double(expected),double(outs[i]-dark),double(side));
        ++numeric_checks;require(std::fabs(outs[i]-expected)<=2e-3f*std::max(1.f,expected),"firefly output matches the weighted resolve model");
        ++numeric_checks;require(std::fabs(side-dark)<=1e-3f,"the dark stationary neighbour of the firefly keeps its value");}
    ++numeric_checks;require(std::fabs(outs[0]-(.1f*bright+.9f*dark))<2e-3f,"unweighted firefly blend is 0.1 * 8 + 0.9 * 0.2 (the clip admits the dark history)");
    for(unsigned i=1;i<3;++i){const float ratio=(outs[i]-dark)/(outs[0]-dark),analytic=(hdr::model(bright,dark,dark,.9f,ks[i])-dark)/(hdr::model(bright,dark,dark,.9f,0)-dark);
        std::printf("HDR_FIREFLY_RATIO k=%g measured=%.4f analytic=%.4f\n",double(ks[i]),double(ratio),double(analytic));
        ++numeric_checks;require(std::fabs(ratio-analytic)<5e-3f&&ratio<.25f,"weighting suppresses the firefly's flicker energy by the analytic factor");}
    // (2) A stationary HDR gradient with chroma (2^-8 .. 2^7.6, RGB 1 : 1/2 : 1/4)
    // resolved twice: the second output equals the stored input within one
    // FP16 ulp after weighting and inversion (exactly at k = 0).
    auto gradient=[](UINT x,UINT y,float* q){const float v=std::ldexp(1.f,int(x)-8)*(1+float(y)/32);q[0]=v;q[1]=v*.5f;q[2]=v*.25f;q[3]=1;};
    for(float k:ks){TemporalPass p;check("initialize hdr gradient pass",p.initialize(d,decoder,resolver));hdr::upload16(scene.p,gradient);
        s.run(p,inputs(k),"hdr gradient first frame");auto out=s.run(p,inputs(k),"hdr gradient second frame");require(out.used_history,"gradient accumulates");
        const unsigned worst=hdr::worst_ulp(readback(d,out.color),gradient);std::printf("HDR_GRADIENT k=%g worst_ulp=%u\n",double(k),worst);
        ++numeric_checks;require(worst<=(k>0?1u:0u),"stationary HDR gradient survives weighting and inversion within one FP16 ulp (exact at k = 0)");}
    // (3) A stable saturated HDR edge (left (4, .1, .1), right (.1, .1, 4),
    // brightening downwards): the clip in the weighted domain keeps every pixel
    // of both sides, edge columns included, within one ulp; the 40 : 1
    // channel ratio is preserved (no desaturation).
    auto edge=[](UINT x,UINT y,float* q){const float sc=1+float(y)/16;const bool left=x<8;q[0]=(left?4.f:.1f)*sc;q[1]=.1f*sc;q[2]=(left?.1f:4.f)*sc;q[3]=1;};
    for(float k:{1.f,4.f}){TemporalPass p;check("initialize hdr edge pass",p.initialize(d,decoder,resolver));hdr::upload16(scene.p,edge);
        s.run(p,inputs(k),"hdr edge first frame");auto out=s.run(p,inputs(k),"hdr edge second frame");require(out.used_history,"edge accumulates");
        double minSaturation=1e30;const unsigned worst=hdr::worst_ulp(readback(d,out.color),edge,&minSaturation);
        std::printf("HDR_EDGE k=%g worst_ulp=%u min_saturation=%.3f\n",double(k),worst,minSaturation);
        ++numeric_checks;require(worst<=1&&minSaturation>=39.,"stable saturated HDR edge keeps its channels within one ulp and its 40:1 saturation");}
    // (4) Negative channels (review 24): an FP16 scene may hold negative
    // values (subtractive blends), which finiteColor admits. A pixel whose
    // luma is <= -1/k would make 1 + k * luma zero or negative; the weighting
    // floors the luma at 0 (such a pixel and its inverse are the identity) so
    // a stationary scene with a 5x5 block of negative grey (-0.5) and a 5x5
    // block of mixed sign with positive luma (-0.5, 1, 0) among 0.2 greys
    // resolves to itself within one ulp at k = 2 (1 + k * luma = 0) and
    // k = 4 (negative), every output finite. (Blocks, not single pixels: a
    // stationary value survives the mean +/- 1.25 sigma clip only where at
    // least four of the nine neighbourhood taps share it, which a 5x5 block's
    // corner just does; an isolated pixel is an outlier at any k, 0 included.)
    auto negative=[](UINT x,UINT y,float* q){q[0]=q[1]=q[2]=.2f;q[3]=1;if(x>=2&&x<=6&&y>=2&&y<=6)q[0]=q[1]=q[2]=-.5f;if(x>=9&&x<=13&&y>=9&&y<=13){q[0]=-.5f;q[1]=1;q[2]=0;}};
    for(float k:{2.f,4.f}){TemporalPass p;check("initialize hdr negative pass",p.initialize(d,decoder,resolver));hdr::upload16(scene.p,negative);
        s.run(p,inputs(k),"hdr negative first frame");auto out=s.run(p,inputs(k),"hdr negative second frame");require(out.used_history,"negative accumulates");
        const auto bytes=readback(d,out.color);bool finite=true;
        for(UINT i=0;i<W*H*3;++i){const unsigned short h=at<unsigned short>(bytes,(i/3)*4+i%3);if((h&0x7c00)==0x7c00)finite=false;}
        const unsigned worst=hdr::worst_ulp(bytes,negative);
        std::printf("HDR_NEGATIVE k=%g worst_ulp=%u finite=%u centre=%.6f mixed=%.6f\n",double(k),worst,finite,double(halfFloat(at<unsigned short>(bytes,(4*W+4)*4))),double(halfFloat(at<unsigned short>(bytes,(11*W+11)*4))));
        ++numeric_checks;require(finite&&worst<=1,"negative-luma and mixed-sign pixels survive the weighting and its inverse within one ulp, every output finite");}
}
// Post-resolve sharpen (src/temporal/sharpen.h, rcas.hlsl; FrameInputs::
// sharpen): the pass's optional RCAS draw of its fresh FP16 history into the
// caller's 8-bit surface, in place of the caller's copy-back. The cases state
// the contract: the switch validated like every constant; 0 drawing nothing
// (main target untouched, history equal to a pass without the program, byte
// for byte); on, the history byte-identical to the off run and the display
// image inside the 3x3 min/max of the unsharpened one with alpha untouched;
// the program itself defined on NaN/inf input.
namespace sharpen {
std::vector<unsigned char> surface(IDirect3DDevice9* d,IDirect3DSurface9* source,UINT w,UINT h,D3DFORMAT format,UINT pixel){
    Com<IDirect3DSurface9> read;check("sharpen readback surface",d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&read.p,nullptr));
    check("sharpen validation-only readback",d->GetRenderTargetData(source,read.p));D3DLOCKED_RECT lock{};check("sharpen readback lock",read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> bytes(std::size_t(w)*h*pixel);for(UINT y=0;y<h;++y)std::memcpy(&bytes[std::size_t(y)*w*pixel],static_cast<char*>(lock.pBits)+y*lock.Pitch,w*pixel);
    check("sharpen readback unlock",read->UnlockRect());return bytes;}
// Per-pixel verdicts of a sharpened BGRA8 image against the unsharpened one:
// channels outside the 3x3 (clamp-addressed) min/max of `base`, alpha
// differences, pixels that changed at all. `nanCode`, when >= 0, replaces the
// codes listed in `nan` (pixel indices) in the bounds.
struct Verdict { unsigned outside=0,alpha=0,changed=0; };
Verdict verdict(const std::vector<unsigned char>& sharp,const std::vector<unsigned char>& base,UINT w,UINT h){
    Verdict v{};
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const std::size_t i=(std::size_t(y)*w+x)*4;bool differs=false;
        for(UINT c=0;c<3;++c){unsigned char mn=255,mx=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int sx=std::min(std::max(int(x)+dx,0),int(w)-1),sy=std::min(std::max(int(y)+dy,0),int(h)-1);
                const unsigned char t=base[(std::size_t(sy)*w+sx)*4+c];mn=std::min(mn,t);mx=std::max(mx,t);}
            const unsigned char value=sharp[i+c];if(value<mn||value>mx)++v.outside;if(value!=base[i+c])differs=true;}
        if(sharp[i+3]!=base[i+3])++v.alpha;
        if(differs)++v.changed;}
    return v;}
}
void sharpen_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver,const DWORD* sharpener){
    std::puts("SHARPEN_CASES");Fixture f(d,compiler);RouteScene s(f,compiler);s.depth([](UINT,UINT){return .5f;});
    // (1) The switch is validated like every other constant, and the draw
    // needs the 8-bit surface input and the program.
    {TemporalPass p;check("initialize sharpen validation pass",p.initialize(d,decoder,resolver,nullptr,sharpener));Output failed;s.fill(1);
        auto in=s.inputs();in.sharpen=-.5f;require(p.run(in,&failed)==E_INVALIDARG&&!failed.color,"negative sharpen refused");
        in=s.inputs();in.sharpen=NAN;require(p.run(in,&failed)==E_INVALIDARG,"NaN sharpen refused");
        in=s.inputs();in.sharpen=1.5f;require(p.run(in,&failed)==E_INVALIDARG,"sharpen above 1 refused");
        auto fp16=f.inputs();fp16.depth_snapshot=nullptr;fp16.current_depth=s.depth32.p;fp16.sharpen=1.f;
        require(p.run(fp16,&failed)==E_INVALIDARG,"sharpen on the FP16 input path refused (the HDR write-back sharpens)");
        TemporalPass plain;check("initialize pass without the sharpen program",plain.initialize(d,decoder,resolver));
        in=s.inputs();in.sharpen=1.f;require(plain.run(in,&failed)==E_INVALIDARG,"sharpen without the sharpen program refused");
        require(readback(d,s.main8.p,D3DFMT_A8R8G8B8,4)==s.reference,"refused runs leave the main target untouched");}
    // (2) Off is the pre-sharpen pass: sharpen 0 (the field's default) on a
    // pass created with the program draws nothing, leaves the main target
    // untouched and produces the history of a pass without the program.
    {TemporalPass with,without;check("initialize sharpen-capable pass",with.initialize(d,decoder,resolver,nullptr,sharpener));check("initialize sharpen-free pass",without.initialize(d,decoder,resolver));
        for(unsigned frame=0;frame<2;++frame){s.fill(7+frame);
            auto a=s.run(without,s.inputs(),"sharpen-free pass");require(readback(d,s.main8.p,D3DFMT_A8R8G8B8,4)==s.reference&&!a.display_written,"a pass without the program leaves the main target untouched");
            auto b=s.run(with,s.inputs(),"sharpen 0 pass");require(readback(d,s.main8.p,D3DFMT_A8R8G8B8,4)==s.reference&&!b.display_written,"sharpen 0 draws nothing");
            ++numeric_checks;require(readback(d,a.color)==readback(d,b.color)&&a.used_history==b.used_history,"sharpen 0 history equals the sharpen-free history byte for byte");}}
    // (3) On: the same two frames through an off and an on pass. History
    // byte-identical (the sharpen never reaches it); the main target after the
    // on run is the display image: every channel inside the 3x3 min/max of
    // the unsharpened 8-bit image (the off run's copy-back), alpha untouched,
    // and some pixels changed.
    for(float sharpness:{1.f,.5f}){TemporalPass off,on;check("initialize off pass",off.initialize(d,decoder,resolver,nullptr,sharpener));check("initialize on pass",on.initialize(d,decoder,resolver,nullptr,sharpener));
        sharpen::Verdict v{};
        for(unsigned frame=0;frame<2;++frame){s.fill(11+frame);
            auto a=s.run(off,s.inputs(),"sharpen off run");require(readback(d,s.main8.p,D3DFMT_A8R8G8B8,4)==s.reference,"off run leaves the main target for the copy-back");
            check("copy-back",d->StretchRect(a.color_surface,nullptr,s.main8.p,nullptr,D3DTEXF_POINT));const auto unsharp=readback(d,s.main8.p,D3DFMT_A8R8G8B8,4);
            s.fill(11+frame); // the same input again for the on pass
            auto in=s.inputs();in.sharpen=sharpness;auto b=s.run(on,in,"sharpen on run");
            require(b.display_written&&a.used_history==b.used_history,"on run wrote the display image");
            ++numeric_checks;require(readback(d,a.color)==readback(d,b.color)&&readback(d,a.depth)==readback(d,b.depth),"history byte-identical between the off and the on run");
            v=sharpen::verdict(readback(d,s.main8.p,D3DFMT_A8R8G8B8,4),unsharp,W,H);}
        std::printf("SHARPEN sharpness=%g changed=%u outside=%u alpha_diff=%u pixels=%u\n",double(sharpness),v.changed,v.outside,v.alpha,W*H);
        ++numeric_checks;require(v.outside==0&&v.alpha==0&&v.changed>0,"sharpened display image inside the 3x3 min/max of the unsharpened one, alpha untouched, some pixels changed");}
    // (4) NaN/inf: the program alone (the pass cannot be handed a non-finite
    // 8-bit input) on an FP16 image holding NaN, +inf and -inf among finite
    // values, drawn into the 8-bit target through the pass's point/clamp
    // sampling. First an all-NaN image measures the backend's saturate(NaN)
    // code; then every output must lie inside the 3x3 min/max of the input
    // with NaN read as that code, +inf as 255 and -inf as 0.
    {Com<IDirect3DPixelShader9> ps;check("sharpen PS",d->CreatePixelShader(sharpener,&ps.p));
        Com<IDirect3DTexture9> image;check("sharpen FP16 image",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&image.p,nullptr));
        auto draw=[&](){s.plain(s.main8.p,nullptr,false);check("sharpen PS bind",d->SetPixelShader(ps.p));const float c23[4]={1.f,1.f/W,1.f/H,0.f};check("sharpen c23",d->SetPixelShaderConstantF(23,c23,1));
            check("sharpen Begin",d->BeginScene());check("sharpen texture",d->SetTexture(0,image.p));s.quad({0,0,W,H},.5f);check("sharpen End",d->EndScene());check("sharpen unbind",d->SetTexture(0,nullptr));
            return readback(d,s.main8.p,D3DFMT_A8R8G8B8,4);};
        const unsigned short nanBits=0x7e00,posInf=0x7c00,negInf=0xfc00;
        {D3DLOCKED_RECT lock{};check("lock NaN image",image->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){unsigned short px[4]={nanBits,nanBits,nanBits,toHalf(1)};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,px,8);}check("unlock NaN image",image->UnlockRect(0));}
        const auto allNan=draw();const unsigned char nanCode=allNan[0];bool uniform=true;for(UINT i=0;i<W*H;++i)for(UINT c=0;c<3;++c)if(allNan[i*4+c]!=nanCode)uniform=false;
        std::printf("SHARPEN_NAN saturate_nan_code=%u uniform=%u\n",nanCode,uniform);
        ++numeric_checks;require(uniform,"an all-NaN input gives one defined code everywhere (the backend's saturate(NaN))");
        std::vector<unsigned char> base(W*H*4);
        {D3DLOCKED_RECT lock{};check("lock mixed image",image->LockRect(0,&lock,nullptr,0));
            for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){float v[3]={(x+y)/30.f,(x+y)/32.f+.02f,x<8?.2f:.7f};unsigned short px[4];unsigned char code[3];
                for(UINT c=0;c<3;++c){px[c]=toHalf(v[c]);code[c]=static_cast<unsigned char>(std::lround(std::min(std::max(halfFloat(px[c]),0.f),1.f)*255));}
                if(x==5&&y==5){px[0]=px[1]=px[2]=nanBits;code[0]=code[1]=code[2]=nanCode;}
                if(x==9&&y==9){px[0]=px[1]=px[2]=posInf;code[0]=code[1]=code[2]=255;}
                if(x==12&&y==3){px[0]=px[1]=px[2]=negInf;code[0]=code[1]=code[2]=0;}
                px[3]=toHalf(1);std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,px,8);
                unsigned char* b=&base[(std::size_t(y)*W+x)*4];b[0]=code[2];b[1]=code[1];b[2]=code[0];b[3]=255;}
            check("unlock mixed image",image->UnlockRect(0));}
        const auto out=draw();const auto v=sharpen::verdict(out,base,W,H);
        std::printf("SHARPEN_NONFINITE outside=%u alpha_diff=%u changed=%u nan_pixel=%u,%u,%u posinf_pixel=%u,%u,%u neginf_pixel=%u,%u,%u\n",v.outside,v.alpha,v.changed,
            out[(5*W+5)*4+2],out[(5*W+5)*4+1],out[(5*W+5)*4],out[(9*W+9)*4+2],out[(9*W+9)*4+1],out[(9*W+9)*4],out[(3*W+12)*4+2],out[(3*W+12)*4+1],out[(3*W+12)*4]);
        ++numeric_checks;require(v.outside==0&&v.alpha==0,"NaN, +inf and -inf taps stay inside the neighbourhood bound as their saturated values and poison nothing");}
}
// Quad vertex program and copy mode twins (D1/D2 of the native-Windows
// audit). Every proxy quad now draws through the embedded vs_3_0
// pass-through with a clip-space quad; this fixture is built with
// X3M_QUAD_FVF_SWITCH, so a pass initialised while X3M_FIXTURE_QUAD_FVF=1 is
// set draws the previous XYZRHW fixed-function quads instead. (1) The two
// paths are byte-identical: history colour and depth, and the display image
// (copy-back, sharpened, and the draw copy mode's own write-back). (2) The
// draw copy mode (configure_copy(true): same-format staging copy plus
// identity draws in place of the format-converting StretchRect) reproduces
// the stretch mode's history byte for byte and its display image within one
// 8-bit code (printed; exact on this backend is the expectation), leaves the
// main target to the caller when the copy program is absent (refused), and
// refuses nothing else.
void quad_twin_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver,const DWORD* sharpener){
    std::puts("QUAD_TWIN_CASES");Fixture f(d,compiler);RouteScene s(f,compiler);s.depth([](UINT,UINT){return .5f;});
    const auto* copy=reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program());
    auto init=[&](TemporalPass& p,bool fvf,bool draw,const char* label){if(fvf)SetEnvironmentVariableA("X3M_FIXTURE_QUAD_FVF","1");check(label,p.initialize(d,decoder,resolver,nullptr,sharpener,copy));if(fvf)SetEnvironmentVariableA("X3M_FIXTURE_QUAD_FVF",nullptr);p.configure_copy(draw);};
    auto display=[&](){return readback(d,s.main8.p,D3DFMT_A8R8G8B8,4);};
    // (1) vs_3_0 quad against the XYZRHW twin: stretch mode, then sharpened, then draw copy mode.
    for(unsigned variant=0;variant<3;++variant){
        const bool draw=variant==2;const float sharpness=variant==1?1.f:0.f;
        TemporalPass vs_pass,fvf_pass;init(vs_pass,false,draw,"initialize vs_3_0 quad pass");init(fvf_pass,true,draw,"initialize XYZRHW twin pass");
        for(unsigned frame=0;frame<3;++frame){
            s.fill(21+frame*5+variant);auto in=s.inputs();in.sharpen=sharpness;
            auto a=s.run(vs_pass,in,"vs_3_0 quad run");
            if(!a.display_written)check("vs copy-back",d->StretchRect(a.color_surface,nullptr,s.main8.p,nullptr,D3DTEXF_POINT));
            const auto display_a=display();const auto color_a=readback(d,a.color),depth_a=readback(d,a.depth);
            s.fill(21+frame*5+variant);
            auto b=s.run(fvf_pass,in,"XYZRHW twin run");
            if(!b.display_written)check("twin copy-back",d->StretchRect(b.color_surface,nullptr,s.main8.p,nullptr,D3DTEXF_POINT));
            require(a.display_written==b.display_written&&a.display_written==(draw||sharpness>0),"display ownership follows the mode on both paths");
            ++numeric_checks;require(color_a==readback(d,b.color)&&depth_a==readback(d,b.depth),"vs_3_0 quad history equals the XYZRHW twin byte for byte");
            ++numeric_checks;require(display_a==display(),"vs_3_0 quad display image equals the XYZRHW twin byte for byte");
        }
        std::printf("QUAD_TWIN variant=%s frames=3 identical=1\n",variant==0?"stretch":variant==1?"sharpen":"draw_copy");
    }
    // (2) Draw copy mode against stretch mode on the same frames.
    {TemporalPass stretch,draw;init(stretch,false,false,"initialize stretch mode pass");init(draw,false,true,"initialize draw copy mode pass");
        unsigned worst=0,differing=0,total=0;bool history_identical=true;
        for(unsigned frame=0;frame<3;++frame){
            s.fill(41+frame*3);auto a=s.run(stretch,s.inputs(),"stretch mode run");check("stretch copy-back",d->StretchRect(a.color_surface,nullptr,s.main8.p,nullptr,D3DTEXF_POINT));
            const auto display_a=display();const auto color_a=readback(d,a.color),depth_a=readback(d,a.depth);
            s.fill(41+frame*3);auto b=s.run(draw,s.inputs(),"draw copy mode run");
            require(b.display_written&&b.copy_result==S_OK&&b.sharpen_result==S_FALSE,"draw copy mode wrote the display itself");
            history_identical=history_identical&&color_a==readback(d,b.color)&&depth_a==readback(d,b.depth);
            const auto display_b=display();
            for(size_t i=0;i<display_a.size();++i){const unsigned diff=unsigned(std::abs(int(display_a[i])-int(display_b[i])));worst=std::max(worst,diff);differing+=diff!=0;++total;}
        }
        std::printf("COPY_MODE draw_vs_stretch history_identical=%u display_max_code_difference=%u display_differing_bytes=%u of %u\n",history_identical,worst,differing,total);
        ++numeric_checks;require(history_identical,"draw copy mode history equals stretch mode history byte for byte");
        ++numeric_checks;require(worst<=1,"draw copy mode display within one code of the stretch mode copy-back");
        // Refusals: draw mode without the copy program, the staging surface as an input alias.
        TemporalPass plain;check("initialize pass without the copy program",plain.initialize(d,decoder,resolver));plain.configure_copy(true);
        Output failed;s.fill(50);require(plain.run(s.inputs(),&failed)==E_INVALIDARG&&!failed.color,"draw copy mode without the copy program refused");
        require(display()==s.reference,"the refused run left the main target untouched");
        auto fp16=f.inputs();fp16.depth_snapshot=nullptr;fp16.current_depth=s.depth32.p;auto c=s.run(draw,fp16,"draw copy mode with an FP16 input");
        require(!c.display_written&&c.copy_result==S_FALSE,"an FP16 input takes no copy in draw mode");}
}
// "sharpen-measure": a 128x128 synthetic resolved-looking image (soft-edged
// slanted shapes at the blur the run-2 analysis measured, plus a ramp) through
// the pass at sharpness 0 (the copy-back), 0.25, 0.5 and 1.0; the 8-bit
// display images are written beside the executable for run_temporal_pass.py,
// which computes the gradient-energy ratio and MTF50 with the iteration-9
// run-2 analysis functions.
void sharpen_measure(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* resolver,const DWORD* sharpener){
    constexpr UINT S=128;std::puts("SHARPEN_MEASURE");
    Com<IDirect3DSurface9> main8;Com<IDirect3DTexture9> source,depth32;Com<IDirect3DPixelShader9> textured;
    check("measure main target",d->CreateRenderTarget(S,S,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&main8.p,nullptr));
    check("measure source",d->CreateTexture(S,S,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&source.p,nullptr));
    check("measure depth",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth32.p,nullptr));
    {Com<ID3DXBuffer> a;compile(compiler,"sampler2D source:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(source,uv);}","ps_3_0",&a.p);check("measure textured PS",d->CreatePixelShader(static_cast<DWORD*>(a->GetBufferPointer()),&textured.p));}
    {Com<IDirect3DTexture9> staging;check("measure depth staging",d->CreateTexture(S,S,1,0,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging.p,nullptr));D3DLOCKED_RECT lock{};check("lock measure depth",staging->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const float z=.5f;std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&z,4);}
        check("unlock measure depth",staging->UnlockRect(0));check("measure depth upload",d->UpdateTexture(staging.p,depth32.p));}
    // The image: 4x4 supersampled shapes on a 0.35 grey (slanted bright and
    // dark bars, a disc, a dark box; the slant spreads the edge phases), box
    // filtered to 128x128, then a Gaussian blur of sigma 0.75 px (a 10-90%
    // rise of about 1.7 px, the resolved edge of burst 629 in run 2), and a
    // horizontal ramp band; a mild tint so every channel is exercised.
    std::vector<float> luma(S*S,0.f);
    {const UINT F=4,B=S*F;std::vector<float> fine(B*B);
        for(UINT y=0;y<B;++y)for(UINT x=0;x<B;++x){const float px=(x+.5f)/F,py=(y+.5f)/F;float v=.35f;const float slant=py/12.f;
            if(px>=20+slant&&px<44+slant)v=.85f;
            if(px>=56-slant&&px<70-slant)v=.12f;
            if(px>=84+slant&&px<108+slant&&py>=10&&py<50)v=.62f;
            if((px-64)*(px-64)+(py-72)*(py-72)<196)v=.92f;
            if(px>=14&&px<40&&py>=60&&py<90)v=.2f;
            if(py>=100&&py<120)v=.2f+.6f*px/S;
            fine[y*B+x]=v;}
        std::vector<float> box(S*S);for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){float sum=0;for(UINT j=0;j<F;++j)for(UINT i=0;i<F;++i)sum+=fine[(y*F+j)*B+x*F+i];box[y*S+x]=sum/(F*F);}
        const float sigma=.75f;float kernel[5],norm=0;for(int k=-2;k<=2;++k){kernel[k+2]=std::exp(-k*k/(2*sigma*sigma));norm+=kernel[k+2];}for(auto& k:kernel)k/=norm;
        std::vector<float> row(S*S);for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){float sum=0;for(int k=-2;k<=2;++k)sum+=kernel[k+2]*box[y*S+std::min(std::max(int(x)+k,0),int(S)-1)];row[y*S+x]=sum;}
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){float sum=0;for(int k=-2;k<=2;++k)sum+=kernel[k+2]*row[std::min(std::max(int(y)+k,0),int(S)-1)*S+x];luma[y*S+x]=sum;}}
    std::vector<unsigned char> input(S*S*4);
    {D3DLOCKED_RECT lock{};check("lock measure source",source->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const float v=luma[y*S+x];unsigned char* p=&input[(std::size_t(y)*S+x)*4];
            p[2]=static_cast<unsigned char>(std::lround(std::min(std::max(v,0.f),1.f)*255));p[1]=static_cast<unsigned char>(std::lround(std::min(std::max(v*.95f+.02f,0.f),1.f)*255));p[0]=static_cast<unsigned char>(std::lround(std::min(std::max(v*.9f+.05f,0.f),1.f)*255));p[3]=255;
            std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,p,4);}
        check("unlock measure source",source->UnlockRect(0));}
    auto fill=[&](){for(UINT n=0;n<8;++n)check("measure unbind",d->SetTexture(n,nullptr));check("measure RT",d->SetRenderTarget(0,main8.p));check("measure RT1",d->SetRenderTarget(1,nullptr));check("measure DS",d->SetDepthStencilSurface(nullptr));
        D3DVIEWPORT9 vp{0,0,S,S,0,1};check("measure VP",d->SetViewport(&vp));check("measure VS",d->SetVertexShader(nullptr));check("measure FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check("measure PS",d->SetPixelShader(textured.p));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})check("measure RS",d->SetRenderState(state,FALSE));
        check("measure cull",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check("measure write",d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        for(auto p:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE}})check("measure sampler",d->SetSamplerState(0,p.first,p.second));
        check("measure Begin",d->BeginScene());check("measure texture",d->SetTexture(0,source.p));
        struct V{float x,y,z,rhw,u,v;};const V v[]={{-.5f,-.5f,.5f,1,0,0},{S-.5f,-.5f,.5f,1,1,0},{-.5f,S-.5f,.5f,1,0,1},{S-.5f,S-.5f,.5f,1,1,1}};
        check("measure raster",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));check("measure End",d->EndScene());check("measure unbind texture",d->SetTexture(0,nullptr));
        require(sharpen::surface(d,main8.p,S,S,D3DFMT_A8R8G8B8,4)==input,"measure input holds the exact bytes");};
    const float sharpnesses[]={0.f,.25f,.5f,1.f};
    for(float sharpness:sharpnesses){fill();TemporalPass p;check("initialize measure pass",p.initialize(d,decoder,resolver,nullptr,sharpener));
        FrameInputs in{};in.color_surface=main8.p;in.current_depth=depth32.p;in.width=S;in.height=S;in.epoch=1;std::copy(identity,identity+16,in.clip_to_previous);
        in.motion_policy=MotionPolicy::KnownCameraOnly;in.reactive_policy=x3m::renderer::ReactivePolicy::KnownNonReactive;in.weight=.9f;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=false;in.sharpen=sharpness;
        Output out;check("measure run",p.run(in,&out));
        if(sharpness==0)check("measure copy-back",d->StretchRect(out.color_surface,nullptr,main8.p,nullptr,D3DTEXF_POINT));else require(out.display_written,"measure run wrote the display image");
        const auto image=sharpen::surface(d,main8.p,S,S,D3DFMT_A8R8G8B8,4);
        if(sharpness==0)require(image==input,"the current-only resolve of the first frame returns the input through the FP16 round trip");
        else{const auto v=sharpen::verdict(image,input,S,S);++numeric_checks;require(v.outside==0&&v.alpha==0&&v.changed>0,"measure image inside the 3x3 bound of the input");}
        char name[64];std::snprintf(name,sizeof name,"sharpen-measure-%g.bgra8",double(sharpness));FILE* file=std::fopen(name,"wb");require(file!=nullptr,"measure image written");
        std::fwrite(image.data(),1,image.size(),file);std::fclose(file);
        std::printf("MEASURE sharpness=%g file=%s width=%u height=%u\n",double(sharpness),name,S,S);}
    for(UINT n=0;n<8;++n)d->SetTexture(n,nullptr);
    d->SetPixelShader(nullptr);
}
// Same inputs and independent histories through unrolled/rolled programs.
// Strict RGB equality is the initial oracle; report every measured difference
// before failing, rather than adjusting tolerances after a mismatch.
void loop_twins(IDirect3DDevice9* d,Compiler compiler,const DWORD* decoder,const DWORD* baseline,const DWORD* candidate){
    std::puts("LOOP_TWINS");Fixture f(d,compiler);RouteScene route(f,compiler);
    TemporalPass original,loop;check("loop twin baseline initialize",original.initialize(d,decoder,baseline));check("loop twin candidate initialize",loop.initialize(d,decoder,candidate));
    Com<IDirect3DTexture9> raw;check("loop twin FP16 mask",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&raw.p,nullptr));
    const char* names[]={"finite","nonfinite","hdr","motion","supplemental"};
    for(unsigned scenario=0;scenario<5;++scenario)for(unsigned frame=0;frame<6;++frame){
        hdr::upload16(f.color.p,[&](UINT x,UINT y,float* pixel){
            for(UINT c=0;c<3;++c){float v=.125f*float(1+(x*3+y*5+frame+c)%11);if(scenario==2)v*=c==0?256.f:c==1?32.f:4.f;
                if(scenario==1&&((x+y+frame)%7==0))v=c==0?NAN:c==1?INFINITY:-INFINITY;
                pixel[c]=v;}
            pixel[3]=(x==0&&y==0)?-0.f:.125f*float(1+(x+frame)%7);
        });
        hdr::upload16(raw.p,[&](UINT x,UINT y,float* pixel){float v=0;
            if(scenario==4&&frame>=1&&frame<=3&&x==5+frame&&y==8)v=frame==3?NAN:1.f;
            pixel[0]=pixel[1]=pixel[2]=v;pixel[3]=0;
        });
        route.depth([&](UINT x,UINT y){return x<2?-1.f:((x+y+frame)%5==0?.25f:.5f);});
        f.uploadMotion(1);
        if(scenario==3){D3DLOCKED_RECT lock{};check("loop twin motion lock",f.motion->LockRect(0,&lock,nullptr,0));
            for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){const float v[]={(x+.875f)/W,(y+.75f)/H,.5f,((x+y+frame)%13==0)?-1.f:1.f};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,v,16);}
            check("loop twin motion unlock",f.motion->UnlockRect(0));}
        auto in=f.inputs();in.depth_snapshot=nullptr;in.current_depth=route.depth32.p;in.epoch=20+scenario;in.weight=.875f;
        in.current_jitter[0]=frame&1?.125f:-.125f;in.current_jitter[1]=frame&2?.25f:-.25f;
        in.clip_to_previous[3]=frame?(.75f/W):0;in.clip_to_previous[7]=frame?(-.5f/H):0;
        in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.sentinel_camera=true;
        if(scenario==2)in.luminance_k=.125f;
        if(scenario==3){in.motion_policy=MotionPolicy::PerPixel;in.motion=f.motion.p;}
        if(scenario==4){in.reactive_policy=ReactivePolicy::SupplementalMaskWithDepthSentinel;in.reactive=raw.p;}
        auto a=f.run(original,in,"unrolled loop twin");auto b=f.run(loop,in,"rolled loop twin");
        const auto ac=readback(d,a.color),bc=readback(d,b.color);unsigned changed=0,maxUlp=0,nonfinite=0;double maxAbs=0;bool alpha=true;
        auto ordered=[](unsigned short bits){return bits&0x8000?0x8000-int(bits&0x7fff):0x8000+int(bits);};
        for(UINT pixel=0;pixel<W*H;++pixel){alpha=alpha&&at<unsigned short>(ac,pixel*4+3)==at<unsigned short>(bc,pixel*4+3);
            for(UINT channel=0;channel<3;++channel){const auto ah=at<unsigned short>(ac,pixel*4+channel),bh=at<unsigned short>(bc,pixel*4+channel);changed+=ah!=bh;
                maxUlp=std::max(maxUlp,unsigned(std::abs(ordered(ah)-ordered(bh))));const float av=halfFloat(ah),bv=halfFloat(bh);
                if(std::isfinite(av)&&std::isfinite(bv))maxAbs=std::max(maxAbs,double(std::fabs(av-bv)));else ++nonfinite;}}
        const bool depth=readback(d,a.depth)==readback(d,b.depth);
        const bool mask=bool(a.reactive)==bool(b.reactive)&&(!a.reactive||readback(d,a.reactive)==readback(d,b.reactive));
        std::printf("LOOP_TWIN case=%s frame=%u rgb_changed=%u max_abs=%.9g max_ulp=%u nonfinite=%u alpha_exact=%u depth_exact=%u mask_exact=%u history_equal=%u\n",names[scenario],frame,changed,maxAbs,maxUlp,nonfinite,unsigned(alpha),unsigned(depth),unsigned(mask),unsigned(a.used_history==b.used_history));
        ++numeric_checks;require(!changed&&!nonfinite&&alpha&&depth&&mask&&a.used_history==b.used_history,"rolled resolve matches independent unrolled history exactly");
    }
}
// Submission-to-EVENT-completion wall time, not GPU-busy time or game FPS.
// Only run/Issue/GetData/QPC are inside each sample; no readback, source upload,
// logging, shader creation, allocation or initial history seeding is timed.
void loop_timings(IDirect3DDevice9* d,const DWORD* baseline,const DWORD* candidate){
    LARGE_INTEGER frequency{};check("timing QPC frequency",QueryPerformanceFrequency(&frequency)?S_OK:E_FAIL);
    Com<IDirect3DQuery9> completion;check("timing event query",d->CreateQuery(D3DQUERYTYPE_EVENT,&completion.p));
    auto stamp=[](){LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;};
    auto drain=[&](){check("timing issue completion",completion->Issue(D3DISSUE_END));const auto start=stamp();HRESULT hr;
        while((hr=completion->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){if(stamp()-start>frequency.QuadPart*10)throw std::runtime_error("timing event timeout");Sleep(0);}check("timing completion",hr);};
    Com<IDirect3DSurface9> saved;check("timing save target",d->GetRenderTarget(0,&saved.p));D3DVIEWPORT9 savedViewport{};check("timing save viewport",d->GetViewport(&savedViewport));
    const char* modes[]={"stationary","fractional","current_only","supplemental_empty"};
    for(auto size:{std::pair<UINT,UINT>{1280,768},{1920,1080}}){const UINT width=size.first,height=size.second;
        Com<IDirect3DTexture9> color,depth,raw;Com<IDirect3DSurface9> caller;
        check("timing color",d->CreateTexture(width,height,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&color.p,nullptr));
        check("timing raw mask",d->CreateTexture(width,height,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&raw.p,nullptr));
        check("timing depth",d->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));
        check("timing caller target",d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&caller.p,nullptr));
        for(auto* texture:{color.p,raw.p}){D3DLOCKED_RECT lock{};check("timing upload lock",texture->LockRect(0,&lock,nullptr,0));const unsigned short pixel[]={toHalf(.5f),toHalf(.25f),toHalf(1),toHalf(.375f)};
            for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto* destination=static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8;if(texture==raw.p)std::memset(destination,0,8);else std::memcpy(destination,pixel,8);}
            check("timing upload unlock",texture->UnlockRect(0));}
        {Com<IDirect3DTexture9> staging;check("timing depth staging",d->CreateTexture(width,height,1,0,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging.p,nullptr));D3DLOCKED_RECT lock{};check("timing depth lock",staging->LockRect(0,&lock,nullptr,0));const float z=.5f;
            for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&z,4);
            check("timing depth unlock",staging->UnlockRect(0));check("timing depth upload",d->UpdateTexture(staging.p,depth.p));}
        for(UINT stage=0;stage<16;++stage)check("timing clear sampler",d->SetTexture(stage,nullptr));
        check("timing clear depth",d->SetDepthStencilSurface(nullptr));for(UINT rt=1;rt<4;++rt)check("timing clear MRT",d->SetRenderTarget(rt,nullptr));check("timing bind caller",d->SetRenderTarget(0,caller.p));
        for(unsigned mode=0;mode<4;++mode){TemporalPass passes[2];check("timing baseline initialize",passes[0].initialize(d,nullptr,baseline));check("timing loop initialize",passes[1].initialize(d,nullptr,candidate));
            FrameInputs in{};in.color=color.p;in.current_depth=depth.p;in.width=width;in.height=height;in.epoch=mode+1;in.weight=.9f;std::copy(identity,identity+16,in.clip_to_previous);
            in.motion_policy=MotionPolicy::KnownCameraOnly;in.reactive_policy=ReactivePolicy::DerivedFromDepthSentinel;in.history_allowed=true;in.caller_queries_idle=true;in.caller_scene_open=false;
            if(mode==1){in.clip_to_previous[3]=.75f/width;in.clip_to_previous[7]=-.5f/height;}
            if(mode==2)in.reactive_policy=ReactivePolicy::Unavailable;
            if(mode==3){in.reactive_policy=ReactivePolicy::SupplementalMaskWithDepthSentinel;in.reactive=raw.p;}
            Output output[2];for(unsigned warm=0;warm<3;++warm)for(unsigned which=0;which<2;++which){check("timing warm run",passes[which].run(in,&output[which]));drain();}
            for(unsigned which=0;which<2;++which)require(output[which].used_history==(mode!=2),"timing warm history policy");
            double ms[2][6]{};for(unsigned pair=0;pair<6;++pair){const unsigned first=pair&1;
                for(unsigned step=0;step<2;++step){const unsigned which=first^step;drain();const auto start=stamp();check("timing measured run",passes[which].run(in,&output[which]));drain();ms[which][pair]=1000.*double(stamp()-start)/double(frequency.QuadPart);}
                std::printf("LOOP_TIMING_PAIR width=%u height=%u case=%s pair=%u first=%s baseline_ms=%.6f loop_ms=%.6f delta_ms=%.6f\n",width,height,modes[mode],pair,first?"loop":"baseline",ms[0][pair],ms[1][pair],ms[1][pair]-ms[0][pair]);}
            // Validate uniform output outside the timed interval, including exact
            // current alpha. Detailed mask/depth equivalence is in loop_twins.
            for(unsigned which=0;which<2;++which){const auto bytes=sharpen::surface(d,output[which].color_surface,width,height,D3DFMT_A16B16G16R16F,8);const UINT center=(height/2*width+width/2)*4;bool exact=true;
                const float expected[]={.5f,.25f,1,.375f};for(UINT c=0;c<4;++c)exact=exact&&at<unsigned short>(bytes,center+c)==toHalf(expected[c]);require(exact,"timed uniform image and alpha remain exact");}
            double baseSum=0,loopSum=0,deltaMin=1e30,deltaMax=-1e30;for(unsigned pair=0;pair<6;++pair){baseSum+=ms[0][pair];loopSum+=ms[1][pair];deltaMin=std::min(deltaMin,ms[1][pair]-ms[0][pair]);deltaMax=std::max(deltaMax,ms[1][pair]-ms[0][pair]);}
            std::printf("LOOP_TIMING_SUMMARY width=%u height=%u case=%s pairs=6 baseline_min_ms=%.6f baseline_max_ms=%.6f baseline_mean_ms=%.6f loop_min_ms=%.6f loop_max_ms=%.6f loop_mean_ms=%.6f paired_delta_min_ms=%.6f paired_delta_max_ms=%.6f paired_delta_mean_ms=%.6f\n",width,height,modes[mode],*std::min_element(ms[0],ms[0]+6),*std::max_element(ms[0],ms[0]+6),baseSum/6,*std::min_element(ms[1],ms[1]+6),*std::max_element(ms[1],ms[1]+6),loopSum/6,deltaMin,deltaMax,(loopSum-baseSum)/6);
        }
        check("timing restore caller target",d->SetRenderTarget(0,saved.p));check("timing restore caller viewport",d->SetViewport(&savedViewport));
    }
}
#include "sun_share_temporal_inc.h"
#include "temporal_flicker_inc.h"
#include "temporal_line_inc.h"
#include "temporal_far_inc.h"
#include "temporal_thin_region_inc.h"
#include "temporal_depth_fold_inc.h"
#include "temporal_history_taps_inc.h"
#include "temporal_region_hold_inc.h"
#include "temporal_box_half_inc.h"
#include "temporal_thin_source_inc.h"
#include "temporal_fold_timing_inc.h"
#include "temporal_far_camera_inc.h"
#include "temporal_far_jitter_line_inc.h"

int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3TemporalPassFixture";RegisterClassA(&cls);HWND window=CreateWindowA(cls.lpszClassName,"X3 temporal production module",WS_OVERLAPPEDWINDOW,90,90,128,128,nullptr,nullptr,cls.hInstance,nullptr);
    // Optional sixth argument: "stationary-only" runs just the stationary
    // stability scene (one generation), used to record the negative proof
    // against a resolve that applies the previous jitter; "sharpen-measure"
    // writes the sharpen measurement images (one generation).
    const bool sunLaneOnly=argc==6&&std::strcmp(argv[5],"sun-lane-only")==0;
    const bool stationaryOnly=argc==6&&std::strcmp(argv[5],"stationary-only")==0;
    const bool measure=argc==6&&std::strcmp(argv[5],"sharpen-measure")==0;
    const bool supplementalOnly=argc==7&&std::strcmp(argv[5],"supplemental-only")==0;
    const bool loopQualify=argc==7&&std::strcmp(argv[5],"loop-qualify")==0;
    const bool lattice=argc==6&&std::strcmp(argv[5],"lattice")==0;
    const bool regionHoldOnly=argc==6&&std::strcmp(argv[5],"region-hold")==0; // the A' cases alone (iteration; the runner uses lattice)
    const bool boxHalfOnly=argc==6&&std::strcmp(argv[5],"box-half")==0; // the S4 cases alone (iteration; the runner uses lattice)
    const bool thinSourceOnly=argc==6&&std::strcmp(argv[5],"thin-source")==0; // the thin-region source cases alone (iteration; the runner uses lattice)
    const bool foldTimingOnly=argc==6&&std::strcmp(argv[5],"fold-timing")==0; // FOLD_TIMING alone (the baseline build's mode; the runner uses lattice)
    const bool farCameraOnly=argc==6&&std::strcmp(argv[5],"far-camera")==0; // FAR_CAMERA_PAN alone (iteration; the runner uses lattice)
    const bool farJitterOnly=argc==6&&std::strcmp(argv[5],"far-jitter-line")==0; // FAR_JITTER_LINE alone (iteration; the runner uses lattice)
    try{if((argc!=5&&!sunLaneOnly&&!stationaryOnly&&!measure&&!supplementalOnly&&!loopQualify&&!lattice&&!regionHoldOnly&&!boxHalfOnly&&!thinSourceOnly&&!foldTimingOnly&&!farCameraOnly&&!farJitterOnly)||!window)throw std::runtime_error("usage: temporal_pass_fixture.exe <D3DX> <decoder> <resolve> <sharpen> [stationary-only|sharpen-measure|supplemental-only <baseline-resolve>|loop-qualify <baseline-resolve>|lattice]");Module runtime("d3d9.dll"),d3dx(argv[1]);auto compiler=symbol<Compiler>(d3dx.h,"D3DXCompileShader");Com<ID3DXBuffer> dc,rc,sc,foldTests;compile(compiler,file(argv[2]),"ps_3_0",&dc.p);resolveSource=file(argv[3]);compile(compiler,resolveSource,"ps_3_0",&rc.p);compile(compiler,"#define X3M_REGION_HOLD 1\n#define X3M_FOLD_TESTS_OUT 1\n"+resolveSource,"ps_3_0",&foldTests.p);foldTestsProgram=static_cast<DWORD*>(foldTests->GetBufferPointer());compile(compiler,file(argv[4]),"ps_3_0",&sc.p);auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime.h,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Create9");D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;Com<IDirect3DDevice9> d;check("CreateDevice",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&d.p));
        Com<ID3DXBuffer> baseline;
        if(supplementalOnly||loopQualify||lattice){
            D3DCAPS9 caps{};check("supplemental shader budget caps",d->GetDeviceCaps(&caps));
            auto disassemble=symbol<decltype(&D3DXDisassembleShader)>(d3dx.h,"D3DXDisassembleShader");
            auto budgetWords=[&](const DWORD* words,unsigned long count,const char* label){Com<ID3DXBuffer> assembly;
                check("supplemental resolve disassembly",disassemble(words,FALSE,nullptr,&assembly.p));
                std::istringstream lines(static_cast<const char*>(assembly->GetBufferPointer()));std::string line;unsigned slots=0;
                while(std::getline(lines,line))if(line.find("instruction slots used")!=std::string::npos){const auto start=line.find_first_of("0123456789");if(start!=std::string::npos)slots=unsigned(std::stoul(line.substr(start)));}
                std::printf("RESOLVE_BUDGET variant=%s dwords=%lu instruction_slots=%u device_limit=%lu headroom=%d within_ceiling_2048=%u\n",label,count,slots,caps.MaxPixelShader30InstructionSlots,int(caps.MaxPixelShader30InstructionSlots)-int(slots),unsigned(slots<=2048));
                require(slots>0&&words[0]==0xffff0300,"resolve instruction budget parsed for ps_3_0");return slots;};
            auto budget=[&](ID3DXBuffer* code,const char* label){return budgetWords(static_cast<DWORD*>(code->GetBufferPointer()),code->GetBufferSize()/sizeof(DWORD),label);};
            if(lattice)budget(rc.p,"plain");
            else{compile(compiler,file(argv[6]),"ps_3_0",&baseline.p);
                const unsigned oldSlots=budget(baseline.p,"baseline"),newSlots=budget(rc.p,loopQualify?"loop":"supplemental");
                if(loopQualify)require(newSlots<=512&&newSlots<=caps.MaxPixelShader30InstructionSlots,"rolled resolve fits guaranteed and advertised instruction budget");
                std::printf("RESOLVE_BUDGET_DELTA instruction_slots=%d dwords=%ld\n",int(newSlots)-int(oldSlots),long(rc->GetBufferSize()/sizeof(DWORD))-long(baseline->GetBufferSize()/sizeof(DWORD)));}
            // Every embedded TAA program (the ones the DLL creates) stays within the 2,048-slot ceiling of docs/architecture/
            // taa-plan-lifted-slot-cap.md section 2 (AGENTS.md "Shader slot budget": 512 is the spec minimum, not a limit;
            // device_limit is the reported cap, a record only).
            if(lattice){namespace r=x3m::renderer;
                #define X3M_BUDGET(program,label) require(budgetWords(reinterpret_cast<const DWORD*>(r::program()),sizeof(r::program())/sizeof(DWORD),label)<=2048,label " within the 2048-slot ceiling")
                X3M_BUDGET(temporal_resolve_program,"embedded_plain");X3M_BUDGET(temporal_resolve_snapshot_program,"embedded_snapshot");
                X3M_BUDGET(temporal_resolve_thin_program,"embedded_thin");X3M_BUDGET(temporal_resolve_age_program,"embedded_age");
                X3M_BUDGET(temporal_line_mask_program,"embedded_line_mask");X3M_BUDGET(temporal_resolve_far_program,"embedded_far");
                X3M_BUDGET(temporal_line_mask_depth_program,"embedded_line_mask_depth");
                // Thin vote (taa-thin-geometry-alternatives.md section 3.2): the screen-gate chain's twin, with the vote-only source's c10.y branch.
                X3M_BUDGET(temporal_line_mask_depth_thin_program,"embedded_line_mask_depth_thin");
                // S3: the 16-tap point twins (--taa-history-taps 16), the bytecode of the earlier four resolve programs (the camera gate has none).
                X3M_BUDGET(temporal_resolve_taps16_program,"embedded_plain_taps16");X3M_BUDGET(temporal_resolve_thin_taps16_program,"embedded_thin_taps16");X3M_BUDGET(temporal_resolve_age_taps16_program,"embedded_age_taps16");X3M_BUDGET(temporal_resolve_far_taps16_program,"embedded_far_taps16");
                // A' with the mask fold (the camera gate's only path): the folded hold resolve (the tests, the holds, RT2 depth) and its
                // 49-tap box; the fixture's X3M_FOLD_TESTS_OUT twin beside it (not embedded).
                X3M_BUDGET(temporal_resolve_far_camera_hold_program,"embedded_far_camera_hold");X3M_BUDGET(temporal_thin_box_hold_program,"embedded_thin_box_hold");
                budget(foldTests.p,"fixture_fold_tests_out");
                // S4 (opt-in X3M_TAA_BOX_RESOLUTION=half): the half-resolution pair.
                X3M_BUDGET(temporal_thin_box_rows_half_program,"embedded_thin_box_rows_half");X3M_BUDGET(temporal_thin_box_columns_half_program,"embedded_thin_box_columns_half");
                #undef X3M_BUDGET
            }
            // The retained baseline also exceeds the advertised limit on X3.
            // Report this unexplained portability concern; actual shader creation
            // and execution below qualify only this backend, not cap compliance.
        }
        auto* sharpener=static_cast<DWORD*>(sc->GetBufferPointer());
        if(lattice){lattice_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));const unsigned latticeNumeric=numeric_checks,latticeState=state_checks;std::printf("LATTICE_BASE numerical=%u state_restorations=%u\n",latticeNumeric,latticeState);flicker_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("FLICKER_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);line_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("LINE_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);far_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("FAR_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);thin_region_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));depth_fold_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()),pp);std::printf("DEPTH_FOLD_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);history_taps::history_taps_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()),pp);std::printf("HISTORY_TAPS_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);region_hold_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("REGION_HOLD_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);box_half_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("BOX_HALF_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);thin_source_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()),pp);std::printf("THIN_SOURCE_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);fold_timing_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("FOLD_TIMING_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);far_camera_pan_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("FAR_CAMERA_PAN_BASE numerical=%u state_restorations=%u\n",numeric_checks,state_checks);far_jitter_line_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u state_restorations=%u lattice=1\n",numeric_checks,state_checks);result=0;}
        else if(farJitterOnly){far_jitter_line_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u state_restorations=%u far_jitter_line_only=1\n",numeric_checks,state_checks);result=0;}
        else if(farCameraOnly){far_camera_pan_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u state_restorations=%u far_camera_only=1\n",numeric_checks,state_checks);result=0;}
        else if(foldTimingOnly){fold_timing_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u state_restorations=%u fold_timing_only=1\n",numeric_checks,state_checks);result=0;}
        else if(thinSourceOnly){thin_source_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()),pp);std::printf("RESULT PASS numerical=%u state_restorations=%u thin_source_only=1\n",numeric_checks,state_checks);result=0;}
        else if(boxHalfOnly){box_half_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u state_restorations=%u box_half_only=1\n",numeric_checks,state_checks);result=0;}
        else if(regionHoldOnly){region_hold_cases(d.p,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u state_restorations=%u region_hold_only=1\n",numeric_checks,state_checks);result=0;}
        else if(sunLaneOnly){sun_lane_cases(d.p,pp,compiler,static_cast<DWORD*>(rc->GetBufferPointer()));result=0;}
        else if(stationaryOnly){stationary_cases(d.p,compiler,static_cast<DWORD*>(dc->GetBufferPointer()),static_cast<DWORD*>(rc->GetBufferPointer()));std::printf("RESULT PASS numerical=%u stationary_only=1\n",numeric_checks);result=0;}
        else if(measure){sharpen_measure(d.p,compiler,static_cast<DWORD*>(dc->GetBufferPointer()),static_cast<DWORD*>(rc->GetBufferPointer()),sharpener);std::printf("RESULT PASS numerical=%u sharpen_measure=1\n",numeric_checks);result=0;}
        else if(supplementalOnly){auto* decoder=static_cast<DWORD*>(dc->GetBufferPointer());auto* resolver=static_cast<DWORD*>(rc->GetBufferPointer());reactive_cases(d.p,compiler,decoder,resolver);supplemental_cases(d.p,pp,compiler,decoder,resolver);std::printf("RESULT PASS numerical=%u state_restorations=%u supplemental_only=1\n",numeric_checks,state_checks);result=0;}
        else for(unsigned generation=0;generation<2;++generation){auto* decoder=static_cast<DWORD*>(dc->GetBufferPointer());auto* resolver=static_cast<DWORD*>(rc->GetBufferPointer());cases(d.p,compiler,decoder,resolver,generation);reactive_cases(d.p,compiler,decoder,resolver);route_cases(d.p,compiler,decoder,resolver);stationary_cases(d.p,compiler,decoder,resolver);edge_cases(d.p,compiler,decoder,resolver);motion_weight_cases(d.p,compiler,decoder,resolver);camera_cases(d.p,compiler,decoder,resolver);hdr_cases(d.p,compiler,decoder,resolver);sharpen_cases(d.p,compiler,decoder,resolver,sharpener);quad_twin_cases(d.p,compiler,decoder,resolver,sharpener);if(!generation)reset_continuity(d.p,pp,compiler,decoder,resolver);}
        if(loopQualify){auto* decoder=static_cast<DWORD*>(dc->GetBufferPointer());auto* resolver=static_cast<DWORD*>(rc->GetBufferPointer());auto* unrolled=static_cast<DWORD*>(baseline->GetBufferPointer());
            std::printf("LOOP_FULL_SUITE numerical=%u state_restorations=%u generations=2\n",numeric_checks,state_checks);
            supplemental_cases(d.p,pp,compiler,decoder,resolver);loop_twins(d.p,compiler,decoder,unrolled,resolver);loop_timings(d.p,unrolled,resolver);
            std::printf("RESULT PASS numerical=%u state_restorations=%u loop_qualify=1\n",numeric_checks,state_checks);result=0;}
        if(!sunLaneOnly&&!stationaryOnly&&!measure&&!supplementalOnly&&!loopQualify&&!lattice&&!regionHoldOnly&&!boxHalfOnly&&!thinSourceOnly&&!foldTimingOnly&&!farCameraOnly&&!farJitterOnly){std::printf("RESULT PASS numerical=%u state_restorations=%u generations=2\n",numeric_checks,state_checks);result=0;}
    }catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());}if(window)DestroyWindow(window);UnregisterClassA(cls.lpszClassName,cls.hInstance);return result;}
