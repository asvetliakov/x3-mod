#include "d3d9_ownership.h"
#include <mutex>
#include <memory>
#include <atomic>
#include <cstring>
#include "managed_upload_contract.h"
#include <limits>
#include <type_traits>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace x3m::ownership {
namespace {

enum class Kind {
    Factory, Device, Texture, CubeTexture, VolumeTexture, Surface, Volume,
    VertexBuffer, IndexBuffer, VertexDeclaration, VertexShader, PixelShader,
    StateBlock, Query, SwapChain
};
struct Node;
struct Factory;
struct Device;
struct Query;
struct FiniteOwner;
void retire_finite(Device*, bool permanent, FiniteEvidenceReason);
void initialize_finite(Device*);

// These are backend-only references. They do not own application wrappers or
// logical device references, and are retired before the native device root.
struct CopyDepth {
    IDirect3DSurface9* original = nullptr; // Native ref only; never substituted.
    CopyDepthView view;
};

// Weak registries contain only wrappers with positive application refcounts.
// Backend binding/state-block retention never adds an application reference.
// Never hold this lock across backend Release: destruction can reenter COM.
std::recursive_mutex registry_mutex;
std::unordered_map<IUnknown*, Node*> application_nodes;
std::unordered_map<IUnknown*, Node*> native_nodes;

struct Node {
    Kind kind;
    ULONG refs = 1;
    IUnknown* backend;
    IUnknown* application = nullptr;
    IUnknown* identity = nullptr; // Borrowed, stable while backend is owned.
    Node* parent;
    Node(Kind type, IUnknown* native, Node* owner) : kind(type), backend(native), parent(owner) {}
    virtual ~Node() = default;
};

HRESULT query(Node* node, REFIID iid, void** out);
ULONG add_ref(Node* node);
ULONG release(Node* node);
HRESULT get_device(Node* node, IDirect3DDevice9** out);
HRESULT get_factory(Device* node, IDirect3D9** out);
HRESULT get_container(Node* node, REFIID iid, void** out);
HRESULT create_device(Factory* node, UINT adapter, D3DDEVTYPE type, HWND window,
                      DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out);
HRESULT reset_device(Device* node, D3DPRESENT_PARAMETERS* pp);
HRESULT observe_result(Device* node, HRESULT hr);
HRESULT clear_device(Device* node, DWORD count, const D3DRECT* rects, DWORD flags,
                     D3DCOLOR color, float depth, DWORD stencil);
void initialize_copy_depth(Device* node, const D3DPRESENT_PARAMETERS& requested);
void retire_copy_depth(Device* node, HRESULT status);
bool copy_source_bound(Device* node, HRESULT* query_status = nullptr);
HRESULT scene_transition(Device* node, bool begin);
HRESULT create_query(Device* node, D3DQUERYTYPE type, IDirect3DQuery9** out);
HRESULT issue_query(Query* node, DWORD flags);
HRESULT begin_state_block(Device* node);
HRESULT end_state_block(Device* node, IDirect3DStateBlock9** out);
void initialize_buffer(Device* device, IDirect3DResource9* native);
HRESULT buffer_lock(Node* node, UINT offset, UINT size, void** data, DWORD flags);
HRESULT buffer_unlock(Node* node);
HRESULT buffer_private_result(Node* node, REFGUID guid, HRESULT hr);
HRESULT process_vertices(Device* node, UINT first, UINT destination, UINT count,
    IDirect3DVertexBuffer9* buffer, IDirect3DVertexDeclaration9* declaration, DWORD flags);
Device* device_of(Node* node);
template<class T> T* unwrap(Device* owner, T* value);
template<class T> HRESULT output(Device* owner, HRESULT hr, T* owned, T** out);

// Some D3D9 failure paths leave an output slot untouched, while others clear it.
// Never initialize from caller storage: it may be uninitialized. This private
// marker is only passed in an output-only slot and is never dereferenced.
unsigned char untouched_output_marker;
template<class T> T* untouched_output() {
    return reinterpret_cast<T*>(&untouched_output_marker);
}

// Generated ABI-complete normal-D3D9 declarations. Handwritten methods above
// own every interface-return, identity, parent, reset and lifetime boundary.
#include "d3d9_classes_inc.h"

struct BufferForwardingSlots { const void* lock; const void* unlock; };
template<class Wrapper> BufferForwardingSlots original_buffer_slots() noexcept {
    // These unregistered local shells own no references and make no COM calls.
    // Snapshot at module initialization, before any application can hook a live
    // object (including a shared-vtable patch). Never derive trust from that
    // object's possibly already replaced table.
    const Wrapper shell(nullptr, nullptr);
    const auto table = *reinterpret_cast<void* const* const*>(shell.application);
    return {table[11], table[12]};
}
const BufferForwardingSlots vertex_buffer_slots = original_buffer_slots<VertexBuffer>();
const BufferForwardingSlots index_buffer_slots = original_buffer_slots<IndexBuffer>();

template<class Interface> Interface* buffer_contract_endpoint(Interface* wrapped,
        Kind kind, const BufferForwardingSlots& expected) noexcept {
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found = application_nodes.find(wrapped);
    if (found == application_nodes.end() || found->second->kind != kind) return nullptr;
    // Unknown pointers are only registry keys; dereference only the recognized,
    // live canonical interface. The caller serializes foreign vtable mutation.
    const auto table = *reinterpret_cast<void* const* const*>(found->second->application);
    if (!table || table[11] != expected.lock || table[12] != expected.unlock) return nullptr;
    return static_cast<Interface*>(found->second->backend);
}

// Full x87 environment/register payload and MXCSR, plus Win32 last-error.
// Volatile XMM register values follow the ordinary C++ ABI.
// Native outgoing state is captured separately from incoming state at dispatch.
struct ExecutionState {
    unsigned char fp[108]; unsigned mxcsr; DWORD error;
    ExecutionState() noexcept : error(GetLastError()) { asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(fp),"=m"(mxcsr) :: "memory"); }
    void restore() const noexcept { asm volatile("frstor %0\n\tldmxcsr %1" :: "m"(fp),"m"(mxcsr) : "memory"); SetLastError(error); }
};
struct PreserveExecution { ExecutionState saved; ~PreserveExecution() { saved.restore(); } };
struct FiniteSidecar;
std::atomic<FiniteSidecar*> finite_retired{nullptr};
thread_local FiniteSidecar* finite_private_expected=nullptr;
thread_local unsigned finite_private_adds=0;
#ifdef X3M_FINITE_FIXTURE
void(*finite_addref_fixture_hook)(void*)=nullptr;
void* finite_addref_fixture_context=nullptr;
#endif
void drain_finite_retired();
constexpr std::uint64_t finite_global_budget=32u*1024u*1024u;
constexpr std::uint64_t finite_global_sidecars=4096, finite_global_owners=64;
std::uint64_t finite_payload_used=0, finite_sidecars_used=0, finite_owners_used=0;
const GUID finite_sidecar_guid={0x03ee519d,0x9308,0x4a86,{0x9b,0x94,0xd3,0x7e,0x35,0xac,0x49,0xaa}};
struct FiniteOwner {
    FiniteUploadStatistics stats;
    std::uint64_t budget,limit;
    FiniteSidecar* head=nullptr;
    bool healthy=true,permanent=false;
    explicit FiniteOwner(const Options& o):budget(o.finite_payload_budget),limit(o.finite_sidecar_limit) {
        ++finite_owners_used; stats.requested=true;stats.active=true;stats.status=S_OK;stats.generation=1;
    }
    ~FiniteOwner() { std::lock_guard<std::recursive_mutex> lock(registry_mutex); --finite_owners_used; }
};
struct FiniteSidecar final : IUnknown {
    std::atomic<ULONG> refs{1};
    std::shared_ptr<FiniteOwner> owner;
    FiniteSidecar* next=nullptr;
    FiniteSidecar* retired_next=nullptr;
    IUnknown* allocation=nullptr; // Numeric weak allocation key, never called or released.
    managed_upload::BufferContract contract{};
    FiniteBufferEvidence evidence;
    FiniteEvidenceReason reason=FiniteEvidenceReason::UnknownCells;
    EvidenceBufferKind kind=EvidenceBufferKind::Unknown;
    std::size_t reserved=0;
    std::uint64_t revision=0,generation=0;
    DWORD thread=0,flags=0;
    UINT offset=0,length=0;
    const void* mapping=nullptr; // Only while the observed native mapping is pending.
    bool writing=false;
    explicit FiniteSidecar(std::shared_ptr<FiniteOwner> value):owner(std::move(value)) {}
    HRESULT WINAPI QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid!=IID_IUnknown)return E_NOINTERFACE;
        AddRef();*out=this;return S_OK;
    }
    ULONG WINAPI AddRef() override {
        const ULONG result=refs.fetch_add(1,std::memory_order_relaxed)+1;
        if(finite_private_expected==this){
            ++finite_private_adds;
#ifdef X3M_FINITE_FIXTURE
            if(finite_addref_fixture_hook)finite_addref_fixture_hook(finite_addref_fixture_context);
#endif
        }
        return result;
    }
    ULONG WINAPI Release() override {
        PreserveExecution preserve;
        const ULONG remaining=refs.fetch_sub(1,std::memory_order_acq_rel)-1;
        if(!remaining){
            // Native private-data callbacks may hold Wine's mutex. Never wait for
            // our registry here: other threads can hold it while entering Wine.
            if(registry_mutex.try_lock()){
                delete this;registry_mutex.unlock();
            }else{
                auto* head=finite_retired.load(std::memory_order_relaxed);
                do{retired_next=head;}while(!finite_retired.compare_exchange_weak(head,this,std::memory_order_release,std::memory_order_relaxed));
            }
        }
        return remaining;
    }
    void drop_payload() {
        evidence.reset();finite_payload_used-=reserved;owner->stats.payload_bytes-=reserved;reserved=0;
        mapping=nullptr;writing=false;
    }
    ~FiniteSidecar() {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        drop_payload(); auto** p=&owner->head;while(*p&&*p!=this)p=&(*p)->next;if(*p)*p=next;
        --finite_sidecars_used;--owner->stats.sidecars;owner->stats.metadata_bytes-=sizeof(FiniteSidecar);
    }
};
struct TickScope {
    std::uint64_t* total; LARGE_INTEGER begin{};
    explicit TickScope(std::uint64_t* value):total(value){if(total)QueryPerformanceCounter(&begin);}
    ~TickScope(){if(total){LARGE_INTEGER end{};QueryPerformanceCounter(&end);*total+=static_cast<std::uint64_t>(end.QuadPart-begin.QuadPart);}}
};
template<class Buffer> bool finite_inspect(FiniteOwner& owner,Buffer* buffer,managed_upload::BufferContract* out){
    TickScope elapsed(&owner.stats.qualifier_ticks);return managed_upload::inspect(buffer,out);
}
bool finite_window(FiniteSidecar* side,UINT offset,UINT size,DWORD flags,const void* data,managed_upload::Window* out){
    TickScope elapsed(&side->owner->stats.qualifier_ticks);return managed_upload::validate_window(side->contract,offset,size,flags,data,out);
}
bool finite_closed(FiniteSidecar* side){
    TickScope elapsed(&side->owner->stats.qualifier_ticks);return managed_upload::validate_closed(side->contract);
}
void drain_finite_retired() {
    // Caller owns registry_mutex and is outside a backend callback.
    auto* side=finite_retired.exchange(nullptr,std::memory_order_acquire);
    while(side){auto* next=side->retired_next;delete side;side=next;}
}
bool retain_live_sidecar(FiniteSidecar* side){
    ULONG refs=side->refs.load(std::memory_order_acquire);
    while(refs&&refs!=ULONG_MAX){if(side->refs.compare_exchange_weak(refs,refs+1,std::memory_order_acq_rel))return true;}
    return false;
}
void finite_reason(FiniteOwner& owner,FiniteEvidenceReason reason) {
    ++owner.stats.reasons[static_cast<unsigned>(reason)];
}
void invalidate_finite(FiniteSidecar* side,FiniteEvidenceReason reason) {
    side->evidence.invalidate(side->revision);side->mapping=nullptr;side->writing=false;side->reason=reason;
    ++side->owner->stats.invalidations;finite_reason(*side->owner,reason);
    auto& detail=side->owner->stats.first_refusal;
    if(!detail.available){detail.available=true;detail.reason=reason;detail.type=side->contract.type;detail.format=side->contract.format;detail.pool=D3DPOOL_MANAGED;detail.size=side->contract.size;detail.usage=D3DUSAGE_WRITEONLY;detail.lock_flags=side->flags;}
}
void retire_finite(Device* device,bool permanent=false,FiniteEvidenceReason reason=FiniteEvidenceReason::DeviceUnavailable) {
    PreserveExecution preserve;std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    drain_finite_retired();
    auto owner=device->finite_owner;if(!owner)return;
    owner->healthy=false;owner->permanent|=permanent;owner->stats.active=false;owner->stats.status=permanent?E_FAIL:S_FALSE;
    if(owner->stats.generation!=UINT64_MAX)++owner->stats.generation;else owner->permanent=true;
    for(auto* side=owner->head;side;side=side->next){invalidate_finite(side,reason);side->drop_payload();}
}
void initialize_finite(Device* device) {
    PreserveExecution preserve;if(!device->options.capture_finite_positions)return;
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    drain_finite_retired();
    if(finite_owners_used>=finite_global_owners){device->finite_status=E_OUTOFMEMORY;return;}
    try{device->finite_owner=std::make_shared<FiniteOwner>(device->options);device->finite_status=S_OK;}
    catch(...){device->finite_status=E_OUTOFMEMORY;}
}
bool reserve_finite(FiniteSidecar* side) {
    if(side->evidence.byte_size())return true;
    std::size_t bytes=0;auto& owner=*side->owner;
    if(!FiniteBufferEvidence::required_payload(side->kind,side->contract.size,&bytes)||
       bytes>owner.budget-owner.stats.payload_bytes||bytes>finite_global_budget-finite_payload_used){
        side->reason=FiniteEvidenceReason::Budget;finite_reason(owner,side->reason);return false;
    }
    if(!side->evidence.initialize(side->kind,side->contract.size,bytes)){
        ++owner.stats.allocation_failures;side->reason=FiniteEvidenceReason::AllocationFailure;finite_reason(owner,side->reason);return false;
    }
    side->reserved=bytes;finite_payload_used+=bytes;owner.stats.payload_bytes+=bytes;
    if(owner.stats.payload_bytes>owner.stats.peak_payload_bytes)owner.stats.peak_payload_bytes=owner.stats.payload_bytes;
    return true;
}
void attach_finite(Device* device,IDirect3DResource9* resource) {
    drain_finite_retired();
    auto owner=device->finite_owner;if(!owner||!owner->healthy||FAILED(device->buffer_tracking_status))return;
    managed_upload::BufferContract contract{};bool accepted=false;
    if(resource->GetType()==D3DRTYPE_VERTEXBUFFER)accepted=finite_inspect(*owner,static_cast<IDirect3DVertexBuffer9*>(resource),&contract);
    else if(resource->GetType()==D3DRTYPE_INDEXBUFFER)accepted=finite_inspect(*owner,static_cast<IDirect3DIndexBuffer9*>(resource),&contract);
    if(!accepted){
        finite_reason(*owner,FiniteEvidenceReason::NativeContract);
        if(!owner->stats.first_refusal.available){
            auto& d=owner->stats.first_refusal;d.reason=FiniteEvidenceReason::NativeContract;
            if(resource->GetType()==D3DRTYPE_VERTEXBUFFER){D3DVERTEXBUFFER_DESC desc{};if(SUCCEEDED(static_cast<IDirect3DVertexBuffer9*>(resource)->GetDesc(&desc))){d.available=true;d.type=desc.Type;d.format=desc.Format;d.pool=desc.Pool;d.size=desc.Size;d.usage=desc.Usage;}}
            else {D3DINDEXBUFFER_DESC desc{};if(SUCCEEDED(static_cast<IDirect3DIndexBuffer9*>(resource)->GetDesc(&desc))){d.available=true;d.type=desc.Type;d.format=desc.Format;d.pool=desc.Pool;d.size=desc.Size;d.usage=desc.Usage;}}
        }
        return;
    }
    if(owner->stats.sidecars>=owner->limit||finite_sidecars_used>=finite_global_sidecars){finite_reason(*owner,FiniteEvidenceReason::Budget);return;}
    auto* side=new(std::nothrow) FiniteSidecar(owner);
    if(!side){++owner->stats.allocation_failures;finite_reason(*owner,FiniteEvidenceReason::AllocationFailure);return;}
    // A surviving external private-IUnknown reference cannot confer identity on a reused address.
    for(auto* old=owner->head;old;old=old->next)if(old->allocation==resource){old->allocation=nullptr;invalidate_finite(old,FiniteEvidenceReason::MissingAllocation);old->drop_payload();}
    side->allocation=resource;side->contract=contract;
    side->kind=contract.type==D3DRTYPE_VERTEXBUFFER?EvidenceBufferKind::Vertex:
        contract.format==D3DFMT_INDEX16?EvidenceBufferKind::Index16:EvidenceBufferKind::Index32;
    side->next=owner->head;owner->head=side;++finite_sidecars_used;++owner->stats.sidecars;owner->stats.metadata_bytes+=sizeof(FiniteSidecar);
    if(reserve_finite(side)){
        const HRESULT hr=resource->SetPrivateData(finite_sidecar_guid,static_cast<IUnknown*>(side),sizeof(IUnknown*),D3DSPD_IUNKNOWN);
        if(FAILED(hr)){finite_reason(*owner,FiniteEvidenceReason::MetadataTampered);}
    }
    side->Release();
}
// Only release references whose same-thread native GetPrivateData AddRef callback
// proves they are ours. Unrelated external COM references may change concurrently.
// Foreign POD bytes, including bytes equal to a live sidecar pointer, are never called.
enum class FiniteAcquireMode { AnyMapping, Closed };
FiniteSidecar* acquire_finite(Device* device,IDirect3DResource9* resource,
    FiniteAcquireMode mode=FiniteAcquireMode::AnyMapping,FiniteEvidenceReason* failure=nullptr) {
    if(failure)*failure=FiniteEvidenceReason::None;
    drain_finite_retired();
    auto owner=device->finite_owner;if(!owner||!owner->healthy)return nullptr;
    FiniteSidecar* side=owner->head;while(side&&side->allocation!=resource)side=side->next;
    if(!side||!retain_live_sidecar(side))return nullptr;
    bool verified=false;
    if(mode==FiniteAcquireMode::Closed){
        // validate_closed already performs the complete fresh inspection and
        // same-token comparison, then requires native map_count==0. Do not run
        // an identical inspection immediately before it. The caller serializes
        // mappings/mutations; the authenticated GetPrivateData callback below
        // can only enter our bounded atomic/TLS AddRef, never a mapping call.
        verified=side->contract.borrowed_native==resource&&finite_closed(side);
    }else{
        managed_upload::BufferContract now{};
        verified=side->contract.type==D3DRTYPE_VERTEXBUFFER
            ?finite_inspect(*owner,static_cast<IDirect3DVertexBuffer9*>(resource),&now)
            :finite_inspect(*owner,static_cast<IDirect3DIndexBuffer9*>(resource),&now);
        verified=verified&&now.backend_resource==side->contract.backend_resource&&now.heap_data==side->contract.heap_data&&now.size==side->contract.size&&now.format==side->contract.format;
    }
    if(!verified){if(failure)*failure=FiniteEvidenceReason::NativeContract;invalidate_finite(side,FiniteEvidenceReason::NativeContract);side->Release();return nullptr;}
    auto* previous_expected=finite_private_expected;const unsigned previous_adds=finite_private_adds;
    finite_private_expected=side;finite_private_adds=0;
    IUnknown* returned=nullptr;DWORD size=sizeof(returned);
    const HRESULT hr=resource->GetPrivateData(finite_sidecar_guid,&returned,&size);
    const unsigned observed=finite_private_adds;
    finite_private_expected=previous_expected;finite_private_adds=previous_adds;
    const bool ours=returned==static_cast<IUnknown*>(side)&&size==sizeof(returned)&&SUCCEEDED(hr)&&observed==1;
    for(unsigned n=0;n<observed;++n)side->Release();
    if(!ours){retire_finite(device,true,FiniteEvidenceReason::MetadataTampered);side->Release();return nullptr;}
    return side;
}
struct SideReference { FiniteSidecar* value=nullptr; ~SideReference(){if(value)value->Release();} };
bool own_buffer_slots(Node* node) {
    const auto table=*reinterpret_cast<void* const* const*>(node->application);
    const auto& expected=node->kind==Kind::VertexBuffer?vertex_buffer_slots:index_buffer_slots;
    return table&&table[11]==expected.lock&&table[12]==expected.unlock;
}

