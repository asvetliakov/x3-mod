// Original GPU-only native D24X8 comparison decoder verification and cost probe.
// Only validation reads pixels to the CPU; timed rendering contains no readback.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

template<class T> struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
};
struct Module {
    HMODULE p;
    explicit Module(const char* path):p(LoadLibraryA(path)){
        if(!p)throw std::runtime_error("LoadLibrary");
        char resolved[MAX_PATH]{};GetModuleFileNameA(p,resolved,MAX_PATH);
        std::printf("MODULE %s\n",resolved);
    }
    ~Module(){FreeLibrary(p);}
};
template<class T>T symbol(HMODULE p,const char* name){
    FARPROC address=GetProcAddress(p,name);T result=nullptr;
    static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));
    if(!result)throw std::runtime_error(name);
    return result;
}
void check(const char* name,HRESULT hr){
    if(FAILED(hr)){std::printf("API %s result=%08lx FAILED\n",name,hr);throw std::runtime_error(name);}
}
using Compiler=decltype(&D3DXCompileShader);
void compile(Compiler fn,const std::string& source,const char* profile,ID3DXBuffer** result){
    Com<ID3DXBuffer> errors;
    HRESULT hr=fn(source.c_str(),UINT(source.size()),nullptr,nullptr,"main",profile,D3DXSHADER_OPTIMIZATION_LEVEL3,result,&errors.p,nullptr);
    if(errors.p)std::printf("COMPILER %s\n",static_cast<char*>(errors->GetBufferPointer()));
    check(profile,hr);
}
struct Vertex {float x,y,z,w,u,v;};
Vertex vertex(float x,float y,float z,float u,float v,UINT w,UINT h){
    return {2*(x-.5f)/w-1,1-2*(y-.5f)/h,z,1,u,v};
}
std::vector<float> values(){
    std::vector<float> result={0,.25f,.75f,1,.9999f,.99999f,
        1.f/16777215.f,2.f/16777215.f,1.f-1.f/16777215.f,1.f-2.f/16777215.f};
    unsigned seed=0x13579bdf;
    while(result.size()<32){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;result.push_back(float(seed&0xffffff)/16777215.f);}
    return result;
}
void waitQuery(IDirect3DQuery9* query,void* data,DWORD size){
    ULONGLONG start=GetTickCount64();HRESULT hr;
    while((hr=query->GetData(data,size,D3DGETDATA_FLUSH))==S_FALSE){
        if(GetTickCount64()-start>10000)throw std::runtime_error("query timeout");
        Sleep(1);
    }
    check("GetData",hr);
}
void drawQuad(IDirect3DDevice9* device,UINT width,UINT height){
    const Vertex quad[]={vertex(0,0,0,0,0,width,height),vertex(float(width),0,0,1,0,width,height),
        vertex(0,float(height),0,0,1,width,height),vertex(float(width),float(height),0,1,1,width,height)};
    check("Draw decode quad",device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
}

// Exact same no-dummy protocol intended for the original-preserving snapshot.
// This function neither opens/closes a scene nor changes geometry/draw state.
void copyDepth(IDirect3DDevice9* d,IDirect3DTexture9* destination,IDirect3DSurface9* source){
    Com<IDirect3DBaseTexture9> previous,after;Com<IDirect3DSurface9> afterDepth;
    DWORD oldPoint=0,afterPoint=0;
    check("GetTexture before copy",d->GetTexture(0,&previous.p));
    check("GetPoint before copy",d->GetRenderState(D3DRS_POINTSIZE,&oldPoint));
    check("Set resolve destination",d->SetTexture(0,destination));
    check("RESZ trigger",d->SetRenderState(D3DRS_POINTSIZE,0x7fa05000));
    check("Restore point size",d->SetRenderState(D3DRS_POINTSIZE,oldPoint));
    check("Restore texture",d->SetTexture(0,previous.p));
    check("GetTexture after copy",d->GetTexture(0,&after.p));
    check("GetPoint after copy",d->GetRenderState(D3DRS_POINTSIZE,&afterPoint));
    check("GetDepth after copy",d->GetDepthStencilSurface(&afterDepth.p));
    bool pass=previous.p==after.p&&oldPoint==afterPoint&&afterDepth.p==source;
    std::printf("COPY_STATE inside_scene=1 original_depth=%u texture=%u pointsize=%u %s\n",afterDepth.p==source,previous.p==after.p,oldPoint==afterPoint,pass?"PASS":"FAIL");
    if(!pass)throw std::runtime_error("copy state mismatch");
}

void runCase(IDirect3DDevice9* d,Compiler compiler,const std::string& hlsl,UINT width,UINT height,unsigned generation){
    std::printf("CASE generation=%u width=%u height=%u\n",generation,width,height);
    Com<IDirect3DSurface9> backbuffer,source,snapshotSurface,color,decoded,readback;
    Com<IDirect3DTexture9> snapshot,sentinel;
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> solid,decode;
    Com<IDirect3DVertexDeclaration9> declaration;Com<ID3DXBuffer> vc,pc,dc;
    compile(compiler,"void main(float4 p:POSITION0,float2 uv:TEXCOORD0,out float4 op:POSITION0,out float2 ou:TEXCOORD0){op=p;ou=uv;}","vs_3_0",&vc.p);
    compile(compiler,"float4 main():COLOR0{return float4(1,0,1,1);}","ps_3_0",&pc.p);
    compile(compiler,hlsl,"ps_3_0",&dc.p);
    check("CreateVS",d->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()),&vs.p));
    check("CreateSolidPS",d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()),&solid.p));
    check("CreateDecodePS",d->CreatePixelShader(static_cast<DWORD*>(dc->GetBufferPointer()),&decode.p));
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    check("CreateDecl",d->CreateVertexDeclaration(elements,&declaration.p));
    check("GetBackBuffer",d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer.p));
    check("Create original D24X8",d->CreateDepthStencilSurface(width,height,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,FALSE,&source.p,nullptr));
    check("Create D24X8 snapshot",d->CreateTexture(width,height,1,D3DUSAGE_DEPTHSTENCIL,D3DFMT_D24X8,D3DPOOL_DEFAULT,&snapshot.p,nullptr));
    check("Get snapshot surface",snapshot->GetSurfaceLevel(0,&snapshotSurface.p));
    check("Create scene color",d->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&color.p,nullptr));
    check("Create decoded R32F",d->CreateRenderTarget(width,height,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&decoded.p,nullptr));
    check("Create readback R32F",d->CreateOffscreenPlainSurface(width,height,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));
    check("Create sentinel",d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&sentinel.p,nullptr));
    check("SetRT scene",d->SetRenderTarget(0,color.p));
    check("SetDS snapshot poison",d->SetDepthStencilSurface(snapshotSurface.p));
    check("Clear snapshot poison",d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.12345f,0));
    check("SetDS original",d->SetDepthStencilSurface(source.p));
    D3DVIEWPORT9 viewport={0,0,width,height,0,1};check("SetVP",d->SetViewport(&viewport));
    check("SetDecl",d->SetVertexDeclaration(declaration.p));check("SetVS",d->SetVertexShader(vs.p));check("SetPS",d->SetPixelShader(solid.p));
    for(auto state: {D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})check("DisableRS",d->SetRenderState(state,FALSE));
    check("CullNone",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check("Zenable",d->SetRenderState(D3DRS_ZENABLE,TRUE));
    check("Zwrite",d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));check("Zalways",d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS));
    check("ColorWrite",d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    check("SentinelTexture",d->SetTexture(0,sentinel.p));check("SentinelPoint",d->SetRenderState(D3DRS_POINTSIZE,0x40500000));
    check("Clear source",d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
    std::vector<Vertex> geometry;const auto depths=values();
    for(unsigned i=0;i<depths.size();++i){
        float x=float((i%8)*width/8),y=float((i/8)*height/4),r=x+width/8.f,b=y+height/4.f,z=depths[i];
        geometry.insert(geometry.end(),{vertex(x,y,z,0,0,width,height),vertex(r,y,z,0,0,width,height),vertex(x,b,z,0,0,width,height),
            vertex(r,y,z,0,0,width,height),vertex(r,b,z,0,0,width,height),vertex(x,b,z,0,0,width,height)});
    }
    check("Begin original scene",d->BeginScene());
    check("Draw original depth tiles",d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(geometry.size()/3),geometry.data(),sizeof(Vertex)));
    copyDepth(d,snapshot.p,source.p); // Crucially: inside the caller's open scene.
    check("End original scene",d->EndScene());
    bool all=true;
    for(unsigned phase=0;phase<2;++phase){
        if(phase)check("Clear original after snapshot",d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.9375f,0));
        Com<IDirect3DStateBlock9> state;check("Capture all state",d->CreateStateBlock(D3DSBT_ALL,&state.p));
        check("Detach source DS for decode",d->SetDepthStencilSurface(nullptr));
        check("Bind decodeRT",d->SetRenderTarget(0,decoded.p));check("Set decodeVP",d->SetViewport(&viewport));
        check("Disable decodeZ",d->SetRenderState(D3DRS_ZENABLE,FALSE));check("Disable decodeZwrite",d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        check("Bind snapshot",d->SetTexture(0,snapshot.p));check("DecodePS",d->SetPixelShader(decode.p));
        check("Point min",d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));check("Point mag",d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        check("No mip",d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));check("ClampU",d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
        check("ClampV",d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));check("No sRGB",d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
        check("Begin decode",d->BeginScene());drawQuad(d,width,height);check("End decode",d->EndScene());
        check("Readback validation only",d->GetRenderTargetData(decoded.p,readback.p));
        D3DLOCKED_RECT lock{};check("Lock validation",readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned i=0;i<depths.size();++i){
            UINT x=(i%8)*width/8+width/16,y=(i/8)*height/4+height/8;float actual=0;
            std::memcpy(&actual,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+x*4,4);
            double err=std::fabs(double(actual)-depths[i]),lsbs=err*16777215.0;bool pass=std::isfinite(actual)&&lsbs<=2.0;
            // Numeric tolerance alone would incorrectly accept mapping the smallest
            // positive depth to zero, or adjacent far geometry to clear depth 1.
            const bool endpoints=(depths[i]==0?actual==0:actual>0)&&(depths[i]==1?actual==1:actual<1);
            pass &= endpoints;
            all &= pass;
            std::printf("SAMPLE generation=%u width=%u height=%u after_source_clear=%u index=%u expected=%.9f actual=%.9f error_d24_lsb=%.6f endpoints=%u %s\n",
                generation,width,height,phase,i,depths[i],actual,lsbs,endpoints,pass?"PASS":"FAIL");
        }
        check("Unlock validation",readback->UnlockRect());
        if(phase){
            Com<IDirect3DQuery9> event,start,end,freq,disjoint;
            check("Create event query",d->CreateQuery(D3DQUERYTYPE_EVENT,&event.p));
            HRESULT hs=d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start.p),he=d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end.p),
                hf=d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq.p),hd=d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint.p);
            bool gpu=SUCCEEDED(hs)&&SUCCEEDED(he)&&SUCCEEDED(hf)&&SUCCEEDED(hd);
            std::printf("TIMING_CAP timestamp=%08lx end=%08lx frequency=%08lx disjoint=%08lx\n",hs,he,hf,hd);
            check("Begin warmup",d->BeginScene());for(unsigned i=0;i<8;++i)drawQuad(d,width,height);check("End warmup",d->EndScene());
            check("Warmup event",event->Issue(D3DISSUE_END));BOOL done=FALSE;waitQuery(event.p,&done,sizeof(done));
            if(!done)throw std::runtime_error("warmup event did not report completion");
            LARGE_INTEGER frequency,t0,t1;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&t0);
            if(gpu)gpu=SUCCEEDED(freq->Issue(D3DISSUE_END))&&SUCCEEDED(disjoint->Issue(D3DISSUE_BEGIN))&&SUCCEEDED(start->Issue(D3DISSUE_END));
            constexpr unsigned iterations=32;
            check("Begin timed",d->BeginScene());for(unsigned i=0;i<iterations;++i)drawQuad(d,width,height);check("End timed",d->EndScene());
            if(gpu)gpu=SUCCEEDED(end->Issue(D3DISSUE_END))&&SUCCEEDED(disjoint->Issue(D3DISSUE_END));
            check("Timed event",event->Issue(D3DISSUE_END));done=FALSE;waitQuery(event.p,&done,sizeof(done));QueryPerformanceCounter(&t1);
            if(!done)throw std::runtime_error("timed event did not report completion");
            double gpuMs=-1;BOOL discontinuous=TRUE;UINT64 a=0,b=0,hz=0;
            if(gpu){waitQuery(start.p,&a,sizeof(a));waitQuery(end.p,&b,sizeof(b));waitQuery(freq.p,&hz,sizeof(hz));waitQuery(disjoint.p,&discontinuous,sizeof(discontinuous));
                if(!discontinuous&&hz&&b>=a)gpuMs=1000.0*double(b-a)/double(hz)/iterations;}
            // Independent GPU completion per draw prevents later identical draws
            // from hiding the earlier passes in a tile renderer's overdraw cull.
            std::vector<double> serialTimes;
            for(unsigned i=0;i<16;++i){
                LARGE_INTEGER aTime,bTime;QueryPerformanceCounter(&aTime);
                check("Begin serialized decode",d->BeginScene());drawQuad(d,width,height);check("End serialized decode",d->EndScene());
                check("Serialized event",event->Issue(D3DISSUE_END));done=FALSE;waitQuery(event.p,&done,sizeof(done));
                QueryPerformanceCounter(&bTime);if(!done)throw std::runtime_error("serialized event did not complete");
                serialTimes.push_back(1000.0*double(bTime.QuadPart-aTime.QuadPart)/frequency.QuadPart);
            }
            std::sort(serialTimes.begin(),serialTimes.end());
            double serialTotal=0;for(double value:serialTimes)serialTotal+=value;
            std::printf("TIMING generation=%u width=%u height=%u iterations=%u gpu_ms_per_decode=%.6f "
                "batch_cpu_gpu_completion_ms_per_decode=%.6f serialized_iterations=16 "
                "serialized_completion_mean_ms=%.6f serialized_completion_median_ms=%.6f "
                "serialized_completion_min_ms=%.6f serialized_completion_max_ms=%.6f "
                "event_poll_sleep_ms=1 disjoint=%u timed_pixel_readback=0\n",
                generation,width,height,iterations,gpuMs,1000.0*double(t1.QuadPart-t0.QuadPart)/frequency.QuadPart/iterations,
                serialTotal/16,(serialTimes[7]+serialTimes[8])*.5,serialTimes.front(),serialTimes.back(),unsigned(discontinuous));
        }
        check("Unbind decoder texture",d->SetTexture(0,nullptr));check("Restore scene RT",d->SetRenderTarget(0,color.p));
        check("Restore scene DS",d->SetDepthStencilSurface(source.p));check("Apply saved state",state->Apply());check("Restore viewport",d->SetViewport(&viewport));
        Com<IDirect3DSurface9> restored,restoredColor;Com<IDirect3DBaseTexture9> restoredTexture;
        Com<IDirect3DPixelShader9> restoredPS;DWORD restoredPoint=0,restoredZ=0,restoredZwrite=0;
        check("Get restored DS",d->GetDepthStencilSurface(&restored.p));check("Get restored texture",d->GetTexture(0,&restoredTexture.p));
        check("Get restored POINTSIZE",d->GetRenderState(D3DRS_POINTSIZE,&restoredPoint));
        check("Get restored RT",d->GetRenderTarget(0,&restoredColor.p));check("Get restored PS",d->GetPixelShader(&restoredPS.p));
        check("Get restored Z",d->GetRenderState(D3DRS_ZENABLE,&restoredZ));check("Get restored Zwrite",d->GetRenderState(D3DRS_ZWRITEENABLE,&restoredZwrite));
        bool restoredOK=restored.p==source.p&&restoredTexture.p==sentinel.p&&restoredPoint==0x40500000&&
            restoredColor.p==color.p&&restoredPS.p==solid.p&&restoredZ==TRUE&&restoredZwrite==TRUE;
        std::printf("RESTORE generation=%u width=%u height=%u phase=%u %s\n",generation,width,height,phase,restoredOK?"PASS":"FAIL");
        all &= restoredOK;
    }
    check("Unbind texture",d->SetTexture(0,nullptr));check("Unbind depth",d->SetDepthStencilSurface(nullptr));
    check("Restore backbuffer",d->SetRenderTarget(0,backbuffer.p));check("Unbind VS",d->SetVertexShader(nullptr));
    check("Unbind PS",d->SetPixelShader(nullptr));check("Unbind declaration",d->SetVertexDeclaration(nullptr));
    if(!all)throw std::runtime_error("numeric decode or restoration mismatch");
}

