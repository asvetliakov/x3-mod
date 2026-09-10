#include "motion_capture.h"
#include "capture_state.h"
#include "cpu_state.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <tuple>

namespace x3m {
namespace {
template<class T> struct Ref { T* p=nullptr;~Ref(){if(p)p->Release();} };
bool lifetime_current(const object_lifetime::Snapshot& before,std::uintptr_t registry,
                      const renderer::RigidDrawKey& key) noexcept {
    object_lifetime::Snapshot after{};
    return before.known && object_lifetime::current(registry,key.node,key.node_handle,key.camera,key.camera_handle,&after) &&
        after.known && before.observer_epoch==after.observer_epoch && before.load_epoch==after.load_epoch &&
        before.registry_epoch==after.registry_epoch && before.mutation_revision==after.mutation_revision &&
        before.node_serial==after.node_serial && before.camera_serial==after.camera_serial &&
        key.object_lifetime==after.node_serial && key.camera_lifetime==after.camera_serial;
}
bool finite_current(const DrawInput& in,const ownership::GeometryLeaseView& view) noexcept {
    const auto& k=in.observation.key;
    return view.status==S_OK && view.vertex_buffer && view.generation==in.finite_positions.generation &&
        view.positions.state==ownership::FiniteStatus::Finite && view.positions.revision==k.vertex_revision &&
        (!k.indexed || (view.index_buffer && view.indices.known && view.indices.revision==k.index_revision &&
          view.indices.minimum>=k.min_vertex && view.indices.minimum<=view.indices.maximum &&
          std::uint64_t(view.indices.maximum)<std::uint64_t(k.min_vertex)+k.vertex_count));
}
}
MotionCapture::MotionCapture() noexcept : history_(ownership::geometry_leases_per_frame,renderer::MotionHistoryPurpose::DiagnosticStorageCorrespondence) {}
MotionCapture::~MotionCapture(){release_geometry();}
bool MotionCapture::same(const Domain& a,const Domain& b) noexcept {
    return std::tie(a.device,a.generation,a.color,a.depth,a.finite,a.observer,a.load,a.registry,a.camera,a.width,a.height)==
           std::tie(b.device,b.generation,b.color,b.depth,b.finite,b.observer,b.load,b.registry,b.camera,b.width,b.height);
}
void MotionCapture::release_geometry() noexcept {
    if(geometry_.value)ownership::end_geometry_frame(geometry_);
    geometry_={};
}
void MotionCapture::invalidate() noexcept {
    release_geometry();records_.clear();history_.invalidate();domain_={};
    active_=sealed_=produced_=confirmed_=false;
}
void MotionCapture::begin_frame(IDirect3DDevice9* application,std::uint64_t frame,bool requested) noexcept {
    if(active_)invalidate();
    release_geometry();records_.clear();sealed_=produced_=confirmed_=false;frame_=frame;
    if(!requested || !frame || ownership::begin_geometry_frame(application,&geometry_)!=S_OK){invalidate();return;}
    active_=true;
}
void MotionCapture::observe(const DrawInput& input,std::uintptr_t registry,
                            const object_lifetime::Snapshot& lifetime) noexcept {
    if(!active_ || sealed_)return;
    if(records_.size()>=ownership::geometry_leases_per_frame){invalidate();return;}
    try { records_.push_back({input,registry,lifetime}); }
    catch(...) { invalidate(); }
}
MotionCaptureDiagnostics MotionCapture::before_clear(IDirect3DDevice9* application,
    const renderer::Selection& selection,DiagnosticConsumer consumer,void* context) noexcept {
    PreserveCpuState preserve;
    MotionCaptureDiagnostics stats{};
    if(!active_ || sealed_ || !selection.valid || selection.frame!=frame_)return stats;
    stats.attempted=true;stats.observations=records_.size();
    auto fail=[&](HRESULT hr){stats.operation=hr;invalidate();return stats;};
    try {
        // The execution view is observed from original wrapper calls; never
        // assume that a recognized Clear sequence proves query idleness.
        auto& execution=stats.execution;
        if(ownership::get_execution_view(application,&execution)!=S_OK || !execution.known ||
           execution.stateblock_recording || !execution.queries_idle)return fail(S_FALSE);
        auto* native=ownership::borrowed_native_device(application);
        if(!native)return fail(E_INVALIDARG);
        Ref<IDirect3DSurface9> app_depth,app_color,native_depth;
        if(FAILED(application->GetDepthStencilSurface(&app_depth.p)) ||
           FAILED(application->GetRenderTarget(0,&app_color.p)) ||
           resource_id(app_depth.p)!=selection.depth.identity || resource_id(app_color.p)!=selection.color.identity ||
           FAILED(native->GetDepthStencilSurface(&native_depth.p)))return fail(E_INVALIDARG);
        // The wrapper getter references only span this function. They are never
        // held by the device's capture context or deferred past this callback.
        std::vector<ownership::GeometryLeaseView> views(records_.size());
        Domain current{};bool have_domain=false;
        for(std::size_t n=0;n<records_.size();++n){
            auto& record=records_[n];auto& in=record.input;auto& observation=in.observation;
            const auto& key=observation.key;
            bool eligible=!in.blockers && observation.proofs==renderer::AllRigidProofs &&
                in.replay_source.qualified() && in.vertex_finite_verified && in.index_range_verified &&
                in.color_target==selection.color.identity && in.depth_target==selection.depth.identity &&
                in.width==selection.color.width && in.height==selection.color.height &&
                in.geometry_lease.value && lifetime_current(record.lifetime,record.registry,key);
            if(eligible)eligible=ownership::inspect_geometry_lease(geometry_,in.geometry_lease,&views[n])==S_OK &&
                finite_current(in,views[n]);
            if(!eligible){observation.proofs&=~renderer::GeometryUnchanged;++stats.rejected;continue;}
            const auto& life=record.lifetime;
            const Domain candidate{selection.device,selection.generation,selection.color.identity,selection.depth.identity,
                in.finite_positions.generation,life.observer_epoch,life.load_epoch,life.registry_epoch,key.camera_lifetime,
                in.width,in.height};
            if(have_domain && !same(current,candidate))return fail(S_FALSE);
            current=candidate;have_domain=true;++stats.eligible;
        }
        if(!have_domain)return fail(S_FALSE);
        // This epoch proves only the explicitly observed storage/resource domain.
        // Camera cuts and complete final-color coverage remain UNKNOWN, and no
        // temporal-color consumer is called with this diagnostic output.
        if(!same(domain_,current)){
            if(epoch_==std::numeric_limits<std::uint64_t>::max())return fail(E_FAIL);
            ++epoch_;domain_=current;
        }
        if(!history_.begin_frame({frame_,epoch_,current.width,current.height}))return fail(E_FAIL);
        for(const auto& record:records_)if(!history_.observe(record.input.observation))return fail(E_FAIL);
        if(!history_.seal())return fail(E_FAIL);
        sealed_=true;
        std::vector<renderer::RigidMotionDraw> draws;draws.reserve(records_.size());
        for(std::size_t n=0;n<records_.size();++n){
            const auto& in=records_[n].input;const auto& key=in.observation.key;
            const auto pair=history_.lookup(key);
            if(pair.status!=renderer::Correspondence::Matched)continue;
            if(!views[n].vertex_buffer)return fail(E_FAIL);
            renderer::RigidMotionDraw draw{};
            draw.source_program=in.replay_source;draw.finite_positions_attested=true;
            draw.vertices=views[n].vertex_buffer;draw.indices=views[n].index_buffer;
            draw.stream_offset=key.stream_offset;draw.stride=key.stride;draw.position_offset=key.position_offset;
            draw.position_type=static_cast<D3DDECLTYPE>(key.position_type);draw.topology=static_cast<D3DPRIMITIVETYPE>(key.topology);
            draw.primitive_count=key.primitives;draw.start_vertex=key.indexed?0:key.first;
            draw.minimum_vertex=key.min_vertex;draw.vertex_count=key.vertex_count;draw.start_index=key.indexed?key.first:0;
            draw.base_vertex=key.base_vertex;draw.cull=in.cull;draw.correspondence_attested=true;
            std::copy(pair.current.begin(),pair.current.end(),draw.current_wvp);
            std::copy(pair.previous.begin(),pair.previous.end(),draw.previous_wvp);
            draws.push_back(draw);
        }
        stats.matched=draws.size();
        // Per-boundary resources avoid logical ownership cycles and guarantee
        // shader/target teardown before Reset or final device retirement.
        Ref<IDirect3DTexture9> motion;
        HRESULT hr=native->CreateTexture(current.width,current.height,1,D3DUSAGE_RENDERTARGET,
            D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr);
        if(FAILED(hr))return fail(hr);
        renderer::RigidMotionPass pass;
        hr=pass.initialize(native);if(FAILED(hr))return fail(hr);
        renderer::RigidMotionInputs inputs{};
        inputs.motion=motion.p;inputs.scene_depth=native_depth.p;inputs.width=current.width;inputs.height=current.height;
        inputs.draws=draws.data();inputs.draw_count=draws.size();inputs.scene_depth_current=true;
        inputs.caller_scene_open=execution.scene_open;inputs.caller_stateblock_recording=execution.stateblock_recording;
        inputs.caller_queries_idle=execution.queries_idle;
        renderer::RigidMotionOutput output{};
        hr=pass.run(inputs,&output);stats.operation=hr;stats.restoration=pass.diagnostics().restoration;
        stats.completed=pass.diagnostics().completed_draws;
        if(hr!=S_OK){ownership::invalidate_execution_state(application);return fail(hr);}
        produced_=stats.produced=true;
        if(consumer)consumer(context,output);
        release_geometry();
        return stats;
    }catch(...){return fail(E_OUTOFMEMORY);}
}
void MotionCapture::after_clear(bool confirmed) noexcept {
    if(active_ && sealed_){confirmed_=confirmed;if(!confirmed)invalidate();}
}
bool MotionCapture::end_frame(bool confirmed,HRESULT result) noexcept {
    if(!active_)return false;
    const bool committed=history_.commit(sealed_ && produced_ && confirmed_ && confirmed && SUCCEEDED(result));
    release_geometry();records_.clear();active_=sealed_=produced_=confirmed_=false;
    return committed;
}
} // namespace x3m