using GeometryRelease = ULONG (WINAPI*)(IUnknown*);
struct GeometryFrameRecord {
    std::uint64_t id=0,generation=0;
    Device* device=nullptr; // Weak; invalidated before the logical owner is deleted.
    std::uint32_t count=0;
};
struct GeometryLeaseRecord {
    std::uint64_t id=0,frame=0,bytes=0;
    IDirect3DVertexBuffer9* vertex=nullptr;
    IDirect3DIndexBuffer9* index=nullptr;
    IUnknown* vertex_identity=nullptr;
    IUnknown* index_identity=nullptr;
    FiniteSidecar* vertex_side=nullptr;
    FiniteSidecar* index_side=nullptr;
    GeometryRelease vertex_release=nullptr,index_release=nullptr;
    GeometryLeaseRequest request;
};
std::array<GeometryFrameRecord,geometry_frame_limit> geometry_frames{};
std::array<GeometryLeaseRecord,geometry_lease_limit> geometry_leases{};
constexpr unsigned geometry_slot_bits=13;
constexpr std::uint64_t geometry_slot_mask=(std::uint64_t(1)<<geometry_slot_bits)-1;
constexpr std::uint64_t geometry_serial_limit=UINT64_MAX>>geometry_slot_bits;
static_assert(geometry_lease_limit==(1u<<geometry_slot_bits));
static_assert(geometry_frame_limit<=geometry_lease_limit&&geometry_lease_limit<UINT16_MAX);
constexpr std::array<std::uint16_t,geometry_lease_limit> initial_geometry_free_slots(){
    std::array<std::uint16_t,geometry_lease_limit> result{};
    for(unsigned i=0;i<geometry_lease_limit;++i)result[i]=static_cast<std::uint16_t>(i+1);
    return result;
}
std::array<std::uint16_t,geometry_lease_limit> geometry_free_next=initial_geometry_free_slots();
std::uint16_t geometry_free_head=0; // geometry_lease_limit is the exhausted sentinel.
std::uint64_t geometry_serial=0,geometry_bytes=0;
std::uint32_t geometry_count=0;
std::uint64_t next_geometry_id(unsigned slot){
    // One shared serial for both handle types. Full ID equality, not just the
    // encoded slot, is mandatory for every lookup. Exhaustion refuses admission.
    return (++geometry_serial<<geometry_slot_bits)|slot;
}
GeometryFrameRecord* geometry_frame(std::uint64_t id){
    const auto slot=static_cast<unsigned>(id&geometry_slot_mask);
    if(!id||slot>=geometry_frame_limit)return nullptr;
    auto& frame=geometry_frames[slot];return frame.id==id?&frame:nullptr;
}
GeometryLeaseRecord* geometry_lease(std::uint64_t frame,std::uint64_t id){
    if(!frame||!id)return nullptr;
    auto& lease=geometry_leases[static_cast<unsigned>(id&geometry_slot_mask)];
    return lease.id==id&&lease.frame==frame?&lease:nullptr;
}
void release_geometry_record(const GeometryLeaseRecord& lease){
    // These entrypoints were certified at acquisition against pinned native code.
    // Do not dispatch through a possibly replaced later vtable during cleanup.
    if(lease.index)lease.index_release(lease.index);
    if(lease.vertex)lease.vertex_release(lease.vertex);
    if(lease.index_side)lease.index_side->Release();
    if(lease.vertex_side)lease.vertex_side->Release();
    // Detached references still consume the process reservation until their
    // native callbacks and CPU-side cleanup have actually completed.
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
     geometry_bytes-=lease.bytes;--geometry_count;}
}
GeometryLeaseRecord detach_geometry_record(GeometryLeaseRecord& lease){
    GeometryLeaseRecord result=lease;
    if(auto* frame=geometry_frame(lease.frame))--frame->count;
    const auto slot=static_cast<std::uint16_t>(lease.id&geometry_slot_mask);
    lease={};geometry_free_next[slot]=geometry_free_head;geometry_free_head=slot;
    return result;
}
void drain_geometry_frame(std::uint64_t frame){
    // The invalidated frame cannot gain new entries. Carry a forward cursor
    // across bounded release chunks instead of rescanning already retired slots.
    unsigned cursor=0;
    while(cursor<geometry_lease_limit){
        std::array<GeometryLeaseRecord,16> retired{};unsigned count=0;
        {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
         while(cursor<geometry_lease_limit&&count<retired.size()){
             auto& lease=geometry_leases[cursor++];
             if(lease.id&&lease.frame==frame)retired[count++]=detach_geometry_record(lease);
         }}
        for(unsigned n=0;n<count;++n)release_geometry_record(retired[n]);
    }
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);drain_finite_retired();}
}
void retire_geometry(Device* device){
    PreserveExecution preserve;std::uint64_t frame=0;
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
     for(auto& entry:geometry_frames)if(entry.id&&entry.device==device){frame=entry.id;entry={};break;}}
    if(frame)drain_geometry_frame(frame);
}
bool geometry_owner_ready(Device* device){
    return device&&!device->retiring&&!device->resetting&&!device->lost&&
        device->options.capture_finite_positions&&SUCCEEDED(device->buffer_tracking_status)&&
        device->finite_owner&&device->finite_owner->healthy;
}
bool geometry_ref_endpoints(FiniteSidecar* side,GeometryRelease& release){
    // managed_upload inspection already proved the exact module hash/imports
    // and native buffer endpoints. Add a narrow lease-specific AddRef/Release
    // proof so a foreign reference-count interceptor cannot fake retention.
    const auto table=*reinterpret_cast<void* const* const*>(side->contract.borrowed_native);
    HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(table[12]),&module))return false;
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    const bool vertex=side->contract.type==D3DRTYPE_VERTEXBUFFER;
    if(reinterpret_cast<std::uintptr_t>(table[1])!=base+(vertex?0x1950u:0x2430u)||
       reinterpret_cast<std::uintptr_t>(table[2])!=base+(vertex?0x19d0u:0x24b0u))return false;
    static_assert(sizeof(release)==sizeof(table[2]));std::memcpy(&release,&table[2],sizeof release);
    return true;
}

