// Included in d3d9_ownership.cpp after its private registry/metadata definitions.
// No COM or foreign callback is made inside the final stage/publication guards.
namespace {
namespace cu=clone_upload;
struct UploadFrame {
    std::uint32_t ready=0;
    bool bound=false,refused=false,producer=false;
    unsigned depth=0;
    DWORD thread=0;
    std::uint64_t invocation=0;
    std::uint64_t boundary=0;
    cu::Identity sources[2]{},destinations[2]{};
    IUnknown* destination_keys[2]{}; // weak registry keys, never directly called
    const void* mappings[2]{};
    std::uint64_t lock_serials[2]{};
    portable_upload::BufferContract contracts[2]{};
    IDirect3DVertexBuffer9* temporary_vertex=nullptr;
    IDirect3DIndexBuffer9* temporary_index=nullptr;
    LockSidecar* authentication_side=nullptr; // explicit CPU-only retained ref
    IUnknown* authentication_returned=nullptr;
};
struct UploadControl {
    cu::Store* store=nullptr;
    Device* device=nullptr; // explicit application reference acquired at arm
    UploadFrame* frame=nullptr;
    D3DVERTEXELEMENT9 declaration[MAXD3DDECLLENGTH+1]{};
    unsigned declaration_count=0;
} upload_control;
std::uint64_t upload_last_boundary=0;
#ifdef X3M_LATTICE_UPLOAD_FIXTURE
CloneUploadFixtureHook upload_fixture_hook=nullptr;
void upload_hook(CloneUploadFixtureEvent event,IUnknown* value=nullptr){if(upload_fixture_hook)upload_fixture_hook(event,value);}
#else
enum class CloneUploadFixtureEvent { Created,BeforeStage,AfterStageQualifiers,BeforeFinal,AfterFinal,BeforeOriginal,
    StageRegistryAcquired,StageCopyCompleted,ResetEntry };
void upload_hook(CloneUploadFixtureEvent,IUnknown* =nullptr){}
#endif
unsigned upload_kind(Node* n){return n->kind==Kind::VertexBuffer?0u:1u;}
cu::Identity upload_identity(Node* n){
    if(!n||!n->lock_sidecar)return {};
    const auto& o=n->lock_sidecar->observation;return {o.allocation_id,o.revision};
}
cu::DeviceGuard upload_device(){
    auto* d=upload_control.device;if(!d)return {};
    return {d->lease_serial,d->buffer_lock_generation,GetCurrentThreadId(),d->resetting,d->lost,d->retiring,
        SUCCEEDED(d->buffer_tracking_status)&&d->finite_owner&&d->finite_owner->healthy&&!d->finite_owner->permanent};
}
void upload_refuse(cu::Refusal reason){
    if(auto* f=upload_control.frame){f->refused=true;if(f->invocation)upload_control.store->refuse(reason);}
}
void upload_reset(Device* d){
    if(upload_control.device==d&&upload_control.store){upload_refuse(cu::Refusal::Device);upload_control.store->reset();}
}
void upload_mutation(Node* n){
    if(!upload_control.store||device_of(n)!=upload_control.device)return;
    upload_control.store->invalidate(upload_identity(n).allocation);upload_refuse(cu::Refusal::Mutation);
}
UploadTicket upload_enter_body(Device* d,Node* n,bool create,DWORD flags){
    if(!d->options.prepare_readable_managed_uploads)return {};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    if(upload_control.device!=d||!upload_control.store)return {};
    if(n&&!(flags&D3DLOCK_READONLY)){
        const auto id=upload_identity(n).allocation;
        for(unsigned s=0;s<cu::pair_count;++s){const auto r=upload_control.store->record(s);
            if(r.producer_payload_valid&&(r.buffers[0].allocation==id||r.buffers[1].allocation==id))upload_control.store->invalidate(id);}
    }
    auto* f=upload_control.frame;if(!f)return {};
    if(f->thread!=GetCurrentThreadId()||!f->producer||f->depth)upload_refuse(cu::Refusal::Reentry);
    if(!create&&n){const auto k=upload_kind(n);const auto id=upload_identity(n).allocation;
        if(!id||(id!=f->sources[k].allocation&&id!=f->destinations[k].allocation))upload_refuse(cu::Refusal::Allocation);}
    ++f->depth;return {f,f->boundary};
}
UploadTicket upload_enter(Device* d,Node* n,bool create,DWORD flags) noexcept {
    try{return upload_enter_body(d,n,create,flags);}
    catch(...){try{std::lock_guard<std::recursive_mutex> lock(registry_mutex);upload_refuse(cu::Refusal::Contract);}catch(...){}return {};}
}
bool upload_ticket(UploadTicket t){return t.frame&&t.frame==upload_control.frame&&t.boundary==upload_control.frame->boundary;}
void upload_leave(UploadTicket t){if(upload_ticket(t)&&upload_control.frame->depth)--upload_control.frame->depth;}
void upload_created_body(UploadTicket t,Device* d,IUnknown* application,HRESULT hr){
    if(!t.frame)return;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        if(!upload_ticket(t))return;
        auto* f=upload_control.frame;
        const auto found=application_nodes.find(application);
        if(FAILED(hr)||found==application_nodes.end()||device_of(found->second)!=d){upload_refuse(cu::Refusal::Allocation);}
        else if(!f->refused){
            Node* n=found->second;
            if((n->kind!=Kind::VertexBuffer&&n->kind!=Kind::IndexBuffer)||!n->lock_sidecar||!d->finite_owner)upload_refuse(cu::Refusal::Allocation);
            else {
                auto* side=find_finite_sidecar(*d->finite_owner,n->identity);const auto k=upload_kind(n);
                if(!side||side->authentication_failed||!n->lock_sidecar->observation.quiet()||f->destination_keys[k])upload_refuse(cu::Refusal::Contract);
                else {
                    const auto& c=side->contract;
                    const cu::Creation created{upload_identity(n),c.size,c.usage,side->requested_usage,true,c.format==D3DFMT_INDEX16,own_buffer_slots(n)};
                    if(upload_control.store->created(f->invocation,static_cast<cu::Buffer>(k),upload_device(),created)){
                        f->destination_keys[k]=application;f->destinations[k]=created.identity;f->contracts[k]=c;
                    }else f->refused=true;
                }
            }
        }
    }
    upload_hook(CloneUploadFixtureEvent::Created,application);
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);upload_leave(t);
}
void upload_locked_body(UploadTicket t,Node* n,UINT offset,UINT size,void* pointer,DWORD flags,HRESULT hr){
    if(!t.frame)return;
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    if(!upload_ticket(t))return;
        auto* f=upload_control.frame;
    if(!f->refused){
        const auto k=upload_kind(n);const auto id=upload_identity(n);const auto kind=static_cast<cu::Buffer>(k);
        const auto* side=n->lock_sidecar;
        const bool tracked=side&&!side->observation.ambiguous&&!side->observation.saturated&&
            side->observation.pending_locks==1&&!side->observation.in_flight_locks&&!side->observation.in_flight_unlocks;
        bool accepted=false;
        if(id.allocation==f->sources[k].allocation)
            accepted=upload_control.store->source_lock(f->invocation,kind,upload_device(),id,offset,size,flags,SUCCEEDED(hr)&&tracked);
        else {
            accepted=upload_control.store->mapped(f->invocation,kind,upload_device(),id,pointer,
                side?side->observation.attempt_serial:0,offset,size,flags,SUCCEEDED(hr)&&tracked);
            if(accepted){f->destinations[k]=id;f->mappings[k]=pointer;f->lock_serials[k]=side->observation.attempt_serial;}
        }
        if(!accepted)f->refused=true;
    }
    upload_leave(t);
}
// All calls below occur OUTSIDE registry. The current wrapper/native resource
// is held by the original in-flight call or a successful public mesh getter.
// Node owns a CPU sidecar reference, so these bounded callbacks cannot destroy it.
void upload_clear_authentication(UploadFrame* frame){
    auto* side=frame->authentication_side;if(!side)return;
    side->authentication_thread.store(0,std::memory_order_release);
    const auto adds=side->authentication_adds.exchange(0,std::memory_order_relaxed);
    const bool returned=frame->authentication_returned==side;
    frame->authentication_side=nullptr;frame->authentication_returned=nullptr;
    // One successful public GetPrivateData result owns exactly one reference.
    // Additional same-thread AddRefs are not ours to release. Their presence
    // refuses authentication and must not cause an over-release of other refs.
    if(returned&&adds)side->Release();
    side->Release(); // Explicit CPU retention, independent of the native result.
}
bool upload_qualify(Node* n,const portable_upload::BufferContract& contract,std::uint32_t pending,std::uint64_t revision){
    auto* side=n->lock_sidecar;if(!side)return false;
    auto* resource=static_cast<IDirect3DResource9*>(n->backend);
    const bool desc=portable_upload::same_description(resource,contract);
    IUnknown* identity=nullptr;
    const HRESULT identity_hr=resource->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity));
    const bool same_identity=SUCCEEDED(identity_hr)&&identity==n->identity;
    if(SUCCEEDED(identity_hr)&&identity)identity->Release();
    BufferMetadata metadata{};DWORD bytes=sizeof metadata;
    const HRESULT metadata_hr=resource->GetPrivateData(buffer_content_guid,&metadata,&bytes);
    UploadFrame* frame=nullptr;
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);frame=upload_control.frame;
        if(!frame||frame->authentication_side)return false;
        side->AddRef();frame->authentication_side=side;frame->authentication_returned=nullptr;}
    struct Clear {UploadFrame* frame;~Clear(){upload_clear_authentication(frame);}} clear{frame};
    side->authentication_adds.store(0,std::memory_order_relaxed);
    side->authentication_thread.store(GetCurrentThreadId(),std::memory_order_release);
    DWORD private_bytes=sizeof frame->authentication_returned;
    const HRESULT private_hr=resource->GetPrivateData(lock_sidecar_guid,&frame->authentication_returned,&private_bytes);
    side->authentication_thread.store(0,std::memory_order_release);
    const auto adds=side->authentication_adds.load(std::memory_order_relaxed);
    const bool authenticated=SUCCEEDED(private_hr)&&frame->authentication_returned==side&&private_bytes==sizeof frame->authentication_returned&&adds==1;
    return desc&&same_identity&&authenticated&&SUCCEEDED(metadata_hr)&&bytes==sizeof metadata&&
        metadata.magic==0x58334252&&metadata.version==1&&!metadata.ambiguous&&metadata.pending==pending&&
        metadata.revision==revision;
}
void upload_before_unlock_body(UploadTicket t,Node* n){
    if(!t.frame)return;
    portable_upload::BufferContract contract{};std::uint64_t revision=0;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        if(!upload_ticket(t)||upload_control.frame->refused)return;
        const auto k=upload_kind(n);const auto* f=upload_control.frame;
        if(upload_identity(n).allocation==f->sources[k].allocation)return;
        contract=f->contracts[k];revision=f->destinations[k].revision;
    }
    upload_hook(CloneUploadFixtureEvent::BeforeStage,n->application);
    const bool qualified=upload_qualify(n,contract,1,revision);
    upload_hook(CloneUploadFixtureEvent::AfterStageQualifiers,n->application);
    // Last callback completed above. This final region is CPU-only and excludes
    // Reset/mutation entry through the same registry mutex for the entire copy.
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    upload_hook(CloneUploadFixtureEvent::StageRegistryAcquired,n->application);
    if(!upload_ticket(t))return;
        auto* f=upload_control.frame;const auto k=upload_kind(n);
    if(f->refused)return;
    if(!qualified||f->depth!=1||f->destination_keys[k]!=n->application||!n->lock_sidecar){upload_refuse(cu::Refusal::Contract);return;}
    const auto& o=n->lock_sidecar->observation;
    cu::MapGuard guard{upload_device(),upload_identity(n),f->mappings[k],o.attempt_serial,
        static_cast<std::uint32_t>(o.pending_locks),static_cast<std::uint32_t>(o.in_flight_locks),static_cast<std::uint32_t>(o.in_flight_unlocks),
        own_buffer_slots(n),qualified,o.ambiguous||o.saturated};
    if(!upload_control.store->stage(f->invocation,static_cast<cu::Buffer>(k),guard))f->refused=true;
    upload_hook(CloneUploadFixtureEvent::StageCopyCompleted,n->application);
}
void upload_unlocked_body(UploadTicket t,Node* n,HRESULT hr){
    if(!t.frame)return;
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    if(!upload_ticket(t))return;
        auto* f=upload_control.frame;const auto k=upload_kind(n);
    if(!f->refused){const auto id=upload_identity(n);const auto kind=static_cast<cu::Buffer>(k);
        const bool quiet=n->lock_sidecar&&n->lock_sidecar->observation.quiet()&&own_buffer_slots(n);
        const bool accepted=id.allocation==f->sources[k].allocation?
            upload_control.store->source_unlock(f->invocation,kind,upload_device(),id,SUCCEEDED(hr)&&quiet):
            upload_control.store->unlocked(f->invocation,kind,upload_device(),id,hr==S_OK,quiet);
        if(!accepted)f->refused=true;
    }
    if(upload_identity(n).allocation==f->destinations[k].allocation)f->mappings[k]=nullptr;
    upload_leave(t);
}
void upload_event_failure(UploadTicket t,bool leave) noexcept {
    try{std::lock_guard<std::recursive_mutex> lock(registry_mutex);if(upload_ticket(t)){upload_refuse(cu::Refusal::Contract);if(leave)upload_leave(t);}}catch(...){}
}
void upload_created(UploadTicket t,Device* d,IUnknown* p,HRESULT hr) noexcept {try{upload_created_body(t,d,p,hr);}catch(...){upload_event_failure(t,true);}}
void upload_locked(UploadTicket t,Node* n,UINT o,UINT s,void* p,DWORD f,HRESULT hr) noexcept {try{upload_locked_body(t,n,o,s,p,f,hr);}catch(...){upload_event_failure(t,true);}}
void upload_before_unlock(UploadTicket t,Node* n) noexcept {try{upload_before_unlock_body(t,n);}catch(...){upload_event_failure(t,false);}}
void upload_unlocked(UploadTicket t,Node* n,HRESULT hr) noexcept {try{upload_unlocked_body(t,n,hr);}catch(...){upload_event_failure(t,true);}}
bool upload_declaration(const D3DVERTEXELEMENT9* declaration,unsigned& count){
    if(!declaration)return false;
    const unsigned sizes[]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
    unsigned stride=0;bool position=false;
    for(count=0;count<=MAXD3DDECLLENGTH;++count){const auto& e=declaration[count];
        if(e.Stream==0xff){++count;return stride==40&&position;}
        if(e.Stream||e.Type>=sizeof(sizes)/sizeof(sizes[0])||e.Method!=D3DDECLMETHOD_DEFAULT)return false;
        const auto end=unsigned(e.Offset)+sizes[e.Type];if(end>stride)stride=end;
        if(e.Usage==D3DDECLUSAGE_POSITION&&e.UsageIndex==0)position=e.Type==D3DDECLTYPE_FLOAT16_4;
    }
    return false;
}
void upload_abort(UploadFrame* f){
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        if(f->bound&&upload_control.frame==f){f->refused=true;f->producer=false;upload_control.store->abort(f->invocation);}}
    // Scope-owned refs remain visible to native unwind, not only C++ RAII.
    // Clear each slot before Release and do not hold registry across callbacks.
    upload_clear_authentication(f);
    if(f->temporary_vertex){auto* p=f->temporary_vertex;f->temporary_vertex=nullptr;p->Release();}
    if(f->temporary_index){auto* p=f->temporary_index;f->temporary_index=nullptr;p->Release();}
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    if(upload_control.frame==f)upload_control.frame=nullptr;
    f->bound=false;
}
struct UploadReferences {
    IDirect3DVertexBuffer9*& vertex;IDirect3DIndexBuffer9*& index;
    explicit UploadReferences(UploadFrame* f):vertex(f->temporary_vertex),index(f->temporary_index){}
    void clear(){if(vertex){auto* p=vertex;vertex=nullptr;p->Release();}if(index){auto* p=index;index=nullptr;p->Release();}}
    ~UploadReferences(){clear();}
};
bool upload_get_references(ID3DXMesh* mesh,UploadReferences& refs){
    IDirect3DVertexBuffer9* vertex=nullptr;
    if(FAILED(mesh->GetVertexBuffer(&vertex)))return false;
    refs.vertex=vertex;if(!vertex)return false;
    IDirect3DIndexBuffer9* index=nullptr;
    if(FAILED(mesh->GetIndexBuffer(&index)))return false;
    refs.index=index;return index!=nullptr;
}
void upload_prepare(UploadFrame* f,ID3DXMesh* mesh,DWORD options,const D3DVERTEXELEMENT9* declaration,IDirect3DDevice9* application,ID3DXMesh** out) noexcept {
    try {
        {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
            if(!upload_control.store||!mesh||!out||!declaration||!upload_control.device||upload_control.device->application!=application)return;
            if(upload_control.frame){upload_refuse(cu::Refusal::Busy);return;}
            if(upload_last_boundary==UINT64_MAX)return;
            f->boundary=++upload_last_boundary;f->bound=true;f->thread=GetCurrentThreadId();upload_control.frame=f;
        }
        unsigned count=0;
        const bool layout=upload_declaration(declaration,count)&&count==upload_control.declaration_count&&
            !std::memcmp(declaration,upload_control.declaration,count*sizeof(*declaration));
        unsigned slot=cu::pair_count;
        const auto vertices=mesh->GetNumVertices(),faces=mesh->GetNumFaces();
        for(unsigned s=0;s<cu::pair_count;++s)if(vertices==cu::shapes[s].vertices&&faces==cu::shapes[s].faces)slot=s;
        if(!layout||slot==cu::pair_count||(options&(D3DXMESH_32BIT|D3DXMESH_VB_SHARE))||(mesh->GetOptions()&D3DXMESH_32BIT)){
            std::lock_guard<std::recursive_mutex> lock(registry_mutex);upload_refuse(cu::Refusal::Selector);return;
        }
        UploadReferences refs(f);
        const bool getters=upload_get_references(mesh,refs);
        cu::Identity source_ids[2]{};bool recognized=getters;
        {
            std::lock_guard<std::recursive_mutex> lock(registry_mutex);
            IUnknown* inputs[]={refs.vertex,refs.index};
            for(unsigned k=0;k<2;++k){auto found=application_nodes.find(inputs[k]);
                if(found==application_nodes.end()||device_of(found->second)!=upload_control.device||
                   found->second->kind!=(k?Kind::IndexBuffer:Kind::VertexBuffer)||!found->second->lock_sidecar||
                   !found->second->lock_sidecar->observation.quiet()||!own_buffer_slots(found->second))recognized=false;
                else source_ids[k]=upload_identity(found->second);
            }
        }
        refs.clear(); // No added resource reference survives the original call.
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        if(f->refused||!recognized){upload_refuse(cu::Refusal::Source);return;}
        f->sources[0]=source_ids[0];f->sources[1]=source_ids[1];
        f->invocation=upload_control.store->begin(slot,upload_device(),source_ids[0],source_ids[1]);
        if(!f->invocation)f->refused=true;
    }catch(...){std::lock_guard<std::recursive_mutex> lock(registry_mutex);if(f->bound)upload_refuse(cu::Refusal::Contract);}
}
void upload_finish(UploadFrame* f,HRESULT hr,ID3DXMesh* mesh) noexcept {
    try {
        bool qualified=false;
        {std::lock_guard<std::recursive_mutex> lock(registry_mutex);if(!f->bound||upload_control.frame!=f)return;f->producer=false;qualified=SUCCEEDED(hr)&&mesh&&!f->refused;}
        cu::FinalGuard guard{};
        UploadReferences refs(f);
        if(qualified){
            upload_hook(CloneUploadFixtureEvent::BeforeFinal,mesh);
            qualified=upload_get_references(mesh,refs);
            Node* nodes[2]{};
            {std::lock_guard<std::recursive_mutex> lock(registry_mutex);IUnknown* inputs[]={refs.vertex,refs.index};
                for(unsigned k=0;k<2;++k){const auto found=application_nodes.find(inputs[k]);
                    if(found==application_nodes.end()||inputs[k]!=f->destination_keys[k])qualified=false;
                    else nodes[k]=found->second;
                }
            }
            if(qualified)for(unsigned k=0;k<2;++k)qualified=upload_qualify(nodes[k],f->contracts[k],0,f->destinations[k].revision)&&qualified;
        }
        refs.clear();
        upload_hook(CloneUploadFixtureEvent::AfterFinal,mesh);
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        if(upload_control.frame!=f)return;
        guard.device=upload_device();guard.authenticated=qualified&&!f->refused;guard.quiet=f->depth==0;guard.own_dispatch=true;
        for(unsigned k=0;k<2;++k){const auto found=application_nodes.find(f->destination_keys[k]);
            if(found==application_nodes.end()||device_of(found->second)!=upload_control.device||!found->second->lock_sidecar){guard.authenticated=false;continue;}
            guard.buffers[k]=upload_identity(found->second);
            guard.quiet=guard.quiet&&found->second->lock_sidecar->observation.quiet();guard.own_dispatch=guard.own_dispatch&&own_buffer_slots(found->second);
        }
        if(f->invocation)upload_control.store->finish(f->invocation,SUCCEEDED(hr),guard);
        upload_control.frame=nullptr;f->bound=false;
    }catch(...){upload_abort(f);}
}
HRESULT upload_arm_core(cu::Store* store,IDirect3DDevice9* application,const D3DVERTEXELEMENT9* declaration) noexcept {
    try {
        unsigned count=0;if(!store||!upload_declaration(declaration,count))return E_INVALIDARG;
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found=application_nodes.find(application);if(found==application_nodes.end()||found->second->kind!=Kind::Device)return E_INVALIDARG;
        auto* d=static_cast<Device*>(found->second);
        if(upload_control.store||store->active()||!d->options.prepare_readable_managed_uploads||!d->options.track_buffer_lock_attempts||
           d->resetting||d->lost||d->retiring||!d->finite_owner||!d->finite_owner->healthy)return S_FALSE;
        ++d->refs;store->reset();upload_control.store=store;upload_control.device=d;
        upload_control.declaration_count=count;std::memcpy(upload_control.declaration,declaration,count*sizeof(*declaration));return S_OK;
    }catch(...){return E_FAIL;}
}
HRESULT upload_disarm_core(cu::Store* store) noexcept {
    try {IUnknown* release=nullptr;
        {std::lock_guard<std::recursive_mutex> lock(registry_mutex);if(!store||upload_control.store!=store||upload_control.frame)return S_FALSE;
            release=upload_control.device->application;store->reset();upload_control=UploadControl{};}
        release->Release();return S_OK;
    }catch(...){return E_FAIL;}
}
__attribute__((noinline)) bool upload_copy_core(unsigned slot,clone_upload::Buffer kind,IDirect3DVertexBuffer9* vertex,IDirect3DIndexBuffer9* index,void* bytes,std::size_t capacity) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);if(!upload_control.store||upload_control.frame)return false;
        cu::FinalGuard guard{};guard.device=upload_device();guard.authenticated=true;guard.quiet=true;guard.own_dispatch=true;
        IUnknown* keys[]={vertex,index};
        for(unsigned k=0;k<2;++k){auto found=application_nodes.find(keys[k]);
            if(found==application_nodes.end()||device_of(found->second)!=upload_control.device||
               found->second->kind!=(k?Kind::IndexBuffer:Kind::VertexBuffer)||!found->second->lock_sidecar)return false;
            guard.buffers[k]=upload_identity(found->second);guard.quiet=guard.quiet&&found->second->lock_sidecar->observation.quiet();
            guard.own_dispatch=guard.own_dispatch&&own_buffer_slots(found->second);
        }
        return upload_control.store->copy_retained(slot,kind,guard,bytes,capacity);
    }catch(...){return false;}
}
}
#pragma GCC push_options
#pragma GCC optimize("no-exceptions")
__attribute__((noinline)) HRESULT arm_clone_upload(clone_upload::Store* store,IDirect3DDevice9* device,const D3DVERTEXELEMENT9* declaration) noexcept {
    CounterAbiState saved;const auto hr=upload_arm_core(store,device,declaration);saved.restore();return hr;
}
__attribute__((noinline)) HRESULT disarm_clone_upload(clone_upload::Store* store) noexcept {
    CounterAbiState saved;const auto hr=upload_disarm_core(store);saved.restore();return hr;
}
__attribute__((noinline)) bool copy_clone_upload(unsigned slot,clone_upload::Buffer kind,IDirect3DVertexBuffer9* vertex,IDirect3DIndexBuffer9* index,void* bytes,std::size_t capacity) noexcept {
    CounterAbiState saved;const bool result=upload_copy_core(slot,kind,vertex,index,bytes,capacity);saved.restore();return result;
}