int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3DepthDecodeProbe";RegisterClassA(&cls);
    HWND window=CreateWindowA(cls.lpszClassName,"X3 original depth decoder",WS_OVERLAPPEDWINDOW,100,100,128,128,nullptr,nullptr,cls.hInstance,nullptr);
    try{
        if(argc!=3||!window)throw std::runtime_error("usage: depth_decode.exe <D3DX> <decoder HLSL>");
        std::ifstream file(argv[2]);if(!file)throw std::runtime_error("open decoder HLSL");
        std::string hlsl((std::istreambuf_iterator<char>(file)),{});
        Module d3dx(argv[1]),runtime("d3d9.dll");auto compiler=symbol<Compiler>(d3dx.p,"D3DXCompileShader");
        auto create=symbol<IDirect3D9* (WINAPI*)(UINT)>(runtime.p,"Direct3DCreate9");Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);
        if(!api.p)throw std::runtime_error("Direct3DCreate9");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;check("CreateDevice",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&device.p));
        for(unsigned generation=0;generation<2;++generation){
            runCase(device.p,compiler,hlsl,64,64,generation);runCase(device.p,compiler,hlsl,1280,768,generation);
            if(!generation){check("Reset after released resources",device->Reset(&pp));std::puts("RESET PASS");}
        }
        std::puts("RESULT PASS: 256 precision samples, 4 inside-scene copies, 8 state restorations, 4 timings, Reset");result=0;
    }catch(const std::exception& e){std::printf("RESULT FAIL: %s\n",e.what());}
    if(window)DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName,cls.hInstance);
    return result;
}