bool supports(Kind kind, REFIID iid) {
    if (iid == IID_IUnknown) return true;
    switch (kind) {
    case Kind::Factory: return iid == IID_IDirect3D9;
    case Kind::Device: return iid == IID_IDirect3DDevice9;
    case Kind::Texture:
        return iid == IID_IDirect3DTexture9 || iid == IID_IDirect3DBaseTexture9 || iid == IID_IDirect3DResource9;
    case Kind::CubeTexture:
        return iid == IID_IDirect3DCubeTexture9 || iid == IID_IDirect3DBaseTexture9 || iid == IID_IDirect3DResource9;
    case Kind::VolumeTexture:
        return iid == IID_IDirect3DVolumeTexture9 || iid == IID_IDirect3DBaseTexture9 || iid == IID_IDirect3DResource9;
    case Kind::Surface: return iid == IID_IDirect3DSurface9 || iid == IID_IDirect3DResource9;
    case Kind::Volume: return iid == IID_IDirect3DVolume9;
    case Kind::VertexBuffer: return iid == IID_IDirect3DVertexBuffer9 || iid == IID_IDirect3DResource9;
    case Kind::IndexBuffer: return iid == IID_IDirect3DIndexBuffer9 || iid == IID_IDirect3DResource9;
    case Kind::VertexDeclaration: return iid == IID_IDirect3DVertexDeclaration9;
    case Kind::VertexShader: return iid == IID_IDirect3DVertexShader9;
    case Kind::PixelShader: return iid == IID_IDirect3DPixelShader9;
    case Kind::StateBlock: return iid == IID_IDirect3DStateBlock9;
    case Kind::Query: return iid == IID_IDirect3DQuery9;
    case Kind::SwapChain: return iid == IID_IDirect3DSwapChain9;
    }
    return false;
}

HRESULT query(Node* node, REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!supports(node->kind, iid)) return E_NOINTERFACE;
    add_ref(node);
    *out = node->application;
    return S_OK;
}
ULONG add_ref(Node* node) {
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    return ++node->refs;
}

void discard_renderer_resources(Device* device) {
    std::vector<IUnknown*> retired;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        retired.swap(device->renderer_resources);
    }
    for (auto it = retired.rbegin(); it != retired.rend(); ++it) (*it)->Release();
}

ULONG release(Node* node) {
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const ULONG remaining = --node->refs;
        if (remaining) return remaining;
        application_nodes.erase(node->application);
        native_nodes.erase(node->identity);
        if (node->kind == Kind::Device) static_cast<Device*>(node)->retiring = true;
    }
    // Parent remains logically alive until backend destruction has completed.
    // A child native Release may internally release its native device.
    Node* parent = node->parent;
    if (node->kind == Kind::Device) {
        auto device = static_cast<Device*>(node);
        retire_geometry(device);
        retire_finite(device);
        retire_copy_depth(device, S_FALSE);
        discard_renderer_resources(device);
    }
    if (node->kind == Kind::Query) {
        auto q = static_cast<Query*>(node);
        device_of(q)->execution.query_destroyed(q->execution_query);
    }
    node->backend->Release();
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);drain_finite_retired();}
    delete node;
    // Dispatch through the application vtable: capture/observation hooks must
    // see a parent's last release even when its last owner was a child wrapper.
    if (parent) parent->application->Release();
    return 0;
}

Device* device_of(Node* node) {
    return node->kind == Kind::Device ? static_cast<Device*>(node) : static_cast<Device*>(node->parent);
}
HRESULT get_device(Node* node, IDirect3DDevice9** out) {
    if (!out) return D3DERR_INVALIDCALL;
    Device* device = device_of(node);
    add_ref(device);
    *out = static_cast<IDirect3DDevice9*>(device);
    return S_OK;
}
HRESULT get_factory(Device* node, IDirect3D9** out) {
    if (!out) return D3DERR_INVALIDCALL;
    auto factory = static_cast<Factory*>(node->parent);
    add_ref(factory);
    *out = static_cast<IDirect3D9*>(factory);
    return S_OK;
}

template<class T> T* unwrap(Device* owner, T* value) {
    if (!value) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    auto found = application_nodes.find(static_cast<IUnknown*>(value));
    // Foreign native objects are passed unchanged for the backend to validate;
    // never invoke RTTI or read a guessed wrapper layout from an unknown pointer.
    if (found == application_nodes.end()) return value;
    Node* node = found->second;
    // All supported child interfaces use their canonical primary COM address.
    // Device mismatch remains visible to the backend using the real native input.
    (void)owner;
    return static_cast<T*>(node->backend);
}

Node* allocate_node(Kind kind, IUnknown* native, Node* parent) {
    switch (kind) {
    case Kind::Factory: return new Factory(static_cast<IDirect3D9*>(native), parent);
    case Kind::Device: return new Device(static_cast<IDirect3DDevice9*>(native), parent);
    case Kind::Texture: return new Texture(static_cast<IDirect3DTexture9*>(native), parent);
    case Kind::CubeTexture: return new CubeTexture(static_cast<IDirect3DCubeTexture9*>(native), parent);
    case Kind::VolumeTexture: return new VolumeTexture(static_cast<IDirect3DVolumeTexture9*>(native), parent);
    case Kind::Surface: return new Surface(static_cast<IDirect3DSurface9*>(native), parent);
    case Kind::Volume: return new Volume(static_cast<IDirect3DVolume9*>(native), parent);
    case Kind::VertexBuffer: return new VertexBuffer(static_cast<IDirect3DVertexBuffer9*>(native), parent);
    case Kind::IndexBuffer: return new IndexBuffer(static_cast<IDirect3DIndexBuffer9*>(native), parent);
    case Kind::VertexDeclaration: return new VertexDeclaration(static_cast<IDirect3DVertexDeclaration9*>(native), parent);
    case Kind::VertexShader: return new VertexShader(static_cast<IDirect3DVertexShader9*>(native), parent);
    case Kind::PixelShader: return new PixelShader(static_cast<IDirect3DPixelShader9*>(native), parent);
    case Kind::StateBlock: return new StateBlock(static_cast<IDirect3DStateBlock9*>(native), parent);
    case Kind::Query: return new Query(static_cast<IDirect3DQuery9*>(native), parent);
    case Kind::SwapChain: return new SwapChain(static_cast<IDirect3DSwapChain9*>(native), parent);
    }
    return nullptr;
}

