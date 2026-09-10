// Original deterministic GPU verification of src/temporal/resolve.hlsl.
// No game assets, game launch, installation, registry writes, or production hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/temporal/resolve.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
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
constexpr unsigned W=16,H=16;using Pixel=std::array<float,4>;using Image=std::vector<Pixel>;
Image image(float value){return Image(W*H,Pixel{value,value,value,1});}
const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
struct Fixture {
    IDirect3DDevice9* d;
    Com<IDirect3DPixelShader9> ps,referencePS;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DTexture9> current,depth,old,oldDepth,motion,output[2];
    Com<IDirect3DSurface9> back,readback,target[2];
    x3::temporal::HistoryState state;
    x3::temporal::ResolveConstants constants;
    Fixture(IDirect3DDevice9* device,Compiler compiler,const std::string& source):d(device){
        Com<ID3DXBuffer> pc,vc;
        compile(compiler,source,"ps_3_0",&pc.p);
        compile(compiler,"struct O{float4 p:POSITION0;float2 uv:TEXCOORD0;};O main(float4 p:POSITION0,float2 uv:TEXCOORD0){O o;o.p=p;o.uv=uv;return o;}","vs_3_0",&vc.p);
        check("CreatePixelShader",d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()),&ps.p));
        Com<ID3DXBuffer> referenceCode;
        compile(compiler,"float4 main(float2 uv:TEXCOORD0):COLOR0{return float4(uv.xxx,1);}","ps_3_0",&referenceCode.p);
        check("Create reference PS",d->CreatePixelShader(static_cast<DWORD*>(referenceCode->GetBufferPointer()),&referencePS.p));
        check("CreateVertexShader",d->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()),&vs.p));
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        check("CreateVertexDeclaration",d->CreateVertexDeclaration(elements,&declaration.p));
        for(auto* texture:{&current,&old})check("Create input FP16",d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&texture->p,nullptr));
        check("Create motion RGBA32F",d->CreateTexture(W,H,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&motion.p,nullptr));
        for(auto* texture:{&depth,&oldDepth})check("Create input R32F",d->CreateTexture(W,H,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&texture->p,nullptr));
        for(unsigned i=0;i<2;++i){check("Create output FP16",d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&output[i].p,nullptr));
            check("GetSurfaceLevel",output[i]->GetSurfaceLevel(0,&target[i].p));}
        check("GetBackBuffer",d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p));
        check("CreateReadback",d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));
        state.begin(W,H,1);state.completed();prepare();
        upload(current.p,image(.5f));upload(old.p,image(.75f));upload(motion.p,Image(W*H,Pixel{0,0,0,0}));
        uploadDepth(depth.p,.5f);uploadDepth(oldDepth.p,.5f);
    }
    ~Fixture(){for(unsigned i=0;i<5;++i)d->SetTexture(i,nullptr);d->SetRenderTarget(0,back.p);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetVertexDeclaration(nullptr);}
    void prepare(float weight=.5f,float cx=0,float cy=0,float px=0,float py=0,bool useMotion=false,const float* matrix=identity){
        require(x3::temporal::prepare(constants,state,matrix,cx,cy,px,py,weight,useMotion),"prepare constants");}
    void upload(IDirect3DTexture9* texture,const Image& input){D3DLOCKED_RECT lock{};
        D3DSURFACE_DESC desc{};check("Input description",texture->GetLevelDesc(0,&desc));
        const bool fullPrecision=desc.Format==D3DFMT_A32B32G32R32F;
        check("Lock input",texture->LockRect(0,&lock,nullptr,0));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            auto* at=static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+x*(fullPrecision?16:8);
            if(fullPrecision)std::memcpy(at,input[y*W+x].data(),16);
            else for(unsigned c=0;c<4;++c){unsigned short h=toHalf(input[y*W+x][c]);std::memcpy(at+c*2,&h,2);}}
        check("Unlock input",texture->UnlockRect(0));}
    void uploadDepth(IDirect3DTexture9* texture,float value){D3DLOCKED_RECT lock{};check("Lock depth",texture->LockRect(0,&lock,nullptr,0));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)std::memcpy(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+x*4,&value,4);
        check("Unlock depth",texture->UnlockRect(0));}
    void depthPixel(IDirect3DTexture9* texture,unsigned x,unsigned y,float value){
        D3DLOCKED_RECT lock{};check("Lock depth pixel",texture->LockRect(0,&lock,nullptr,0));
        std::memcpy(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+x*4,&value,4);
        check("Unlock depth pixel",texture->UnlockRect(0));
    }
    Image render(unsigned which=0,IDirect3DTexture9* history=nullptr,float rasterScale=0){
        // Always unbind before changing targets; no read/write texture aliasing.
        for(unsigned i=0;i<5;++i)check("Unbind",d->SetTexture(i,nullptr));
        check("Target",d->SetRenderTarget(0,target[which].p));check("Depth null",d->SetDepthStencilSurface(nullptr));
        D3DVIEWPORT9 viewport={0,0,W,H,0,1};check("Viewport",d->SetViewport(&viewport));
        check("VS",d->SetVertexShader(vs.p));check("PS",d->SetPixelShader(rasterScale?referencePS.p:ps.p));check("Declaration",d->SetVertexDeclaration(declaration.p));
        check("Constants",d->SetPixelShaderConstantF(0,&constants.clip_to_previous[0][0],8));
        for(auto pair:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},
            {D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15}})
            check("RenderState",d->SetRenderState(pair.first,pair.second));
        IDirect3DTexture9* textures[]={current.p,depth.p,history?history:old.p,oldDepth.p,motion.p};
        for(unsigned i=0;i<5;++i){check("SetTexture",d->SetTexture(i,textures[i]));
            for(auto pair:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},
                {D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE}})
                check("Sampler",d->SetSamplerState(i,pair.first,pair.second));}
        struct Vertex{float x,y,z,w,u,v;};
        // D3D9 raster centers are integer positions; shift vertices by -0.5 pixel
        // so sample (x,y) interpolates texture UV ((x+.5)/W,(y+.5)/H).
        Vertex vertices[]={{-1-1.f/W,1+1.f/H,0,1,0,0},{3-1.f/W,1+1.f/H,0,1,2,0},{-1-1.f/W,-3+1.f/H,0,1,0,2}};
        // An independent geometry oracle uses UNADJUSTED raw clip positions.
        // The rasterizer (not a texture UV formula) establishes their sample
        // locations. Its interpolated UV.x is the original world's linear color.
        if(rasterScale){vertices[0]={-rasterScale,1,0,1,0,0};
            vertices[1]={3*rasterScale,1,0,1,2,0};vertices[2]={-rasterScale,-3,0,1,0,2};}
        check("BeginScene",d->BeginScene());check("Draw",d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex)));check("EndScene",d->EndScene());
        check("Readback",d->GetRenderTargetData(target[which].p,readback.p));D3DLOCKED_RECT lock{};check("Lock readback",readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        Image result(W*H);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<4;++c){unsigned short h;
            std::memcpy(&h,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+x*8+c*2,2);result[y*W+x][c]=halfFloat(h);}
        check("Unlock readback",readback->UnlockRect());return result;
    }
};
unsigned samples=0;
void expect(const char* label,const Image& actual,unsigned x,unsigned y,float value,float tolerance=.002f){
    bool ok=true;for(unsigned c=0;c<3;++c)ok &= std::isfinite(actual[y*W+x][c])&&std::fabs(actual[y*W+x][c]-value)<=tolerance;
    ok &= std::fabs(actual[y*W+x][3]-1)<=tolerance;
    std::printf("SAMPLE %s xy=%u,%u actual=%.8f,%.8f,%.8f,%.8f expected=%.8f tolerance=%.8f %s\n",label,x,y,
        actual[y*W+x][0],actual[y*W+x][1],actual[y*W+x][2],actual[y*W+x][3],value,tolerance,ok?"PASS":"FAIL");
    ++samples;if(!ok)throw std::runtime_error(label);
}
Image checker(float a,float b){Image result=image(a);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        if((x+y)&1)result[y*W+x]={b,b,b,1};
    }
    return result;
}
void cases(Fixture& f,unsigned generation){
    std::printf("GENERATION %u\n",generation);
    require(toHalf(1.999755859375f)==0x4000,"half rounding exponent carry 2");
    require(toHalf(7.9990234375f)==0x4800,"half rounding exponent carry 8");
    require(toHalf(1.00048828125f)==0x3c00,"half nearest even tie down");
    require(toHalf(1.00146484375f)==0x3c02,"half nearest even tie up");
    f.upload(f.current.p,checker(.25f,1));
    Image raster=f.render(1,nullptr,2);
    expect("raw viewport center geometry",raster,8,8,.5f);
    expect("raw viewport neighbor geometry",raster,9,8,.53125f);
    float zoom[16];std::memcpy(zoom,identity,sizeof(zoom));zoom[0]=2;
    f.prepare(.5f,0,0,0,0,false,zoom);
    expect("zoom rasterized geometry reprojection",f.render(0,f.output[1].p),8,8,.375f);
    Image zoomHistory=image(.25f);zoomHistory[8*W+8]={.75f,.75f,.75f,1};zoomHistory[8*W+9]={.875f,.875f,.875f,1};
    zoomHistory[7*W+8]={.875f,.875f,.875f,1};f.upload(f.old.p,zoomHistory);
    expect("zoom optical center",f.render(),8,8,.5f);
    const float roll[16]={0,-1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1};
    f.prepare(.5f,0,0,0,0,false,roll);expect("rotation optical center",f.render(),8,8,.5f);
    f.upload(f.current.p,checker(.25f,1));f.upload(f.old.p,image(.75f));f.prepare();
    expect("static blend",f.render(),8,8,.5f);
    f.state.invalidate();f.prepare();expect("history invalid",f.render(),8,8,.25f);f.state.completed();
    f.prepare();f.uploadDepth(f.oldDepth.p,.25f);expect("disocclusion",f.render(),8,8,.25f);f.uploadDepth(f.oldDepth.p,.5f);
    float translate[16];std::memcpy(translate,identity,sizeof(translate));translate[3]=2.f/W;
    Image old=image(.25f);old[8*W+9]={.875f,.875f,.875f,1};f.upload(f.old.p,old);f.prepare(.5f,0,0,0,0,false,translate);
    expect("camera one pixel",f.render(),8,8,.5625f);
    // Perspective W division and predicted previous device depth are distinct
    // from comparing against the untransformed current depth.
    float perspective[16];std::memcpy(perspective,identity,sizeof(perspective));
    perspective[0]=2;perspective[5]=2;perspective[15]=2;perspective[3]=4.f/W;
    f.uploadDepth(f.oldDepth.p,.25f);f.prepare(.5f,0,0,0,0,false,perspective);
    expect("perspective previous depth",f.render(),8,8,.5625f);f.uploadDepth(f.oldDepth.p,.5f);
    // Reject the far/occluded half of a bilinear footprint BEFORE reconstruction.
    old[8*W+8]={.75f,.75f,.75f,1};f.upload(f.old.p,old);
    f.depthPixel(f.oldDepth.p,9,8,.25f);f.prepare(.5f,0,0,.5f,0);
    expect("mixed depth footprint",f.render(),8,8,.5f);f.uploadDepth(f.oldDepth.p,.5f);
    old[8*W+8]={.25f,.25f,.25f,1};f.upload(f.old.p,old);
    translate[3]=2;f.prepare(.5f,0,0,0,0,false,translate);expect("outside bounds",f.render(),8,8,.25f);
    std::memcpy(translate,identity,sizeof(translate));translate[15]=-1;f.prepare(.5f,0,0,0,0,false,translate);expect("behind camera",f.render(),8,8,.25f);
    // Explicit object motion selects previous x=9, expected depth=.25 despite
    // static camera depth=.5. This checks independent object depth and UV routing.
    f.uploadDepth(f.oldDepth.p,.25f);Image motion(W*H,Pixel{0,0,0,0});motion[8*W+8]={(9.5f)/W,(8.5f)/H,.25f,1};
    f.upload(f.motion.p,motion);f.prepare(.5f,0,0,0,0,true);expect("object motion",f.render(),8,8,.5625f);
    motion[8*W+8][2]=.5002f;f.upload(f.motion.p,motion);f.uploadDepth(f.oldDepth.p,.5002f);
    expect("full precision object expected depth",f.render(),8,8,.5625f);
    motion[8*W+8][3]=-1;f.upload(f.motion.p,motion);expect("object invalid sentinel",f.render(),8,8,.25f);
    motion[8*W+8][3]=.25f;f.upload(f.motion.p,motion);expect("reserved motion state rejected",f.render(),8,8,.25f);
    f.uploadDepth(f.oldDepth.p,.5f);motion[8*W+8][3]=0;f.upload(f.motion.p,motion);f.upload(f.old.p,image(.75f));expect("motion static fallback",f.render(),8,8,.5f);f.upload(f.old.p,old);
    // The previous raster jitter is added AFTER the unjittered motion UV.
    motion[8*W+8]={(9.25f)/W,(8.5f)/H,.5f,1};f.upload(f.motion.p,motion);f.prepare(.5f,0,0,.25f,0,true);
    expect("motion previous jitter",f.render(),8,8,.5625f);
    f.upload(f.old.p,image(8));f.prepare();expect("neighborhood clipping",f.render(),8,8,.625f);
    f.upload(f.current.p,checker(2,16));f.upload(f.old.p,image(8));expect("HDR preservation",f.render(),8,8,5,.01f);
    f.upload(f.current.p,checker(.25f,1));f.upload(f.old.p,image(std::numeric_limits<float>::quiet_NaN()));expect("invalid history color",f.render(),8,8,.25f);
    Image current=checker(.25f,1);current[8*W+8]={INFINITY,INFINITY,INFINITY,1};f.upload(f.current.p,current);expect("invalid current color",f.render(),8,8,0);
    f.upload(f.current.p,checker(.25f,1));f.upload(f.old.p,image(.75f));f.uploadDepth(f.depth.p,NAN);expect("invalid current depth",f.render(),8,8,.25f);
    f.uploadDepth(f.depth.p,.5f);f.uploadDepth(f.oldDepth.p,NAN);expect("invalid history depth",f.render(),8,8,.25f);f.uploadDepth(f.oldDepth.p,.5f);
    // Jitter-aware camera reprojection selects previous x=9 only after subtracting
    // current raster jitter and adding previous jitter (difference +1 pixel).
    f.upload(f.old.p,old);f.prepare(.5f,-.5f,0,.5f,0);expect("camera jitter signs",f.render(),8,8,.5625f);
    // Alternating subpixel-jittered samples of a static, one-pixel stripe pattern.
    // Each raw sample is 0 or 1; its analytic pixel coverage is 0.5. GPU history
    // ping-pongs for 16 frames; no CPU filtering or substitution supplies history.
    f.state.invalidate();float previousJitter=0;Image resolved;
    for(unsigned frame=0;frame<16;++frame){float jitter=(frame&1)?.25f:-.25f;
        Image stripes=image(0);for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            int cell=static_cast<int>(std::floor(float(x)-jitter));float value=(cell&1)?1.f:0.f;stripes[y*W+x]={value,value,value,1};}
        f.upload(f.current.p,stripes);f.prepare(.875f,jitter,0,previousJitter,0);
        resolved=f.render(frame&1,frame?f.output[(frame-1)&1].p:nullptr);f.state.completed();previousJitter=jitter;
        if(frame>=14)expect("static jitter accumulated coverage",resolved,8,8,.5f,.07f);
    }
    f.state.begin(W,H,2);f.prepare();f.upload(f.current.p,image(.25f));expect("epoch reset",f.render(0,f.output[1].p),8,8,.25f);
    f.state.completed();f.state.begin(W*2,H,2);require(!f.state.valid,"resize invalidates");f.state.begin(W,H,2);
    f.state.completed();f.state.invalidate();require(!f.state.valid,"device reset invalidates");
    require(!x3::temporal::prepare(f.constants,f.state,identity,0,0,0,0,NAN,false),"invalid CPU weight rejected");
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;
    cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3TemporalResolveProbe";RegisterClassA(&cls);
    HWND window=CreateWindowA(cls.lpszClassName,"X3 original temporal resolve probe",WS_OVERLAPPEDWINDOW,100,100,128,128,nullptr,nullptr,cls.hInstance,nullptr);
    int result=1;
    try{if(argc!=3||!window)throw std::runtime_error("expected D3DX DLL and production shader paths");
        std::ifstream stream(argv[2]);std::string source((std::istreambuf_iterator<char>(stream)),{});if(source.empty())throw std::runtime_error("shader read");
        Module d3dx(argv[1]),runtime("d3d9.dll");Compiler compiler=symbol<Compiler>(d3dx.h,"D3DXCompileShader");
        auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime.h,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("Direct3DCreate9");
        D3DADAPTER_IDENTIFIER9 adapter{};check("adapter",api->GetAdapterIdentifier(0,0,&adapter));std::printf("ADAPTER %s driver=%s\n",adapter.Description,adapter.Driver);
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;check("CreateDevice",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&device.p));
        for(unsigned generation=0;generation<2;++generation){ {Fixture fixture(device.p,compiler,source);cases(fixture,generation);}
            if(!generation){check("Reset",device->Reset(&pp));std::puts("RESET PASS");}}
        std::printf("RESULT PASS samples=%u generations=2\n",samples);result=0;
    }catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());}
    if(window)DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName,cls.hInstance);return result;}