#pragma GCC pop_options

#ifdef X3M_LATTICE_UPLOAD_FIXTURE
void clone_upload_fixture_hook(CloneUploadFixtureHook hook) noexcept {upload_fixture_hook=hook;}
bool clone_upload_fixture_scope_active() noexcept {std::lock_guard<std::recursive_mutex> lock(registry_mutex);return upload_control.frame!=nullptr;}
namespace {void upload_fixture_reset_entry(Device* device){upload_hook(CloneUploadFixtureEvent::ResetEntry,device->application);}}
#endif

namespace clone_upload_abi {
namespace {
static_assert(sizeof(UploadFrame)<=sizeof(Context::bytes));
static_assert(alignof(UploadFrame)<=alignof(Context));
static_assert(std::is_trivially_destructible_v<UploadFrame>);
UploadFrame* frame(Context& context) noexcept {return std::launder(reinterpret_cast<UploadFrame*>(context.bytes));}
void prepare(Context& context,const Arguments& arguments) noexcept {
    auto* f=new(context.bytes) UploadFrame{};
    f->ready=0x58334355u;
    upload_prepare(f,arguments.source,arguments.options,arguments.declaration,arguments.device,arguments.output);
}
void before_original(Context& context) noexcept {
    auto* f=frame(context);
    try {
        {std::lock_guard<std::recursive_mutex> lock(registry_mutex);if(f->bound)f->producer=true;}
        upload_hook(CloneUploadFixtureEvent::BeforeOriginal);
    }catch(...){try{std::lock_guard<std::recursive_mutex> lock(registry_mutex);if(f->bound)upload_refuse(cu::Refusal::Contract);}catch(...){}}
}
void finish(Context& context,HRESULT hr,ID3DXMesh* result) noexcept {upload_finish(frame(context),hr,result);}
void abort(Context& context) noexcept {
    // ABI zeroes Context and makes it abort-eligible BEFORE entering prepare.
    // No object-lifetime assumption is made if construction has not completed.
    std::uint32_t ready=0;std::memcpy(&ready,context.bytes,sizeof ready);
    if(ready!=0x58334355u)return;
    try{upload_abort(frame(context));}catch(...){}
}
}
const Observer observer{prepare,before_original,finish,abort};
}
