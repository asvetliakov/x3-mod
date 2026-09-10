#include "capture.h"
#include "capture_state.h"
#include "telemetry.h"
#include "loading_trace.h"
#include "../ownership/d3d9_ownership.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace x3m {
namespace {
std::recursive_mutex mutex;
FILE* logfile;
std::wstring directory;
unsigned capture_start = 120;
unsigned capture_count = 1;
std::set<uint64_t> dumped;
uint64_t next_device_id = 1;

// Each object owns a private copy of the backend vtable. We don't patch shared
// executable pages, wrap resources, or change IUnknown identity. Ex tails are
// retained if supported, even though this first capture implementation targets 9.
struct Hooks {
    void** original;
    std::vector<void*> table;
    Hooks(void* object, size_t size) {
        original = *static_cast<void***>(object);
        table.assign(original, original + size);
    }
    void install(void* object) { *static_cast<void***>(object) = table.data(); }
    template<typename Fn> Fn get(size_t slot) const {
        return reinterpret_cast<Fn>(original[slot]);
    }
    template<typename Fn> void set(size_t slot, Fn fn) {
        table[slot] = reinterpret_cast<void*>(fn);
    }
};
struct Device : Hooks {
    D3DCAPS9 caps{};
    uint64_t id = next_device_id++;
    uint64_t frame = 0;
    uint64_t draws = 0;
    uint64_t events = 0;
    telemetry::State stats;
    unsigned remaining = 0;
    bool capture = false;
    bool key_down = false;
    explicit Device(void* object, size_t size) : Hooks(object, size) {}
};
// Preserve the original serialization while exposing its CPU-side wait cost.
struct HookGuard {
    std::unique_lock<std::recursive_mutex> lock;
    HookGuard():lock(mutex,std::defer_lock){const auto start=telemetry::now();lock.lock();telemetry::record(telemetry::process(),telemetry::Metric::LockWait,telemetry::now()-start);}
};
struct CallTimer {
    Device& ctx; uint64_t start, backend_start=0, backend_ticks=0;
    bool captured;
    explicit CallTimer(Device& value):ctx(value),start(telemetry::now()),captured(value.capture){}
    void begin(){backend_start=telemetry::now();}
    void end(){backend_ticks=telemetry::now()-backend_start;}
    ~CallTimer(){if(captured)telemetry::record(ctx.stats,telemetry::Metric::CaptureCpu,telemetry::now()-start-backend_ticks);}
};
void presentation_parameters(const char* phase,uint64_t device,HWND focus,const D3DPRESENT_PARAMETERS* p){
    if(!telemetry::enabled())return;
    if(!p){log("telemetry_presentation phase=%s device=%llu focus_window=%p params_null=1",phase,device,focus);return;}
    log("telemetry_presentation phase=%s device=%llu thread=%lu focus_window=%p device_window=%p width=%u height=%u format=%u count=%u msaa=%u quality=%lu swap_effect=%u windowed=%d auto_depth=%d depth_format=%u flags=%08lx refresh=%u interval=%u",phase,device,GetCurrentThreadId(),focus,p->hDeviceWindow,p->BackBufferWidth,p->BackBufferHeight,p->BackBufferFormat,p->BackBufferCount,p->MultiSampleType,p->MultiSampleQuality,p->SwapEffect,p->Windowed,p->EnableAutoDepthStencil,p->AutoDepthStencilFormat,p->Flags,p->FullScreen_RefreshRateInHz,p->PresentationInterval);
}
void capture_event(Device& ctx,const char* operation,HRESULT result,bool before_draw=false){
    if(ctx.capture)log("capture_event device=%llu frame=%llu seq=%llu after_draw=%llu op=%s result=%08lx qpc=%llu",ctx.id,ctx.frame,++ctx.events,ctx.draws-(before_draw?1:0),operation,result,telemetry::now());
}
std::map<IDirect3D9*, std::unique_ptr<Hooks>> factories;
std::map<IDirect3DDevice9*, std::unique_ptr<Device>> devices;

uint64_t hash_bytes(const void* data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    auto bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
template<typename Shader> uint64_t shader_id(Shader* shader, const char* kind) {
    if (!shader) return 0;
    telemetry::Scope inspect(telemetry::process(),telemetry::Metric::ShaderInspect);
    const auto get_begin=telemetry::now();
    UINT bytes = 0;
    if (FAILED(shader->GetFunction(nullptr, &bytes)) || !bytes || bytes > 4*1024*1024) return 0;
    std::vector<unsigned char> code(bytes);
    if (FAILED(shader->GetFunction(code.data(), &bytes))) return 0;
    telemetry::record(telemetry::process(),telemetry::Metric::ShaderGetFunction,telemetry::now()-get_begin);
    const auto hash_begin=telemetry::now();
    auto hash = hash_bytes(code.data(), bytes);
    telemetry::record(telemetry::process(),telemetry::Metric::ShaderHash,telemetry::now()-hash_begin,false,bytes);
    if (dumped.insert(hash).second) {
        const auto dump_begin=telemetry::now();
        wchar_t suffix[80];
        swprintf(suffix, 80, L"\\%hs_%016llx.bin", kind, static_cast<unsigned long long>(hash));
        FILE* file = _wfopen((directory + suffix).c_str(), L"wb");
        size_t written=0;bool dump_ok=false;
        if (file) { written=fwrite(code.data(),1,bytes,file);const int closed=fclose(file);dump_ok=written==bytes&&closed==0; }
        telemetry::record(telemetry::process(),telemetry::Metric::ShaderDump,telemetry::now()-dump_begin,!dump_ok,written);
        log("shader kind=%s id=%016llx bytes=%u dumped=%u", kind,
            static_cast<unsigned long long>(hash), bytes, dump_ok);
    }
    return hash;
}
void surface_info(const char* name, IDirect3DSurface9* surface) {
    if (!surface) { log("surface role=%s ptr=0 identity=0",name); return; }
    IDirect3DBaseTexture9* container = nullptr;
    const HRESULT container_result=surface->GetContainer(IID_IDirect3DBaseTexture9,reinterpret_cast<void**>(&container));
    const auto parent_id=resource_id(container);
    const UINT parent_type=container ? container->GetType() : 0;
    if (container) container->Release();
    const auto id=resource_id(surface);
    D3DSURFACE_DESC desc{};
    const HRESULT result=surface->GetDesc(&desc);
    if (SUCCEEDED(result))
        log("surface role=%s ptr=%p identity=%llu width=%u height=%u format=%u usage=%lu msaa=%u container=%llu container_type=%u container_result=%08lx", name,
            surface,id,desc.Width,desc.Height,desc.Format,desc.Usage,desc.MultiSampleType,parent_id,parent_type,container_result);
    else log("surface role=%s ptr=%p identity=%llu result=%08lx",name,surface,id,result);
}
void ownership_depth_info(IDirect3DDevice9* d, uint64_t device, const char* phase) {
    // This is a borrowed diagnostic snapshot, never a resource adoption or a
    // GPU allocation. Native/default mode must not query ownership internals.
    if (!ownership::borrowed_native_device(d)) return;
    ownership::DepthView view{};
    const HRESULT result = ownership::get_depth_view(d, &view);
    const auto& desc = view.logical_desc;
    log("ownership_depth phase=%s device=%llu result=%08lx status=%08lx requested=%u available=%u bound=%u generation=%llu clear_epoch=%llu logical_width=%u logical_height=%u logical_format=%u logical_type=%u logical_usage=%lu logical_pool=%u logical_msaa=%u logical_quality=%lu",
        phase,device,result,view.status,view.requested,view.available,view.bound,
        view.generation,view.clear_epoch,desc.Width,desc.Height,desc.Format,desc.Type,
        desc.Usage,desc.Pool,desc.MultiSampleType,desc.MultiSampleQuality);
}
void snapshot(IDirect3DDevice9* d, const char* kind, D3DPRIMITIVETYPE type, UINT primitives, bool user_memory=false) {
    auto& ctx = *devices.at(d);
    ++ctx.draws;
    if (!ctx.capture) return;
    telemetry::Scope timed(ctx.stats,telemetry::Metric::Snapshot);
    capture_event(ctx,"draw_begin",S_OK,true);
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    d->GetVertexShader(&vs); d->GetPixelShader(&ps);
    const auto vhash = shader_id(vs,"vs"), phash = shader_id(ps,"ps");
    if (vs) vs->Release();
    if (ps) ps->Release();
    log("draw device=%llu frame=%llu index=%llu kind=%s topology=%u primitives=%u vs=%016llx ps=%016llx",
        ctx.id,ctx.frame,ctx.draws,kind,type,primitives,vhash,phash);
    capture_geometry(d,user_memory,ctx.caps);
    IDirect3DSurface9* rt = nullptr;
    for (DWORD i = 0; i < 4; ++i) {
        if (SUCCEEDED(d->GetRenderTarget(i,&rt)) && rt) {
            char role[8]; snprintf(role,sizeof role,"rt%lu",i);
            surface_info(role,rt); rt->Release(); rt = nullptr;
        }
    }
    if (SUCCEEDED(d->GetDepthStencilSurface(&rt)) && rt) { surface_info("depth",rt); rt->Release(); }
    for (auto state : {D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,
                       D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,
                       D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,
                       D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,
                       D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,
                       D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,
                       D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_TWOSIDEDSTENCILMODE,
                       D3DRS_CCW_STENCILFUNC,D3DRS_CCW_STENCILFAIL,
                       D3DRS_CCW_STENCILZFAIL,D3DRS_CCW_STENCILPASS}) {
        DWORD value = 0;
        HRESULT hr = d->GetRenderState(state,&value);
        if (SUCCEEDED(hr)) log("state id=%u value=%lu",state,value);
    }
    D3DVIEWPORT9 vp{};
    if (SUCCEEDED(d->GetViewport(&vp))) log("viewport x=%lu y=%lu w=%lu h=%lu minz=%g maxz=%g",vp.X,vp.Y,vp.Width,vp.Height,vp.MinZ,vp.MaxZ);
    for (DWORD i=0; i<16; ++i) {
        IDirect3DBaseTexture9* texture = nullptr;
        if (SUCCEEDED(d->GetTexture(i,&texture)) && texture) {
            log("texture stage=%lu ptr=%p type=%u identity=%llu",i,texture,texture->GetType(),resource_id(texture));
            if (texture->GetType()==D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC desc{};
                if (SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc)))
                    log("texture_desc stage=%lu w=%u h=%u format=%u",i,desc.Width,desc.Height,desc.Format);
            }
            texture->Release();
        }
        for (auto state : {D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_MAXANISOTROPY,D3DSAMP_SRGBTEXTURE}) {
            DWORD value = 0;
            if (SUCCEEDED(d->GetSamplerState(i,state,&value))) log("sampler stage=%lu state=%u value=%lu",i,state,value);
        }
    }
    IDirect3DVertexDeclaration9* declaration = nullptr;
    if (SUCCEEDED(d->GetVertexDeclaration(&declaration)) && declaration) {
        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{}; UINT count=MAXD3DDECLLENGTH+1;
        if (SUCCEEDED(declaration->GetDeclaration(elements,&count)))
            for (UINT i=0; i<count; ++i) log("vertex_element stream=%u offset=%u type=%u method=%u usage=%u index=%u",elements[i].Stream,elements[i].Offset,elements[i].Type,elements[i].Method,elements[i].Usage,elements[i].UsageIndex);
        declaration->Release();
    }
    for (auto state : {D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION}) {
        D3DMATRIX m{};
        if (SUCCEEDED(d->GetTransform(state,&m)))
            for (unsigned i=0;i<4;++i) log("transform state=%u row=%u values=%.9g,%.9g,%.9g,%.9g",state,i,m.m[i][0],m.m[i][1],m.m[i][2],m.m[i][3]);
    }
    capture_constants(d,true,ctx.caps); capture_constants(d,false,ctx.caps);
}
ULONG WINAPI release_device(IDirect3DDevice9* d) {
    HookGuard lock;
    auto fn = devices.at(d)->get<ULONG (WINAPI*)(IDirect3DDevice9*)>(2);
    ULONG refs=fn(d);
    if (!refs) { telemetry::summary(devices.at(d)->stats,"device_destroy",devices.at(d)->frame); telemetry::summary(telemetry::process(),"device_destroy",devices.at(d)->frame); log("device_destroy ptr=%p device=%llu",d,devices.at(d)->id); devices.erase(d); }
    return refs;
}
HRESULT WINAPI present(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r) {
    HookGuard lock;
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*)>(17);
    const auto begin=telemetry::now();
    const HRESULT hr=fn(d,a,b,w,r);
    const auto end=telemetry::now();
    telemetry::present(ctx.stats,ctx.frame,ctx.capture,begin,end,hr);
    if(telemetry::enabled()&&(!ctx.stats.present_override_known||ctx.stats.present_override!=w)){
        log("telemetry_present_window device=%llu frame=%llu override=%p device_window=%p result=%08lx",ctx.id,ctx.frame,w,ctx.stats.window,hr);
        ctx.stats.present_override_known=true;ctx.stats.present_override=w;
    }
    if (ctx.capture || ctx.frame%300==0) log("frame_end device=%llu frame=%llu draws=%llu capture=%u present=%08lx",ctx.id,ctx.frame,ctx.draws,ctx.capture,hr);
    if (ctx.capture && ctx.remaining) --ctx.remaining;
    ++ctx.frame; ctx.draws=0; ctx.events=0; ctx.stats.frame=ctx.frame;
    const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if ((down&&!ctx.key_down) || (capture_count && ctx.frame==capture_start)) ctx.remaining=capture_count ? capture_count : 1;
    ctx.key_down=down; ctx.capture=ctx.remaining>0;
    if (ctx.capture) log("frame_begin device=%llu frame=%llu",ctx.id,ctx.frame);
    if (logfile) { const auto begin=telemetry::now(); fflush(logfile); telemetry::record(ctx.stats,telemetry::Metric::LogFlush,telemetry::now()-begin); }
    return hr;
}
HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    HookGuard lock;
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*)>(16);
    ctx.capture=false; ctx.remaining=0;ctx.stats.had_present=false;ctx.stats.last_frame_capture=false;++ctx.stats.resets;
    presentation_parameters("reset_before",ctx.id,ctx.stats.focus_window,p);
    log("reset_begin ptr=%p device=%llu",d,ctx.id);
    const auto begin=telemetry::now();
    HRESULT hr=fn(d,p); telemetry::record(ctx.stats,telemetry::Metric::Reset,telemetry::now()-begin,FAILED(hr));
    presentation_parameters("reset_after",ctx.id,ctx.stats.focus_window,p);
    ownership_depth_info(d,ctx.id,"reset_after");
    if(SUCCEEDED(hr)&&p&&p->hDeviceWindow)ctx.stats.window=p->hDeviceWindow;
    telemetry::summary(ctx.stats,"reset",ctx.frame);
    log("reset_end device=%llu result=%08lx",ctx.id,hr); return hr;
}
HRESULT WINAPI draw_primitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT c) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    snapshot(d,"primitive",t,c);
    if (devices.at(d)->capture) log("draw_args start_vertex=%u",s);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT)>(81)(d,t,s,c);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    snapshot(d,"indexed",t,c);
    if (devices.at(d)->capture) log("draw_args base_vertex=%d min_vertex=%u num_vertices=%u start_index=%u",b,m,n,s);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)>(82)(d,t,b,m,n,s,c);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT c,const void* data,UINT stride) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    snapshot(d,"up",t,c,true);
    if (devices.at(d)->capture) log("draw_args vertex_ptr=%p stride=%u",data,stride);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(83)(d,t,c,data,stride);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT m,UINT n,UINT c,const void* indices,D3DFORMAT f,const void* data,UINT stride) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    snapshot(d,"indexed_up",t,c,true);
    if (devices.at(d)->capture) log("draw_args min_vertex=%u num_vertices=%u vertex_ptr=%p stride=%u index_ptr=%p index_format=%u",m,n,data,stride,indices,f);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)>(84)(d,t,m,n,c,indices,f,data,stride);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI clear(IDirect3DDevice9* d,DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s) {
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(d,n,r,f,c,z,s);
    timer.end();
    if(ctx.capture){
        capture_event(ctx,"clear",result);
        log("clear flags=%lu color=%08lx z=%g stencil=%lu rect_count=%lu rect_ptr=%p",f,c,z,s,n,r);
        if(SUCCEEDED(result)&&r&&n){
            const DWORD count=n<16?n:16;
            log("clear_rects recorded=%lu omitted=%lu",count,n-count);
            for(DWORD i=0;i<count;++i)log("clear_rect index=%lu left=%ld top=%ld right=%ld bottom=%ld",i,r[i].x1,r[i].y1,r[i].x2,r[i].y2);
        }
        // A depth-only Clear can divide scene and HUD without a target rebind.
        IDirect3DSurface9* surface=nullptr;
        if(SUCCEEDED(d->GetRenderTarget(0,&surface))&&surface){surface_info("clear_rt0",surface);surface->Release();surface=nullptr;}
        if(SUCCEEDED(d->GetDepthStencilSurface(&surface))&&surface){surface_info("clear_depth",surface);surface->Release();}
    }
    return result;
}
HRESULT WINAPI set_rt(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9* rt) {
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(37)(d,index,rt);timer.end();
    if(ctx.capture){capture_event(ctx,"set_rt",result);log("set_rt index=%lu result=%08lx ptr=%p",index,result,rt);if(SUCCEEDED(result))surface_info("binding",rt);}
    return result;
}
HRESULT WINAPI set_depth(IDirect3DDevice9* d,IDirect3DSurface9* depth) {
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*)>(39)(d,depth);timer.end();
    if(ctx.capture){capture_event(ctx,"set_depth",result);log("set_depth ptr=%p result=%08lx",depth,result);if(SUCCEEDED(result))surface_info("depth_binding",depth);}
    return result;
}
HRESULT WINAPI stretch_rect(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* source_rect,IDirect3DSurface9* dest,const RECT* dest_rect,D3DTEXTUREFILTERTYPE filter){
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)>(34)(d,source,source_rect,dest,dest_rect,filter);timer.end();
    if(ctx.capture){
        capture_event(ctx,"stretch_rect",result);log("stretch_rect source=%p dest=%p filter=%u source_rect_null=%u dest_rect_null=%u",source,dest,filter,source_rect==nullptr,dest_rect==nullptr);
        if(SUCCEEDED(result)){
            if(source_rect)log("stretch_source_rect left=%ld top=%ld right=%ld bottom=%ld",source_rect->left,source_rect->top,source_rect->right,source_rect->bottom);
            if(dest_rect)log("stretch_dest_rect left=%ld top=%ld right=%ld bottom=%ld",dest_rect->left,dest_rect->top,dest_rect->right,dest_rect->bottom);
            surface_info("stretch_source",source);surface_info("stretch_dest",dest);
        }
    }
    return result;
}
// These resource hooks are installed only when CPU telemetry is enabled. The
// backend receives every pointer/flag unchanged; returned objects are not wrapped.
HRESULT WINAPI create_texture(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*)>(23)(d,w,h,levels,usage,format,pool,out,shared);
    telemetry::record(ctx.stats,telemetry::Metric::Texture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_volume(IDirect3DDevice9* d,UINT w,UINT h,UINT depth,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DVolumeTexture9** out,HANDLE* shared){
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture9**,HANDLE*)>(24)(d,w,h,depth,levels,usage,format,pool,out,shared);
    telemetry::record(ctx.stats,telemetry::Metric::VolumeTexture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_cube(IDirect3DDevice9* d,UINT edge,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DCubeTexture9** out,HANDLE* shared){
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DCubeTexture9**,HANDLE*)>(25)(d,edge,levels,usage,format,pool,out,shared);
    telemetry::record(ctx.stats,telemetry::Metric::CubeTexture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_vb(IDirect3DDevice9* d,UINT length,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared){
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*)>(26)(d,length,usage,fvf,pool,out,shared);
    telemetry::record(ctx.stats,telemetry::Metric::VertexBuffer,telemetry::now()-begin,FAILED(result),SUCCEEDED(result)?length:0);return result;
}
HRESULT WINAPI create_ib(IDirect3DDevice9* d,UINT length,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared){
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*)>(27)(d,length,usage,format,pool,out,shared);
    telemetry::record(ctx.stats,telemetry::Metric::IndexBuffer,telemetry::now()-begin,FAILED(result),SUCCEEDED(result)?length:0);return result;
}
HRESULT WINAPI create_rt(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,D3DMULTISAMPLE_TYPE ms, DWORD quality,BOOL lockable,IDirect3DSurface9** out,HANDLE* shared){
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)>(28)(d,w,h,format,ms,quality,lockable,out,shared);
    telemetry::record(ctx.stats,telemetry::Metric::RenderTarget,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_depth(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,D3DMULTISAMPLE_TYPE ms,DWORD quality,BOOL discard,IDirect3DSurface9** out,HANDLE* shared){
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)>(29)(d,w,h,format,ms,quality,discard,out,shared);
    telemetry::record(ctx.stats,telemetry::Metric::DepthStencil,telemetry::now()-begin,FAILED(result));return result;
}
// Restore after mutex/timing/logging destructors, preserving both backend error
// output and the incoming value when the backend leaves last-error untouched.
struct LastErrorPreserver {
    DWORD incoming=GetLastError(),outgoing=incoming;
    void before(){SetLastError(incoming);}
    void after(){outgoing=GetLastError();}
    ~LastErrorPreserver(){SetLastError(outgoing);}
};
bool cursor_change_allowed(telemetry::State& stats){
    const auto stamp=telemetry::now();if(stats.last_cursor_change && stamp-stats.last_cursor_change<telemetry::frequency()/4){++stats.cursor_changes_suppressed;return false;}stats.last_cursor_change=stamp;return true;
}
HRESULT WINAPI cursor_properties(IDirect3DDevice9* d,UINT x,UINT y,IDirect3DSurface9* surface){
    LastErrorPreserver errors;HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    errors.before();
    const HRESULT result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,IDirect3DSurface9*)>(10)(d,x,y,surface);errors.after();
    telemetry::record(stats,telemetry::Metric::CursorProperties,telemetry::now()-begin,FAILED(result));
    if((!stats.properties_known||stats.cursor_x!=x||stats.cursor_y!=y||stats.cursor_surface!=surface||FAILED(result))&&cursor_change_allowed(stats))log("telemetry_cursor_api device=%llu frame=%llu op=properties hotspot=%u,%u surface=%p result=%08lx",ctx.id,ctx.frame,x,y,surface,result);
    if(SUCCEEDED(result)){stats.properties_known=true;stats.cursor_x=x;stats.cursor_y=y;stats.cursor_surface=surface;}
    return result;
}
void WINAPI cursor_position(IDirect3DDevice9* d,int x,int y,DWORD flags){
    LastErrorPreserver errors;HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    errors.before();
    ctx.get<void(WINAPI*)(IDirect3DDevice9*,int,int,DWORD)>(11)(d,x,y,flags);errors.after();
    telemetry::record(stats,telemetry::Metric::CursorPosition,telemetry::now()-begin);
    const auto stamp=telemetry::now();
    if((!stats.last_position||x!=stats.logged_position_x||y!=stats.logged_position_y||flags!=stats.logged_position_flags) && (!stats.last_position||stamp-stats.last_position>=telemetry::frequency()/4)){log("telemetry_cursor_api device=%llu frame=%llu op=position x=%d y=%d flags=%lu",ctx.id,ctx.frame,x,y,flags);stats.last_position=stamp;stats.logged_position_x=x;stats.logged_position_y=y;stats.logged_position_flags=flags;}
    else ++stats.position_suppressed;

}
BOOL WINAPI cursor_show(IDirect3DDevice9* d,BOOL show){
    LastErrorPreserver errors;HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    errors.before();
    const BOOL previous=ctx.get<BOOL(WINAPI*)(IDirect3DDevice9*,BOOL)>(12)(d,show);errors.after();
    telemetry::record(stats,telemetry::Metric::CursorShow,telemetry::now()-begin);
    if((!stats.api_show_known||stats.api_show!=show)&&cursor_change_allowed(stats))log("telemetry_cursor_api device=%llu frame=%llu op=show requested=%d previous_visible=%d",ctx.id,ctx.frame,show,previous);
    stats.api_show_known=true;stats.api_show=show;return previous;
}
HRESULT WINAPI create_vs(IDirect3DDevice9* d,const DWORD* code,IDirect3DVertexShader9** out) {
    HookGuard lock;
    const auto begin=telemetry::now();
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**)>(91)(d,code,out);
    telemetry::record(devices.at(d)->stats,telemetry::Metric::ShaderVS,telemetry::now()-begin,FAILED(hr));
    if(SUCCEEDED(hr)&&out) shader_id(*out,"vs");
    return hr;
}
HRESULT WINAPI create_ps(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9** out) {
    HookGuard lock;
    const auto begin=telemetry::now();
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**)>(106)(d,code,out);
    telemetry::record(devices.at(d)->stats,telemetry::Metric::ShaderPS,telemetry::now()-begin,FAILED(hr));
    if(SUCCEEDED(hr)&&out) shader_id(*out,"ps");
    return hr;
}
void hook_device(IDirect3DDevice9* d,HWND window,HWND focus) {
    if (devices.count(d)) return;
    IDirect3DDevice9Ex* ex=nullptr;
    const bool supports_ex=SUCCEEDED(d->QueryInterface(IID_IDirect3DDevice9Ex,reinterpret_cast<void**>(&ex)))
        && static_cast<void*>(ex)==static_cast<void*>(d);
    if(ex) ex->Release();
    auto ctx=std::make_unique<Device>(d,supports_ex?134:119);
    ctx->stats.device=ctx->id;ctx->stats.window=window;ctx->stats.focus_window=focus;
    const HRESULT caps_result=d->GetDeviceCaps(&ctx->caps);
    log("capture_caps result=%08lx streams=%lu vs_float_count=%lu ps_version=%08lx",caps_result,ctx->caps.MaxStreams,ctx->caps.MaxVertexShaderConst,ctx->caps.PixelShaderVersion);
    ctx->set(2,release_device); ctx->set(16,reset); ctx->set(17,present);
    ctx->set(37,set_rt);ctx->set(43,clear);
    if(telemetry::enabled()){
        ctx->set(34,stretch_rect);ctx->set(39,set_depth);
        ctx->set(10,cursor_properties);ctx->set(11,cursor_position);ctx->set(12,cursor_show);
        ctx->set(23,create_texture);ctx->set(24,create_volume);ctx->set(25,create_cube);
        ctx->set(26,create_vb);ctx->set(27,create_ib);ctx->set(28,create_rt);ctx->set(29,create_depth);
    }
    ctx->set(81,draw_primitive); ctx->set(82,draw_indexed); ctx->set(83,draw_up); ctx->set(84,draw_indexed_up);
    ctx->set(91,create_vs); ctx->set(106,create_ps);
    // Publish only after the owning map allocation succeeds.
    auto entry=devices.emplace(d,std::move(ctx));
    entry.first->second->install(d);
    log("device_hooked ptr=%p device=%llu ex=%u",d,devices.at(d)->id,supports_ex);
    ownership_depth_info(d,devices.at(d)->id,"create_after");
}
ULONG WINAPI release_factory(IDirect3D9* d) {
    HookGuard lock;
    ULONG refs=factories.at(d)->get<ULONG (WINAPI*)(IDirect3D9*)>(2)(d);
    if(!refs) factories.erase(d);
    return refs;
}
HRESULT WINAPI create_device(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
    HookGuard lock;
    presentation_parameters("create_before",0,window,p);
    if(p) log("create_device adapter=%u flags=%08lx width=%u height=%u format=%u windowed=%u msaa=%u interval=%u",adapter,flags,p->BackBufferWidth,p->BackBufferHeight,p->BackBufferFormat,p->Windowed,p->MultiSampleType,p->PresentationInterval);
    const auto begin=telemetry::now();
    HRESULT hr=factories.at(d)->get<HRESULT (WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**)>(16)(d,adapter,type,window,flags,p,out);
    telemetry::record(telemetry::process(),telemetry::Metric::CreateDevice,telemetry::now()-begin,FAILED(hr));
    presentation_parameters("create_after",0,window,p);
    log("create_device_result hr=%08lx",hr);
    if(SUCCEEDED(hr)&&out&&*out) hook_device(*out,p&&p->hDeviceWindow?p->hDeviceWindow:window,window);
    return hr;
}
}
void initialize_log(HMODULE module) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    wchar_t path[32768]{}; GetModuleFileNameW(module,path,32768);
    directory=path; directory.resize(directory.find_last_of(L"\\/"));
    directory+=L"\\x3-modern-captures"; CreateDirectoryW(directory.c_str(),nullptr);
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t suffix[100]; swprintf(suffix,100,L"\\session-%04u%02u%02u-%02u%02u%02u-%lu.log",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    logfile=_wfopen((directory+suffix).c_str(),L"w");
    if(logfile) setvbuf(logfile,nullptr,_IOFBF,1024*1024);
    wchar_t setting[32]{};
    if(GetEnvironmentVariableW(L"X3M_CAPTURE_START",setting,32)>0) capture_start=wcstoul(setting,nullptr,10);
    if(GetEnvironmentVariableW(L"X3M_CAPTURE_FRAMES",setting,32)>0) capture_count=wcstoul(setting,nullptr,10);
    if(capture_count>8) capture_count=8;
    log("x3-modern-renderer version=0.4 schema=2 capture_start=%u capture_frames=%u pointer_bits=32",capture_start,capture_count);
    telemetry::initialize([]{if(logfile)fflush(logfile);});
    if(telemetry::enabled())loading_trace::initialize();
}
void log(const char* format,...) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!logfile) return;
    va_list args; va_start(args,format); vfprintf(logfile,format,args); va_end(args); fputc('\n',logfile);
}
void hook_direct3d(IDirect3D9* d) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(factories.count(d)) return;
    IDirect3D9Ex* ex=nullptr;
    const bool supports_ex=SUCCEEDED(d->QueryInterface(IID_IDirect3D9Ex,reinterpret_cast<void**>(&ex)))
        && static_cast<void*>(ex)==static_cast<void*>(d);
    if(ex) ex->Release();
    auto ctx=std::make_unique<Hooks>(d,supports_ex?22:17);
    ctx->set(2,release_factory); ctx->set(16,create_device);
    auto entry=factories.emplace(d,std::move(ctx));
    entry.first->second->install(d);
    D3DADAPTER_IDENTIFIER9 id{};
    if(SUCCEEDED(d->GetAdapterIdentifier(D3DADAPTER_DEFAULT,0,&id))) log("adapter description=%s driver=%s vendor=%08lx device=%08lx",id.Description,id.Driver,id.VendorId,id.DeviceId);
}
}
