// Original synthetic scene: exercises the production automatic-depth wrapper.
// No X3 assets, game launch, installation or display output is involved.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <array>

namespace own = x3m::ownership;
template<class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T* operator->() const { return p; }
};
static unsigned checks, samples;
static void expect(const char* label, bool pass) {
    ++checks;
    std::printf("CHECK %s %s\n", label, pass ? "PASS" : "FAIL");
    if (!pass) throw std::runtime_error(label);
}
static void ok(const char* label, HRESULT hr) {
    std::printf("API %s result=%08lx\n",label,static_cast<unsigned long>(hr));
    expect(label, SUCCEEDED(hr));
}
template<class T> static T entry(HMODULE module, const char* name) {
    FARPROC address=GetProcAddress(module,name); T fn=nullptr;
    static_assert(sizeof(fn)==sizeof(address)); std::memcpy(&fn,&address,sizeof(fn));
    if (!fn) throw std::runtime_error(name);
    return fn;
}
static own::DepthView depth_view(IDirect3DDevice9* device) {
    own::DepthView view{}; ok("get renderer depth view",own::get_depth_view(device,&view)); return view;
}
using Compiler=decltype(&D3DXCompileShader);
static void compile(Compiler compiler,const char* source,const char* profile,ID3DXBuffer** out) {
    Com<ID3DXBuffer> errors;
    HRESULT hr=compiler(source,UINT(std::strlen(source)),nullptr,nullptr,"main",profile,
        D3DXSHADER_OPTIMIZATION_LEVEL3,out,&errors.p,nullptr);
    if(errors.p) std::printf("compiler %s\n",static_cast<char*>(errors->GetBufferPointer()));
    ok(profile,hr);
}
static float half(unsigned short h) {
    const unsigned e=(h>>10)&31;
    return e==31 ? NAN : (h&0x8000?-1.f:1.f)*(e?std::ldexp(float(1024+(h&1023)),int(e)-25):std::ldexp(float(h&1023),-24));
}
struct Geometry { float x,y,z,rhw; D3DCOLOR color; };
static void draw_scene(IDirect3DDevice9* d) {
    ok("geometry no VS",d->SetVertexShader(nullptr));
    ok("geometry no PS",d->SetPixelShader(nullptr));
    ok("geometry FVF",d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
    ok("geometry no texture",d->SetTexture(0,nullptr));
    ok("geometry diffuse",d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
    ok("geometry diffuse arg",d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    ok("geometry Z",d->SetRenderState(D3DRS_ZENABLE,TRUE));
    ok("geometry write Z",d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
    ok("geometry less",d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS));
    ok("geometry cull",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    const Geometry tri[]={
        {3.5f,3.5f,.25f,1,0xffff00ff},{27.5f,3.5f,.25f,1,0xffff00ff},{3.5f,27.5f,.25f,1,0xffff00ff},
        {35.5f,3.5f,.75f,1,0xff00ffff},{59.5f,3.5f,.75f,1,0xff00ffff},{59.5f,27.5f,.75f,1,0xff00ffff},
        {3.5f,3.5f,.875f,1,0xffffffff},{27.5f,3.5f,.875f,1,0xffffffff},{3.5f,27.5f,.875f,1,0xffffffff}};
    ok("geometry begin",d->BeginScene());
    ok("geometry draw",d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,3,tri,sizeof(Geometry)));
    ok("geometry end",d->EndScene());
}
// This bounded fixture copy models consumption before X3's destructive clear.
// The production wrapper owns INTZ; fixture-owned snapshot resources never escape.
struct Snapshot {
    Com<IDirect3DSurface9> color,readback;
    UINT width,height;
    Snapshot(IDirect3DDevice9* d,UINT w,UINT h):width(w),height(h) {
        ok("snapshot target",d->CreateRenderTarget(w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&color.p,nullptr));
        ok("snapshot readback",d->CreateOffscreenPlainSurface(w,h,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));
    }
    void capture(IDirect3DDevice9* d,IDirect3DTexture9* texture,Compiler compiler) {
        Com<IDirect3DStateBlock9> state; Com<IDirect3DSurface9> rt,ds;
        D3DVIEWPORT9 vp{};
        ok("save RT",d->GetRenderTarget(0,&rt.p)); ok("save depth",d->GetDepthStencilSurface(&ds.p));
        ok("save viewport",d->GetViewport(&vp)); ok("save state",d->CreateStateBlock(D3DSBT_ALL,&state.p));
        Com<ID3DXBuffer> vc,pc; Com<IDirect3DVertexShader9> vs; Com<IDirect3DPixelShader9> ps;
        Com<IDirect3DVertexDeclaration9> decl;
        compile(compiler,"void main(float4 p:POSITION0,float2 t:TEXCOORD0,out float4 o:POSITION0,out float2 u:TEXCOORD0){o=p;u=t;}","vs_3_0",&vc.p);
        compile(compiler,"sampler2D d:register(s0);float4 main(float2 u:TEXCOORD0):COLOR0{float z=tex2D(d,u).r;return float4(z,z,z,1);}","ps_3_0",&pc.p);
        ok("snapshot VS",d->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()),&vs.p));
        ok("snapshot PS",d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()),&ps.p));
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        ok("snapshot decl",d->CreateVertexDeclaration(elements,&decl.p));
        ok("snapshot unbind depth",d->SetDepthStencilSurface(nullptr));
        ok("snapshot RT",d->SetRenderTarget(0,color.p));
        D3DVIEWPORT9 full={0,0,width,height,0,1}; ok("snapshot viewport",d->SetViewport(&full));
        ok("snapshot bind VS",d->SetVertexShader(vs.p)); ok("snapshot bind PS",d->SetPixelShader(ps.p));
        ok("snapshot bind decl",d->SetVertexDeclaration(decl.p));
        const D3DRENDERSTATETYPE disabled[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,
            D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE};
        for(auto s:disabled) ok("snapshot disable state",d->SetRenderState(s,0));
        ok("snapshot cull",d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        ok("snapshot fill",d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
        ok("snapshot color write",d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        ok("snapshot texture",d->SetTexture(0,texture));
        ok("snapshot point min",d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));
        ok("snapshot point mag",d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        ok("snapshot no mip",d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        ok("snapshot clamp U",d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
        ok("snapshot clamp V",d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        ok("snapshot no srgb",d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
        struct V {float x,y,z,w,u,v;};
        const float l=-1.f-1.f/width,r=1.f-1.f/width,t=1.f+1.f/height,b=-1.f+1.f/height;
        const V quad[]={{l,t,0,1,0,0},{r,t,0,1,1,0},{l,b,0,1,0,1},{r,b,0,1,1,1}};
        ok("snapshot begin",d->BeginScene());
        ok("snapshot draw",d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));
        ok("snapshot end",d->EndScene());
        ok("snapshot unbind texture",d->SetTexture(0,nullptr));
        ok("restore RT",d->SetRenderTarget(0,rt.p)); ok("restore depth",d->SetDepthStencilSurface(ds.p));
        ok("restore state",state->Apply()); ok("restore viewport",d->SetViewport(&vp));
    }
    void verify(IDirect3DDevice9* d,const char* epoch,bool cleared) {
        ok("read snapshot",d->GetRenderTargetData(color.p,readback.p)); D3DLOCKED_RECT lr{};
        ok("lock snapshot",readback->LockRect(&lr,nullptr,D3DLOCK_READONLY));
        struct P {UINT x,y;float depth;}; const P points[]={{8,8,.25f},{56,8,.75f},{32,40,1},{24,24,1}};
        bool pass=true;
        for(const auto& p:points) {
            unsigned short value[4]; std::memcpy(value,static_cast<const char*>(lr.pBits)+p.y*lr.Pitch+p.x*8,8);
            const float actual=half(value[0]),expected=cleared?1:p.depth;
            const bool valid=std::isfinite(actual)&&std::fabs(actual-expected)<.001f;
            pass &= valid; ++samples;
            std::printf("SAMPLE epoch=%s xy=%u,%u expected=%.6f actual=%.6f %s\n",epoch,p.x,p.y,expected,actual,valid?"PASS":"FAIL");
        }
        ok("unlock snapshot",readback->UnlockRect()); expect(epoch,pass);
    }
};

static void exercise(IDirect3D9* api,HWND window,Compiler compiler,DWORD flags) {
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window; pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> app; ok("create wrapped auto-depth device",api->CreateDevice(0,D3DDEVTYPE_HAL,window,flags,&pp,&app.p));
    auto* native=own::borrowed_native_device(app.p); expect("native seam available",native!=nullptr);
    unsigned long long previous_generation=0;
    for(unsigned generation=0;generation<2;++generation) {
        std::printf("CASE flags=%08lx generation=%u size=%ux%u\n",static_cast<unsigned long>(flags),generation,pp.BackBufferWidth,pp.BackBufferHeight);
        auto view=depth_view(app.p);
        expect("replacement available and bound",view.available&&view.bound&&view.texture);
        expect("generation advances",view.generation>previous_generation);previous_generation=view.generation;
        Com<IDirect3DSurface9> logical,again,physical;
        ok("logical GetDepth",app->GetDepthStencilSurface(&logical.p));ok("logical GetDepth again",app->GetDepthStencilSurface(&again.p));
        expect("canonical logical depth",logical.p==again.p);again.reset();
        D3DSURFACE_DESC desc{},actual{};ok("logical descriptor",logical->GetDesc(&desc));
        expect("logical D24X8 dimensions",desc.Format==D3DFMT_D24X8&&desc.Width==pp.BackBufferWidth&&desc.Height==pp.BackBufferHeight);
        ok("physical GetDepth",native->GetDepthStencilSurface(&physical.p));ok("physical descriptor",physical->GetDesc(&actual));
        expect("physical INTZ",actual.Format==D3DFORMAT(MAKEFOURCC('I','N','T','Z')));physical.reset();
        Com<IDirect3DTexture9> container;HRESULT hr=logical->GetContainer(IID_IDirect3DTexture9,reinterpret_cast<void**>(&container.p));
        expect("logical source hides texture container",FAILED(hr)&&!container.p);
        Com<IDirect3DDevice9> parent;ok("logical source GetDevice",logical->GetDevice(&parent.p));expect("logical parent canonical",parent.p==app.p);parent.reset();
        const GUID key={0x5727b9a3,0x69c6,0x4ef5,{0x9d,0xc2,0x26,0x76,0x4b,0x2c,0x75,0x29}};
        DWORD token=0x5319abcd,value=0,bytes=sizeof(value);ok("logical private data set",logical->SetPrivateData(key,&token,sizeof(token),0));
        ok("logical private data get",logical->GetPrivateData(key,&value,&bytes));expect("private data retained",value==token);
        // Native stateblocks do not own target/depth bindings; preserve this contract.
        Com<IDirect3DStateBlock9> block;ok("stateblock create",app->CreateStateBlock(D3DSBT_ALL,&block.p));
        ok("logical unbind",app->SetDepthStencilSurface(nullptr));expect("view tracks unbind",!depth_view(app.p).bound);
        ok("stateblock apply with no DS",block->Apply());hr=app->GetDepthStencilSurface(&again.p);
        expect("stateblock does not restore DS",hr==D3DERR_NOTFOUND&&!again.p);block.reset();
        ok("logical rebind",app->SetDepthStencilSurface(logical.p));expect("view tracks rebind",depth_view(app.p).bound);
        Com<IDirect3DSurface9> other;ok("other depth create",app->CreateDepthStencilSurface(desc.Width,desc.Height,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,TRUE,&other.p,nullptr));
        ok("other depth bind",app->SetDepthStencilSurface(other.p));expect("other source not selected",!depth_view(app.p).bound);
        ok("other depth getter",app->GetDepthStencilSurface(&again.p));expect("other source identity",again.p==other.p);again.reset();
        ok("source rebind",app->SetDepthStencilSurface(logical.p));other.reset();
        const auto epoch=depth_view(app.p).clear_epoch;
        ok("background clear",app->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,.875f,0));
        ok("main epoch clear",app->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));
        expect("clear epochs tracked",depth_view(app.p).clear_epoch==epoch+2);
        draw_scene(app.p);
        {
            Snapshot before(native,desc.Width,desc.Height),after(native,desc.Width,desc.Height);
            before.capture(native,view.texture,compiler);before.verify(native,"main-before-overlay-clear",false);
            ok("logical depth after snapshot",app->GetDepthStencilSurface(&again.p));expect("snapshot restores logical identity",again.p==logical.p);again.reset();
            ok("overlay destroys current depth",app->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));
            before.verify(native,"preserved-main-after-overlay-clear",false);
            after.capture(native,view.texture,compiler);after.verify(native,"live-depth-after-overlay-clear",true);
        }
        if(generation==0) {
            // Keep the game's reference across Reset: replacement must not hide the native failure.
            hr=app->Reset(&pp);std::printf("OBSERVE reset_with_logical_source=%08lx\n",static_cast<unsigned long>(hr));
            expect("retained source Reset fails",FAILED(hr));expect("failed Reset disables depth",!depth_view(app.p).available);
            logical.reset();pp.BackBufferWidth=80;pp.BackBufferHeight=48;ok("reset retry resized",app->Reset(&pp));
        }
    }
    expect("final logical device release",app.p->Release()==0);app.p=nullptr;
}

// Fail one backend operation at a time using object-local vtables. This checks
// rollback with actual Wine resources rather than duplicating the wrapper logic.
static unsigned fault_mode, fault_hits;
static void** factory_original;
static void** device_original;
static void** texture_original;
static IDirect3DDevice9* fault_device;
static std::array<void*,17> factory_table;
static std::array<void*,119> device_table;
static std::array<void*,22> texture_table;
static HRESULT WINAPI fail_level(IDirect3DTexture9* self,UINT,IDirect3DSurface9** out) {
    *reinterpret_cast<void***>(self)=texture_original;
    if(out)*out=nullptr;
    ++fault_hits;return E_OUTOFMEMORY;
}
static HRESULT WINAPI fail_texture(IDirect3DDevice9* self,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared) {
    using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
    const bool intz=format==D3DFORMAT(MAKEFOURCC('I','N','T','Z'));
    if(intz&&fault_mode==1) {if(out)*out=nullptr;++fault_hits;return E_OUTOFMEMORY;}
    HRESULT hr=reinterpret_cast<Fn>(device_original[23])(self,w,h,levels,usage,format,pool,out,shared);
    if(intz&&fault_mode==2&&SUCCEEDED(hr)&&out&&*out) {
        texture_original=*reinterpret_cast<void***>(*out);
        std::memcpy(texture_table.data(),texture_original,sizeof(texture_table));
        texture_table[18]=reinterpret_cast<void*>(fail_level);
        *reinterpret_cast<void***>(*out)=texture_table.data();
    }
    return hr;
}
static HRESULT WINAPI fail_bind(IDirect3DDevice9* self,IDirect3DSurface9* surface) {
    D3DSURFACE_DESC desc{};
    if(fault_mode==3&&surface&&SUCCEEDED(surface->GetDesc(&desc))&&desc.Format==D3DFORMAT(MAKEFOURCC('I','N','T','Z'))) {
        ++fault_hits;return E_OUTOFMEMORY;
    }
    using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*);
    return reinterpret_cast<Fn>(device_original[39])(self,surface);
}
static HRESULT WINAPI instrument_device(IDirect3D9* self,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out) {
    using Fn=HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
    HRESULT hr=reinterpret_cast<Fn>(factory_original[16])(self,adapter,type,window,flags,pp,out);
    if(SUCCEEDED(hr)&&out&&*out) {
        fault_device=*out;device_original=*reinterpret_cast<void***>(*out);
        std::memcpy(device_table.data(),device_original,sizeof(device_table));
        device_table[23]=reinterpret_cast<void*>(fail_texture);device_table[39]=reinterpret_cast<void*>(fail_bind);
        *reinterpret_cast<void***>(*out)=device_table.data();
    }
    return hr;
}
static void allocation_failure(IDirect3D9* backend,HWND window,unsigned stage) {
    fault_mode=stage;fault_hits=0;fault_device=nullptr;
    factory_original=*reinterpret_cast<void***>(backend);std::memcpy(factory_table.data(),factory_original,sizeof(factory_table));
    factory_table[16]=reinterpret_cast<void*>(instrument_device);*reinterpret_cast<void***>(backend)=factory_table.data();
    Com<IDirect3D9> api;own::Options options;options.sampleable_auto_depth=true;
    HRESULT hr=own::wrap_factory(backend,&api.p,options);
    if(FAILED(hr)){*reinterpret_cast<void***>(backend)=factory_original;backend->Release();}
    ok("fault factory wrap",hr);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;
    Com<IDirect3DDevice9> app;hr=api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&app.p);
    *reinterpret_cast<void***>(backend)=factory_original;
    if(fault_device)*reinterpret_cast<void***>(fault_device)=device_original;
    ok("device creation survives injected depth allocation failure",hr);
    const auto view=depth_view(app.p);
    expect("injected operation reached exactly once",fault_hits==1);
    expect("allocation failure is reported without replacement",!view.available&&!view.texture&&view.status==E_OUTOFMEMORY);
    Com<IDirect3DSurface9> physical;ok("failure preserved native depth binding",fault_device->GetDepthStencilSurface(&physical.p));
    D3DSURFACE_DESC desc{};ok("failure depth descriptor",physical->GetDesc(&desc));expect("failure original D24X8 retained",desc.Format==D3DFMT_D24X8);physical.reset();
    ok("failure original depth usable",app->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.5f,0));
    ok("failed allocation Reset recovery",app->Reset(&pp));expect("Reset retries allocation",depth_view(app.p).available);
    std::printf("ALLOCATION_FAILURE stage=%u rollback=1 reset_recovered=1\n",stage);
}

