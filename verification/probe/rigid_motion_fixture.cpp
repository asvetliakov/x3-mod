// Calls the production temporal runtime and actual production shader bytecode.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/rigid_motion.h"
#include "../../src/renderer/rigid_motion_pixel_program.h"
#include "../../src/temporal/resolve.h"
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
#include <type_traits>
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
unsigned checks=0;
void require(bool value,const char* label){++checks;std::printf("CHECK %s %s\n",label,value?"PASS":"FAIL");if(!value)throw std::runtime_error(label);}
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
using Assembler=decltype(&D3DXAssembleShader);
Assembler assemble_shader=nullptr;
void assemble(const std::string& source,ID3DXBuffer** code){Com<ID3DXBuffer> errors;
    HRESULT hr=assemble_shader(source.c_str(),UINT(source.size()),nullptr,nullptr,0,code,&errors.p);
    if(errors.p)std::printf("ASSEMBLER %s\n",static_cast<char*>(errors->GetBufferPointer()));
    check("independent original assembly",hr);
}
using Compiler=decltype(&D3DXCompileShader);
void compile(Compiler c,const std::string& source,const char* target,ID3DXBuffer** code){Com<ID3DXBuffer> errors;
    HRESULT hr=c(source.c_str(),UINT(source.size()),nullptr,nullptr,"main",target,D3DXSHADER_OPTIMIZATION_LEVEL3,code,&errors.p,nullptr);
    if(errors.p)std::printf("COMPILER %s\n",static_cast<char*>(errors->GetBufferPointer()));
    check(target,hr);}