// Consumes owned only on success, including when another live wrapper exists.
HRESULT adopt(Node* parent, Kind kind, IUnknown* owned, REFIID iid, void** out,
              const Options* options = nullptr) noexcept {
    if (!out || !owned) return D3DERR_INVALIDCALL;
    *out = nullptr;
    IUnknown* identity = nullptr;
    HRESULT hr = owned->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
    if (FAILED(hr)) return hr;
    identity->Release(); // borrowed key remains alive through owned
    Node* fresh = nullptr;
    Node* existing = nullptr;
    try {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        auto found = native_nodes.find(identity);
        if (found != native_nodes.end()) {
            existing = found->second;
            if (!supports(existing->kind, iid) || existing->parent != parent) return E_NOINTERFACE;
            if (kind == Kind::Factory && options &&
                (static_cast<Factory*>(existing)->options.capture_auto_depth != options->capture_auto_depth ||
                 static_cast<Factory*>(existing)->options.track_buffer_writes != options->track_buffer_writes ||
                 static_cast<Factory*>(existing)->options.track_execution_state != options->track_execution_state ||
                 static_cast<Factory*>(existing)->options.capture_finite_positions != options->capture_finite_positions ||
                 static_cast<Factory*>(existing)->options.finite_payload_budget != options->finite_payload_budget ||
                 static_cast<Factory*>(existing)->options.finite_sidecar_limit != options->finite_sidecar_limit))
                return E_INVALIDARG; // Never silently reconfigure a live factory.
            ++existing->refs;
            *out = existing->application;
        } else {
            if (!supports(kind, iid)) return E_NOINTERFACE;
            fresh = allocate_node(kind, owned, parent);
            if (options && kind == Kind::Factory) static_cast<Factory*>(fresh)->options = *options;
            if (options && kind == Kind::Device) static_cast<Device*>(fresh)->options = *options;
            fresh->identity = identity;
            native_nodes.emplace(identity, fresh);
            try { application_nodes.emplace(fresh->application, fresh); }
            catch (...) { native_nodes.erase(identity); throw; }
            if (parent) ++parent->refs;
            *out = fresh->application;
        }
    } catch (const std::bad_alloc&) {
        delete fresh; // constructors/destructors do not own until adoption succeeds
        return E_OUTOFMEMORY;
    } catch (...) {
        delete fresh;
        return E_FAIL;
    }
    if (existing) owned->Release(); // redundant getter reference is consumed
    return S_OK;
}

// Native-resource private bytes hold no interface pointers or ownership edges.
// A failure latches the DEVICE unknown for its whole lifetime (including Reset),
// so stale managed-resource records can never silently regain a known revision.
const GUID buffer_content_guid = {0x0cdb7df1,0xd3ca,0x4c69,{0x9a,0x4f,0x6e,0x28,0x33,0xf2,0x95,0x68}};
struct BufferMetadata {
    std::uint32_t magic = 0x58334252, version = 1;
    std::uint64_t revision = 0;
    std::uint32_t pending = 0;
    DWORD flags = 0;
    std::uint32_t ambiguous = 0;
};
static_assert(std::is_trivially_copyable_v<BufferMetadata>);

void fail_buffer_tracking(Device* device, HRESULT status) {
    if (SUCCEEDED(device->buffer_tracking_status)) {
        device->buffer_tracking_status = FAILED(status) ? status : E_FAIL;
        retire_finite(device,true,FiniteEvidenceReason::TrackingUnavailable);
    }
}
HRESULT read_buffer_metadata(Device* device, IDirect3DResource9* native, BufferMetadata& value) {
    if (FAILED(device->buffer_tracking_status)) return device->buffer_tracking_status;
    DWORD size = sizeof(value);
    const HRESULT hr = native->GetPrivateData(buffer_content_guid, &value, &size);
    if (hr == D3DERR_NOTFOUND) return hr;
    if (FAILED(hr)) { fail_buffer_tracking(device, hr); return hr; }
    if (size != sizeof(value) || value.magic != 0x58334252 || value.version != 1 || value.ambiguous > 1) {
        fail_buffer_tracking(device, E_FAIL); return E_FAIL;
    }
    return S_OK;
}
void write_buffer_metadata(Device* device, IDirect3DResource9* native, const BufferMetadata& value) {
    const HRESULT hr = native->SetPrivateData(buffer_content_guid, &value, sizeof(value), 0);
    if (FAILED(hr)) fail_buffer_tracking(device, hr);
}
void initialize_buffer(Device* device, IDirect3DResource9* native) {
    PreserveExecution preserve;
    if (!device->options.track_buffer_writes) return;
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    if (FAILED(device->buffer_tracking_status)) return;
    // Only a successfully created new buffer establishes a known revision zero.
    // Getters/adoption of an untagged pre-existing resource never do so.
    write_buffer_metadata(device, native, BufferMetadata{});
    attach_finite(device, native);
}
enum class BufferEvent { Lock, Unlock, FailedUnlock, ProcessVertices };
void record_buffer_event(Device* device, IDirect3DResource9* native, BufferEvent event, DWORD flags = 0) {
    if (!device->options.track_buffer_writes) return;
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    BufferMetadata value{};
    const HRESULT hr = read_buffer_metadata(device, native, value);
    if (hr == D3DERR_NOTFOUND) { value = {}; value.ambiguous = 1; }
    else if (FAILED(hr)) return;
    if (event == BufferEvent::Lock) {
        if (value.pending) value.ambiguous = 1; // Nested order/access is not inferred.
        if (value.pending == std::numeric_limits<std::uint32_t>::max()) value.ambiguous = 1;
        else ++value.pending;
        value.flags = flags;
    } else if (event == BufferEvent::Unlock) {
        if (value.pending) --value.pending;
        else value.ambiguous = 1; // Successful unlock without an observed lock.
    } else if (event == BufferEvent::FailedUnlock) value.ambiguous = 1;
    else if (value.pending) value.ambiguous = 1;
    if ((event == BufferEvent::Lock && !(flags & D3DLOCK_READONLY)) || event == BufferEvent::ProcessVertices) {
        if (value.revision == std::numeric_limits<std::uint64_t>::max()) value.ambiguous = 1;
        else ++value.revision;
    }
    write_buffer_metadata(device, native, value);
}
HRESULT buffer_lock(Node* node, UINT offset, UINT size, void** data, DWORD flags) {
    ExecutionState incoming;
    auto* device=device_of(node);
    // Invalidate any prior write transaction before a nested/unsupported attempt.
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex);
      SideReference hold{acquire_finite(device,static_cast<IDirect3DResource9*>(node->backend))};
      if(hold.value&&hold.value->writing)invalidate_finite(hold.value,FiniteEvidenceReason::Pending); }
    incoming.restore();
    const HRESULT hr = node->kind == Kind::VertexBuffer
        ? static_cast<IDirect3DVertexBuffer9*>(node->backend)->Lock(offset, size, data, flags)
        : static_cast<IDirect3DIndexBuffer9*>(node->backend)->Lock(offset, size, data, flags);
    ExecutionState outgoing;
    observe_result(device,hr);
    if (SUCCEEDED(hr)) {
        auto* resource=static_cast<IDirect3DResource9*>(node->backend);
        record_buffer_event(device,resource,BufferEvent::Lock,flags);
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        SideReference hold{acquire_finite(device,resource)};auto* side=hold.value;
        if(side){
            BufferMetadata metadata{};
            if(FAILED(read_buffer_metadata(device,resource,metadata)))invalidate_finite(side,FiniteEvidenceReason::TrackingUnavailable);
            else if(metadata.ambiguous||metadata.pending!=1)invalidate_finite(side,FiniteEvidenceReason::Ambiguous);
            else if(flags&D3DLOCK_READONLY){ /* Ordinary read preserves evidence; pending metadata blocks queries. */ }
            else {
                ++side->owner->stats.uploads; side->revision=metadata.revision;side->flags=flags;
                managed_upload::Window window{};
                if(!own_buffer_slots(node))invalidate_finite(side,FiniteEvidenceReason::NativeContract);
                else if(flags!=0&&flags!=D3DLOCK_NOSYSLOCK)invalidate_finite(side,FiniteEvidenceReason::UnsupportedWrite);
                else if(!data||!*data||!finite_window(side,offset,size,flags,*data,&window))invalidate_finite(side,FiniteEvidenceReason::MappingMismatch);
                else if(reserve_finite(side)&&side->evidence.begin_write(metadata.revision,window.offset,window.size,EvidenceWriteMode::Preserving)){
                    side->writing=true;side->mapping=*data;side->offset=window.offset;side->length=window.size;
                    side->flags=flags;side->thread=GetCurrentThreadId();side->generation=side->owner->stats.generation;
                    side->reason=FiniteEvidenceReason::Pending;
                }else invalidate_finite(side,side->reason);
            }
        }
    }
    outgoing.restore();return hr;
}
HRESULT buffer_unlock(Node* node) {
    ExecutionState incoming;
    Device* device=device_of(node);auto* resource=static_cast<IDirect3DResource9*>(node->backend);
    SideReference hold;bool staged=false;
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex);
      hold.value=acquire_finite(device,resource);auto* side=hold.value;
      if(side&&side->writing){
        BufferMetadata metadata{};managed_upload::Window window{};
        FiniteEvidenceReason failure=FiniteEvidenceReason::None;
        if(FAILED(read_buffer_metadata(device,resource,metadata)))failure=FiniteEvidenceReason::TrackingUnavailable;
        else if(metadata.ambiguous||metadata.pending!=1||metadata.revision!=side->revision)failure=FiniteEvidenceReason::Ambiguous;
        else if(side->thread!=GetCurrentThreadId())failure=FiniteEvidenceReason::ThreadMismatch;
        else if(side->generation!=side->owner->stats.generation)failure=FiniteEvidenceReason::DeviceUnavailable;
        else if(!own_buffer_slots(node))failure=FiniteEvidenceReason::NativeContract;
        else if(!finite_window(side,side->offset,side->length,side->flags,side->mapping,&window))failure=FiniteEvidenceReason::MappingMismatch;
        if(failure==FiniteEvidenceReason::None){
            LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
            asm volatile("mfence" ::: "memory");
            const auto before=side->evidence.counters();
            staged=side->evidence.stage_mapped(side->revision,side->mapping,side->length);
            QueryPerformanceCounter(&end);++side->owner->stats.scans;
            side->owner->stats.classified_bytes+=side->evidence.counters().classified_bytes-before.classified_bytes;
            side->owner->stats.scan_ticks+=static_cast<std::uint64_t>(end.QuadPart-begin.QuadPart);
            if(!staged)invalidate_finite(side,FiniteEvidenceReason::MappingMismatch);
        }else invalidate_finite(side,failure);
      }
    }
    incoming.restore();
    const HRESULT hr = node->kind == Kind::VertexBuffer
        ? static_cast<IDirect3DVertexBuffer9*>(node->backend)->Unlock()
        : static_cast<IDirect3DIndexBuffer9*>(node->backend)->Unlock();
    ExecutionState outgoing;
    observe_result(device,hr);
    record_buffer_event(device,resource,SUCCEEDED(hr)?BufferEvent::Unlock:BufferEvent::FailedUnlock);
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex);
      auto* side=hold.value;
      if(side&&staged){
        BufferMetadata metadata{};
        SideReference reservation{acquire_finite(device,resource)};
        const bool valid=hr==S_OK&&reservation.value==side&&side->owner->healthy&&
            SUCCEEDED(device->buffer_tracking_status)&&SUCCEEDED(read_buffer_metadata(device,resource,metadata))&&
            !metadata.ambiguous&&!metadata.pending&&metadata.revision==side->revision&&
            side->generation==side->owner->stats.generation&&side->thread==GetCurrentThreadId()&&
            own_buffer_slots(node)&&finite_closed(side);
        if(side->evidence.finish_write(side->revision,valid)&&valid){++side->owner->stats.publications;side->reason=FiniteEvidenceReason::None;}
        else invalidate_finite(side,FAILED(hr)?FiniteEvidenceReason::UnlockFailed:FiniteEvidenceReason::Ambiguous);
        side->mapping=nullptr;side->writing=false;
      }else if(side&&FAILED(hr))invalidate_finite(side,FiniteEvidenceReason::UnlockFailed);
    }
    if(hold.value){hold.value->Release();hold.value=nullptr;}
    outgoing.restore();return hr;
}
HRESULT buffer_private_result(Node* node, REFGUID guid, HRESULT hr) {
    PreserveExecution preserve;Device* device=device_of(node);
    if(SUCCEEDED(hr)&&(guid==buffer_content_guid||guid==finite_sidecar_guid)){
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        if(device->options.track_buffer_writes&&guid==buffer_content_guid)fail_buffer_tracking(device,E_FAIL);
        if(device->options.capture_finite_positions)retire_finite(device,true,FiniteEvidenceReason::MetadataTampered);
    }
    return observe_result(device,hr);
}
HRESULT process_vertices(Device* node, UINT first, UINT destination, UINT count,
    IDirect3DVertexBuffer9* buffer, IDirect3DVertexDeclaration9* declaration, DWORD flags) {
    ExecutionState incoming;auto native=unwrap(node,buffer);auto native_declaration=unwrap(node,declaration);
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex);
      SideReference side{native?acquire_finite(node,native):nullptr};
      if(side.value)invalidate_finite(side.value,FiniteEvidenceReason::ProcessVertices); }
    incoming.restore();
    const HRESULT hr=node->native_->ProcessVertices(first,destination,count,native,native_declaration,flags);
    ExecutionState outgoing;
    if(SUCCEEDED(hr)&&native)record_buffer_event(node,native,BufferEvent::ProcessVertices);
    observe_result(node,hr);
    outgoing.restore();return hr;
}