struct Compatibility {
    HRESULT stencil_clear=E_FAIL,copy_from=E_FAIL,copy_into=E_FAIL;
    DWORD stencil_enabled=0,pixel=0;
};
static Compatibility compatibility(IDirect3D9* api,HWND window,const char* mode) {
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;
    Com<IDirect3DDevice9> app;ok("compatibility device",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&app.p));
    Com<IDirect3DSurface9> source,other,rt,readback;
    ok("compatibility logical source",app->GetDepthStencilSurface(&source.p));
    ok("compatibility other DS",app->CreateDepthStencilSurface(64,64,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,TRUE,&other.p,nullptr));
    Compatibility result;result.stencil_clear=app->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,0);
    ok("compatibility unbind",app->SetDepthStencilSurface(nullptr));
    result.copy_from=app->StretchRect(source.p,nullptr,other.p,nullptr,D3DTEXF_NONE);
    result.copy_into=app->StretchRect(other.p,nullptr,source.p,nullptr,D3DTEXF_NONE);
    ok("compatibility rebind",app->SetDepthStencilSurface(source.p));
    ok("compatibility clear",app->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0));
    ok("enable logical stencil",app->SetRenderState(D3DRS_STENCILENABLE,TRUE));
    ok("reject all if real stencil exists",app->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_NEVER));
    draw_scene(app.p);
    ok("get logical stencil enabled",app->GetRenderState(D3DRS_STENCILENABLE,&result.stencil_enabled));
    ok("compatibility get RT",app->GetRenderTarget(0,&rt.p));
    ok("compatibility readback",app->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback.p,nullptr));
    ok("compatibility read target",app->GetRenderTargetData(rt.p,readback.p));
    D3DLOCKED_RECT lr{};ok("compatibility lock",readback->LockRect(&lr,nullptr,D3DLOCK_READONLY));
    std::memcpy(&result.pixel,static_cast<const char*>(lr.pBits)+8*lr.Pitch+8*4,4);ok("compatibility unlock",readback->UnlockRect());
    std::printf("COMPATIBILITY mode=%s stencil_clear=%08lx copy_from=%08lx copy_into=%08lx stencil_enabled=%lu pixel=%08lx\n",
        mode,static_cast<unsigned long>(result.stencil_clear),static_cast<unsigned long>(result.copy_from),
        static_cast<unsigned long>(result.copy_into),result.stencil_enabled,result.pixel);
    return result;
}

