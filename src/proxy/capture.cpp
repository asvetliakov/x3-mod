#include "capture.h"
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
    uint64_t frame = 0;
    uint64_t draws = 0;
    unsigned remaining = 0;
    bool capture = false;
    bool key_down = false;
    explicit Device(void* object, size_t size) : Hooks(object, size) {}
};
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
    UINT bytes = 0;
    if (FAILED(shader->GetFunction(nullptr, &bytes)) || !bytes || bytes > 4*1024*1024) return 0;
    std::vector<unsigned char> code(bytes);
    if (FAILED(shader->GetFunction(code.data(), &bytes))) return 0;
    auto hash = hash_bytes(code.data(), bytes);
    if (dumped.insert(hash).second) {
        wchar_t suffix[80];
        swprintf(suffix, 80, L"\\%hs_%016llx.bin", kind, static_cast<unsigned long long>(hash));
        FILE* file = _wfopen((directory + suffix).c_str(), L"wb");
        if (file) { fwrite(code.data(), 1, bytes, file); fclose(file); }
        log("shader kind=%s id=%016llx bytes=%u dumped=%u", kind,
            static_cast<unsigned long long>(hash), bytes, file != nullptr);
    }
    return hash;
}
void surface_info(const char* name, IDirect3DSurface9* surface) {
    if (!surface) { log("surface role=%s ptr=0", name); return; }
    D3DSURFACE_DESC desc{};
    if (SUCCEEDED(surface->GetDesc(&desc)))
        log("surface role=%s ptr=%p width=%u height=%u format=%u usage=%lu msaa=%u", name,
            surface, desc.Width, desc.Height, desc.Format, desc.Usage, desc.MultiSampleType);
}
void constants(IDirect3DDevice9* d, bool vertex) {
    // Full live state includes effects restored through state blocks; setter-only
    // tracing would silently miss those. Pure devices may reject these queries.
    float values[256*4]{};
    UINT count = vertex ? 256 : 224;
    HRESULT hr = vertex ? d->GetVertexShaderConstantF(0, values, count)
                        : d->GetPixelShaderConstantF(0, values, count);
    if (FAILED(hr)) { log("constants kind=%s unavailable=%08lx", vertex ? "vs":"ps", hr); return; }
    for (UINT i=0; i<count; ++i) {
        const float* v = values+i*4;
        // Write bit patterns so NaNs, infinities and signed zero are lossless.
        uint32_t bits[4]; memcpy(bits, v, sizeof bits);
        if (bits[0] || bits[1] || bits[2] || bits[3])
            log("constant kind=%s reg=%u bits=%08x,%08x,%08x,%08x", vertex?"vs":"ps",i,bits[0],bits[1],bits[2],bits[3]);
    }
}
void snapshot(IDirect3DDevice9* d, const char* kind, D3DPRIMITIVETYPE type, UINT primitives) {
    auto& ctx = *devices.at(d);
    ++ctx.draws;
    if (!ctx.capture) return;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    d->GetVertexShader(&vs); d->GetPixelShader(&ps);
    const auto vhash = shader_id(vs,"vs"), phash = shader_id(ps,"ps");
    if (vs) vs->Release();
    if (ps) ps->Release();
    log("draw frame=%llu index=%llu kind=%s topology=%u primitives=%u vs=%016llx ps=%016llx",
        ctx.frame,ctx.draws,kind,type,primitives,vhash,phash);
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
                       D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE}) {
        DWORD value = 0;
        HRESULT hr = d->GetRenderState(state,&value);
        if (SUCCEEDED(hr)) log("state id=%u value=%lu",state,value);
    }
    D3DVIEWPORT9 vp{};
    if (SUCCEEDED(d->GetViewport(&vp))) log("viewport x=%lu y=%lu w=%lu h=%lu minz=%g maxz=%g",vp.X,vp.Y,vp.Width,vp.Height,vp.MinZ,vp.MaxZ);
    for (DWORD i=0; i<16; ++i) {
        IDirect3DBaseTexture9* texture = nullptr;
        if (SUCCEEDED(d->GetTexture(i,&texture)) && texture) {
            log("texture stage=%lu ptr=%p type=%u",i,texture,texture->GetType());
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
    constants(d,true); constants(d,false);
}
ULONG WINAPI release_device(IDirect3DDevice9* d) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto fn = devices.at(d)->get<ULONG (WINAPI*)(IDirect3DDevice9*)>(2);
    ULONG refs=fn(d);
    if (!refs) { log("device_destroy ptr=%p",d); devices.erase(d); }
    return refs;
}
HRESULT WINAPI present(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*)>(17);
    const HRESULT hr=fn(d,a,b,w,r);
    if (ctx.capture || ctx.frame%300==0) log("frame_end frame=%llu draws=%llu capture=%u present=%08lx",ctx.frame,ctx.draws,ctx.capture,hr);
    if (ctx.capture && ctx.remaining) --ctx.remaining;
    ++ctx.frame; ctx.draws=0;
    const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if ((down&&!ctx.key_down) || (capture_count && ctx.frame==capture_start)) ctx.remaining=capture_count ? capture_count : 1;
    ctx.key_down=down; ctx.capture=ctx.remaining>0;
    if (ctx.capture) log("frame_begin frame=%llu",ctx.frame);
    if (logfile) fflush(logfile);
    return hr;
}
HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*)>(16);
    ctx.capture=false; ctx.remaining=0;
    log("reset_begin device=%p",d);
    HRESULT hr=fn(d,p); log("reset_end result=%08lx",hr); return hr;
}
HRESULT WINAPI draw_primitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT c) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    snapshot(d,"primitive",t,c);
    return devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT)>(81)(d,t,s,c);
}
HRESULT WINAPI draw_indexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    snapshot(d,"indexed",t,c);
    return devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)>(82)(d,t,b,m,n,s,c);
}
HRESULT WINAPI draw_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT c,const void* data,UINT stride) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    snapshot(d,"up",t,c);
    return devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(83)(d,t,c,data,stride);
}
HRESULT WINAPI draw_indexed_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT m,UINT n,UINT c,const void* indices,D3DFORMAT f,const void* data,UINT stride) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    snapshot(d,"indexed_up",t,c);
    return devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)>(84)(d,t,m,n,c,indices,f,data,stride);
}
HRESULT WINAPI clear(IDirect3DDevice9* d,DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(devices.at(d)->capture) log("clear flags=%lu color=%08lx z=%g stencil=%lu",f,c,z,s);
    return devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(d,n,r,f,c,z,s);
}
HRESULT WINAPI set_rt(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9* rt) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(37)(d,index,rt);
    if(devices.at(d)->capture) { log("set_rt index=%lu result=%08lx",index,hr); surface_info("binding",rt); }
    return hr;
}
HRESULT WINAPI create_vs(IDirect3DDevice9* d,const DWORD* code,IDirect3DVertexShader9** out) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**)>(91)(d,code,out);
    if(SUCCEEDED(hr)&&out) shader_id(*out,"vs");
    return hr;
}
HRESULT WINAPI create_ps(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9** out) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**)>(106)(d,code,out);
    if(SUCCEEDED(hr)&&out) shader_id(*out,"ps");
    return hr;
}
void hook_device(IDirect3DDevice9* d) {
    if (devices.count(d)) return;
    IDirect3DDevice9Ex* ex=nullptr;
    const bool supports_ex=SUCCEEDED(d->QueryInterface(IID_IDirect3DDevice9Ex,reinterpret_cast<void**>(&ex)))
        && static_cast<void*>(ex)==static_cast<void*>(d);
    if(ex) ex->Release();
    auto ctx=std::make_unique<Device>(d,supports_ex?134:119);
    ctx->set(2,release_device); ctx->set(16,reset); ctx->set(17,present);
    ctx->set(37,set_rt); ctx->set(43,clear);
    ctx->set(81,draw_primitive); ctx->set(82,draw_indexed); ctx->set(83,draw_up); ctx->set(84,draw_indexed_up);
    ctx->set(91,create_vs); ctx->set(106,create_ps);
    // Publish only after the owning map allocation succeeds.
    auto entry=devices.emplace(d,std::move(ctx));
    entry.first->second->install(d);
    log("device_hooked ptr=%p ex=%u",d,supports_ex);
}
ULONG WINAPI release_factory(IDirect3D9* d) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    ULONG refs=factories.at(d)->get<ULONG (WINAPI*)(IDirect3D9*)>(2)(d);
    if(!refs) factories.erase(d);
    return refs;
}
HRESULT WINAPI create_device(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(p) log("create_device adapter=%u flags=%08lx width=%u height=%u format=%u windowed=%u msaa=%u interval=%u",adapter,flags,p->BackBufferWidth,p->BackBufferHeight,p->BackBufferFormat,p->Windowed,p->MultiSampleType,p->PresentationInterval);
    HRESULT hr=factories.at(d)->get<HRESULT (WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**)>(16)(d,adapter,type,window,flags,p,out);
    log("create_device_result hr=%08lx",hr);
    if(SUCCEEDED(hr)&&out&&*out) hook_device(*out);
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
    log("x3-modern-renderer version=0.1 capture_start=%u capture_frames=%u pointer_bits=32",capture_start,capture_count);
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