// Factory/device wrappers deliberately do not advertise Ex or backend-private
// interfaces. Reject Ex creation before exposing the normal wrapper boundary.
bool has_ex(IUnknown* native, REFIID iid) {
    IUnknown* extended = nullptr;
    const HRESULT hr = native->QueryInterface(iid, reinterpret_cast<void**>(&extended));
    if (extended) extended->Release();
    return SUCCEEDED(hr);
}

template<class T> struct Traits;
#define X3M_TRAIT(type, tag) template<> struct Traits<type> { \
    static constexpr Kind kind = Kind::tag; \
    static const GUID& iid() { return IID_##type; } \
};
X3M_TRAIT(IDirect3DTexture9, Texture)
X3M_TRAIT(IDirect3DCubeTexture9, CubeTexture)
X3M_TRAIT(IDirect3DVolumeTexture9, VolumeTexture)
X3M_TRAIT(IDirect3DSurface9, Surface)
X3M_TRAIT(IDirect3DVolume9, Volume)
X3M_TRAIT(IDirect3DVertexBuffer9, VertexBuffer)
X3M_TRAIT(IDirect3DIndexBuffer9, IndexBuffer)
X3M_TRAIT(IDirect3DVertexDeclaration9, VertexDeclaration)
X3M_TRAIT(IDirect3DVertexShader9, VertexShader)
X3M_TRAIT(IDirect3DPixelShader9, PixelShader)
X3M_TRAIT(IDirect3DStateBlock9, StateBlock)
X3M_TRAIT(IDirect3DQuery9, Query)
X3M_TRAIT(IDirect3DSwapChain9, SwapChain)
#undef X3M_TRAIT

template<class T> HRESULT output(Device* owner, HRESULT hr, T* owned, T** out) {
    if (owned == untouched_output<T>()) return observe_result(owner, hr);
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return observe_result(owner, hr); }
    const HRESULT wrapped = adopt(owner, Traits<T>::kind, owned, Traits<T>::iid(), reinterpret_cast<void**>(out));
    if (FAILED(wrapped)) owned->Release();
    return observe_result(owner, FAILED(wrapped) ? wrapped : hr);
}
template<> HRESULT output(Device* owner, HRESULT hr, IDirect3DBaseTexture9* owned,
                          IDirect3DBaseTexture9** out) {
    if (owned == untouched_output<IDirect3DBaseTexture9>()) return observe_result(owner, hr);
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return observe_result(owner, hr); }
    Kind kind;
    switch (owned->GetType()) {
    case D3DRTYPE_TEXTURE: kind = Kind::Texture; break;
    case D3DRTYPE_CUBETEXTURE: kind = Kind::CubeTexture; break;
    case D3DRTYPE_VOLUMETEXTURE: kind = Kind::VolumeTexture; break;
    default: owned->Release(); return observe_result(owner, E_NOINTERFACE);
    }
    const HRESULT wrapped = adopt(owner, kind, owned, IID_IDirect3DBaseTexture9, reinterpret_cast<void**>(out));
    if (FAILED(wrapped)) owned->Release();
    return observe_result(owner, FAILED(wrapped) ? wrapped : hr);
}

HRESULT get_container(Node* node, REFIID iid, void** out) {
    Device* owner = device_of(node);
    IUnknown* owned = untouched_output<IUnknown>();
    HRESULT hr;
    if (node->kind == Kind::Surface)
        hr = static_cast<IDirect3DSurface9*>(node->backend)->GetContainer(iid, out ? reinterpret_cast<void**>(&owned) : nullptr);
    else
        hr = static_cast<IDirect3DVolume9*>(node->backend)->GetContainer(iid, out ? reinterpret_cast<void**>(&owned) : nullptr);
    if (owned == untouched_output<IUnknown>()) return observe_result(owner, hr);
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return observe_result(owner, hr); }
    Device* device = device_of(node);
    // Classify only known COM interfaces, and always return a canonical wrapper.
    // Unknown requested IIDs never escape as backend interfaces.
    struct Candidate { Kind kind; const IID* iid; };
    const Candidate candidates[] = {
        {Kind::Device, &IID_IDirect3DDevice9}, {Kind::Factory, &IID_IDirect3D9},
        {Kind::Texture, &IID_IDirect3DTexture9}, {Kind::CubeTexture, &IID_IDirect3DCubeTexture9},
        {Kind::VolumeTexture, &IID_IDirect3DVolumeTexture9}, {Kind::SwapChain, &IID_IDirect3DSwapChain9}
    };
    for (const auto& candidate : candidates) {
        if (!supports(candidate.kind, iid)) continue;
        IUnknown* typed = nullptr;
        const HRESULT typed_result = owned->QueryInterface(*candidate.iid, reinterpret_cast<void**>(&typed));
        if (FAILED(typed_result)) {
            if (typed) typed->Release();
            if (typed_result == D3DERR_DEVICELOST || typed_result == D3DERR_DEVICENOTRESET) {
                owned->Release(); return observe_result(owner, typed_result);
            }
            continue;
        }
        if (!typed) { owned->Release(); return observe_result(owner, E_FAIL); }
        Node* parent = candidate.kind == Kind::Factory ? nullptr :
                       candidate.kind == Kind::Device ? device->parent : static_cast<Node*>(device);
        const HRESULT wrapped = adopt(parent, candidate.kind, typed, iid, out);
        if (FAILED(wrapped)) typed->Release();
        owned->Release();
        return observe_result(owner, FAILED(wrapped) ? wrapped : hr);
    }
    owned->Release();
    return observe_result(owner, E_NOINTERFACE);
}

HRESULT create_device(Factory* node, UINT adapter, D3DDEVTYPE type, HWND window,
                      DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    const D3DPRESENT_PARAMETERS requested = pp ? *pp : D3DPRESENT_PARAMETERS{};
    IDirect3DDevice9* owned = untouched_output<IDirect3DDevice9>();
    HRESULT hr = node->native_->CreateDevice(adapter, type, window, flags, pp, out ? &owned : nullptr);
    if (owned == untouched_output<IDirect3DDevice9>()) return hr;
    if (out) *out = nullptr;
    if (FAILED(hr) || !owned) { if (owned) owned->Release(); return hr; }
    if (has_ex(owned, IID_IDirect3DDevice9Ex)) { owned->Release(); return E_NOINTERFACE; }
    const HRESULT wrapped = adopt(node, Kind::Device, owned, IID_IDirect3DDevice9, reinterpret_cast<void**>(out), &node->options);
    if (FAILED(wrapped)) owned->Release();
    else {
        auto device = static_cast<Device*>(*out);
        device->execution.initialize(device->options.track_execution_state);
        initialize_finite(device);
        initialize_copy_depth(device, requested);
    }
    return FAILED(wrapped) ? wrapped : hr;
}

HRESULT reset_device(Device* node, D3DPRESENT_PARAMETERS* pp) {
    node->execution.before_reset();
    const D3DPRESENT_PARAMETERS requested = pp ? *pp : D3DPRESENT_PARAMETERS{};
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->resetting = true; }
    retire_geometry(node);
    retire_finite(node);
    retire_copy_depth(node, S_FALSE);
    discard_renderer_resources(node);
    const HRESULT hr = node->native_->Reset(pp);
    node->execution.after_reset(hr);
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        node->resetting = false;
        node->lost = FAILED(hr);
    }
    if (SUCCEEDED(hr)) {
        node->recording_state_block = false;
        if(node->finite_owner&&!node->finite_owner->permanent){node->finite_owner->healthy=true;node->finite_owner->stats.active=true;node->finite_owner->stats.status=S_OK;}
        initialize_copy_depth(node, requested);
    } else {
        node->copy_depth.view.status = hr;
    }
    return hr;
}
HRESULT observe_result(Device* node, HRESULT hr) {
    node->execution.observe_result(hr);
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) {
        { std::lock_guard<std::recursive_mutex> lock(registry_mutex); node->lost = true; }
        retire_geometry(node);
        retire_finite(node);
        retire_copy_depth(node, hr);
        discard_renderer_resources(node);
    }
    return hr;
}