using namespace x3m::renderer;
UINT W=16,H=16;
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
        const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,D3DRS_STENCILPASS,D3DRS_STENCILZFAIL,D3DRS_TWOSIDEDSTENCILMODE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_VERTEXBLEND,D3DRS_FILLMODE,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,D3DRS_MULTISAMPLEMASK,D3DRS_WRAP0,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_POINTSIZE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS};
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
    using Draw=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
    using Texture=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
    static inline Draw original=nullptr;static inline Texture textureOriginal=nullptr;
    static inline HRESULT drawFailure=S_OK;
    static inline bool drawn=false,restoreFailure=false,restoreLoss=false;
    static inline unsigned restoreCalls=0;
    void** previous;void* table[119];IDirect3DDevice9* d;
    static HRESULT WINAPI draw(IDirect3DDevice9* d,D3DPRIMITIVETYPE p,UINT start,UINT count){
        drawn=true;return FAILED(drawFailure)?drawFailure:original(d,p,start,count);
    }
    static HRESULT WINAPI texture(IDirect3DDevice9* d,DWORD stage,IDirect3DBaseTexture9* t){
        if(drawn){++restoreCalls;if(restoreFailure&&restoreCalls==1)return E_FAIL;
            if(restoreLoss&&restoreCalls==2)return D3DERR_DEVICELOST;}
        return textureOriginal(d,stage,t);
    }
    Fault(IDirect3DDevice9* device,HRESULT failure,bool restore=false,bool loss=false):previous(*reinterpret_cast<void***>(device)),d(device){
        std::copy(previous,previous+119,table);std::memcpy(&original,&table[81],sizeof original);
        std::memcpy(&textureOriginal,&table[65],sizeof textureOriginal);
        auto a=&draw;auto b=&texture;std::memcpy(&table[81],&a,sizeof a);std::memcpy(&table[65],&b,sizeof b);
        drawFailure=failure;drawn=false;restoreFailure=restore;restoreLoss=loss;restoreCalls=0;
        *reinterpret_cast<void***>(device)=table;
    }
    ~Fault(){*reinterpret_cast<void***>(d)=previous;}
};
struct Pixel {float x,y,z,w;};
void numeric(float actual,float expected,const char* label,float tolerance=1e-4f){
    ++numeric_checks;bool okay=std::isfinite(actual)&&std::fabs(actual-expected)<=tolerance;
    std::printf("SAMPLE %s actual=%.9f expected=%.9f %s\n",label,actual,expected,okay?"PASS":"FAIL");
    if(!okay)throw std::runtime_error(label);
}
struct Fixture {
    IDirect3DDevice9* d;
    Com<IDirect3DTexture9> motion;
    Com<IDirect3DSurface9> depth,scene,secondary,back;
    Com<IDirect3DVertexBuffer9> floats,halves;
    Com<IDirect3DIndexBuffer9> indices;
    Com<IDirect3DVertexDeclaration9> floatDecl,halfDecl;
    Com<IDirect3DVertexShader9> sceneVS;
    Com<IDirect3DPixelShader9> scenePS;
    Fixture(IDirect3DDevice9* device,Compiler compiler):d(device){
        check("motion RT",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));
        check("scene DS",d->CreateDepthStencilSurface(W,H,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,FALSE,&depth.p,nullptr));
        check("scene RT",d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&scene.p,nullptr));
        check("secondary RT",d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&secondary.p,nullptr));
        check("back",d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p));
        check("float VB",d->CreateVertexBuffer(256,0,0,D3DPOOL_MANAGED,&floats.p,nullptr));
        check("half VB",d->CreateVertexBuffer(256,0,0,D3DPOOL_MANAGED,&halves.p,nullptr));
        check("IB",d->CreateIndexBuffer(32,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr));
        // Unrelated leading stream bytes and vertex fields exercise both offsets.
        // One oversized triangle covers the viewport; W in packed input is 7.
        const float positions[3][3]={{-1,1,.5f},{3,1,.5f},{-1,-3,.5f}};
        void* bytes=nullptr;check("float lock",floats->Lock(0,0,&bytes,0));std::memset(bytes,0xcc,256);
        for(UINT i=0;i<3;++i)std::memcpy(static_cast<char*>(bytes)+16+4+i*24,positions[i],12);
        check("float unlock",floats->Unlock());
        check("half lock",halves->Lock(0,0,&bytes,0));std::memset(bytes,0xdd,256);
        for(UINT i=0;i<3;++i){unsigned short p[4]={toHalf(positions[i][0]),toHalf(positions[i][1]),toHalf(1.f/3),toHalf(7)};
            std::memcpy(static_cast<char*>(bytes)+16+4+i*16,p,8);}
        check("half unlock",halves->Unlock());
        check("index lock",indices->Lock(0,0,&bytes,0));std::memset(bytes,0,32);
        const unsigned short order[3]={0,1,2};std::memcpy(static_cast<char*>(bytes)+4,order,6);check("index unlock",indices->Unlock());
        D3DVERTEXELEMENT9 elements[]={{0,4,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        check("float declaration",d->CreateVertexDeclaration(elements,&floatDecl.p));elements[0].Type=D3DDECLTYPE_FLOAT16_4;
        check("half declaration",d->CreateVertexDeclaration(elements,&halfDecl.p));
        Com<ID3DXBuffer> vs,ps;
        // Independent original synthetic source contract: SM3, exact homogeneous
        // MAD constructor, temp-before-row full-precision DP4, XYZW write order.
        // It is explicitly issued as synthetic, never claimed as an archive hash.
        assemble("vs_3_0\ndef c20, 1, 0, 0, 0\ndcl_position v0\ndcl_position o0\n"
                 "mad r3, v0.xyzx, c20.xxxy, c20.yyyx\n"
                 "dp4 o0.x, r3, c0\ndp4 o0.y, r3, c1\ndp4 o0.z, r3, c2\ndp4 o0.w, r3, c3\n",&vs.p);
        compile(compiler,"float4 color:register(c0);float4 main():COLOR0{return color;}","ps_3_0",&ps.p);
        check("scene VS",d->CreateVertexShader(static_cast<DWORD*>(vs->GetBufferPointer()),&sceneVS.p));
        check("scene PS",d->CreatePixelShader(static_cast<DWORD*>(ps->GetBufferPointer()),&scenePS.p));
    }
    ~Fixture(){
        for(UINT n=0;n<20;++n)d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr);
        d->SetRenderTarget(1,nullptr);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,back.p);
        d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetVertexDeclaration(nullptr);
        d->SetStreamSource(0,nullptr,0,0);d->SetStreamSource(1,nullptr,0,0);d->SetIndices(nullptr);
        d->SetStreamSourceFreq(0,1);d->SetStreamSourceFreq(1,1);
    }
    RigidMotionDraw draw(bool half=false,bool indexed=false){
        RigidMotionDraw x;x.source_program=original_synthetic_sm3_contract();x.finite_positions_attested=true;
        x.vertices=half?halves.p:floats.p;x.indices=indexed?indices.p:nullptr;
        x.stream_offset=16;x.position_offset=4;x.stride=half?16:24;
        x.position_type=half?D3DDECLTYPE_FLOAT16_4:D3DDECLTYPE_FLOAT3;
        x.primitive_count=1;x.vertex_count=3;x.start_index=2;x.correspondence_attested=true;
        std::copy(identity,identity+16,x.current_wvp);std::copy(identity,identity+16,x.previous_wvp);return x;
    }
    RigidMotionInputs input(const RigidMotionDraw* draws,std::size_t count=1){
        RigidMotionInputs in;in.motion=motion.p;in.scene_depth=depth.p;in.width=W;in.height=H;
        in.draws=draws;in.draw_count=count;in.scene_depth_current=true;in.caller_queries_idle=true;return in;
    }
    void geometry(const RigidMotionDraw& x,const float* color){
        check("scene decl",d->SetVertexDeclaration(x.position_type==D3DDECLTYPE_FLOAT16_4?halfDecl.p:floatDecl.p));
        check("scene stream",d->SetStreamSource(0,x.vertices,x.stream_offset,x.stride));
        check("scene indices",d->SetIndices(x.indices));check("scene matrix",d->SetVertexShaderConstantF(0,x.current_wvp,4));
        check("scene color",d->SetPixelShaderConstantF(0,color,1));
        check("scene draw",x.indices?d->DrawIndexedPrimitive(x.topology,x.base_vertex,x.minimum_vertex,x.vertex_count,x.start_index,x.primitive_count):d->DrawPrimitive(x.topology,x.start_vertex,x.primitive_count));
    }
    void scene_state(){
        for(UINT n=0;n<20;++n)check("scene unbind texture",d->SetTexture(n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16,nullptr));
        check("scene unbind MRT",d->SetRenderTarget(1,nullptr));check("scene RT",d->SetRenderTarget(0,scene.p));check("scene DS",d->SetDepthStencilSurface(depth.p));
        D3DVIEWPORT9 vp{0,0,W,H,0,1};check("scene VP",d->SetViewport(&vp));
        check("scene freq0",d->SetStreamSourceFreq(0,1));check("scene freq1",d->SetStreamSourceFreq(1,1));
        for(auto state:{D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})check("scene disable",d->SetRenderState(state,FALSE));
        for(auto p:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_COLORWRITEENABLE,15},{D3DRS_CLIPPING,TRUE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_WRAP0,0},{D3DRS_VERTEXBLEND,D3DVBF_DISABLE}})check("scene state",d->SetRenderState(p.first,p.second));
        check("scene VS bind",d->SetVertexShader(sceneVS.p));check("scene PS bind",d->SetPixelShader(scenePS.p));
    }
    void render_scene(const RigidMotionDraw& x,bool occluder=false){
        scene_state();check("clear scene",d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
        const float white[4]={1,1,1,1},red[4]={1,0,0,1};check("scene Begin",d->BeginScene());geometry(x,white);
        if(occluder){auto front=x;std::copy(identity,identity+16,front.current_wvp);front.current_wvp[10]=0;front.current_wvp[11]=.25f;
            RECT right{9,0,16,16};check("occluder scissor",d->SetScissorRect(&right));check("occluder enable scissor",d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));geometry(front,red);check("occluder disable scissor",d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));}
        check("scene End",d->EndScene());
    }
    void hostile(){
        check("hostile RT0",d->SetRenderTarget(0,scene.p));check("hostile RT1",d->SetRenderTarget(1,secondary.p));check("hostile DS",d->SetDepthStencilSurface(depth.p));
        D3DVIEWPORT9 vp{2,3,10,9,.2f,.8f};RECT rect{3,4,6,7};check("hostile VP",d->SetViewport(&vp));check("hostile scissor",d->SetScissorRect(&rect));
        check("hostile decl",d->SetVertexDeclaration(halfDecl.p));check("hostile VS",d->SetVertexShader(sceneVS.p));check("hostile PS",d->SetPixelShader(scenePS.p));
        check("hostile VB0",d->SetStreamSource(0,halves.p,16,16));check("hostile VB1",d->SetStreamSource(1,floats.p,24,24));check("hostile IB",d->SetIndices(indices.p));
        check("hostile instance0",d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|3));check("hostile instance1",d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|2));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE})check("hostile enable",d->SetRenderState(state,TRUE));
        for(auto p:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZFUNC,D3DCMP_NEVER},{D3DRS_ALPHAFUNC,D3DCMP_NEVER},{D3DRS_STENCILFUNC,D3DCMP_NEVER},{D3DRS_CLIPPLANEENABLE,1},{D3DRS_VERTEXBLEND,D3DVBF_1WEIGHTS},{D3DRS_CULLMODE,D3DCULL_CW},{D3DRS_FILLMODE,D3DFILL_WIREFRAME},{D3DRS_COLORWRITEENABLE,2},{D3DRS_MULTISAMPLEMASK,0},{D3DRS_WRAP0,D3DWRAP_U|D3DWRAP_V},{D3DRS_DEPTHBIAS,0x3c800000},{D3DRS_SLOPESCALEDEPTHBIAS,0x3f000000}})check("hostile RS",d->SetRenderState(p.first,p.second));
        float constants[32];for(UINT i=0;i<32;++i)constants[i]=i*.125f+3;check("hostile PS constants",d->SetPixelShaderConstantF(0,constants,8));check("hostile VS constants",d->SetVertexShaderConstantF(0,constants,8));
        for(UINT n=0;n<20;++n){UINT slot=n<16?n:D3DVERTEXTEXTURESAMPLER0+n-16;check("hostile texture",d->SetTexture(slot,motion.p));}
    }
    std::vector<Pixel> read_motion(){
        Com<IDirect3DSurface9> surface,readback;check("read source",motion->GetSurfaceLevel(0,&surface.p));
        check("readback create",d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));
        check("test-only readback",d->GetRenderTargetData(surface.p,readback.p));D3DLOCKED_RECT lock{};check("readback lock",readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<Pixel> pixels(W*H);for(UINT y=0;y<H;++y)std::memcpy(&pixels[y*W],static_cast<char*>(lock.pBits)+y*lock.Pitch,W*sizeof(Pixel));check("readback unlock",readback->UnlockRect());return pixels;
    }
    float read_scene(UINT x,UINT y,UINT component){
        Com<IDirect3DSurface9> readback;check("scene readback create",d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));check("scene test-only readback",d->GetRenderTargetData(scene.p,readback.p));D3DLOCKED_RECT lock{};check("scene readback lock",readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));unsigned short value;std::memcpy(&value,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8+component*2,2);check("scene readback unlock",readback->UnlockRect());return halfFloat(value);
    }
    void run(RigidMotionPass& pass,RigidMotionInputs in,const char* label,bool open=true){
        hostile();Snapshot before(d);in.caller_scene_open=open;
        if(open)check("caller BeginScene",d->BeginScene());
        RigidMotionOutput out;check(label,pass.run(in,&out));
        if(open)check("caller EndScene",d->EndScene());
        before.equals(d,label);
        require(out.motion==motion.p&&out.generation!=0,"atomic borrowed output");
    }
};
// Independent analytic ray/plane oracle: solve raster NDC constraints for the
// original z=.5 plane. It does not use shader outputs or interpolate GPU values.
Pixel expected(const RigidMotionDraw& x,UINT px,UINT py,const float* jitter){
    const double nx=2.*px/W-1,ny=1-2.*py/H,z=x.position_type==D3DDECLTYPE_FLOAT16_4?halfFloat(toHalf(1.f/3)):.5;
    const float* a=x.current_wvp;double A=a[0]-nx*a[12],B=a[1]-nx*a[13],C=nx*(a[14]*z+a[15])-(a[2]*z+a[3]);
    double D=a[4]-ny*a[12],E=a[5]-ny*a[13],F=ny*(a[14]*z+a[15])-(a[6]*z+a[7]);
    double denominator=A*E-B*D,position[4]={(C*E-B*F)/denominator,(A*F-C*D)/denominator,z,1};
    double old[4]{};for(UINT row=0;row<4;++row)for(UINT col=0;col<4;++col)old[row]+=x.previous_wvp[4*row+col]*position[col];
    return {float(old[0]/old[3]*.5+.5+.5/W-jitter[0]/W),float(-old[1]/old[3]*.5+.5+.5/H-jitter[1]/H),float(old[2]/old[3]),1};
}
void sample(Fixture& f,const RigidMotionDraw& x,const float* jitter,UINT px,UINT py,const char* label){
    auto actual=f.read_motion()[py*W+px];auto want=expected(x,px,py,jitter);
    std::printf("PIXEL %s (%u,%u) %.9f %.9f %.9f %.9f\n",label,px,py,actual.x,actual.y,actual.z,actual.w);
    const float a[]={actual.x,actual.y,actual.z,actual.w},b[]={want.x,want.y,want.z,want.w};
    for(UINT i=0;i<4;++i){char name[128];std::snprintf(name,sizeof name,"%s component%u",label,i);numeric(a[i],b[i],name);}
}
void consume_motion(Fixture& f,const DWORD* resolver);
void cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver,UINT generation){
    std::printf("GENERATION %u\n",generation);Fixture f(d,compiler);RigidMotionPass pass;check("initialize production",pass.initialize(d));
    auto x=f.draw();auto in=f.input(&x);
    f.render_scene(x);numeric(f.read_scene(8,8,0),1,"original scene center");f.run(pass,in,"stationary rigid");sample(f,x,in.previous_jitter,8,8,"stationary center");
    x.current_wvp[3]=.25f;f.render_scene(x);f.run(pass,in,"object translation owned scene",false);sample(f,x,in.previous_jitter,8,8,"object translation");
    consume_motion(f,resolver);
    x=f.draw(true,true);x.current_wvp[3]=-.125f;x.previous_wvp[7]=.125f;
    x.current_wvp[8]=.125f;x.current_wvp[9]=.0625f;x.current_wvp[10]=.5f;x.current_wvp[11]=.25f;
    f.render_scene(x);f.run(pass,in,"packed half indexed");sample(f,x,in.previous_jitter,10,6,"half numeric conversion and forced W1");
    x=f.draw();x.current_wvp[12]=.25f;x.current_wvp[13]=.125f;x.previous_wvp[13]=-.125f;
    x.previous_wvp[3]=.125f;x.previous_wvp[11]=.125f;
    f.render_scene(x);f.run(pass,in,"perspective varying W");sample(f,x,in.previous_jitter,10,6,"perspective correspondence");sample(f,x,in.previous_jitter,6,10,"perspective second ray");
    x=f.draw(true,true);in.previous_jitter[0]=-.375f;in.previous_jitter[1]=.25f;
    // Current camera/object motion plus actual current jitter; previous matrix
    // contains previous jitter. Resolve will add previous jitter back once.
    x.current_wvp[3]=.25f+2*.25f/W;x.current_wvp[7]=-2*(-.375f)/H;
    x.previous_wvp[3]=2*in.previous_jitter[0]/W;x.previous_wvp[7]=-2*in.previous_jitter[1]/H;
    f.render_scene(x);f.run(pass,in,"distinct submitted jitter");sample(f,x,in.previous_jitter,8,8,"jitter removed once");
    x=f.draw();in=f.input(&x);f.render_scene(x,true);f.run(pass,in,"unsupported nearer occluder");
    sample(f,x,in.previous_jitter,4,8,"visible supported surface");numeric(f.read_motion()[8*W+12].w,-1,"unsupported occluder invalid");
    numeric(f.read_scene(4,8,1),1,"scene color unchanged visible");numeric(f.read_scene(12,8,1),0,"scene color unchanged occluded");
    // Depth semantics: .375 must pass against untouched .5 on the left, but
    // fail against untouched .25 on the right. No CPU depth readback is needed.
    f.scene_state();auto middle=x;middle.current_wvp[10]=0;middle.current_wvp[11]=.375f;
    const float blue[4]={0,0,1,1};check("depth integrity Begin",d->BeginScene());f.geometry(middle,blue);check("depth integrity End",d->EndScene());
    numeric(f.read_scene(4,8,0),0,"depth left accepts middle");numeric(f.read_scene(12,8,0),1,"depth right rejects middle");
    f.render_scene(x);in.draw_count=0;f.run(pass,in,"empty batch invalidates all pixels");
    bool all_invalid=true;for(auto px:f.read_motion())all_invalid=all_invalid&&px.w==-1;
    require(all_invalid,"empty output contains no camera fallback");in.draw_count=1;
    x.previous_wvp[15]=-1;f.run(pass,in,"previous behind camera");numeric(f.read_motion()[8*W+8].w,-1,"previous negative W invalid");
    x=f.draw();x.previous_wvp[11]=1;f.run(pass,in,"previous depth outside clip");numeric(f.read_motion()[8*W+8].w,-1,"previous depth invalid");
    x=f.draw();f.render_scene(x);x.current_wvp[11]=.125f;f.run(pass,in,"mismatched current depth");numeric(f.read_motion()[8*W+8].w,-1,"depth EQUAL rejects mismatch");
    x=f.draw();f.render_scene(x);f.hostile();Snapshot rejected(d);RigidMotionOutput out;
    auto refuse=[&](const char* label){require(pass.run(in,&out)==E_INVALIDARG&&!out.motion,label);};
    x.source_program={};refuse("unknown source program refused");x.source_program=original_synthetic_sm3_contract();
    x.finite_positions_attested=false;refuse("unverified finite POSITION payload refused");x.finite_positions_attested=true;
    x.correspondence_attested=false;refuse("unattested correspondence refused");x.correspondence_attested=true;
    x.position_type=D3DDECLTYPE_FLOAT4;refuse("unverified conversion refused");x.position_type=D3DDECLTYPE_FLOAT3;
    x.stream_frequency=D3DSTREAMSOURCE_INDEXEDDATA|2;refuse("instancing refused");x.stream_frequency=1;
    x.topology=D3DPT_LINELIST;refuse("line topology refused");x.topology=D3DPT_TRIANGLELIST;
    x.previous_wvp[0]=NAN;refuse("nonfinite WVP refused");x.previous_wvp[0]=1;
    x.stream_offset=UINT_MAX;refuse("stream byte overflow refused");x.stream_offset=16;
    x.primitive_count=UINT_MAX;x.start_vertex=UINT_MAX;x.stride=UINT_MAX;refuse("extreme count stride overflow refused");x=f.draw();
    x.indices=f.indices.p;x.vertex_count=UINT_MAX;x.minimum_vertex=UINT_MAX;x.base_vertex=INT_MAX;x.stride=UINT_MAX;refuse("extreme indexed vertex range refused");x=f.draw();
    x.indices=f.indices.p;x.vertex_count=3;x.start_index=UINT_MAX;refuse("index range overflow refused");x=f.draw();
    in.scene_depth_current=false;refuse("stale depth refused");in.scene_depth_current=true;
    in.previous_jitter[1]=INFINITY;refuse("nonfinite jitter refused");in.previous_jitter[1]=0;
    in.width=W+1;refuse("target extent mismatch refused");in.width=W;
    in.draw_count=65537;refuse("unbounded batch refused");in.draw_count=1;
    in.caller_stateblock_recording=true;check("caller stateblock begin",d->BeginStateBlock());refuse("stateblock recording refused");Com<IDirect3DStateBlock9> record;check("caller stateblock end",d->EndStateBlock(&record.p));in.caller_stateblock_recording=false;
    Com<IDirect3DQuery9> query;check("occlusion query",d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query.p));check("query begin",query->Issue(D3DISSUE_BEGIN));in.caller_queries_idle=false;refuse("active or unknown query refused");check("query end",query->Issue(D3DISSUE_END));in.caller_queries_idle=true;
    rejected.equals(d,"all prevalidation refusals preserve caller state");
    DWORD pixels=~0u;HRESULT ready=S_FALSE;for(UINT i=0;i<1000&&ready==S_FALSE;++i){ready=query->GetData(&pixels,sizeof pixels,D3DGETDATA_FLUSH);if(ready==S_FALSE)Sleep(1);}check("query result",ready);require(ready==S_OK&&pixels==0,"refused pass emits zero query samples");
    // Labeled native-vtable HRESULT injection checks failure/restore orchestration.
    f.hostile();Snapshot failedBefore(d);check("failure Begin",d->BeginScene());
    {Fault fault(d,E_FAIL);require(pass.run(in,&out)==E_FAIL&&!out.motion&&pass.diagnostics().completed_draws==0,"draw error refuses publication");}
    check("failure End",d->EndScene());failedBefore.equals(d,"ordinary failure restoration");
    f.hostile();Snapshot restoreBefore(d);check("restore failure Begin",d->BeginScene());
    {Fault fault(d,S_OK,true,false);require(pass.run(in,&out)==E_FAIL&&!out.motion&&pass.diagnostics().operation==S_OK&&pass.diagnostics().restoration==E_FAIL,"restore error refuses publication");}
    check("restore failure End",d->EndScene());restoreBefore.equals(d,"ordinary restore error continues state restoration");
    f.hostile();check("loss Begin",d->BeginScene());
    {Fault fault(d,D3DERR_DEVICELOST);require(pass.run(in,&out)==D3DERR_DEVICELOST&&!out.motion&&Fault::restoreCalls==0,"draw loss stops ordinary setters");}
    check("loss End test-only repair",d->EndScene());f.hostile();
    check("restore loss Begin",d->BeginScene());
    {Fault fault(d,S_OK,true,true);require(pass.run(in,&out)==D3DERR_DEVICELOST&&!out.motion&&Fault::restoreCalls==2,"restore loss overrides error and stops setters");}
    check("restore loss End test-only repair",d->EndScene());f.hostile();
    f.render_scene(x);f.run(pass,in,"recovery after injected failures");sample(f,x,in.previous_jitter,8,8,"recovered producer");
    pass.before_reset();require(pass.run(in,&out)==E_INVALIDARG&&!out.motion,"before_reset retires pass");
}
void consume_motion(Fixture& f,const DWORD* resolver){
    auto* d=f.d;Com<IDirect3DTexture9> current,previous,depth;
    Com<IDirect3DSurface9> target;Com<IDirect3DPixelShader9> ps;
    check("resolve current",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&current.p,nullptr));
    check("resolve previous",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&previous.p,nullptr));
    check("resolve depth",d->CreateTexture(W,H,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&depth.p,nullptr));
    check("resolve target",d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr));
    check("actual production resolve PS",d->CreatePixelShader(resolver,&ps.p));
    for(UINT which=0;which<3;++which){IDirect3DTexture9* texture=which==0?current.p:which==1?previous.p:depth.p;D3DLOCKED_RECT lock{};
        check("resolve upload lock",texture->LockRect(0,&lock,nullptr,0));
        for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){if(which==2){float z=.5f;std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&z,4);}else{
            float v=which==0?((x+y)%2?1.f:.25f):(x==6?.75f:.875f);unsigned short color[]={toHalf(v),toHalf(v),toHalf(v),toHalf(1)};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,color,8);}}
        check("resolve upload unlock",texture->UnlockRect(0));
    }
    f.scene_state();check("resolve no DS",d->SetDepthStencilSurface(nullptr));check("resolve RT",d->SetRenderTarget(0,target.p));
    check("resolve disable Z",d->SetRenderState(D3DRS_ZENABLE,FALSE));check("resolve disable writes",d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    check("resolve VS",d->SetVertexShader(nullptr));check("resolve FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));check("resolve PS",d->SetPixelShader(ps.p));
    const std::array<IDirect3DTexture9*,5> textures={current.p,depth.p,previous.p,depth.p,f.motion.p};
    for(UINT slot=0;slot<5;++slot){check("resolve bind input",d->SetTexture(slot,textures[slot]));
        for(auto setting:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})check("resolve sampler",d->SetSamplerState(slot,setting.first,setting.second));}
    x3::temporal::HistoryState history;history.begin(W,H,1);history.completed();x3::temporal::ResolveConstants constants;
    require(x3::temporal::prepare(constants,history,identity,0,0,0,0,.5f,true),"prepare actual resolve ABI");
    struct V{float x,y,z,rhw,u,v;};const V vertices[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    for(UINT enabled=0;enabled<2;++enabled){constants.options[0]=float(enabled);check("resolve constants",d->SetPixelShaderConstantF(0,&constants.clip_to_previous[0][0],8));
        check("resolve Begin",d->BeginScene());check("consume produced GPU motion",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V)));check("resolve End",d->EndScene());
        Com<IDirect3DSurface9> read;check("resolve readback surface",d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&read.p,nullptr));check("resolve test-only readback",d->GetRenderTargetData(target.p,read.p));D3DLOCKED_RECT lock{};check("resolve readback lock",read->LockRect(&lock,nullptr,D3DLOCK_READONLY));unsigned short actual;std::memcpy(&actual,static_cast<char*>(lock.pBits)+8*lock.Pitch+8*8,2);check("resolve readback unlock",read->UnlockRect());
        numeric(halfFloat(actual),enabled?.5f:.5625f,enabled?"production resolve consumes object motion":"camera fallback distinguishing control",.001f);
    }
    f.scene_state();
}
void precision_control(IDirect3DDevice9* d,Compiler compiler,UINT width,UINT height){
    W=width;H=height;std::printf("PRECISION_VIEWPORT %ux%u\n",W,H);
    Fixture f(d,compiler);RigidMotionPass pass;check("precision initialize",pass.initialize(d));
    auto x=f.draw();x.current_wvp[12]=.25f;x.current_wvp[13]=.125f;x.previous_wvp[13]=-.125f;
    x.previous_wvp[3]=.125f;x.previous_wvp[11]=.125f;auto in=f.input(&x);
    f.render_scene(x);f.run(pass,in,"large viewport perspective");const auto pixels=f.read_motion();
    for(const auto point:{std::pair<UINT,UINT>{5*W/8,3*H/8},{3*W/8,5*H/8}}){
        const auto actual=pixels[point.second*W+point.first];const auto want=expected(x,point.first,point.second,in.previous_jitter);
        const float a[]={actual.x,actual.y,actual.z,actual.w},b[]={want.x,want.y,want.z,want.w};
        for(UINT component=0;component<4;++component){char label[128];std::snprintf(label,sizeof label,"perspective %ux%u (%u,%u) component%u",W,H,point.first,point.second,component);numeric(a[component],b[component],label);}
        require(W*std::fabs(actual.x-want.x)<=.005f&&H*std::fabs(actual.y-want.y)<=.005f&&std::fabs(actual.z-want.z)<=.000002f,"large viewport error bounded in pixels and depth");
        std::printf("PRECISION_ERROR width=%u height=%u x=%u y=%u uv_x=%.12f uv_y=%.12f pixel_x=%.9f pixel_y=%.9f depth=%.12f\n",W,H,point.first,point.second,std::fabs(actual.x-want.x),std::fabs(actual.y-want.y),W*std::fabs(actual.x-want.x),H*std::fabs(actual.y-want.y),std::fabs(actual.z-want.z));
    }
    pass.before_reset();
}
void source_contract_tests(const char* sm3Path,const char* legacyPath){
    static_assert(!std::is_aggregate<RigidReplayContract>::value,"caller cannot aggregate-initialize proof");
    static_assert(!std::is_constructible<RigidReplayContract,bool>::value,"caller cannot assert arbitrary proof");
    auto words=[](const char* path){std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error(path);
        std::vector<char> bytes{std::istreambuf_iterator<char>(input),{}};if(bytes.empty()||bytes.size()%4)throw std::runtime_error("invalid test bytecode");
        std::vector<std::uint32_t> out(bytes.size()/4);std::memcpy(out.data(),bytes.data(),bytes.size());return out;};
    require(!RigidReplayContract{}.qualified(),"default source token unknown");
    require(!qualify_rigid_replay_source(nullptr,0).qualified(),"null source token unknown");
    auto sm3=words(sm3Path);const auto admitted=qualify_rigid_replay_source(sm3.data(),sm3.size());
    require(admitted.qualified()&&admitted.source()==RigidReplaySource::ReviewedArchiveSm3&&admitted.source_hash()!=0&&admitted.source_words()==sm3.size(),"actual exact SM3 source admitted once");
    require(!qualify_rigid_replay_source(sm3.data(),sm3.size()-1).qualified(),"truncated source token unknown");
    sm3[0]^=1;require(!qualify_rigid_replay_source(sm3.data(),sm3.size()).qualified(),"modified source token unknown");
    sm3.clear();sm3.shrink_to_fit();auto copied=admitted;
    require(copied.qualified()&&copied.source_hash()==admitted.source_hash()&&copied.source_words()==admitted.source_words(),"cached token owns value after source release");
    auto legacy=words(legacyPath);require(!qualify_rigid_replay_source(legacy.data(),legacy.size()).qualified(),"actual reviewed legacy source refused");
    legacy[0]=D3DVS_VERSION(3,0);require(!qualify_rigid_replay_source(legacy.data(),legacy.size()).qualified(),"version-only forged legacy source refused");
    auto synthetic=original_synthetic_sm3_contract();require(synthetic.qualified()&&synthetic.source()==RigidReplaySource::OriginalSyntheticSm3&&!synthetic.source_hash()&&!synthetic.source_words(),"verification-only synthetic source never impersonates archive");
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3RigidMotionFixture";RegisterClassA(&cls);
    HWND window=CreateWindowA(cls.lpszClassName,"X3 original rigid motion",WS_OVERLAPPEDWINDOW,90,90,128,128,nullptr,nullptr,cls.hInstance,nullptr);
    try{if(argc!=6||!window)throw std::runtime_error("usage: fixture.exe D3DX motionPS resolver admittedSM3 refusedLegacy");Module runtime("d3d9.dll"),d3dx(argv[1]);auto compiler=symbol<Compiler>(d3dx.h,"D3DXCompileShader");assemble_shader=symbol<Assembler>(d3dx.h,"D3DXAssembleShader");source_contract_tests(argv[4],argv[5]);
        Com<ID3DXBuffer> ps,resolver;compile(compiler,file(argv[2]),"ps_3_0",&ps.p);compile(compiler,file(argv[3]),"ps_3_0",&resolver.p);
        require(ps->GetBufferSize()==sizeof(rigid_motion_pixel_program())&&std::memcmp(ps->GetBufferPointer(),rigid_motion_pixel_program(),sizeof(rigid_motion_pixel_program()))==0,"embedded pixel program equals native HLSL compilation");
        auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime.h,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Create9");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> d;check("CreateDevice pure",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&d.p));
        for(UINT generation=0;generation<2;++generation){cases(d.p,compiler,static_cast<DWORD*>(resolver->GetBufferPointer()),generation);if(!generation){check("actual Reset",d->Reset(&pp));std::puts("RESET PASS");}}
        for(auto size:{std::pair<UINT,UINT>{1280,768},{5120,1440}})precision_control(d.p,compiler,size.first,size.second);
        std::printf("RESULT PASS numerical=%u checks=%u state_restorations=%u generations=2\n",numeric_checks,checks,state_checks);result=0;
    }catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());}
    if(window)DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName,cls.hInstance);return result;
}
