#include "capture.h"
#include "capture_state.h"
#include "telemetry.h"
#include "loading_trace.h"
#include "gz_buffer.h"
#include "resource_reader.h"
#include "sampling_profiler.h"
#include "scene_capture.h"
#include "object_trace.h"
#include "scene_hook.h"
#include "camera_state.h"
#include "object_lifetime.h"
#include "draw_input.h"
#include "motion_capture.h"
#include "motion_output.h"
#include "cpu_state.h"
#include "../ownership/d3d9_ownership.h"
#include "../ownership/application_admission_abi.h"
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
bool motion_capture_requested = false;
bool motion_output_requested = false;
bool motion_jitter_requested = false;
bool taa_requested = false, taa_debug_requested = false;
float taa_k_override = -1.f; // X3M_TAA_K (stage 3): fixed k of the resolve's luminance weighting on the HDR path; negative: derived from the exposure
// X3M_TAA_MIP_BIAS=<float> (default 0 = off, bit-identical; -0.5 intended):
// D3DSAMP_MIPMAPLODBIAS the route applies to the mip-mapped stages of routed
// draws while the jitter is on (docs/architecture/temporal-integration.md,
// "Mip LOD bias"); installs the light SetTexture/SetSamplerState hooks.
float taa_mip_bias = 0.f;
// X3M_TAA_SHARPEN=<0..1> (requires X3M_TAA=1): post-resolve RCAS of the display
// image on both routes (docs/architecture/temporal-integration.md,
// "Post-resolve sharpen"); 0 or unset: off, bit-identical output.
float taa_sharpen = 0.f;
// X3M_HDR=1 (default off; requires X3M_MOTION_OUTPUT=1): the FP16 HDR scene
// path (docs/architecture/hdr-scene-path.md). Stage 2 switches, all
// defaulting to the stage-1 identity behaviour: X3M_HDR_TONEMAP=agx|identity,
// X3M_HDR_DECODE=gamma2.2|pow22|srgb|none, X3M_HDR_LOOK=none|golden|punchy,
// X3M_HDR_CLAMP=<float>, X3M_HDR_EXPOSURE=auto|manual, X3M_HDR_EV_MANUAL=<ev>
// (implies manual), X3M_HDR_EV=<offset> (alias X3M_HDR_EV_OFFSET),
// X3M_HDR_KEY, X3M_HDR_EV_MIN/MAX, X3M_HDR_ADAPT_UP/DOWN (seconds),
// X3M_HDR_DT_MS (fixed adaptation step; fixtures).
bool hdr_requested = false;
x3m::renderer::HdrConfig hdr_config{};
// X3M_MOTION_RT_MODE=lazy keeps the route's RT1/RT2 bindings across routed
// draws (experiment; default perdraw); X3M_MOTION_FRAME_LOG=<n> sets the
// periodic motion_output_frame cadence with telemetry on (default 60).
bool motion_rt_lazy = false;
unsigned motion_frame_log = 60;
// X3M_STATE_SHADOW=0 turns the route's render-state shadow off (default on:
// SetRenderState is hooked and the per-draw state queries never reach
// GetRenderState after the first read); X3M_SCENE_HOOK is parsed by scene_hook
// (default on with the route, X3M_SCENE_HOOK=0 off).
bool motion_state_shadow = true;
// X3M_TAA_K=<k> (stage 3 of the HDR scene path; requires X3M_HDR=1 and
// X3M_TAA=1) fixes k of the resolve's luminance weighting (0: unweighted);
// unset: k = exp2(EV) of the AgX write-back, 0 with the identity write-back.
// X3M_TAA_SENTINEL=auto|1|2 selects the resolve's depth-sentinel policy
// (auto: far-plane camera reprojection whenever the live camera read yields a
// transform; 1: current-only; 2: strict, skip the resolve without one);
// X3M_CAMERA_CUT_DEG bounds the camera rotation per frame before a cut is
// declared (default 20); X3M_CAMERA_LOG is the camera_state line cadence (300).
x3m::renderer::SentinelMode taa_sentinel_mode = x3m::renderer::SentinelMode::Auto;
float camera_cut_degrees = 20.f;
unsigned camera_log_frames = 300;
unsigned motion_jitter_samples = 8;
float motion_cut_median_px = 48.f, motion_cut_missing = .25f;
// Component fixtures serialize every write and replay. The live capture mutex
// does not cover worker-thread VB/IB Lock/Unlock or mapped writes. No production
// exclusion token is available yet: do not turn a requested diagnostic into an
// unsafe GPU replay merely because the latest revisions looked unchanged.
constexpr bool motion_live_replay_available = false;
std::set<uint64_t> dumped;
uint64_t next_device_id = 1;
}
unsigned long long dll_load_qpc = 0; // stamped in DllMain (loader.cpp)
namespace {

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
    uint64_t frame_end_qpc = 0; // stamp of the previous frame_end line (dt_ms)
    telemetry::State stats;
    SceneCapture scene_depth;
    DrawInputReader draw_inputs;
    MotionCapture motion;
    MotionOutput motion_output;
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
// Same serialization without the lock-wait telemetry, for the hot setter hooks
// of the motion route: telemetry::record's reporting deadline can reach the
// log formatter, which the LightCallBoundary contract excludes from the path.
struct PlainHookGuard {
    std::lock_guard<std::recursive_mutex> lock{mutex};
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
// State blocks change device state outside the setter hooks. With the motion
// route enabled, each block gets a private vtable so Apply can resynchronize
// the route's shadow. The block keeps its device alive, so the device pointer
// stays valid for the block's lifetime. Slots verified in abi_check.cpp.
struct StateBlockHooks : Hooks {
    IDirect3DDevice9* device;
    StateBlockHooks(void* object,IDirect3DDevice9* owner):Hooks(object,6),device(owner){}
};
std::map<IDirect3DStateBlock9*, std::unique_ptr<StateBlockHooks>> stateblocks;
// Queries change their result with every draw between Issue(BEGIN) and
// Issue(END); the resolve must not inject draws while one is open. With the
// route enabled every query object gets a private vtable (slots 2 Release,
// 6 Issue) so the route knows how many are open. EVENT queries have no BEGIN
// and never count.
struct QueryHooks : Hooks {
    IDirect3DDevice9* device;
    bool active=false;
    QueryHooks(void* object,IDirect3DDevice9* owner):Hooks(object,8),device(owner){}
};
std::map<IDirect3DQuery9*, std::unique_ptr<QueryHooks>> queries;
}
#ifdef X3M_MOTION_OUTPUT_FIXTURE
void fixture_apply(Device& ctx);
#endif
namespace {

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
    bool scene_draw = false;
};
ObservedDraw read_draw_input(Device& ctx,IDirect3DDevice9* device,const DrawArguments& arguments) {
    if(!ctx.capture)return {};
    ObservedDraw draw{};
    object_trace::Snapshot scope{};
    const bool scoped=object_trace::current(&scope);
    draw.scene_draw=ctx.scene_depth.collecting_scene();
    draw.input=ctx.draw_inputs.read(device,arguments,scoped?&scope:nullptr,
        draw.scene_draw?ctx.motion.geometry_frame():ownership::GeometryFrameHandle{});
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
    if(draw.scene_draw)ctx.motion.observe(input,draw.registry,draw.lifetime);
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
void ownership_depth_info(IDirect3DDevice9* d, uint64_t device, uint64_t frame, const char* phase) {
    // This is a borrowed diagnostic snapshot, never a resource adoption or a
    // GPU allocation. Native/default mode must not query ownership internals.
    if (!ownership::borrowed_native_device(d)) return;
    ownership::CopyDepthView view{};
    const HRESULT result = ownership::get_copy_depth_view(d, &view);
    const auto& desc = view.source_desc;
    log("ownership_copy_depth phase=%s device=%llu frame=%llu result=%08lx status=%08lx requested=%u available=%u source_bound=%u copy_valid=%u generation=%llu source_epoch=%llu copy_epoch=%llu source_width=%u source_height=%u source_format=%u source_type=%u source_usage=%lu source_pool=%u source_msaa=%u source_quality=%lu",
        phase,device, frame,result,view.status,view.requested,view.available,view.source_bound,view.copy_valid,
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
            log("texture stage=%lu ptr=%p type=%u identity=%llu levels=%lu",i,texture,texture->GetType(),resource_id(texture),texture->GetLevelCount());
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
        // The LOD bias is a float bit pattern in the DWORD: logged raw (same
        // line shape as above) and reinterpreted; MAXMIPLEVEL shows whether
        // level 0 of the chain is reachable (sampler-states-and-mips.md, 5).
        DWORD value = 0;
        if (SUCCEEDED(d->GetSamplerState(i,D3DSAMP_MIPMAPLODBIAS,&value))) {
            float bias = 0.f; memcpy(&bias,&value,sizeof bias);
            log("sampler stage=%lu state=%u value=%lu bias=%g",i,unsigned(D3DSAMP_MIPMAPLODBIAS),value,double(bias));
        }
        if (SUCCEEDED(d->GetSamplerState(i,D3DSAMP_MAXMIPLEVEL,&value))) log("sampler stage=%lu state=%u value=%lu",i,unsigned(D3DSAMP_MAXMIPLEVEL),value);
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
void final_admission_metric(ownership::AdmissionMonitor* monitor,const char* phase) {
    const auto state=ownership::admission_snapshot(monitor);
    log("application_admission_final phase=%s active_roots=%llu waiting_roots=%llu admitted_roots=%llu promotions=%llu vetoes=%u first_veto=%u enabled=%u",
        phase,state.active_roots,state.waiting_roots,state.admitted_roots,state.promotions,
        unsigned(state.vetoes),unsigned(state.first_veto),monitor!=nullptr);
}
ULONG WINAPI release_device(IDirect3DDevice9* d) {
    CpuCallBoundary cpu;
    auto* monitor=ownership::process_admission_monitor();
    ownership::ApplicationAdmissionAbi admission(monitor);
    ULONG refs;bool last_device_destroyed=false;
    {
        HookGuard lock;
        auto& ctx=*devices.at(d);
        auto fn=ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(2);
        // Owned motion objects (variants, RT1) each hold one device reference,
        // so the application's final Release could never reach zero. Probe the
        // count through the native slots; when only the caller's reference and
        // ours remain, release ours first so the original semantics hold.
        if(const unsigned held=ctx.motion_output.device_references()){
            ctx.motion_output.restore_bindings(); // A kept binding would hold RT1/RT2 through the final Release.
            const ULONG count=ctx.get<ULONG (WINAPI*)(IDirect3DDevice9*)>(1)(d);
            const ULONG after=fn(d);
            // Child destruction re-enters this hook through the public vtable;
            // device_references() reports zero while release_resources runs.
            if(after==held+1){ctx.motion_output.release_resources();log("motion_output_release device=%llu held=%u count=%lu released=1",ctx.id,held,count);}
        }
        cpu.before_original();
        refs=fn(d);cpu.after_original();
        if(!refs){telemetry::summary(devices.at(d)->stats,"device_destroy",devices.at(d)->frame);telemetry::summary(telemetry::process(),"device_destroy",devices.at(d)->frame);log("device_destroy ptr=%p device=%llu",d,devices.at(d)->id);devices.erase(d);}
        last_device_destroyed=!refs&&devices.empty();
    }
    // The profiler's quiescent stop: the last device is gone and the capture
    // mutex is released, so its final report cannot wait on a lock we hold.
    if(last_device_destroyed){
        sampling_profiler::shutdown();
        // The frame routine cannot run without a device: quiescent for the
        // callsite restore (the object-trace patch keeps its own lifetime).
        if(scene_hook::installed()){const bool restored=scene_hook::shutdown();log("scene_hook_shutdown restored=%u status=%s",restored,scene_hook::status());}
        resource_reader::report(); // final summary without telemetry; the reader itself stays installed (loading continues without a device)
    }
    if(!refs){
        // A nested final factory Release can report this device root still
        // active. Finish the outer device root only after its own cleanup.
        admission.finish();
        final_admission_metric(monitor,"device");
    }
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
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*)>(17);
    ctx.motion_output.before_present();
    const auto begin=telemetry::now();
    cpu.before_original();
    const HRESULT hr=fn(d,a,b,w,r);cpu.after_original();
    ctx.motion_output.after_present(hr);
    const bool scene_confirmed=ctx.scene_depth.end_frame(hr);
    const bool motion_committed=ctx.motion.end_frame(scene_confirmed,hr);
    if(ctx.capture && motion_capture_requested)
        log("motion_frame device=%llu frame=%llu scene_confirmed=%u storage_history_committed=%u present=%08lx temporal_history_committed=0",
            ctx.id,ctx.frame,scene_confirmed,motion_committed,hr);
    const auto end=telemetry::now();
    telemetry::present(ctx.stats,ctx.frame,ctx.capture,begin,end,hr);
    if(telemetry::enabled()&&(!ctx.stats.present_override_known||ctx.stats.present_override!=w)){
        log("telemetry_present_window device=%llu frame=%llu override=%p device_window=%p result=%08lx",ctx.id,ctx.frame,w,ctx.stats.window,hr);
        ctx.stats.present_override_known=true;ctx.stats.present_override=w;
    }
    if (ctx.capture || ctx.frame%300==0) {
        // One QPC per logged line (every 300 frames or a capture frame), in every
        // mode: elapsed_ms since DllMain and dt_ms since the previous frame_end
        // line make load times readable from a plain --direct log. Integer only.
        static uint64_t frequency=0;
        if(!frequency){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart>0?uint64_t(f.QuadPart):1;}
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const uint64_t now=uint64_t(stamp.QuadPart);
        const uint64_t elapsed_ms=(now-dll_load_qpc)*1000ull/frequency;
        const uint64_t dt_ms=ctx.frame_end_qpc?(now-ctx.frame_end_qpc)*1000ull/frequency:0;
        ctx.frame_end_qpc=now;
        log("frame_end device=%llu frame=%llu draws=%llu capture=%u present=%08lx elapsed_ms=%llu dt_ms=%llu qpc=%llu",ctx.id,ctx.frame,ctx.draws,ctx.capture,hr,elapsed_ms,dt_ms,now);
    }
    if(ctx.capture||ctx.frame%300==0)finite_upload_metrics(d,ctx,"present");
    // With the route requested, log the wrapper's copy-depth epochs per capture
    // frame: source_epoch counts the application's depth clears that found the
    // original depth bound, so it witnesses that the route's own depth unbind
    // (the sentinel fill, restored inside the draw hook) never reached one.
    if(ctx.capture&&motion_output_requested)ownership_depth_info(d,ctx.id,ctx.frame,"present");
    if(ctx.capture||ctx.frame%300==0){
        if(auto* monitor=ownership::process_admission_monitor()){
            const auto state=ownership::admission_snapshot(monitor);
            log("admission_metric device=%llu frame=%llu phase=present active_roots=%llu waiting_roots=%llu admitted_roots=%llu promotions=%llu veto_bits=%lu first_reason=%u replay_active=%u live_replay_enabled=0 coverage_complete=0",
                ctx.id,ctx.frame,state.active_roots,state.waiting_roots,state.admitted_roots,state.promotions,
                static_cast<DWORD>(state.vetoes),unsigned(state.first_veto),state.replay_active);
        }
    }
    if (ctx.capture && ctx.remaining) --ctx.remaining;
    ++ctx.frame; ctx.draws=0; ctx.events=0; ctx.stats.frame=ctx.frame;
    const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if ((down&&!ctx.key_down) || (capture_count && ctx.frame==capture_start)) ctx.remaining=capture_count ? capture_count : 1;
    ctx.key_down=down; ctx.capture=ctx.remaining>0;
    ctx.scene_depth.begin_frame(d,ctx.id,ctx.frame,ctx.capture);
    ctx.motion_output.begin_frame(ctx.frame,ctx.capture);
    ctx.motion.begin_frame(d,ctx.frame,ctx.capture && motion_capture_requested && motion_live_replay_available &&
        object_trace::active() && object_lifetime::active());
    if (ctx.capture) log("frame_begin device=%llu frame=%llu",ctx.id,ctx.frame);
    if (logfile) { const auto begin=telemetry::now(); fflush(logfile); telemetry::record(ctx.stats,telemetry::Metric::LogFlush,telemetry::now()-begin); }
    return hr;
}
HRESULT WINAPI reset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& ctx=*devices.at(d);
    auto fn=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*)>(16);
    ctx.capture=false; ctx.remaining=0;ctx.stats.had_present=false;ctx.stats.last_frame_capture=false;++ctx.stats.resets;
    ctx.scene_depth.invalidate();
    ctx.motion.invalidate();
    ctx.motion_output.before_reset();
    presentation_parameters("reset_before",ctx.id,ctx.stats.focus_window,p);
    log("reset_begin ptr=%p device=%llu",d,ctx.id);
    finite_upload_metrics(d,ctx,"reset_before");
    const auto begin=telemetry::now();
    cpu.before_original();
    HRESULT hr=fn(d,p);cpu.after_original(); telemetry::record(ctx.stats,telemetry::Metric::Reset,telemetry::now()-begin,FAILED(hr));
    presentation_parameters("reset_after",ctx.id,ctx.stats.focus_window,p);
    ctx.motion_output.after_reset(hr);
    ownership_depth_info(d,ctx.id,ctx.frame,"reset_after");
    finite_upload_metrics(d,ctx,"reset_after");
    if(SUCCEEDED(hr)&&p&&p->hDeviceWindow)ctx.stats.window=p->hDeviceWindow;
    telemetry::summary(ctx.stats,"reset",ctx.frame);
    log("reset_end device=%llu result=%08lx",ctx.id,hr); return hr;
}
HRESULT WINAPI draw_primitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT c) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::Primitive,t,c,s});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"primitive",t,c);
    if (devices.at(d)->capture) log("draw_args start_vertex=%u",s);
    auto route=ctx.motion_output.before_draw({false,false,t,c,s,0,0,0});
    timer.begin();
    cpu.before_original();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT)>(81)(d,t,s,c);cpu.after_original();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::Indexed,t,c,s,b,m,n});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"indexed",t,c);
    if (devices.at(d)->capture) log("draw_args base_vertex=%d min_vertex=%u num_vertices=%u start_index=%u",b,m,n,s);
    auto route=ctx.motion_output.before_draw({true,false,t,c,s,b,m,n});
    timer.begin();
    cpu.before_original();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)>(82)(d,t,b,m,n,s,c);cpu.after_original();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT c,const void* data,UINT stride) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::UserMemory,t,c});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"up",t,c,true);
    if (devices.at(d)->capture) log("draw_args vertex_ptr=%p stride=%u",data,stride);
    auto route=ctx.motion_output.before_draw({false,true,t,c,0,0,0,0});
    timer.begin();
    cpu.before_original();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(83)(d,t,c,data,stride);cpu.after_original();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI draw_indexed_up(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT m,UINT n,UINT c,const void* indices,D3DFORMAT f,const void* data,UINT stride) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& ctx=*devices.at(d);CallTimer timer(ctx);
    if(ctx.capture)ctx.motion_output.restore_bindings(); // Capture diagnostics below read the application's bindings.
    auto input=read_draw_input(ctx,d,{DrawMethod::IndexedUserMemory,t,c,0,0,m,n});
    ctx.scene_depth.before_draw(d,t,c);
    snapshot(d,"indexed_up",t,c,true);
    if (devices.at(d)->capture) log("draw_args min_vertex=%u num_vertices=%u vertex_ptr=%p stride=%u index_ptr=%p index_format=%u",m,n,data,stride,indices,f);
    auto route=ctx.motion_output.before_draw({true,true,t,c,0,0,m,n});
    timer.begin();
    cpu.before_original();
    const HRESULT result=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)>(84)(d,t,m,n,c,indices,f,data,stride);cpu.after_original();
    timer.end();telemetry::record(ctx.stats,telemetry::Metric::DrawBackend,timer.backend_ticks,FAILED(result));
    ctx.motion_output.after_draw(route,result);
    ctx.scene_depth.after_draw(result);
    record_draw_input(ctx,input,result);
    if (devices.at(d)->capture) log("draw_result device=%llu frame=%llu index=%llu result=%08lx",devices.at(d)->id,devices.at(d)->frame,devices.at(d)->draws,result);
    return result;
}
HRESULT WINAPI clear(IDirect3DDevice9* d,DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD s) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    const auto motion_boundary=ctx.scene_depth.before_clear(d,n,r,f,z);
    if(motion_boundary.valid && motion_capture_requested && motion_live_replay_available){
        const auto begin=telemetry::now();
        const auto motion=ctx.motion.before_clear(d,motion_boundary);
        log("motion_replay device=%llu frame=%llu event=%llu attempted=%u candidate_produced=%u observations=%llu eligible=%llu matched=%llu rejected=%llu completed=%llu operation=%08lx restoration=%08lx cpu_ticks=%llu execution_known=%u execution_reason=%u active_queries=%llu continuity_known=%u color_coverage_known=%u temporal_consumed=0",
            ctx.id,ctx.frame,motion_boundary.sequence,motion.attempted,motion.produced,motion.observations,
            motion.eligible,motion.matched,motion.rejected,motion.completed,motion.operation,motion.restoration,
            telemetry::now()-begin,motion.execution.known,unsigned(motion.execution.reason),motion.execution.active_queries,
            motion.continuity_known,motion.color_coverage_known);
    }
    ctx.motion_output.before_clear(n,f,z);
    timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(d,n,r,f,c,z,s);
    cpu.after_original();
    timer.end();
    ctx.motion_output.after_clear(result);
    const bool motion_confirmed=ctx.scene_depth.after_clear(d,result);
    if(motion_boundary.valid)ctx.motion.after_clear(motion_confirmed);
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
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    ctx.motion_output.restore_bindings();
    // HDR redirect: the application's main surface maps to the FP16 target
    // while the scene is redirected; the shadow below records the logical binding.
    IDirect3DSurface9* physical=ctx.motion_output.before_set_render_target(index,rt);
    timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(37)(d,index,physical);cpu.after_original();timer.end();
    ctx.scene_depth.after_set_rt(d,index,result);
    ctx.motion_output.after_set_render_target(index,rt,result);
    if(ctx.capture){capture_event(ctx,"set_rt",result);log("set_rt index=%lu result=%08lx ptr=%p",index,result,rt);if(SUCCEEDED(result))surface_info("binding",rt);}
    return result;
}
HRESULT WINAPI set_depth(IDirect3DDevice9* d,IDirect3DSurface9* depth) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*)>(39)(d,depth);cpu.after_original();timer.end();
    ctx.scene_depth.after_set_depth(d,result);
    ctx.motion_output.after_set_depth(depth,result);
    if(ctx.capture){capture_event(ctx,"set_depth",result);log("set_depth ptr=%p result=%08lx",depth,result);if(SUCCEEDED(result))surface_info("depth_binding",depth);}
    return result;
}
HRESULT WINAPI stretch_rect(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* source_rect,IDirect3DSurface9* dest,const RECT* dest_rect,D3DTEXTUREFILTERTYPE filter){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);CallTimer timer(ctx);
    // The temporal resolve runs here, before the application's bloom copy of
    // the main target, so the copy and everything after it see the resolved image.
    ctx.motion_output.before_stretch(source,source_rect,dest,dest_rect);
    timer.begin();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT (WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)>(34)(d,source,source_rect,dest,dest_rect,filter);cpu.after_original();timer.end();
    // The application's own copy (the resolve above is excluded: it runs before timer.begin).
    telemetry::record(ctx.stats,telemetry::Metric::StretchBackend,timer.backend_ticks,FAILED(result));
    ctx.scene_depth.after_stretch(d,source,source_rect,dest,dest_rect,result);
    ctx.motion_output.after_stretch(source,source_rect,dest,dest_rect,result);
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
HRESULT WINAPI begin_scene(IDirect3DDevice9* d){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*)>(41)(d);cpu.after_original();
    ctx.motion_output.after_begin_scene(hr);
    return hr;
}
HRESULT WINAPI end_scene(IDirect3DDevice9* d){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_end_scene(); // HDR: flush the FP16 content while draws are legal
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*)>(42)(d);cpu.after_original();
    ctx.motion_output.after_end_scene(hr);
    return hr;
}
ULONG WINAPI query_release(IDirect3DQuery9* query){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& hooks=*queries.at(query);
    auto fn=hooks.get<ULONG(WINAPI*)(IDirect3DQuery9*)>(2);
    cpu.before_original();
    const ULONG refs=fn(query);cpu.after_original();
    if(!refs){
        // An open query released without END no longer changes with draws.
        const auto device=devices.find(hooks.device);
        if(hooks.active&&device!=devices.end())device->second->motion_output.query_active(false);
        queries.erase(query);
    }
    return refs;
}
HRESULT WINAPI query_issue(IDirect3DQuery9* query,DWORD flags){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& hooks=*queries.at(query);
    const auto device=devices.find(hooks.device);
    // A query bracket must cover the application's bindings only.
    if(device!=devices.end())device->second->motion_output.restore_bindings();
    cpu.before_original();
    const HRESULT hr=hooks.get<HRESULT(WINAPI*)(IDirect3DQuery9*,DWORD)>(6)(query,flags);cpu.after_original();
    if(SUCCEEDED(hr)&&device!=devices.end()){
        const bool begin=(flags&D3DISSUE_BEGIN)!=0, end=(flags&D3DISSUE_END)!=0;
        if(begin&&!end&&!hooks.active){hooks.active=true;device->second->motion_output.query_active(true);}
        else if(end&&hooks.active){hooks.active=false;device->second->motion_output.query_active(false);}
    }
    return hr;
}
HRESULT WINAPI create_query(IDirect3DDevice9* d,D3DQUERYTYPE type,IDirect3DQuery9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DQUERYTYPE,IDirect3DQuery9**)>(118)(d,type,out);cpu.after_original();
    if(ctx.capture)log("create_query type=%u result=%08lx ptr=%p",unsigned(type),hr,out?*out:nullptr);
    if(SUCCEEDED(hr)&&out&&*out&&!queries.count(*out)){
        auto hooks=std::make_unique<QueryHooks>(*out,d);
        hooks->set(2,query_release);hooks->set(6,query_issue);
        auto entry=queries.emplace(*out,std::move(hooks));
        entry.first->second->install(*out);
    }
    return hr;
}
// Optional scene capture must not mistake omitted GPU writes for a contiguous
// known render sequence. Forward these calls unchanged and reject the candidate.
HRESULT WINAPI update_surface(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* rect,IDirect3DSurface9* dest,const POINT* point){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_render_target_write(dest); // HDR: a write into the main target ends the redirect first
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*)>(30)(d,source,rect,dest,point);cpu.after_original();
    ctx.scene_depth.unsupported("UpdateSurface",hr);ctx.motion_output.unsupported(hr);return hr;
}
HRESULT WINAPI update_texture(IDirect3DDevice9* d,IDirect3DBaseTexture9* source,IDirect3DBaseTexture9* dest){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*)>(31)(d,source,dest);cpu.after_original();
    ctx.scene_depth.unsupported("UpdateTexture",hr);ctx.motion_output.unsupported(hr);return hr;
}
HRESULT WINAPI color_fill(IDirect3DDevice9* d,IDirect3DSurface9* surface,const RECT* rect,D3DCOLOR color){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_render_target_write(surface); // HDR: a fill of the main target ends the redirect first
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,D3DCOLOR)>(35)(d,surface,rect,color);cpu.after_original();
    ctx.scene_depth.after_color_fill(d,surface,rect,hr);
    ctx.motion_output.after_color_fill(surface,rect,hr);
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
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,const float*,const D3DRECTPATCH_INFO*)>(115)(d,handle,segments,info);cpu.after_original();
    ctx.scene_depth.unsupported("DrawRectPatch",hr);ctx.motion_output.unsupported(hr);return hr;
}
HRESULT WINAPI draw_tri_patch(IDirect3DDevice9* d,UINT handle,const float* segments,const D3DTRIPATCH_INFO* info){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const auto hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,const float*,const D3DTRIPATCH_INFO*)>(116)(d,handle,segments,info);cpu.after_original();
    ctx.scene_depth.unsupported("DrawTriPatch",hr);ctx.motion_output.unsupported(hr);return hr;
}
// These resource hooks are installed only when CPU telemetry is enabled. The
// backend receives every pointer/flag unchanged; returned objects are not wrapped.
HRESULT WINAPI create_texture(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*)>(23)(d,w,h,levels,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::Texture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_volume(IDirect3DDevice9* d,UINT w,UINT h,UINT depth,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DVolumeTexture9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture9**,HANDLE*)>(24)(d,w,h,depth,levels,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::VolumeTexture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_cube(IDirect3DDevice9* d,UINT edge,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DCubeTexture9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DCubeTexture9**,HANDLE*)>(25)(d,edge,levels,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::CubeTexture,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_vb(IDirect3DDevice9* d,UINT length,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*)>(26)(d,length,usage,fvf,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::VertexBuffer,telemetry::now()-begin,FAILED(result),SUCCEEDED(result)?length:0);return result;
}
HRESULT WINAPI create_ib(IDirect3DDevice9* d,UINT length,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*)>(27)(d,length,usage,format,pool,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::IndexBuffer,telemetry::now()-begin,FAILED(result),SUCCEEDED(result)?length:0);return result;
}
HRESULT WINAPI create_rt(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,D3DMULTISAMPLE_TYPE ms, DWORD quality,BOOL lockable,IDirect3DSurface9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)>(28)(d,w,h,format,ms,quality,lockable,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::RenderTarget,telemetry::now()-begin,FAILED(result));return result;
}
HRESULT WINAPI create_depth(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT format,D3DMULTISAMPLE_TYPE ms,DWORD quality,BOOL discard,IDirect3DSurface9** out,HANDLE* shared){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);const auto begin=telemetry::now();
    cpu.before_original();
    const auto result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)>(29)(d,w,h,format,ms,quality,discard,out,shared);cpu.after_original();
    telemetry::record(ctx.stats,telemetry::Metric::DepthStencil,telemetry::now()-begin,FAILED(result));return result;
}
bool cursor_change_allowed(telemetry::State& stats){
    const auto stamp=telemetry::now();if(stats.last_cursor_change && stamp-stats.last_cursor_change<telemetry::frequency()/4){++stats.cursor_changes_suppressed;return false;}stats.last_cursor_change=stamp;return true;
}
HRESULT WINAPI cursor_properties(IDirect3DDevice9* d,UINT x,UINT y,IDirect3DSurface9* surface){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    cpu.before_original();
    const HRESULT result=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,IDirect3DSurface9*)>(10)(d,x,y,surface);cpu.after_original();
    telemetry::record(stats,telemetry::Metric::CursorProperties,telemetry::now()-begin,FAILED(result));
    if((!stats.properties_known||stats.cursor_x!=x||stats.cursor_y!=y||stats.cursor_surface!=surface||FAILED(result))&&cursor_change_allowed(stats))log("telemetry_cursor_api device=%llu frame=%llu op=properties hotspot=%u,%u surface=%p result=%08lx",ctx.id,ctx.frame,x,y,surface,result);
    if(SUCCEEDED(result)){stats.properties_known=true;stats.cursor_x=x;stats.cursor_y=y;stats.cursor_surface=surface;}
    return result;
}
void WINAPI cursor_position(IDirect3DDevice9* d,int x,int y,DWORD flags){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    cpu.before_original();
    ctx.get<void(WINAPI*)(IDirect3DDevice9*,int,int,DWORD)>(11)(d,x,y,flags);cpu.after_original();
    telemetry::record(stats,telemetry::Metric::CursorPosition,telemetry::now()-begin);
    const auto stamp=telemetry::now();
    if((!stats.last_position||x!=stats.logged_position_x||y!=stats.logged_position_y||flags!=stats.logged_position_flags) && (!stats.last_position||stamp-stats.last_position>=telemetry::frequency()/4)){log("telemetry_cursor_api device=%llu frame=%llu op=position x=%d y=%d flags=%lu",ctx.id,ctx.frame,x,y,flags);stats.last_position=stamp;stats.logged_position_x=x;stats.logged_position_y=y;stats.logged_position_flags=flags;}
    else ++stats.position_suppressed;

}
BOOL WINAPI cursor_show(IDirect3DDevice9* d,BOOL show){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);auto& stats=ctx.stats;const auto begin=telemetry::now();
    cpu.before_original();
    const BOOL previous=ctx.get<BOOL(WINAPI*)(IDirect3DDevice9*,BOOL)>(12)(d,show);cpu.after_original();
    telemetry::record(stats,telemetry::Metric::CursorShow,telemetry::now()-begin);
    if((!stats.api_show_known||stats.api_show!=show)&&cursor_change_allowed(stats))log("telemetry_cursor_api device=%llu frame=%llu op=show requested=%d previous_visible=%d",ctx.id,ctx.frame,show,previous);
    stats.api_show_known=true;stats.api_show=show;return previous;
}
HRESULT WINAPI create_vs(IDirect3DDevice9* d,const DWORD* code,IDirect3DVertexShader9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    const auto begin=telemetry::now();
    cpu.before_original();
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**)>(91)(d,code,out);cpu.after_original();
    telemetry::record(devices.at(d)->stats,telemetry::Metric::ShaderVS,telemetry::now()-begin,FAILED(hr));
    if(SUCCEEDED(hr)&&out){
        const auto hash=shader_id(*out,"vs");
        UINT bytes=0;
        if(motion_output_requested&&*out&&SUCCEEDED((*out)->GetFunction(nullptr,&bytes)))
            devices.at(d)->motion_output.register_vertex_shader(*out,code,bytes,hash);
    }
    return hr;
}
HRESULT WINAPI create_ps(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    const auto begin=telemetry::now();
    cpu.before_original();
    HRESULT hr=devices.at(d)->get<HRESULT (WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**)>(106)(d,code,out);cpu.after_original();
    telemetry::record(devices.at(d)->stats,telemetry::Metric::ShaderPS,telemetry::now()-begin,FAILED(hr));
    if(SUCCEEDED(hr)&&out){
        const auto hash=shader_id(*out,"ps");
        UINT bytes=0;
        if(motion_output_requested&&*out&&SUCCEEDED((*out)->GetFunction(nullptr,&bytes)))
            devices.at(d)->motion_output.register_pixel_shader(*out,code,bytes,hash);
    }
    return hr;
}
// Shadow-state hooks for the live motion route, installed only when
// X3M_MOTION_OUTPUT=1 and the device passed the route's capability gate. Each
// forwards unchanged and records the application's new binding after native
// success; the route restores exactly these values. Slot indices are verified
// against the SDK layout in abi_check.cpp.
//
// `boundary`/`guard` select the CPU-state contract. The hot setters (shaders,
// constants, viewport) do only integer/SSE memory work on both sides of the
// native call, no logging and no telemetry, so LightCallBoundary (MXCSR +
// last error) with a plain lock preserves the same application-observed state
// as CpuCallBoundary without four FNSAVE/FRSTOR per call;
// verification/probe/check_no_x87.py proves the absence of x87 opcodes on
// every function those hooks reach in the built DLL. Hooks whose shadow calls
// foreign code or the logger (resource_id private data for stream/indices,
// GetVertexDeclaration/GetDeclaration for declaration/FVF, state block
// recording) keep the full boundary.
#define X3M_SHADOW_HOOK(boundary,guard,name,slot,signature,call,update) \
HRESULT WINAPI name signature { \
    boundary cpu; \
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor()); \
    guard lock;auto& ctx=*devices.at(d); \
    cpu.before_original(); \
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)signature>(slot)call;cpu.after_original(); \
    if(SUCCEEDED(hr))ctx.motion_output.update; \
    return hr; \
}
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,set_vs,92,(IDirect3DDevice9* d,IDirect3DVertexShader9* shader),(d,shader),set_vertex_shader(shader))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,set_ps,107,(IDirect3DDevice9* d,IDirect3DPixelShader9* shader),(d,shader),set_pixel_shader(shader))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,set_vs_constant_f,94,(IDirect3DDevice9* d,UINT start,const float* data,UINT count),(d,start,data,count),set_vertex_constants_f(start,data,count))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,set_vs_constant_i,96,(IDirect3DDevice9* d,UINT start,const int* data,UINT count),(d,start,data,count),set_vertex_constants_i(start,data,count))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,set_ps_constant_f,109,(IDirect3DDevice9* d,UINT start,const float* data,UINT count),(d,start,data,count),set_pixel_constants_f(start,data,count))
X3M_SHADOW_HOOK(CpuCallBoundary,HookGuard,set_stream_source,100,(IDirect3DDevice9* d,UINT stream,IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride),(d,stream,buffer,offset,stride),set_stream_source(stream,buffer,offset,stride))
X3M_SHADOW_HOOK(CpuCallBoundary,HookGuard,set_indices,104,(IDirect3DDevice9* d,IDirect3DIndexBuffer9* buffer),(d,buffer),set_indices(buffer))
X3M_SHADOW_HOOK(LightCallBoundary,PlainHookGuard,set_viewport,47,(IDirect3DDevice9* d,const D3DVIEWPORT9* viewport),(d,viewport),set_viewport(viewport))
X3M_SHADOW_HOOK(CpuCallBoundary,HookGuard,set_declaration,87,(IDirect3DDevice9* d,IDirect3DVertexDeclaration9* declaration),(d,declaration),set_vertex_declaration(declaration))
X3M_SHADOW_HOOK(CpuCallBoundary,HookGuard,set_fvf,89,(IDirect3DDevice9* d,DWORD fvf),(d,fvf),set_fvf(fvf))
#undef X3M_SHADOW_HOOK
// Render-state shadow (X3M_STATE_SHADOW, default on). Light boundary like the
// other hot setters: before the native call only the lazy-mode flush of a
// held write mask (no logging, no telemetry record: motion_output.cpp,
// flush_bindings<true>), after it the shadow store; check_no_x87.py walks
// this hook too.
HRESULT WINAPI set_render_state(IDirect3DDevice9* d,D3DRENDERSTATETYPE state,DWORD value){
    LightCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    PlainHookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.before_set_render_state(state);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD)>(57)(d,state,value);cpu.after_original();
    if(SUCCEEDED(hr))ctx.motion_output.set_render_state(state,value);
    return hr;
}
// Mip LOD bias (X3M_TAA_MIP_BIAS, installed only with a non-zero bias): the
// application's texture bindings and MIPFILTER/MIPMAPLODBIAS writes feed the
// route's sampler shadow. Light boundary like the other hot setters: integer
// stores on both sides of the native call. The texture's level count is read
// once per pointer change, inside the native section, from the object the
// application just passed (valid by the call's own contract); GetLevelCount
// is a D3D runtime accessor like the native SetTexture beside it, an
// indirect call check_no_x87.py does not walk (same as the native slot).
HRESULT WINAPI set_texture(IDirect3DDevice9* d,DWORD stage,IDirect3DBaseTexture9* texture){
    LightCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    PlainHookGuard lock;auto& ctx=*devices.at(d);
    const bool query=ctx.motion_output.texture_levels_wanted(stage,texture);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(65)(d,stage,texture);
    const DWORD levels=SUCCEEDED(hr)&&query?texture->GetLevelCount():0;
    cpu.after_original();
    if(SUCCEEDED(hr))ctx.motion_output.set_texture(stage,texture,levels,query);
    return hr;
}
HRESULT WINAPI set_sampler_state(IDirect3DDevice9* d,DWORD stage,D3DSAMPLERSTATETYPE type,DWORD value){
    LightCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    PlainHookGuard lock;auto& ctx=*devices.at(d);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD)>(69)(d,stage,type,value);cpu.after_original();
    if(SUCCEEDED(hr))ctx.motion_output.set_sampler_state(stage,type,value);
    return hr;
}
// Lazy mode only: an application read of a write mask the route holds must
// see the application's own value (the other half of the lazy-mode hole).
HRESULT WINAPI get_render_state(IDirect3DDevice9* d,D3DRENDERSTATETYPE state,DWORD* value){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD*)>(58)(d,state,value);cpu.after_original();
    return hr;
}
HRESULT WINAPI begin_stateblock(IDirect3DDevice9* d){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings(); // Recording starts from the application's bindings.
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*)>(60)(d);cpu.after_original();
    if(SUCCEEDED(hr))ctx.motion_output.begin_stateblock();
    return hr;
}
// Lazy-mode and HDR hooks (X3M_MOTION_RT_MODE=lazy, X3M_HDR=1): the
// application's target getters must report its own bindings, so a kept
// RT1/RT2 is released first and, while the scene is redirected to the FP16
// target, GetRenderTarget(0) answers with the application's logical main
// surface (the logical-binding shim) and a read of the main target's contents
// receives the pending FP16 content first.
HRESULT WINAPI get_rt(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    if(ctx.motion_output.hdr_logical_render_target(index,out))return S_OK;
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**)>(38)(d,index,out);cpu.after_original();
    return hr;
}
HRESULT WINAPI get_rt_data(IDirect3DDevice9* d,IDirect3DSurface9* source,IDirect3DSurface9* dest){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    ctx.motion_output.before_render_target_read(source);
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,IDirect3DSurface9*)>(32)(d,source,dest);cpu.after_original();
    return hr;
}
ULONG WINAPI stateblock_release(IDirect3DStateBlock9* block){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto fn=stateblocks.at(block)->get<ULONG(WINAPI*)(IDirect3DStateBlock9*)>(2);
    cpu.before_original();
    const ULONG refs=fn(block);cpu.after_original();
    if(!refs)stateblocks.erase(block);
    return refs;
}
HRESULT WINAPI stateblock_apply(IDirect3DStateBlock9* block){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    auto& hooks=*stateblocks.at(block);
    const auto device=devices.find(hooks.device);
    if(device!=devices.end())device->second->motion_output.restore_bindings(); // Apply must land on the application's bindings.
    cpu.before_original();
    const HRESULT hr=hooks.get<HRESULT(WINAPI*)(IDirect3DStateBlock9*)>(5)(block);cpu.after_original();
    if(SUCCEEDED(hr)&&device!=devices.end())device->second->motion_output.stateblock_applied();
    return hr;
}
void hook_stateblock(IDirect3DDevice9* d,IDirect3DStateBlock9* block){
    if(!block||stateblocks.count(block))return;
    auto hooks=std::make_unique<StateBlockHooks>(block,d);
    hooks->set(2,stateblock_release);hooks->set(5,stateblock_apply);
    auto entry=stateblocks.emplace(block,std::move(hooks));
    entry.first->second->install(block);
}
HRESULT WINAPI create_stateblock(IDirect3DDevice9* d,D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings(); // The block captures the application's bindings.
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**)>(59)(d,type,out);cpu.after_original();
    if(SUCCEEDED(hr)&&out)hook_stateblock(d,*out);
    return hr;
}
HRESULT WINAPI end_stateblock(IDirect3DDevice9* d,IDirect3DStateBlock9** out){
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;auto& ctx=*devices.at(d);
    ctx.motion_output.restore_bindings();
    cpu.before_original();
    const HRESULT hr=ctx.get<HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DStateBlock9**)>(61)(d,out);cpu.after_original();
    // Recording ends whether or not the block was produced; the shadow resyncs.
    ctx.motion_output.end_stateblock();
    if(SUCCEEDED(hr)&&out)hook_stateblock(d,*out);
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
    // Attach after install: the route's own device calls use the native table
    // captured by Hooks, so nothing here re-enters the hooks.
    auto& hooked=*devices.at(d);
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    fixture_apply(hooked);
#endif
    hooked.motion_output.configure_jitter(motion_jitter_requested,motion_jitter_samples);
    hooked.motion_output.configure_cut_bounds(motion_cut_median_px,motion_cut_missing);
    hooked.motion_output.configure_taa(taa_requested,taa_debug_requested);
    hooked.motion_output.configure_taa_k(taa_k_override);
    hooked.motion_output.configure_mip_bias(taa_mip_bias);
    hooked.motion_output.configure_taa_sharpen(taa_sharpen);
    hooked.motion_output.configure_rt_mode(motion_rt_lazy);
    hooked.motion_output.configure_frame_log(motion_frame_log);
    hooked.motion_output.configure_sentinel(taa_sentinel_mode,camera_cut_degrees,camera_log_frames);
    hooked.motion_output.configure_state_shadow(motion_state_shadow);
    // The engine scene-end hook is installed at backend load; a device created
    // after the last one was destroyed (the patch restored then) reinstalls it.
    if(scene_hook::requested()&&!scene_hook::installed()){
        scene_hook::initialize(&scene_end_signal);
        log("scene_hook active=%u status=%s reinstalled=1",scene_hook::active(),scene_hook::status());
    }
    hooked.motion_output.configure_scene_hook(scene_hook::active());
    hooked.motion_output.configure_hdr(hdr_requested,hdr_config);
    hooked.motion_output.attach(d,hooked.original,hooked.id,hooked.caps,motion_output_requested,&hooked.stats);
    if(hooked.motion_output.enabled()){
        // The route needs the complete selector event stream plus setter
        // shadows. Installed only after the capability gate passed, so a device
        // the route refuses pays nothing per setter call; the private table is
        // already live, so these slots take effect immediately.
        hooked.set(34,stretch_rect);hooked.set(39,set_depth);
        hooked.set(30,update_surface);hooked.set(31,update_texture);hooked.set(35,color_fill);
        hooked.set(115,draw_rect_patch);hooked.set(116,draw_tri_patch);
        hooked.set(92,set_vs);hooked.set(107,set_ps);hooked.set(94,set_vs_constant_f);hooked.set(96,set_vs_constant_i);
        hooked.set(109,set_ps_constant_f);hooked.set(100,set_stream_source);hooked.set(104,set_indices);
        hooked.set(87,set_declaration);hooked.set(89,set_fvf);hooked.set(47,set_viewport);
        hooked.set(59,create_stateblock);hooked.set(60,begin_stateblock);hooked.set(61,end_stateblock);
        // Scene and query tracking for the resolve's caller contract.
        hooked.set(41,begin_scene);hooked.set(42,end_scene);hooked.set(118,create_query);
        // Render-state shadow: the application's render-state writes. Lazy
        // mode needs the same hook with the shadow off (X3M_STATE_SHADOW=0):
        // an application write to a held write mask must flush the binding first.
        if(hooked.motion_output.state_shadow()||hooked.motion_output.lazy_rt_mode())hooked.set(57,set_render_state);
        // Mip LOD bias: the sampler shadow's two light setter hooks (only with a non-zero bias).
        if(hooked.motion_output.mip_bias_active()){hooked.set(65,set_texture);hooked.set(69,set_sampler_state);}
        // Lazy binding: the application's target and write-mask getters restore first.
        if(hooked.motion_output.lazy_rt_mode()){hooked.set(38,get_rt);hooked.set(32,get_rt_data);hooked.set(58,get_render_state);}
        // HDR redirect: the application's GetRenderTarget(0) and its reads of
        // the main target's contents go through the logical-binding shim.
        if(hooked.motion_output.hdr_enabled()){hooked.set(38,get_rt);hooked.set(32,get_rt_data);}
    }
    ownership_depth_info(d,devices.at(d)->id,devices.at(d)->frame,"create_after");
}
ULONG WINAPI release_factory(IDirect3D9* d) {
    CpuCallBoundary cpu;
    auto* monitor=ownership::process_admission_monitor();
    ownership::ApplicationAdmissionAbi admission(monitor);
    ULONG refs;
    {
        HookGuard lock;
        cpu.before_original();
        refs=factories.at(d)->get<ULONG (WINAPI*)(IDirect3D9*)>(2)(d);
        cpu.after_original();
        if(!refs)factories.erase(d);
    }
    if(!refs){
        // End this application root after native/map cleanup and after releasing
        // capture's mutex. An outer caller may still be active; this snapshot is
        // diagnostic, never a general assertion of process-wide quiescence.
        admission.finish();
        final_admission_metric(monitor,"factory");
    }
    return refs;
}
HRESULT WINAPI create_device(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    HookGuard lock;
    presentation_parameters("create_before",0,window,p);
    if(p) log("create_device adapter=%u flags=%08lx width=%u height=%u format=%u windowed=%u msaa=%u interval=%u",adapter,flags,p->BackBufferWidth,p->BackBufferHeight,p->BackBufferFormat,p->Windowed,p->MultiSampleType,p->PresentationInterval);
    const auto begin=telemetry::now();
    cpu.before_original();
    HRESULT hr=factories.at(d)->get<HRESULT (WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**)>(16)(d,adapter,type,window,flags,p,out);cpu.after_original();
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
    motion_capture_requested=GetEnvironmentVariableW(L"X3M_MOTION_CAPTURE",setting,32)==1 && setting[0]==L'1' &&
        scene_depth_capture_requested && finite_positions_requested;
    log("motion_capture_mode requested=%u enabled=0 reason=write_exclusion_unavailable scope=private_rigid_diagnostic temporal_consumer=0",motion_capture_requested);
    motion_output_requested=GetEnvironmentVariableW(L"X3M_MOTION_OUTPUT",setting,32)==1 && setting[0]==L'1';
    // Per-draw jitter (off by default) with its Halton sample count, and the
    // cut detector bounds (median origin displacement at 1280 px width,
    // missing-key fraction); see docs/architecture/temporal-integration.md.
    motion_jitter_requested=GetEnvironmentVariableW(L"X3M_MOTION_JITTER",setting,32)==1 && setting[0]==L'1';
    if(GetEnvironmentVariableW(L"X3M_MOTION_JITTER_SAMPLES",setting,32)>0){const unsigned long n=wcstoul(setting,nullptr,10);if(n>=2&&n<=64)motion_jitter_samples=unsigned(n);}
    if(GetEnvironmentVariableW(L"X3M_MOTION_CUT_MEDIAN_PX",setting,32)>0)motion_cut_median_px=wcstof(setting,nullptr);
    if(GetEnvironmentVariableW(L"X3M_MOTION_CUT_MISSING",setting,32)>0)motion_cut_missing=wcstof(setting,nullptr);
    // The temporal resolve at the bloom copy (temporal step 3): requires the
    // route and implies the jitter; X3M_TAA_DEBUG=<n> (n > 0) writes the
    // resolved FP16 image and the pre-resolve color in capture frames.
    taa_requested=motion_output_requested && GetEnvironmentVariableW(L"X3M_TAA",setting,32)==1 && setting[0]==L'1';
    if(taa_requested)motion_jitter_requested=true;
    taa_debug_requested=taa_requested && GetEnvironmentVariableW(L"X3M_TAA_DEBUG",setting,32)>0 && wcstoul(setting,nullptr,10)>0;
    // X3M_TAA_K=<k> (0 <= k <= 65504): a fixed luminance-weighting constant for
    // the resolve on the FP16 scene (X3M_HDR=1); 0 is the unweighted resolve.
    // Unset, out of range or not a number: derived from the write-back's
    // exposure (0 is a valid override, so a failed conversion, which wcstof
    // reports as 0, must not be taken: the whole string has to be consumed).
    if(GetEnvironmentVariableW(L"X3M_TAA_K",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=0.f&&v<=65504.f)taa_k_override=v;}
    // X3M_TAA_MIP_BIAS=<bias> (-8 <= bias <= 8, whole string consumed; 0, unset
    // or invalid: off): requires the route with the jitter (X3M_TAA=1 or
    // X3M_MOTION_JITTER=1); the bias is applied only while the jitter is active.
    if(motion_jitter_requested && GetEnvironmentVariableW(L"X3M_TAA_MIP_BIAS",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=-8.f&&v<=8.f)taa_mip_bias=v;}
    // X3M_TAA_SHARPEN=<s> (0 <= s <= 1): the post-resolve sharpen; the whole
    // string must parse (0 is off, so a failed conversion must not be taken).
    taa_sharpen=0.f;
    if(taa_requested&&GetEnvironmentVariableW(L"X3M_TAA_SHARPEN",setting,32)>0){wchar_t* end=nullptr;const float v=wcstof(setting,&end);if(end!=setting&&*end==L'\0'&&v>=0.f&&v<=1.f)taa_sharpen=v;}
    // The FP16 HDR scene path (stage 1: redirect, identity write-back) needs
    // the route's hooks and selector.
    hdr_requested=motion_output_requested && GetEnvironmentVariableW(L"X3M_HDR",setting,32)==1 && setting[0]==L'1';
    hdr_config=x3m::renderer::HdrConfig{};
    if(GetEnvironmentVariableW(L"X3M_HDR_TONEMAP",setting,32)>0 && (!wcscmp(setting,L"agx")||!wcscmp(setting,L"1")))hdr_config.tonemap=x3m::renderer::HdrTonemap::Agx;
    if(GetEnvironmentVariableW(L"X3M_HDR_DECODE",setting,32)>0){
        if(!wcscmp(setting,L"none"))hdr_config.decode=x3::temporal::AgxDecode::none;
        else if(!wcscmp(setting,L"srgb"))hdr_config.decode=x3::temporal::AgxDecode::srgb;
        else hdr_config.decode=x3::temporal::AgxDecode::gamma22; // gamma2.2 | pow22 | gamma
    }
    if(GetEnvironmentVariableW(L"X3M_HDR_LOOK",setting,32)>0){
        if(!wcscmp(setting,L"golden"))hdr_config.look=x3::temporal::AgxLook::golden;
        else if(!wcscmp(setting,L"punchy"))hdr_config.look=x3::temporal::AgxLook::punchy;
    }
    if(GetEnvironmentVariableW(L"X3M_HDR_CLAMP",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=65504.f)hdr_config.clamp_max=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_EXPOSURE",setting,32)>0 && !wcscmp(setting,L"manual"))hdr_config.exposure=x3m::renderer::ExposureMode::Manual;
    if(GetEnvironmentVariableW(L"X3M_HDR_EV_MANUAL",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f){hdr_config.exposure=x3m::renderer::ExposureMode::Manual;hdr_config.ev_manual=v;}}
    if(GetEnvironmentVariableW(L"X3M_HDR_EV",setting,32)>0||GetEnvironmentVariableW(L"X3M_HDR_EV_OFFSET",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f)hdr_config.params.ev_offset=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_KEY",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=64.f)hdr_config.params.key=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_EV_MIN",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f)hdr_config.params.ev_min=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_EV_MAX",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>=-16.f&&v<=16.f)hdr_config.params.ev_max=v;}
    if(hdr_config.params.ev_min>hdr_config.params.ev_max)hdr_config.params.ev_min=hdr_config.params.ev_max;
    if(GetEnvironmentVariableW(L"X3M_HDR_ADAPT_UP",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=60.f)hdr_config.params.tau_up=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_ADAPT_DOWN",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=60.f)hdr_config.params.tau_down=v;}
    if(GetEnvironmentVariableW(L"X3M_HDR_DT_MS",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=1000.f)hdr_config.fixed_dt=v/1000.f;}
    hdr_config.sharpen=taa_sharpen; // the HDR write-back sharpens the resolved image with the same setting
    motion_rt_lazy=GetEnvironmentVariableW(L"X3M_MOTION_RT_MODE",setting,32)>0 && !wcscmp(setting,L"lazy");
    motion_state_shadow=!(GetEnvironmentVariableW(L"X3M_STATE_SHADOW",setting,32)==1 && setting[0]==L'0');
    const bool scene_hook_requested=scene_hook::wanted(); // default on with the route (X3M_SCENE_HOOK=0 turns it off)
    if(GetEnvironmentVariableW(L"X3M_MOTION_FRAME_LOG",setting,32)>0){const unsigned long n=wcstoul(setting,nullptr,10);if(n>=1&&n<=100000)motion_frame_log=unsigned(n);}
    if(GetEnvironmentVariableW(L"X3M_TAA_SENTINEL",setting,32)>0){
        if(!wcscmp(setting,L"1"))taa_sentinel_mode=x3m::renderer::SentinelMode::CurrentOnly;
        else if(!wcscmp(setting,L"2"))taa_sentinel_mode=x3m::renderer::SentinelMode::Camera;
        else taa_sentinel_mode=x3m::renderer::SentinelMode::Auto;
    }
    if(GetEnvironmentVariableW(L"X3M_CAMERA_CUT_DEG",setting,32)>0){const float v=wcstof(setting,nullptr);if(v>0&&v<=180)camera_cut_degrees=v;}
    if(GetEnvironmentVariableW(L"X3M_CAMERA_LOG",setting,32)>0){const unsigned long n=wcstoul(setting,nullptr,10);if(n>=1&&n<=1000000)camera_log_frames=unsigned(n);}
    log("motion_output_mode requested=%u scope=live_same_draw_diagnostic history_requires=object_trace,object_lifetime temporal_consumer=%u taa=%u taa_debug=%u jitter=%u jitter_samples=%u cut_median_px=%.3f cut_missing=%.3f rt_mode=%s frame_log=%u sentinel=%s camera_cut_deg=%.2f camera_log=%u state_shadow=%u scene_hook=%u hdr=%u taa_k=%.5f mip_bias=%g taa_sharpen=%.3f",
        motion_output_requested,taa_requested,taa_requested,taa_debug_requested,motion_jitter_requested,motion_jitter_samples,motion_cut_median_px,motion_cut_missing,motion_rt_lazy?"lazy":"perdraw",motion_frame_log,
        taa_sentinel_mode==x3m::renderer::SentinelMode::CurrentOnly?"1":taa_sentinel_mode==x3m::renderer::SentinelMode::Camera?"2":"auto",camera_cut_degrees,camera_log_frames,motion_state_shadow,scene_hook_requested,hdr_requested,taa_k_override,double(taa_mip_bias),taa_sharpen);
    log("x3-modern-renderer version=0.4 schema=2 capture_start=%u capture_frames=%u pointer_bits=32",capture_start,capture_count);
    telemetry::initialize([]{if(logfile)fflush(logfile);});
    if(telemetry::enabled()||gz_buffer::requested())loading_trace::initialize(); // X3M_GZ_BUFFER=1 patches the gz rows alone
    resource_reader::initialize(); // X3M_RESOURCE_READ=verify|fast, X3M_DAT_HANDLES=1; after the probes so its stub chains behind theirs
    sampling_profiler::initialize(); // X3M_PROFILE=1 only; outside loader lock, after the log exists
}
const wchar_t* capture_directory() { return directory.c_str(); }
#ifdef X3M_MOTION_OUTPUT_FIXTURE
// Fixture-only exports (verification/probe/motion_output_fixture.cpp). Absent
// from production builds; the seam DLL is linked by build_motion_output.sh.
namespace { MotionOutputFixtureConfig fixture_config{}; bool fixture_configured=false; unsigned fixture_hdr_fault_kind=0, fixture_hdr_fault_count=0; }
void fixture_apply(Device& ctx) {
    if(fixture_configured) ctx.motion_output.fixture_configure(fixture_config);
    if(fixture_hdr_fault_count){ctx.motion_output.fixture_hdr_fault(fixture_hdr_fault_kind,fixture_hdr_fault_count);fixture_hdr_fault_count=0;}
}
#endif

// The engine scene-end signal: render thread, outside any device hook; every
// route sees it under the same mutex the hooks hold.
void scene_end_signal() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for(auto& entry:devices) entry.second->motion_output.scene_end_hook();
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
#ifdef X3M_MOTION_OUTPUT_FIXTURE
extern "C" __declspec(dllexport) void x3m_motion_output_fixture_configure(const x3m::MotionOutputFixtureConfig* config) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    if(!config||config->size!=sizeof(x3m::MotionOutputFixtureConfig)) return;
    x3m::fixture_config=*config; x3m::fixture_configured=true;
    for(auto& entry:x3m::devices) x3m::fixture_apply(*entry.second);
}
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_readback_target(IDirect3DDevice9* device,unsigned target,float* out,unsigned floats,unsigned* width,unsigned* height) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_readback(target,out,floats,width,height);
}
// Compatibility spellings: RT1 (motion, 4 floats per pixel) and RT2 (depth, 1 float per pixel).
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_readback(IDirect3DDevice9* device,float* out,unsigned floats,unsigned* width,unsigned* height) {
    return x3m_motion_output_fixture_readback_target(device,1,out,floats,width,height);
}
// The fixture executable's own camera pointer slots stand in for the engine
// globals (camera_state::fixture_install); the identity gate is bypassed.
extern "C" __declspec(dllexport) void x3m_camera_state_fixture_install(const float* const* projection_slot,const float* const* view_slot) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    x3m::camera_state::fixture_install(projection_slot,view_slot);
}
// Engine scene-end hook seam: the fixture executable's own E8 callsite and
// compositor stand in for 0x004721b1 / 0x004c4750; identity gate bypassed.
extern "C" __declspec(dllexport) int x3m_scene_hook_fixture_install(void* site,void* target) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    const bool result=x3m::scene_hook::fixture_install(site,target,&x3m::scene_end_signal);
    for(auto& entry:x3m::devices) entry.second->motion_output.configure_scene_hook(x3m::scene_hook::active());
    return result;
}
extern "C" __declspec(dllexport) int x3m_scene_hook_fixture_shutdown() {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    const bool result=x3m::scene_hook::fixture_shutdown();
    for(auto& entry:x3m::devices) entry.second->motion_output.configure_scene_hook(x3m::scene_hook::active());
    return result;
}
extern "C" __declspec(dllexport) unsigned x3m_scene_hook_fixture_signals() { return unsigned(x3m::scene_hook::signals()); }
extern "C" __declspec(dllexport) const char* x3m_scene_hook_fixture_status() { return x3m::scene_hook::status(); }
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_last_pixel_abi(IDirect3DDevice9* device,float* out,unsigned floats) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_last_pixel_abi(out,floats);
}
extern "C" __declspec(dllexport) HRESULT x3m_motion_output_fixture_readback_depth(IDirect3DDevice9* device,float* out,unsigned floats,unsigned* width,unsigned* height) {
    return x3m_motion_output_fixture_readback_target(device,2,out,floats,width,height);
}
// HDR seam: fault injection (renderer::HdrFault kinds, `count` firings; a null
// device queues the fault for every hooked device and the next attach) and
// the FP16 target as floats.
extern "C" __declspec(dllexport) void x3m_hdr_fixture_fault(IDirect3DDevice9* device,unsigned kind,unsigned count) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    for(auto& entry:x3m::devices) if(!device||entry.first==device) entry.second->motion_output.fixture_hdr_fault(kind,count);
    if(!device){x3m::fixture_hdr_fault_kind=kind;x3m::fixture_hdr_fault_count=count;}
}
extern "C" __declspec(dllexport) HRESULT x3m_hdr_fixture_readback(IDirect3DDevice9* device,float* out,unsigned floats,unsigned* width,unsigned* height) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_hdr_readback(out,floats,width,height);
}
// Stage 2: the exposure state (ev, ev_adapted, ev_target, avg_log_l, dt, exposure, steps, k).
extern "C" __declspec(dllexport) HRESULT x3m_hdr_fixture_exposure(IDirect3DDevice9* device,float* out,unsigned floats) {
    std::lock_guard<std::recursive_mutex> lock(x3m::mutex);
    const auto it=x3m::devices.find(device);
    if(it==x3m::devices.end()) return D3DERR_INVALIDCALL;
    return it->second->motion_output.fixture_hdr_exposure(out,floats);
}
#endif