bool same_surface(IDirect3DSurface9* left, IDirect3DSurface9* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    IUnknown *a = nullptr, *b = nullptr;
    const HRESULT ha = left->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a));
    const HRESULT hb = right->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b));
    const bool equal = SUCCEEDED(ha) && SUCCEEDED(hb) && a == b;
    if (a) a->Release();
    if (b) b->Release();
    return equal;
}

HRESULT clear_device(Device* node, DWORD count, const D3DRECT* rects, DWORD flags,
                     D3DCOLOR color, float depth, DWORD stencil) {
    HRESULT source_status = S_OK;
    const bool original_bound = node->copy_depth.view.available &&
        (flags & D3DCLEAR_ZBUFFER) && copy_source_bound(node, &source_status);
    // Snapshot bookkeeping must not change an application's Clear result.
    if (FAILED(source_status)) node->copy_depth.view.status = source_status;
    const HRESULT hr = node->native_->Clear(count, rects, flags, color, depth, stencil);
    if (SUCCEEDED(hr) && original_bound) ++node->copy_depth.view.source_epoch;
    // Preserve the app's own HRESULT, but a loss observed by our bookkeeping
    // query must still invalidate native snapshots and borrowed resource views.
    if (FAILED(source_status)) observe_result(node, source_status);
    observe_result(node, hr);
    return hr;
}

HRESULT scene_transition(Device* node, bool begin) {
    const HRESULT hr = begin ? node->native_->BeginScene() : node->native_->EndScene();
    if (begin) node->execution.begin_scene(hr); else node->execution.end_scene(hr);
    return observe_result(node, hr);
}
HRESULT create_query(Device* node, D3DQUERYTYPE type, IDirect3DQuery9** out) {
    IDirect3DQuery9* owned = untouched_output<IDirect3DQuery9>();
    const HRESULT hr = node->native_->CreateQuery(type, out ? &owned : nullptr);
    const HRESULT result = output(node, hr, owned, out);
    if (SUCCEEDED(hr) && SUCCEEDED(result) && owned != untouched_output<IDirect3DQuery9>() && out && *out) {
        auto wrapped = static_cast<Query*>(*out);
        node->execution.query_created(wrapped->execution_query, static_cast<std::uint32_t>(type));
        if (hr != S_OK) node->execution.unknown_native_execution();
    }
    return result;
}
HRESULT issue_query(Query* node, DWORD flags) {
    const HRESULT hr = node->native_->Issue(flags);
    auto owner = device_of(node);
    owner->execution.query_issue(node->execution_query, flags, hr);
    return observe_result(owner, hr);
}
HRESULT begin_state_block(Device* node) {
    const HRESULT hr = node->native_->BeginStateBlock();
    node->execution.begin_stateblock(hr);
    if (SUCCEEDED(hr)) node->recording_state_block = true;
    return observe_result(node, hr);
}

HRESULT end_state_block(Device* node, IDirect3DStateBlock9** out) {
    IDirect3DStateBlock9* owned = untouched_output<IDirect3DStateBlock9>();
    const HRESULT hr = node->native_->EndStateBlock(out ? &owned : nullptr);
    node->execution.end_stateblock(hr);
    if (SUCCEEDED(hr)) node->recording_state_block = false;
    const HRESULT result = output(node, hr, owned, out);
    // output() cleans/adopts before observing native loss.
    return result;
}

bool copy_source_bound(Device* node, HRESULT* query_status) {
    if (query_status) *query_status = S_OK;
    if (!node->copy_depth.original || node->lost) return false;
    IDirect3DSurface9* actual = nullptr;
    const HRESULT hr = node->native_->GetDepthStencilSurface(&actual);
    if (query_status) *query_status = hr == D3DERR_NOTFOUND ? S_OK : hr;
    const bool bound = SUCCEEDED(hr) && same_surface(actual, node->copy_depth.original);
    if (actual) actual->Release();
    return bound;
}

void retire_copy_depth(Device* node, HRESULT status) {
    auto& copy = node->copy_depth;
    auto original = copy.original;
    auto texture = copy.view.texture;
    copy.original = nullptr;
    copy.view.texture = nullptr;
    copy.view.available = false;
    copy.view.copy_valid = false;
    copy.view.source_bound = false;
    copy.view.source_epoch = 0;
    copy.view.copy_epoch = 0;
    copy.view.status = status;
    if (texture) ++copy.view.generation;
    if (texture) texture->Release();
    if (original) original->Release();
}

void initialize_copy_depth(Device* node, const D3DPRESENT_PARAMETERS& requested) {
    auto& copy = node->copy_depth;
    copy.view.requested = node->options.capture_auto_depth;
    copy.view.status = S_FALSE;
    copy.view.source_desc = {};
    if (!copy.view.requested || !requested.EnableAutoDepthStencil ||
        requested.AutoDepthStencilFormat != D3DFMT_D24X8 ||
        requested.MultiSampleType != D3DMULTISAMPLE_NONE) return;
    IDirect3DSurface9* original = nullptr;
    IDirect3DTexture9* texture = nullptr;
    IDirect3DBaseTexture9* sampled = nullptr;
    auto attempt = [&]() -> HRESULT {
        HRESULT hr = node->native_->GetDepthStencilSurface(&original);
        if (FAILED(hr)) return hr;
        if (!original) return E_FAIL;
        D3DSURFACE_DESC desc{};
        hr = original->GetDesc(&desc);
        if (FAILED(hr)) return hr;
        copy.view.source_desc = desc;
        if (desc.Format != D3DFMT_D24X8 || desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
            desc.Pool != D3DPOOL_DEFAULT || !(desc.Usage & D3DUSAGE_DEPTHSTENCIL)) return S_FALSE;
        // The exact save/restore calls are required on the real pure device;
        // unsupported getters decline capture before application rendering.
        hr = node->native_->GetTexture(0, &sampled);
        if (FAILED(hr)) return hr;
        DWORD point_size = 0;
        hr = node->native_->GetRenderState(D3DRS_POINTSIZE, &point_size);
        if (FAILED(hr)) return hr;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        D3DDISPLAYMODE mode{};
        hr = node->native_->GetCreationParameters(&creation);
        if (FAILED(hr)) return hr;
        auto factory = static_cast<Factory*>(node->parent);
        hr = factory->native_->GetAdapterDisplayMode(creation.AdapterOrdinal, &mode);
        if (FAILED(hr)) return hr;
        hr = factory->native_->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
            D3DFORMAT(MAKEFOURCC('R', 'E', 'S', 'Z')));
        if (FAILED(hr)) return hr;
        hr = factory->native_->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_D24X8);
        if (FAILED(hr)) return hr;
        return node->native_->CreateTexture(desc.Width, desc.Height, 1,
            D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24X8, D3DPOOL_DEFAULT, &texture, nullptr);
    };
    const HRESULT hr = attempt();
    copy.view.status = hr;
    if (hr == S_OK) {
        copy.original = original;
        copy.view.texture = texture;
        copy.view.available = true;
        copy.view.copy_valid = false;
        copy.view.source_bound = true;
        copy.view.source_epoch = 0;
        copy.view.copy_epoch = 0;
        ++copy.view.generation;
    } else {
        if (texture) texture->Release();
        if (original) original->Release();
    }
    if (sampled) sampled->Release();
    // Optional setup can observe device loss too. Retire only after temporary
    // native references are released, and reject later renderer adoption.
    observe_result(node, hr);
}

HRESULT copy_depth(Device* node) {
    auto& copy = node->copy_depth;
    auto reject = [&](HRESULT hr) {
        copy.view.status = hr;
        // Call only after any saved native references have been released.
        // A pre-mutation loss invalidates an earlier successful snapshot too.
        return observe_result(node, hr);
    };
    if (node->lost) return reject(copy.view.status == D3DERR_DEVICENOTRESET
        ? D3DERR_DEVICENOTRESET : D3DERR_DEVICELOST);
    if (node->resetting || node->retiring || node->recording_state_block ||
        !copy.view.available) return reject(D3DERR_INVALIDCALL);
    HRESULT bound_status = S_OK;
    const bool bound = copy_source_bound(node, &bound_status);
    if (FAILED(bound_status)) return reject(bound_status);
    if (!bound) return reject(D3DERR_INVALIDCALL);
    IDirect3DBaseTexture9* texture0 = nullptr;
    DWORD point_size = 0;
    HRESULT hr = node->native_->GetTexture(0, &texture0);
    if (FAILED(hr)) { if (texture0) texture0->Release(); return reject(hr); }
    hr = node->native_->GetRenderState(D3DRS_POINTSIZE, &point_size);
    if (FAILED(hr)) { if (texture0) texture0->Release(); return reject(hr); }

    // Verified on Preview with native D24X8 source/destination and no dummy
    // draw. Do not open/close a scene or change the original depth binding.
    copy.view.copy_valid = false;
    hr = node->native_->SetTexture(0, copy.view.texture);
    auto loss = [](HRESULT value) {
        return value == D3DERR_DEVICELOST || value == D3DERR_DEVICENOTRESET;
    };
    if (loss(hr)) {
        if (texture0) texture0->Release();
        return reject(hr); // No ordinary Set calls after observed loss.
    }
    HRESULT point_restored = S_OK;
    if (SUCCEEDED(hr)) {
        hr = node->native_->SetRenderState(D3DRS_POINTSIZE, 0x7fa05000u);
        if (loss(hr)) {
            if (texture0) texture0->Release();
            return reject(hr);
        }
        point_restored = node->native_->SetRenderState(D3DRS_POINTSIZE, point_size);
        if (loss(point_restored)) {
            if (texture0) texture0->Release();
            return reject(point_restored);
        }
    }
    const HRESULT texture_restored = node->native_->SetTexture(0, texture0);
    if (texture0) texture0->Release();
    // A restoration loss takes precedence over an earlier ordinary failure:
    // otherwise its dead-device state could leave a borrowed snapshot exposed.
    if (loss(texture_restored)) return reject(texture_restored);
    if (FAILED(hr)) return reject(hr);
    if (FAILED(point_restored)) return reject(point_restored);
    if (FAILED(texture_restored)) return reject(texture_restored);
    copy.view.copy_valid = true;
    copy.view.copy_epoch = copy.view.source_epoch;
    copy.view.status = S_OK;
    return S_OK;
}

#include "d3d9_forwarders_inc.h"
} // namespace

HRESULT wrap_factory(IDirect3D9* owned_native, IDirect3D9** out, const Options& options) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);drain_finite_retired();}
    if (!owned_native) return E_INVALIDARG;
    if ((options.capture_finite_positions&&!options.track_buffer_writes)||options.finite_payload_budget>finite_global_budget||options.finite_sidecar_limit>finite_global_sidecars) return E_INVALIDARG;
    { std::lock_guard<std::recursive_mutex> lock(registry_mutex);
      if (application_nodes.count(owned_native)) return E_INVALIDARG; }
    if (has_ex(owned_native, IID_IDirect3D9Ex)) return E_NOINTERFACE;
    return adopt(nullptr, Kind::Factory, owned_native, IID_IDirect3D9, reinterpret_cast<void**>(out), &options);
}