static void ineligible(IDirect3D9* api,HWND window,D3DFORMAT format,bool automatic,
                       D3DMULTISAMPLE_TYPE msaa=D3DMULTISAMPLE_NONE) {
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil=automatic;pp.AutoDepthStencilFormat=format;pp.MultiSampleType=msaa;
    std::printf("INELIGIBLE automatic=%u format=%u msaa=%u\n",automatic,unsigned(format),unsigned(msaa));
    Com<IDirect3DDevice9> app;ok("create ineligible device",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&app.p));
    const auto view=depth_view(app.p);expect("ineligible has no replacement",!view.available&&!view.texture&&view.status==S_FALSE);
    if(automatic) {
        Com<IDirect3DSurface9> surface;ok("ineligible depth getter",app->GetDepthStencilSurface(&surface.p));
        D3DSURFACE_DESC desc{};ok("ineligible descriptor",surface->GetDesc(&desc));
        expect("ineligible native format preserved",desc.Format==format&&desc.MultiSampleType==msaa);
    }
}

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;HWND window=nullptr;
    try {
        if(argc!=2)throw std::runtime_error("usage: auto_depth_fixture.exe <D3DX path>");
        WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3AutoDepthFixture";
        RegisterClassA(&wc);window=CreateWindowA(wc.lpszClassName,"X3 automatic depth fixture",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,wc.hInstance,nullptr);
        expect("hidden window",window!=nullptr);
        auto compiler=entry<Compiler>(LoadLibraryA(argv[1]),"D3DXCompileShader");
        auto create=entry<IDirect3D9* (WINAPI*)(UINT)>(LoadLibraryA("d3d9.dll"),"Direct3DCreate9");
        for(unsigned stage=1;stage<=3;++stage) {
            auto* fault_backend=create(D3D_SDK_VERSION);expect("fault native factory",fault_backend!=nullptr);
            allocation_failure(fault_backend,window,stage);
        }
        IDirect3D9* backend=create(D3D_SDK_VERSION);expect("native factory",backend!=nullptr);
        Com<IDirect3D9> api;own::Options options;options.sampleable_auto_depth=true;
        HRESULT hr=own::wrap_factory(backend,&api.p,options);if(FAILED(hr))backend->Release();ok("wrap factory enabled",hr);
        ineligible(api.p,window,D3DFMT_D16,true);
        ineligible(api.p,window,D3DFMT_D24S8,true);
        ineligible(api.p,window,D3DFMT_D24X8,false);
        DWORD color_quality=0,depth_quality=0;
        if(SUCCEEDED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_2_SAMPLES,&color_quality)) &&
           SUCCEEDED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_D24X8,TRUE,D3DMULTISAMPLE_2_SAMPLES,&depth_quality)) && color_quality && depth_quality)
            ineligible(api.p,window,D3DFMT_D24X8,true,D3DMULTISAMPLE_2_SAMPLES);
        else std::puts("COVERAGE MSAA creation unavailable on backend; eligibility predicate remains source-reviewed only");
        {
            IDirect3D9* off_native=create(D3D_SDK_VERSION);expect("disabled native factory",off_native!=nullptr);
            Com<IDirect3D9> off;hr=own::wrap_factory(off_native,&off.p);if(FAILED(hr))off_native->Release();ok("disabled wrapper",hr);
            ineligible(off.p,window,D3DFMT_D24X8,true);
            auto original=compatibility(off.p,window,"original");auto replaced=compatibility(api.p,window,"sampleable");
            expect("logical stencil clear HRESULT parity",original.stencil_clear==replaced.stencil_clear);
            expect("logical stencil getter parity",original.stencil_enabled==replaced.stencil_enabled&&original.stencil_enabled==TRUE);
            expect("logical stencil pixel parity",original.pixel==replaced.pixel&&original.pixel==0xffff00ff);
            std::printf("DEPTH_COPY_PARITY matched=%u\n",original.copy_from==replaced.copy_from&&original.copy_into==replaced.copy_into);

        }
        exercise(api.p,window,compiler,D3DCREATE_HARDWARE_VERTEXPROCESSING);
        exercise(api.p,window,compiler,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE);
        std::printf("RESULT PASS checks=%u samples=%u\n",checks,samples);result=0;
    } catch(const std::exception& e) {std::printf("RESULT FAIL %s checks=%u samples=%u\n",e.what(),checks,samples);}
    if(window)DestroyWindow(window);
    return result;
}
