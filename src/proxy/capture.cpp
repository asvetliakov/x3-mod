#include "capture.h"
#include "capture_state.h"
#include "telemetry.h"
#include "loading_trace.h"
#include "scene_capture.h"
#include "object_trace.h"
#include "object_lifetime.h"
#include "draw_input.h"
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
bool scene_depth_capture_requested = false;
bool finite_positions_requested = false;
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
    SceneCapture scene_depth;
    DrawInputReader draw_inputs;
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
struct ObservedDraw {
    DrawInput input{};
    object_lifetime::Snapshot lifetime{};
    std::uintptr_t registry = 0;
    bool lifetime_observed = false;
};
ObservedDraw read_draw_input(Device& ctx,IDirect3DDevice9* device,const DrawArguments& arguments) {
    if(!ctx.capture)return {};
    ObservedDraw draw{};
    object_trace::Snapshot scope{};
    const bool scoped=object_trace::current(&scope);
    draw.input=ctx.draw_inputs.read(device,arguments,scoped?&scope:nullptr);
    draw.lifetime_observed=object_lifetime::active();
    if(draw.lifetime_observed)draw.lifetime.reason=object_lifetime::Reason::LookupUnavailable;
    if(scoped&&!(draw.input.blockers&ObjectScope)) {
        draw.registry=scope.registry;
        if(object_lifetime::current(scope.registry,scope.node,scope.node_handle,
                                    scope.camera,scope.camera_handle,&draw.lifetime)) {
            auto& observation=draw.input.observation;
            observation.key.object_lifetime=draw.lifetime.node_serial;
            observation.key.camera_lifetime=draw.lifetime.camera_serial;
            observation.proofs|=renderer::LifetimeVerified;
        }
    }
    return draw;
}
void record_draw_input(Device& ctx,ObservedDraw& draw,HRESULT result) {
    if(!ctx.capture)return;
    auto& input=draw.input;
    DrawInputReader::complete(input,result);
    const auto& o=input.observation;const auto& k=o.key;
    object_lifetime::Snapshot after{};
    if(draw.lifetime.known) {
        const bool known=object_lifetime::current(draw.registry,k.node,k.node_handle,k.camera,k.camera_handle,&after);
        if(!known||after.observer_epoch!=draw.lifetime.observer_epoch||after.load_epoch!=draw.lifetime.load_epoch||
           after.registry_epoch!=draw.lifetime.registry_epoch||after.mutation_revision!=draw.lifetime.mutation_revision||
           after.node_serial!=draw.lifetime.node_serial||after.camera_serial!=draw.lifetime.camera_serial)
            input.observation.proofs&=~renderer::LifetimeVerified;
    } else after=draw.lifetime;
    log("motion_input device=%llu frame=%llu index=%llu blockers=%08lx proofs=%lu position_path=%u vs=%016llx ps=%016llx declaration=%016llx rows_hash=%016llx color=%llu depth=%llu width=%u height=%u cull=%u vb=%llu vb_revision=%llu ib=%llu ib_revision=%llu position_offset=%u position_type=%u lifetime_verified=%u vertex_finite_verified=%u",
        ctx.id,ctx.frame,ctx.draws,static_cast<DWORD>(input.blockers),static_cast<DWORD>(o.proofs),unsigned(input.position_path),
        input.vertex_program,input.pixel_program,k.declaration,hash_bytes(o.submitted_wvp.data(),sizeof(o.submitted_wvp)),
        input.color_target,input.depth_target,input.width,input.height,unsigned(input.cull),
        k.vertex_buffer,k.vertex_revision,k.index_buffer,k.index_revision,k.position_offset,k.position_type,
        (o.proofs&renderer::LifetimeVerified)!=0,input.vertex_finite_verified);
    const auto& finite=input.finite_positions;const auto& indices=input.indices;
    log("motion_geometry device=%llu frame=%llu index=%llu source_qualified=%u source_hash=%016llx source_words=%u finite_requested=%u finite_state=%u finite_reason=%u finite_status=%08lx finite_generation=%llu finite_revision=%llu index_required=%u index_requested=%u index_known=%u index_range_verified=%u index_exact=%u index_min=%u index_max=%u index_reason=%u index_status=%08lx index_generation=%llu index_revision=%llu",
        ctx.id,ctx.frame,ctx.draws,input.replay_source.qualified(),input.replay_source.source_hash(),input.replay_source.source_words(),
        finite.requested,unsigned(finite.state),unsigned(finite.reason),finite.status,finite.generation,finite.revision,
        k.indexed,indices.requested,indices.known,input.index_range_verified,indices.exact_range,indices.minimum,indices.maximum,
        unsigned(indices.reason),indices.status,indices.generation,indices.revision);
    // Preserve the terminal failure record if the observer disabled itself
    // during either lookup. Its current active state must not hide that cause.
    if(draw.lifetime_observed)
        log("motion_lifetime device=%llu frame=%llu index=%llu before_known=%u after_known=%u before_reason=%u after_reason=%u registry=%p observer_epoch=%llu load_epoch=%llu registry_epoch=%llu mutation_before=%llu mutation_after=%llu node_serial=%llu camera_serial=%llu observer_epoch_after=%llu load_epoch_after=%llu registry_epoch_after=%llu node_serial_after=%llu camera_serial_after=%llu",
            ctx.id,ctx.frame,ctx.draws,draw.lifetime.known,after.known,unsigned(draw.lifetime.reason),unsigned(after.reason),
            reinterpret_cast<void*>(draw.registry),draw.lifetime.observer_epoch,draw.lifetime.load_epoch,draw.lifetime.registry_epoch,
            draw.lifetime.mutation_revision,after.mutation_revision,draw.lifetime.node_serial,draw.lifetime.camera_serial,
            after.observer_epoch,after.load_epoch,after.registry_epoch,after.node_serial,after.camera_serial);
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
    ownership::CopyDepthView view{};
    const HRESULT result = ownership::get_copy_depth_view(d, &view);
    const auto& desc = view.source_desc;
    log("ownership_copy_depth phase=%s device=%llu result=%08lx status=%08lx requested=%u available=%u source_bound=%u copy_valid=%u generation=%llu source_epoch=%llu copy_epoch=%llu source_width=%u source_height=%u source_format=%u source_type=%u source_usage=%lu source_pool=%u source_msaa=%u source_quality=%lu",
        phase,device,result,view.status,view.requested,view.available,view.source_bound,view.copy_valid,
        view.generation,view.source_epoch,view.copy_epoch,desc.Width,desc.Height,desc.Format,desc.Type,
        desc.Usage,desc.Pool,desc.MultiSampleType,desc.MultiSampleQuality);
}
void object_context(const Device& ctx) {
    if (!object_trace::active()) return;
    object_trace::Snapshot value{};
    const bool scoped=object_trace::current(&value);
    log("object_context device=%llu frame=%llu index=%llu scoped=%u valid=%lu session=%llu scope_depth=%lu mesh=%p node=%p node_handle=%lu camera=%p camera_handle=%lu registry=%p engine=%p model=%08lx lod=%08lx flags12c=%08lx flags130=%08lx",
        ctx.id,ctx.frame,ctx.draws,scoped,static_cast<unsigned long>(value.valid),value.session,
        static_cast<unsigned long>(value.scope_depth),reinterpret_cast<void*>(value.mesh),
        reinterpret_cast<void*>(value.node),static_cast<unsigned long>(value.node_handle),
        reinterpret_cast<void*>(value.camera),static_cast<unsigned long>(value.camera_handle),
        reinterpret_cast<void*>(value.registry),reinterpret_cast<void*>(value.engine),
        static_cast<unsigned long>(value.model),static_cast<unsigned long>(value.lod),
        static_cast<unsigned long>(value.flags12c),static_cast<unsigned long>(value.flags130));
    if(!scoped)return;
    const auto rows=[&](const char* role,const uint32_t* bits,unsigned count){
        for(unsigned row=0;row<count;++row)
            log("object_matrix role=%s row=%u bits=%08lx,%08lx,%08lx,%08lx",role,row,
                static_cast<unsigned long>(bits[row*4]),static_cast<unsigned long>(bits[row*4+1]),
                static_cast<unsigned long>(bits[row*4+2]),static_cast<unsigned long>(bits[row*4+3]));
    };
    if(value.valid&object_trace::World)rows("world",value.world,4);
    if(value.valid&object_trace::WorldBasis)rows("world_basis",value.world_basis,4);
    if(value.valid&object_trace::View)rows("view",value.view,4);
    if(value.valid&object_trace::Projection)rows("projection",value.projection,4);
    if(value.valid&object_trace::Node){
        log("object_position bits=%08lx,%08lx,%08lx",static_cast<unsigned long>(value.position[0]),static_cast<unsigned long>(value.position[1]),static_cast<unsigned long>(value.position[2]));
        rows("scale",value.scale,1);
        for(unsigned row=0;row<3;++row)
            log("object_basis row=%u bits=%08lx,%08lx,%08lx",row,static_cast<unsigned long>(value.basis[row*3]),static_cast<unsigned long>(value.basis[row*3+1]),static_cast<unsigned long>(value.basis[row*3+2]));
    }
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
    object_context(ctx);
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
void finite_upload_metrics(IDirect3DDevice9* device,const Device& ctx,const char* phase) {
    if(!finite_positions_requested)return;
    ownership::FiniteUploadStatistics s{};
    const HRESULT hr=ownership::get_finite_upload_statistics(device,&s);
    // Cumulative per-owner counters and current process reservations, emitted in
    // batches. No resource uploads or payload contents are logged individually.
    log("finite_upload_metric device=%llu frame=%llu phase=%s result=%08lx status=%08lx requested=%u active=%u generation=%llu payload_bytes=%llu peak_payload_bytes=%llu sidecars=%llu metadata_bytes=%llu global_payload_bytes=%llu global_sidecars=%llu uploads=%llu publications=%llu invalidations=%llu allocation_failures=%llu scans=%llu classified_bytes=%llu scan_ticks=%llu qualifier_ticks=%llu queries=%llu query_ticks=%llu query_cache_hits=%llu position_components=%llu",
        ctx.id,ctx.frame,phase,hr,s.status,s.requested,s.active,s.generation,s.payload_bytes,s.peak_payload_bytes,s.sidecars,s.metadata_bytes,
        s.global_payload_bytes,s.global_sidecars,s.uploads,s.publications,s.invalidations,s.allocation_failures,s.scans,s.classified_bytes,
        s.scan_ticks,s.qualifier_ticks,s.queries,s.query_ticks,s.query_cache_hits,s.position_components);
    if(s.first_refusal.available){
        const auto& f=s.first_refusal;
        log("finite_upload_first_refusal device=%llu frame=%llu phase=%s reason=%u name=%s type=%u format=%u pool=%u size=%u usage=%08lx lock_flags=%08lx",
            ctx.id,ctx.frame,phase,unsigned(f.reason),ownership::finite_evidence_reason_name(f.reason),
            unsigned(f.type),unsigned(f.format),unsigned(f.pool),f.size,static_cast<DWORD>(f.usage),static_cast<DWORD>(f.lock_flags));
    }
    for(unsigned i=0;i<ownership::finite_evidence_reason_count;++i)if(s.reasons[i])
        log("finite_upload_reason device=%llu frame=%llu phase=%s reason=%u name=%s count=%llu",ctx.id,ctx.frame,phase,i,
            ownership::finite_evidence_reason_name(static_cast<ownership::FiniteEvidenceReason>(i)),s.reasons[i]);
}
HRESULT WINAPI present(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r) {
    HookGuard lock;
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*)>(17);
    const auto begin=telemetry::now();
    const HRESULT hr=fn(d,a,b,w,r);
    ctx.scene_depth.end_frame(hr);
    const auto end=telemetry::now();
    telemetry::present(ctx.stats,ctx.frame,ctx.capture,begin,end,hr);
    if(telemetry::enabled()&&(!ctx.stats.present_override_known||ctx.stats.present_override!=w)){
        log("telemetry_present_window device=%llu frame=%llu override=%p device_window=%p result=%08lx",ctx.id,ctx.frame,w,ctx.stats.window,hr);
        ctx.stats.present_override_known=true;ctx.stats.present_override=w;
    }
    if (ctx.capture || ctx.frame%300==0) log("frame_end device=%llu frame=%llu draws=%llu capture=%u present=%08lx",ctx.id,ctx.frame,ctx.draws,ctx.capture,hr);
    if(ctx.capture||ctx.frame%300==0)finite_upload_metrics(d,ctx,"present");
    if (ctx.capture && ctx.remaining) --ctx.remaining;
    ++ctx.frame; ctx.draws=0; ctx.events=0; ctx.stats.frame=ctx.frame;
    const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if ((down&&!ctx.key_down) || (capture_count && ctx.frame==capture_start)) ctx.remaining=capture_count ? capture_count : 1;
    ctx.key_down=down; ctx.capture=ctx.remaining>0;
    ctx.scene_depth.begin_frame(d,ctx.id,ctx.frame,ctx.capture);
    if (ctx.capture) log("frame_begin device=%llu frame=%llu",ctx.id,ctx.frame);
    if (logfile) { const auto begin=telemetry::now(); fflush(logfile); telemetry::record(ctx.stats,telemetry::Metric::LogFlush,telemetry::now()-begin); }
    return hr;
}
HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    HookGuard lock;
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*)>(16);
    ctx.capture=false; ctx.remaining=0;ctx.stats.had_present=false;ctx.stats.last_frame_capture=false;++ctx.stats.resets;
    ctx.scene_depth.invalidate();
    presentation_parameters("reset_before",ctx.id,ctx.stats.focus_window,p);
    log("reset_begin ptr=%p device=%llu",d,ctx.id);
    finite_upload_metrics(d,ctx,"reset_before");
    const auto begin=telemetry::now();
    HRESULT hr=fn(d,p); telemetry::record(ctx.stats,telemetry::Metric::Reset,telemetry::now()-begin,FAILED(hr));
    presentation_parameters("reset_after",ctx.id,ctx.stats.focus_window,p);
    ownership_depth_info(d,ctx.id,"reset_after");
    finite_upload_metrics(d,ctx,"reset_after");
    if(SUCCEEDED(hr)&&p&&p->hDeviceWindow)ctx.stats.window=p->hDeviceWindow;
    telemetry::summary(ctx.stats,"reset",ctx.frame);
    log("reset_end device=%llu result=%08lx",ctx.id,hr); return hr;
}
HRESULT WINAPI draw_primitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT c) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    auto input=read_draw_input(ctx,d,{DrawMethod::Primitive,t,c,s});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"primitive",t,c);
    if (devices.at(d)->capture) log("draw_args start_vertex=%u",s);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT)>(81)(d,t,s,c);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    auto input=read_draw_input(ctx,d,{DrawMethod::Indexed,t,c,s,b,m,n});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"indexed",t,c);
    if (devices.at(d)->capture) log("draw_args base_vertex=%d min_vertex=%u num_vertices=%u start_index=%u",b,m,n,s);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)>(82)(d,t,b,m,n,s,c);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT c,const void* data,UINT stride) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    auto input=read_draw_input(ctx,d,{DrawMethod::UserMemory,t,c});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"up",t,c,true);
    if (devices.at(d)->capture) log("draw_args vertex_ptr=%p stride=%u",data,stride);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(83)(d,t,c,data,stride);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT m,UINT n,UINT c,const void* indices,D3DFORMAT f,const void* data,UINT stride) {
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    auto input=read_draw_input(ctx,d,{DrawMethod::IndexedUserMemory,t,c,0,0,m,n});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"indexed_up",t,c,true);
    if (devices.at(d)->capture) log("draw_args min_vertex=%u num_vertices=%u vertex_ptr=%p stride=%u index_ptr=%p index_format=%u",m,n,data,stride,indices,f);
    timer.begin();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)>(84)(d,t,m,n,c,indices,f,data,stride);
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI clear(IDirect3DDevice9* d,DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s) {
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    ctx.scene_depth.before_clear(d,n,r,f,z);
    timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(d,n,r,f,c,z,s);
    timer.end();
    ctx.scene_depth.after_clear(d,result);
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
        D3DVIEWPORT9 vp{};
        const HRESULT vp_result=d->GetViewport(&vp);
        log("clear_viewport result=%08lx x=%lu y=%lu w=%lu h=%lu minz=%.9g maxz=%.9g",vp_result,vp.X,vp.Y,vp.Width,vp.Height,vp.MinZ,vp.MaxZ);
    }
    return result;
}
HRESULT WINAPI set_rt(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9* rt) {
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(37)(d,index,rt);timer.end();
    ctx.scene_depth.after_set_rt(d,index,result);
    if(ctx.capture){capture_event(ctx,"set_rt",result);log("set_rt index=%lu result=%08lx ptr=%p",index,result,rt);if(SUCCEEDED(result))surface_info("binding",rt);}
    return result;
}
HRESULT WINAPI set_depth(IDirect3DDevice9* d,IDirect3DSurface9* depth) {
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*)>(39)(d,depth);timer.end();
    ctx.scene_depth.after_set_depth(d,result);
    if(ctx.capture){capture_event(ctx,"set_depth",result);log("set_depth ptr=%p result=%08lx",depth,result);if(SUCCEEDED(result))surface_info("depth_binding",depth);}
    return result;
}
HRESULT WINAPI stretch_rect(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* source_rect,IDirect3DSurface9* dest,const RECT* dest_rect,D3DTEXTUREFILTERTYPE filter){
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);timer.begin();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)>(34)(d,source,source_rect,dest,dest_rect,filter);timer.end();
    ctx.scene_depth.after_stretch(d,source,source_rect,dest,dest_rect,result);
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
// Optional scene capture must not mistake omitted GPU writes for a contiguous
// known render sequence. Forward these calls unchanged and reject the candidate.
HRESULT WINAPI update_surface(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* rect,IDirect3DSurface9* dest,const POINT* point){
    HookGuard lock;auto& ctx=*devices.at(d);
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*)>(30)(d,source,rect,dest,point);
    ctx.scene_depth.unsupported("UpdateSurface",hr);return hr;
}
HRESULT WINAPI update_texture(IDirect3DDevice9* d,IDirect3DBaseTexture9* source,IDirect3DBaseTexture9* dest){
    HookGuard lock;auto& ctx=*devices.at(d);
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*)>(31)(d,source,dest);
    ctx.scene_depth.unsupported("UpdateTexture",hr);return hr;
}
HRESULT WINAPI color_fill(IDirect3DDevice9* d,IDirect3DSurface9* surface,const RECT* rect,D3DCOLOR color){
    HookGuard lock;auto& ctx=*devices.at(d);
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,D3DCOLOR)>(35)(d,surface,rect,color);
    ctx.scene_depth.after_color_fill(d,surface,rect,hr);
    if(ctx.capture){
        capture_event(ctx,"color_fill",hr);
        log("color_fill target=%p rect=%p result=%08lx color=%08lx rect_null=%u",surface,rect,hr,color,rect==nullptr);
        // A failed native call may reject before reading either pointer. Logging
        // must not make a previously untouched invalid argument observable.
        if(SUCCEEDED(hr)){
            if(rect)log("color_fill_rect left=%ld top=%ld right=%ld bottom=%ld",rect->left,rect->top,rect->right,rect->bottom);
            if(surface)surface_info("color_fill_target",surface);
        }
    }
    return hr;
}
HRESULT WINAPI draw_rect_patch(IDirect3DDevice9* d,UINT handle,const float* segments,const D3DRECTPATCH_INFO* info){
    HookGuard lock;auto& ctx=*devices.at(d);
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,const float*,const D3DRECTPATCH_INFO*)>(115)(d,handle,segments,info);
    ctx.scene_depth.unsupported("DrawRectPatch",hr);return hr;
}
HRESULT WINAPI draw_tri_patch(IDirect3DDevice9* d,UINT handle,const float* segments,const D3DTRIPATCH_INFO* info){
    HookGuard lock;auto& ctx=*devices.at(d);
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,const float*,const D3DTRIPATCH_INFO*)>(116)(d,handle,segments,info);
    ctx.scene_depth.unsupported("DrawTriPatch",hr);return hr;
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
    ctx->scene_depth.configure(scene_depth_capture_requested);
    ctx->stats.device=ctx->id;ctx->stats.window=window;ctx->stats.focus_window=focus;
    const HRESULT caps_result=d->GetDeviceCaps(&ctx->caps);
    log("capture_caps result=%08lx streams=%lu vs_float_count=%lu ps_version=%08lx",caps_result,ctx->caps.MaxStreams,ctx->caps.MaxVertexShaderConst,ctx->caps.PixelShaderVersion);
    ctx->set(2,release_device); ctx->set(16,reset); ctx->set(17,present);
    ctx->set(37,set_rt);ctx->set(43,clear);
    if(scene_depth_capture_requested){
        ctx->set(34,stretch_rect);ctx->set(39,set_depth);
        ctx->set(30,update_surface);ctx->set(31,update_texture);ctx->set(35,color_fill);
        ctx->set(115,draw_rect_patch);ctx->set(116,draw_tri_patch);
    }
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
    scene_depth_capture_requested=GetEnvironmentVariableW(L"X3M_SCENE_DEPTH_CAPTURE",setting,32)==1 && setting[0]==L'1';
    finite_positions_requested=GetEnvironmentVariableW(L"X3M_FINITE_POSITIONS",setting,32)==1 && setting[0]==L'1';
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