HRESULT get_execution_view(IDirect3DDevice9* application, ExecutionView* out) noexcept {
    if (!out) return E_POINTER;
    *out = {};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found = application_nodes.find(application);
    if (found == application_nodes.end() || found->second->kind != Kind::Device) return E_INVALIDARG;
    *out = static_cast<Device*>(found->second)->execution.view();
    return S_OK;
}
HRESULT invalidate_execution_state(IDirect3DDevice9* application) noexcept {
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found = application_nodes.find(application);
    if (found == application_nodes.end() || found->second->kind != Kind::Device) return E_INVALIDARG;
    static_cast<Device*>(found->second)->execution.unknown_native_execution();
    return S_OK;
}

IDirect3DDevice9* borrowed_native_device(IDirect3DDevice9* wrapped) noexcept {
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found = application_nodes.find(wrapped);
    return found != application_nodes.end() && found->second->kind == Kind::Device
        ? static_cast<IDirect3DDevice9*>(found->second->backend) : nullptr;
}

IDirect3DVertexBuffer9* borrowed_native_buffer_for_lock_contract(IDirect3DVertexBuffer9* wrapped) noexcept {
    return buffer_contract_endpoint(wrapped, Kind::VertexBuffer, vertex_buffer_slots);
}
IDirect3DIndexBuffer9* borrowed_native_buffer_for_lock_contract(IDirect3DIndexBuffer9* wrapped) noexcept {
    return buffer_contract_endpoint(wrapped, Kind::IndexBuffer, index_buffer_slots);
}

HRESULT get_buffer_content_view(IDirect3DResource9* application, BufferContentView* out) noexcept {
    if (!out) return E_POINTER;
    *out = {};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found = application_nodes.find(application);
    if (found == application_nodes.end() ||
        (found->second->kind != Kind::VertexBuffer && found->second->kind != Kind::IndexBuffer)) return E_INVALIDARG;
    Node* node = found->second;
    Device* device = device_of(node);
    out->requested = device->options.track_buffer_writes;
    if (!out->requested) return S_OK;
    BufferMetadata value{};
    const HRESULT hr = read_buffer_metadata(device, static_cast<IDirect3DResource9*>(node->backend), value);
    out->status = hr;
    if (FAILED(hr)) { out->ambiguous = true; return S_OK; }
    out->revision = value.revision;
    out->pending_locks = value.pending;
    out->last_lock_flags = value.flags;
    out->ambiguous = value.ambiguous != 0;
    out->known = !out->ambiguous && !value.pending;
    out->status = out->known ? S_OK : S_FALSE;
    return S_OK;
}

const char* finite_evidence_reason_name(FiniteEvidenceReason reason) noexcept {
    static const char* names[]={"none","disabled","unrecognized","device_unavailable","tracking_unavailable","missing_allocation","revision_mismatch","pending","ambiguous","native_contract","unsupported_write","thread_mismatch","mapping_mismatch","unlock_failed","invalid_layout","invalid_range","unknown_cells","nonfinite","index_unknown","allocation_failure","budget","metadata_tampered","process_vertices"};
    const auto index=static_cast<unsigned>(reason);return index<finite_evidence_reason_count?names[index]:"invalid";
}
namespace {
FiniteEvidenceReason finite_native_ready(Device* device,IDirect3DResource9* resource,std::uint64_t expected,SideReference& hold,BufferMetadata& metadata) {
    if(!device->options.capture_finite_positions)return FiniteEvidenceReason::Disabled;
    if(device->retiring||device->resetting||device->lost)return FiniteEvidenceReason::DeviceUnavailable;
    if(FAILED(device->buffer_tracking_status))return FiniteEvidenceReason::TrackingUnavailable;
    auto owner=device->finite_owner;
    if(!owner)return FiniteEvidenceReason::AllocationFailure;
    ++owner->stats.queries;
    if(!owner->healthy)return owner->permanent?FiniteEvidenceReason::MetadataTampered:FiniteEvidenceReason::DeviceUnavailable;
    if(FAILED(read_buffer_metadata(device,resource,metadata)))return FiniteEvidenceReason::TrackingUnavailable;
    if(metadata.pending)return FiniteEvidenceReason::Pending;
    if(metadata.ambiguous)return FiniteEvidenceReason::Ambiguous;
    if(!expected||expected!=metadata.revision)return FiniteEvidenceReason::RevisionMismatch;
    FiniteEvidenceReason acquisition_failure=FiniteEvidenceReason::None;
    hold.value=acquire_finite(device,resource,FiniteAcquireMode::Closed,&acquisition_failure);
    if(!hold.value)return !owner->healthy?FiniteEvidenceReason::MetadataTampered:
        acquisition_failure!=FiniteEvidenceReason::None?acquisition_failure:FiniteEvidenceReason::MissingAllocation;
    if(hold.value->reason!=FiniteEvidenceReason::None)return hold.value->reason;
    return FiniteEvidenceReason::None;
}
FiniteEvidenceReason finite_query_ready(Node* node,std::uint64_t expected,SideReference& hold,BufferMetadata& metadata){
    if(!own_buffer_slots(node))return FiniteEvidenceReason::NativeContract;
    return finite_native_ready(device_of(node),static_cast<IDirect3DResource9*>(node->backend),expected,hold,metadata);
}
}
HRESULT get_finite_position_view(IDirect3DVertexBuffer9* application,const FinitePositionRequest& request,FinitePositionView* out) noexcept {
    PreserveExecution preserve;if(!out)return E_POINTER;*out={};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    auto found=application_nodes.find(application);
    if(found==application_nodes.end()||found->second->kind!=Kind::VertexBuffer)return E_INVALIDARG;
    Node* node=found->second;Device* device=device_of(node);out->requested=device->options.capture_finite_positions;
    TickScope elapsed(device->finite_owner?&device->finite_owner->stats.query_ticks:nullptr);
    SideReference hold;BufferMetadata metadata{};
    out->reason=finite_query_ready(node,request.expected_revision,hold,metadata);out->revision=metadata.revision;
    auto owner=device->finite_owner;if(owner)out->generation=owner->stats.generation;
    if(out->reason==FiniteEvidenceReason::None){
        PositionStorage storage=PositionStorage::Unknown;
        if(request.position_type==D3DDECLTYPE_FLOAT3)storage=PositionStorage::Float3;
        else if(request.position_type==D3DDECLTYPE_FLOAT16_4)storage=PositionStorage::Half4;
        if(storage==PositionStorage::Unknown)out->reason=FiniteEvidenceReason::InvalidLayout;
        else {
            const auto before=hold.value->evidence.counters();
            out->state=hold.value->evidence.query_positions(request.expected_revision,{request.stream_offset,request.stride,request.position_offset,request.first_vertex,request.vertex_count,storage});
            const auto after=hold.value->evidence.counters();
            owner->stats.query_cache_hits+=after.cache_hits-before.cache_hits;
            owner->stats.position_components+=after.position_components-before.position_components;
            out->reason=out->state==FiniteStatus::Finite?FiniteEvidenceReason::None:
                out->state==FiniteStatus::NonFinite?FiniteEvidenceReason::NonFinite:FiniteEvidenceReason::UnknownCells;
            out->status=out->state==FiniteStatus::Unknown?S_FALSE:S_OK;
        }
    }
    if(owner)finite_reason(*owner,out->reason);
    return S_OK;
}
HRESULT get_index_range_view(IDirect3DIndexBuffer9* application,const IndexRangeRequest& request,IndexRangeView* out) noexcept {
    PreserveExecution preserve;if(!out)return E_POINTER;*out={};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    auto found=application_nodes.find(application);
    if(found==application_nodes.end()||found->second->kind!=Kind::IndexBuffer)return E_INVALIDARG;
    Node* node=found->second;Device* device=device_of(node);out->requested=device->options.capture_finite_positions;
    TickScope elapsed(device->finite_owner?&device->finite_owner->stats.query_ticks:nullptr);
    SideReference hold;BufferMetadata metadata{};
    out->reason=finite_query_ready(node,request.expected_revision,hold,metadata);out->revision=metadata.revision;
    auto owner=device->finite_owner;if(owner)out->generation=owner->stats.generation;
    if(out->reason==FiniteEvidenceReason::None){
        if(request.format!=hold.value->contract.format)out->reason=FiniteEvidenceReason::InvalidLayout;
        else {
            const auto bounds=hold.value->evidence.query_indices(request.expected_revision,request.start_index,request.index_count);
            out->known=bounds.known;out->minimum=bounds.minimum;out->maximum=bounds.maximum;out->exact_range=bounds.exact_range;
            out->reason=bounds.known?FiniteEvidenceReason::None:FiniteEvidenceReason::IndexUnknown;out->status=bounds.known?S_OK:S_FALSE;
        }
    }
    if(owner)finite_reason(*owner,out->reason);
    return S_OK;
}
HRESULT get_finite_upload_statistics(IDirect3DDevice9* application,FiniteUploadStatistics* out) noexcept {
    PreserveExecution preserve;if(!out)return E_POINTER;*out={};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    auto found=application_nodes.find(application);
    if(found==application_nodes.end()||found->second->kind!=Kind::Device)return E_INVALIDARG;
    auto* device=static_cast<Device*>(found->second);
    drain_finite_retired();
    if(device->finite_owner)*out=device->finite_owner->stats;
    else {out->requested=device->options.capture_finite_positions;out->status=device->finite_status;}
    out->global_payload_bytes=finite_payload_used;out->global_sidecars=finite_sidecars_used;return S_OK;
}

namespace {
HRESULT validate_geometry_record(Device* device,GeometryLeaseRecord& record,std::uint64_t generation,
                                 GeometryLeaseView& view,bool retain_sides){
    view.generation=generation;view.positions.requested=true;view.positions.generation=generation;
    view.indices.requested=record.request.indexed;view.indices.generation=generation;
    if(!geometry_owner_ready(device)||device->finite_owner->stats.generation!=generation||
       record.request.expected_generation!=generation){view.reason=FiniteEvidenceReason::DeviceUnavailable;return S_FALSE;}
    // A dead wrapper is never retained or dereferenced. If a canonical wrapper
    // currently exists, its visible forwarding route must still be ours.
    const auto current_vertex=native_nodes.find(record.vertex_identity);
    const auto current_index=native_nodes.find(record.index_identity);
    if((current_vertex!=native_nodes.end()&&(current_vertex->second->kind!=Kind::VertexBuffer||
        current_vertex->second->parent!=device||current_vertex->second->backend!=record.vertex||!own_buffer_slots(current_vertex->second)))||
       (record.index&&current_index!=native_nodes.end()&&(current_index->second->kind!=Kind::IndexBuffer||
        current_index->second->parent!=device||current_index->second->backend!=record.index||!own_buffer_slots(current_index->second)))){
        view.reason=FiniteEvidenceReason::NativeContract;return S_FALSE;
    }
    BufferMetadata vertex_metadata{},index_metadata{};SideReference vertex,index;
    view.reason=finite_native_ready(device,record.vertex,record.request.positions.expected_revision,vertex,vertex_metadata);
    view.positions.revision=vertex_metadata.revision;view.positions.reason=view.reason;
    if(view.reason!=FiniteEvidenceReason::None)return S_FALSE;
    if(record.vertex_side&&vertex.value!=record.vertex_side){view.reason=FiniteEvidenceReason::MetadataTampered;return S_FALSE;}
    GeometryRelease vertex_release=nullptr,index_release=nullptr;
    if(!geometry_ref_endpoints(vertex.value,vertex_release)){view.reason=FiniteEvidenceReason::NativeContract;return S_FALSE;}
    if(record.request.indexed){
        view.reason=finite_native_ready(device,record.index,record.request.indices.expected_revision,index,index_metadata);
        view.indices.revision=index_metadata.revision;view.indices.reason=view.reason;
        if(view.reason!=FiniteEvidenceReason::None)return S_FALSE;
        if(record.index_side&&index.value!=record.index_side){view.reason=FiniteEvidenceReason::MetadataTampered;return S_FALSE;}
        if(!geometry_ref_endpoints(index.value,index_release)){view.reason=FiniteEvidenceReason::NativeContract;return S_FALSE;}
        if(record.request.indices.format!=index.value->contract.format){view.reason=FiniteEvidenceReason::InvalidLayout;return S_FALSE;}
        const auto bounds=index.value->evidence.query_indices(record.request.indices.expected_revision,
            record.request.indices.start_index,record.request.indices.index_count);
        view.indices.known=bounds.known;view.indices.minimum=bounds.minimum;view.indices.maximum=bounds.maximum;
        view.indices.exact_range=bounds.exact_range;view.indices.status=bounds.known?S_OK:S_FALSE;
        view.indices.reason=bounds.known?FiniteEvidenceReason::None:FiniteEvidenceReason::IndexUnknown;
        if(!bounds.known){view.reason=FiniteEvidenceReason::IndexUnknown;return S_FALSE;}
    }
    const auto& request=record.request.positions;
    const auto storage=request.position_type==D3DDECLTYPE_FLOAT3?PositionStorage::Float3:
        request.position_type==D3DDECLTYPE_FLOAT16_4?PositionStorage::Half4:PositionStorage::Unknown;
    if(storage==PositionStorage::Unknown){view.reason=FiniteEvidenceReason::InvalidLayout;return S_FALSE;}
    const auto before=vertex.value->evidence.counters();
    view.positions.state=vertex.value->evidence.query_positions(request.expected_revision,
        {request.stream_offset,request.stride,request.position_offset,request.first_vertex,request.vertex_count,storage});
    const auto after=vertex.value->evidence.counters();
    device->finite_owner->stats.query_cache_hits+=after.cache_hits-before.cache_hits;
    device->finite_owner->stats.position_components+=after.position_components-before.position_components;
    view.positions.status=view.positions.state==FiniteStatus::Unknown?S_FALSE:S_OK;
    view.positions.reason=view.positions.state==FiniteStatus::Finite?FiniteEvidenceReason::None:
        view.positions.state==FiniteStatus::NonFinite?FiniteEvidenceReason::NonFinite:FiniteEvidenceReason::UnknownCells;
    view.reason=view.positions.reason;
    if(view.positions.state!=FiniteStatus::Finite)return S_FALSE;
    if(retain_sides){
        // Transfer both temporary CPU references only after the entire pair passed.
        record.vertex_side=vertex.value;vertex.value=nullptr;
        record.index_side=index.value;index.value=nullptr;
        record.vertex_release=vertex_release;record.index_release=index_release;
        record.bytes=record.vertex_side->contract.size+(record.index_side?std::uint64_t(record.index_side->contract.size):0);
    }
    view.status=S_OK;view.reason=FiniteEvidenceReason::None;return S_OK;
}
}
HRESULT begin_geometry_frame(IDirect3DDevice9* application,GeometryFrameHandle* out) noexcept {
    PreserveExecution preserve;if(!out)return E_POINTER;*out={};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    const auto found=application_nodes.find(application);
    if(found==application_nodes.end()||found->second->kind!=Kind::Device)return E_INVALIDARG;
    auto* device=static_cast<Device*>(found->second);
    if(!geometry_owner_ready(device))return D3DERR_INVALIDCALL;
    GeometryFrameRecord* available=nullptr;
    for(auto& frame:geometry_frames){if(frame.id&&frame.device==device)return D3DERR_INVALIDCALL;if(!frame.id&&!available)available=&frame;}
    if(!available||geometry_serial==geometry_serial_limit)return E_OUTOFMEMORY;
    const auto slot=static_cast<unsigned>(available-geometry_frames.data());
    *available={next_geometry_id(slot),device->finite_owner->stats.generation,device,0};out->value=available->id;return S_OK;
}
HRESULT acquire_geometry_lease(GeometryFrameHandle frame,IDirect3DVertexBuffer9* vertex_buffer,
    IDirect3DIndexBuffer9* index_buffer,const GeometryLeaseRequest& request,GeometryLeaseHandle* out) noexcept {
    PreserveExecution preserve;if(!out)return E_POINTER;*out={};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    auto* owner=geometry_frame(frame.value);if(!owner)return E_INVALIDARG;
    if(!geometry_owner_ready(owner->device))return D3DERR_INVALIDCALL;
    if(!vertex_buffer||request.indexed!=(index_buffer!=nullptr)||!request.expected_generation)return E_INVALIDARG;
    const auto vertex=application_nodes.find(vertex_buffer),index=application_nodes.find(index_buffer);
    if(vertex==application_nodes.end()||vertex->second->kind!=Kind::VertexBuffer||vertex->second->parent!=owner->device||
       (index_buffer&&(index==application_nodes.end()||index->second->kind!=Kind::IndexBuffer||index->second->parent!=owner->device)))return E_INVALIDARG;
    if(!own_buffer_slots(vertex->second)||(index_buffer&&!own_buffer_slots(index->second)))return S_FALSE;
    if(owner->count>=geometry_leases_per_frame||geometry_count>=geometry_lease_limit||geometry_serial==geometry_serial_limit)return E_OUTOFMEMORY;
    if(geometry_free_head>=geometry_lease_limit)return E_OUTOFMEMORY;
    const unsigned slot=geometry_free_head;auto* available=&geometry_leases[slot];
    GeometryLeaseRecord candidate;candidate.frame=frame.value;candidate.request=request;
    candidate.vertex=static_cast<IDirect3DVertexBuffer9*>(vertex->second->backend);
    candidate.vertex_identity=vertex->second->identity;candidate.index_identity=index_buffer?index->second->identity:nullptr;
    candidate.index=index_buffer?static_cast<IDirect3DIndexBuffer9*>(index->second->backend):nullptr;
    GeometryLeaseView validated;
    const HRESULT hr=validate_geometry_record(owner->device,candidate,owner->generation,validated,true);
    if(hr!=S_OK)return hr;
    if(candidate.bytes>geometry_native_byte_limit-geometry_bytes){
        if(candidate.index_side)candidate.index_side->Release();
        candidate.vertex_side->Release();return E_OUTOFMEMORY;
    }
    // Verified native AddRef cannot fail. No fallible allocation follows it.
    candidate.vertex->AddRef();if(candidate.index)candidate.index->AddRef();
    candidate.id=next_geometry_id(slot);geometry_free_head=geometry_free_next[slot];*available=candidate;++owner->count;++geometry_count;geometry_bytes+=candidate.bytes;
    out->value=candidate.id;return S_OK;
}
HRESULT inspect_geometry_lease(GeometryFrameHandle frame,GeometryLeaseHandle lease,GeometryLeaseView* out) noexcept {
    PreserveExecution preserve;if(!out)return E_POINTER;*out={};
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);
    auto* owner=geometry_frame(frame.value);if(!owner)return E_INVALIDARG;
    auto* record=geometry_lease(frame.value,lease.value);if(!record)return E_INVALIDARG;
    out->frame=frame;out->lease=lease;
    const HRESULT hr=validate_geometry_record(owner->device,*record,owner->generation,*out,false);
    if(hr==S_OK){out->vertex_buffer=record->vertex;out->index_buffer=record->index;}
    return hr;
}
HRESULT release_geometry_lease(GeometryFrameHandle frame,GeometryLeaseHandle lease) noexcept {
    PreserveExecution preserve;GeometryLeaseRecord retired;
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
     if(!geometry_frame(frame.value))return E_INVALIDARG;
     auto* record=geometry_lease(frame.value,lease.value);if(!record)return E_INVALIDARG;
     retired=detach_geometry_record(*record);}
    release_geometry_record(retired);
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);drain_finite_retired();}
    return S_OK;
}
HRESULT end_geometry_frame(GeometryFrameHandle frame) noexcept {
    PreserveExecution preserve;
    {std::lock_guard<std::recursive_mutex> lock(registry_mutex);
     auto* owner=geometry_frame(frame.value);if(!owner)return E_INVALIDARG;*owner={};}
    drain_geometry_frame(frame.value);return S_OK;
}

HRESULT get_copy_depth_view(IDirect3DDevice9* wrapped, CopyDepthView* out) noexcept {
    if (!out) return E_POINTER;
    *out = {};
    Device* device;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found = application_nodes.find(wrapped);
        if (found == application_nodes.end() || found->second->kind != Kind::Device) return E_INVALIDARG;
        device = static_cast<Device*>(found->second);
        *out = device->copy_depth.view;
    }
    HRESULT bound_status = S_OK;
    out->source_bound = out->available && copy_source_bound(device, &bound_status);
    if (FAILED(bound_status)) {
        if (bound_status == D3DERR_DEVICELOST || bound_status == D3DERR_DEVICENOTRESET) {
            observe_result(device, bound_status);
            *out = device->copy_depth.view;
        } else out->status = bound_status;
    }
    return S_OK;
}

HRESULT copy_auto_depth(IDirect3DDevice9* wrapped) noexcept {
    Device* device;
    {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found = application_nodes.find(wrapped);
        if (found == application_nodes.end() || found->second->kind != Kind::Device) return E_INVALIDARG;
        device = static_cast<Device*>(found->second);
    }
    return copy_depth(device);
}

HRESULT retain_renderer_resource(IDirect3DDevice9* wrapped, IUnknown* owned_resource) noexcept {
    if (!owned_resource) return E_INVALIDARG;
    try {
        std::lock_guard<std::recursive_mutex> lock(registry_mutex);
        const auto found = application_nodes.find(wrapped);
        if (found == application_nodes.end() || found->second->kind != Kind::Device ||
            application_nodes.count(owned_resource)) return E_INVALIDARG;
        auto device = static_cast<Device*>(found->second);
        if (device->retiring || device->resetting || device->lost) return D3DERR_INVALIDCALL;
        device->renderer_resources.push_back(owned_resource);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_FAIL; }
}

#ifdef X3M_FINITE_FIXTURE
void finite_fixture_addref_callback(void(*callback)(void*),void* data){finite_addref_fixture_hook=callback;finite_addref_fixture_context=data;}
void finite_fixture_with_registry(void(*callback)(void*),void* data){
    std::lock_guard<std::recursive_mutex> lock(registry_mutex);callback(data);drain_finite_retired();
}
#endif
} // namespace x3m::ownership
