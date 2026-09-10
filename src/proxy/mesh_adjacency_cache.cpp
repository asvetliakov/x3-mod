#include "mesh_adjacency_cache.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace x3m::mesh_adjacency_cache {
namespace {
constexpr size_t max_retained=16u*1024*1024,max_scratch=4u*1024*1024;
constexpr size_t header_bytes=72; // schema, SHA256, generation, function, epsilon, five DWORDs.
struct X87Environment { DWORD control,status,tag,ip,cs,dp,ds; };
static_assert(sizeof(X87Environment)==28 && sizeof(void*)==4,"x86 native mesh ABI");
struct FpState { X87Environment x87{}; DWORD mxcsr=0; };
FpState fp_state() noexcept {
    FpState v;
    // FNSTENV temporarily masks exceptions; FLDENV immediately restores them.
    asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(v.x87),"=m"(v.mxcsr)::"memory");
    return v;
}
void restore_fp(const FpState& v) noexcept {
    asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(v.x87),"m"(v.mxcsr):"memory");
}
bool supported_fp(const FpState& v) noexcept {
    // Verified D3D9 default: masked exceptions, 24-bit x87 round-to-nearest,
    // SSE round-to-nearest, no FTZ/DAZ. Never normalize an unsupported caller.
    return (v.x87.control&0xffff)==0x007f && (v.x87.tag&0xffff)==0xffff &&
           (v.x87.status&0xb800)==0 && (v.mxcsr&~DWORD(0x3f))==0x1f80;
}
bool admissible_fp(const FpState& before,const FpState& after) noexcept {
    return supported_fp(after) && before.x87.control==after.x87.control &&
           before.x87.tag==after.x87.tag && ((before.mxcsr^after.mxcsr)&~DWORD(0x3f))==0;
}
uint64_t ticks() noexcept { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return uint64_t(t.QuadPart); }
bool overlaps(const void* a,size_t an,const void* b,size_t bn) noexcept {
    const uint64_t aa=reinterpret_cast<uintptr_t>(a),bb=reinterpret_cast<uintptr_t>(b);
    constexpr uint64_t end=uint64_t(1)<<32;
    // Invalid wrapping spans cannot be inspected or accelerated safely.
    return aa+an>end || bb+bn>end || (aa<bb+bn && bb<aa+an);
}
uint64_t hash_bytes(const unsigned char* p,size_t n) noexcept {
    uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;
}
void append(unsigned char*& cursor,const void* data,size_t size) noexcept {
    std::memcpy(cursor,data,size);cursor+=size;
}
template<class T>void append(unsigned char*& cursor,const T& value) noexcept { append(cursor,&value,sizeof value); }
}
Cache::Cache(Config c) noexcept:config_(c) {
    config_.retained_bytes=std::min(config_.retained_bytes,max_retained);
    config_.scratch_bytes=std::min(config_.scratch_bytes,max_scratch);
    config_.entries=std::min(config_.entries,max_entries);
    counters_.retained_bytes.store(sizeof(Cache),std::memory_order_relaxed);
}
Cache::~Cache(){clear();}
void Cache::clear() noexcept {
    const DWORD error=GetLastError();const auto fp=fp_state();
    for(auto& e:entries_){if(e.allocation)HeapFree(GetProcessHeap(),0,e.allocation);e={};}
    retained_=sizeof(Cache);count_=0;age_=0;
    counters_.retained_bytes.store(retained_,std::memory_order_relaxed);
    counters_.retained_entries.store(0,std::memory_order_relaxed);
    restore_fp(fp);SetLastError(error);
}
const char* bypass_reason_name(unsigned reason) noexcept {
    static constexpr const char* names[]={"input","runtime","floating_point","epsilon","configuration","disabled","contention","options","metadata","declaration","size","output_range","allocation","buffer_read"};
    return reason<bypass_reason_count?names[reason]:"unknown";
}
Statistics Cache::statistics() const noexcept {
    Statistics s;
#define COPY(n) s.n=counters_.n.load(std::memory_order_relaxed)
    COPY(calls);COPY(hits);COPY(misses);COPY(bypasses);COPY(contention);COPY(admissions);COPY(evictions);
    COPY(allocation_failures);COPY(acquisition_failures);COPY(unrecoverable_unlocks);
    COPY(native_calls);COPY(native_failures);COPY(retained_bytes);COPY(retained_entries);
    COPY(acquired_bytes);COPY(copied_bytes);COPY(evicted_bytes);
    COPY(acquisition_ticks);COPY(lookup_ticks);COPY(copy_ticks);COPY(native_ticks);COPY(total_ticks);
    COPY(rejected_result);COPY(rejected_last_error);COPY(rejected_fp);
    for(unsigned i=0;i<bypass_reason_count;++i)s.bypass_reasons[i]=counters_.bypass_reasons[i].load(std::memory_order_relaxed);
#undef COPY
    if(counters_.unsupported_fp_publication.load(std::memory_order_acquire)==2){s.unsupported_fp_available=true;s.unsupported_fp=counters_.unsupported_fp;}
    return s;
}
Outcome Cache::generate(ID3DXMesh* mesh,FLOAT epsilon,DWORD* output,Generate original,
                        const RuntimeIdentity& runtime) noexcept {
    const DWORD incoming_error=GetLastError();const FpState incoming_fp=fp_state();
    DWORD outgoing_error=incoming_error;FpState outgoing_fp=incoming_fp;
    const uint64_t begin=ticks();counters_.calls.fetch_add(1,std::memory_order_relaxed);
    bool leased=false;unsigned char* candidate=nullptr;Origin origin=original?Origin::Native:Origin::InvalidCallback;
    uint64_t acquire_begin=0;bool acquiring=false;
    auto end_acquisition=[&]() noexcept {
        if(acquiring){counters_.acquisition_ticks.fetch_add(ticks()-acquire_begin,std::memory_order_relaxed);acquiring=false;}
    };
    auto finish=[&](HRESULT hr) noexcept {
        if(candidate)HeapFree(GetProcessHeap(),0,candidate);
        if(leased)workspace_.clear(std::memory_order_release);
        counters_.total_ticks.fetch_add(ticks()-begin,std::memory_order_relaxed);
        restore_fp(outgoing_fp);SetLastError(outgoing_error);return Outcome{hr,origin};
    };
    auto native=[&]() noexcept {
        if(!original)return E_POINTER;
        counters_.native_calls.fetch_add(1,std::memory_order_relaxed);
        const auto start=ticks();restore_fp(incoming_fp);SetLastError(incoming_error);
        const HRESULT hr=original(mesh,epsilon,output);
        outgoing_error=GetLastError();outgoing_fp=fp_state();
        counters_.native_ticks.fetch_add(ticks()-start,std::memory_order_relaxed);
        if(FAILED(hr))counters_.native_failures.fetch_add(1,std::memory_order_relaxed);
        return hr;
    };
    auto bypass=[&](BypassReason reason) noexcept {end_acquisition();counters_.bypasses.fetch_add(1,std::memory_order_relaxed);counters_.bypass_reasons[static_cast<unsigned>(reason)].fetch_add(1,std::memory_order_relaxed);return finish(native());};
    DWORD epsilon_bits=0;std::memcpy(&epsilon_bits,&epsilon,sizeof epsilon);
    bool has_identity=false;for(auto b:runtime.sha256)has_identity=has_identity||b!=0;
    if(!mesh||!output||!original)return bypass(BypassReason::Input);
    if(!runtime.verified||!runtime.success_preserves_last_error||!runtime.generation||!has_identity)return bypass(BypassReason::Runtime);
    if(!supported_fp(incoming_fp)){
        unsigned empty=0;if(counters_.unsupported_fp_publication.compare_exchange_strong(empty,1,std::memory_order_acquire)){
            counters_.unsupported_fp={incoming_fp.x87.control,incoming_fp.x87.status,incoming_fp.x87.tag,incoming_fp.mxcsr};
            counters_.unsupported_fp_publication.store(2,std::memory_order_release);
        }
        return bypass(BypassReason::FloatingPoint);
    }
    if((epsilon_bits&0x80000000u)||(epsilon_bits&0x7f800000u)==0x7f800000u)return bypass(BypassReason::Epsilon);
    if(!config_.entries||config_.retained_bytes<=sizeof(Cache))return bypass(BypassReason::Configuration);
    if(disabled_.load(std::memory_order_relaxed))return bypass(BypassReason::Disabled);
    if(workspace_.test_and_set(std::memory_order_acquire)){
        counters_.contention.fetch_add(1,std::memory_order_relaxed);return bypass(BypassReason::Contention);
    }
    leased=true;
    acquire_begin=ticks();acquiring=true;
    const DWORD options=mesh->GetOptions(),vertices=mesh->GetNumVertices(),faces=mesh->GetNumFaces(),stride=mesh->GetNumBytesPerVertex();
    if((options&D3DXMESH_SYSTEMMEM)!=D3DXMESH_SYSTEMMEM ||
       (options&(D3DXMESH_WRITEONLY|D3DXMESH_VB_SHARE)) ||
       ((options&D3DXMESH_DYNAMIC)&&!runtime.systemmem_dynamic_readonly_verified))return bypass(BypassReason::Options);
    if(!vertices||!faces||!stride)return bypass(BypassReason::Metadata);
    D3DVERTEXELEMENT9 declaration[MAX_FVF_DECL_SIZE]{};
    HRESULT acquired=mesh->GetDeclaration(declaration);
    unsigned decl_count=0;
    if(SUCCEEDED(acquired))for(;decl_count<MAX_FVF_DECL_SIZE;++decl_count){
        if(declaration[decl_count].Stream==0xff){++decl_count;break;}
        if(declaration[decl_count].Stream!=0){acquired=E_INVALIDARG;break;}
    }
    if(!decl_count||decl_count>MAX_FVF_DECL_SIZE||declaration[decl_count-1].Stream!=0xff)acquired=E_INVALIDARG;
    if(FAILED(acquired)){counters_.acquisition_failures.fetch_add(1,std::memory_order_relaxed);return bypass(BypassReason::Declaration);}
    const uint64_t vb_bytes=uint64_t(vertices)*stride,ib_bytes=uint64_t(faces)*3*((options&D3DXMESH_32BIT)?4:2);
    const uint64_t value_bytes=uint64_t(faces)*3*sizeof(DWORD);
    // Reject individual products before addition: hostile DWORD counts/stride
    // can otherwise wrap a 64-bit combined allocation on this x86 adapter.
    if(vb_bytes>config_.scratch_bytes||ib_bytes>config_.scratch_bytes||value_bytes>config_.scratch_bytes)return bypass(BypassReason::Size);
    // Include input FP control/status to avoid treating equal geometry under
    // different computational environments as identical. Unsupported controls bypass.
    const uint64_t key_bytes=header_bytes+3*sizeof(DWORD)+uint64_t(decl_count)*sizeof(D3DVERTEXELEMENT9)+vb_bytes+ib_bytes;
    const uint64_t allocation_bytes=key_bytes+value_bytes;
    if(allocation_bytes>config_.scratch_bytes||allocation_bytes>config_.retained_bytes-sizeof(Cache))return bypass(BypassReason::Size);
    if(uint64_t(reinterpret_cast<uintptr_t>(output))+value_bytes>(uint64_t(1)<<32))return bypass(BypassReason::OutputRange);
#ifdef X3M_MESH_CACHE_FIXTURE
    if(fail_allocation_){fail_allocation_=false;candidate=nullptr;}
    else
#endif
    candidate=static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(),0,size_t(allocation_bytes)));
    if(!candidate){counters_.allocation_failures.fetch_add(1,std::memory_order_relaxed);return bypass(BypassReason::Allocation);}
    const SIZE_T actual_bytes=HeapSize(GetProcessHeap(),0,candidate);
    if(actual_bytes==SIZE_T(-1)||actual_bytes>config_.scratch_bytes||actual_bytes>config_.retained_bytes-sizeof(Cache))return bypass(BypassReason::Size);
    auto cursor=candidate;const DWORD schema=1,fn=DWORD(reinterpret_cast<uintptr_t>(original));
    append(cursor,schema);append(cursor,runtime.sha256.data(),runtime.sha256.size());append(cursor,runtime.generation);
    append(cursor,fn);append(cursor,epsilon_bits);append(cursor,options);append(cursor,vertices);append(cursor,faces);
    append(cursor,stride);append(cursor,DWORD(decl_count));
    append(cursor,incoming_fp.x87.control);append(cursor,incoming_fp.x87.status);append(cursor,incoming_fp.mxcsr);
    append(cursor,declaration,size_t(decl_count)*sizeof(D3DVERTEXELEMENT9));
    bool poisoned=false;
    auto read_buffer=[&](bool vertex,size_t bytes) noexcept {
        void* data=nullptr;
        HRESULT hr=vertex?mesh->LockVertexBuffer(D3DLOCK_READONLY,&data):mesh->LockIndexBuffer(D3DLOCK_READONLY,&data);
        if(FAILED(hr))return hr;
        const bool valid=data&&!overlaps(output,size_t(value_bytes),data,bytes);
        if(valid)append(cursor,data,bytes);
        HRESULT unlocked=vertex?mesh->UnlockVertexBuffer():mesh->UnlockIndexBuffer();
        if(FAILED(unlocked)){
            // A failed unlock has unknown lock state. One recovery attempt; never
            // invoke the algorithm while an acquisition lock may remain held.
            unlocked=vertex?mesh->UnlockVertexBuffer():mesh->UnlockIndexBuffer();
            if(FAILED(unlocked)){poisoned=true;return unlocked;}
            return E_FAIL; // Recovered acquisition is still ineligible for reuse.
        }
        return valid?S_OK:E_INVALIDARG;
    };
    acquired=read_buffer(true,size_t(vb_bytes));
    if(SUCCEEDED(acquired))acquired=read_buffer(false,size_t(ib_bytes));
    end_acquisition();
    if(FAILED(acquired)){
        counters_.acquisition_failures.fetch_add(1,std::memory_order_relaxed);
        if(poisoned){
            disabled_.store(true,std::memory_order_relaxed);
            counters_.unrecoverable_unlocks.fetch_add(1,std::memory_order_relaxed);
            origin=Origin::AcquisitionCleanupFailure;return finish(acquired);
        }
        return bypass(BypassReason::BufferRead);
    }
    counters_.acquired_bytes.fetch_add(vb_bytes+ib_bytes,std::memory_order_relaxed);
    const auto lookup_begin=ticks();uint64_t hash=hash_bytes(candidate,size_t(key_bytes));
#ifdef X3M_MESH_CACHE_FIXTURE
    if(collision_)hash=0;
#endif
    Entry* hit=nullptr;
    for(unsigned i=0;i<config_.entries;++i){auto& e=entries_[i];
        if(e.allocation&&e.hash==hash&&e.key_bytes==key_bytes&&std::memcmp(candidate,e.allocation,e.key_bytes)==0){hit=&e;break;}
    }
    counters_.lookup_ticks.fetch_add(ticks()-lookup_begin,std::memory_order_relaxed);
    if(hit){
        const auto copy_begin=ticks();std::memcpy(output,hit->allocation+hit->key_bytes,hit->value_bytes);
        counters_.copy_ticks.fetch_add(ticks()-copy_begin,std::memory_order_relaxed);
        counters_.copied_bytes.fetch_add(hit->value_bytes,std::memory_order_relaxed);
        counters_.hits.fetch_add(1,std::memory_order_relaxed);hit->age=++age_;
        outgoing_fp.x87.status=hit->x87_status;outgoing_fp.mxcsr=hit->mxcsr;
        origin=Origin::CacheHit;return finish(S_OK);
    }
    counters_.misses.fetch_add(1,std::memory_order_relaxed);
    const HRESULT hr=native();
    if(hr!=S_OK){counters_.rejected_result.fetch_add(1,std::memory_order_relaxed);return finish(hr);}
    if(outgoing_error!=incoming_error){counters_.rejected_last_error.fetch_add(1,std::memory_order_relaxed);return finish(hr);}
    if(!admissible_fp(incoming_fp,outgoing_fp)){counters_.rejected_fp.fetch_add(1,std::memory_order_relaxed);return finish(hr);}
    const auto copy_begin=ticks();std::memcpy(candidate+size_t(key_bytes),output,size_t(value_bytes));
    counters_.copy_ticks.fetch_add(ticks()-copy_begin,std::memory_order_relaxed);
    counters_.copied_bytes.fetch_add(value_bytes,std::memory_order_relaxed);
    auto evict=[&](Entry& e) noexcept {
        retained_-=e.allocation_bytes;--count_;
        counters_.evictions.fetch_add(1,std::memory_order_relaxed);
        counters_.evicted_bytes.fetch_add(e.allocation_bytes,std::memory_order_relaxed);
        HeapFree(GetProcessHeap(),0,e.allocation);e={};
    };
    Entry* destination=nullptr;
    while(!destination){
        if(retained_+actual_bytes<=config_.retained_bytes && count_<config_.entries){
            for(unsigned i=0;i<config_.entries;++i)if(!entries_[i].allocation){destination=&entries_[i];break;}
        }else{
            Entry* oldest=nullptr;
            for(unsigned i=0;i<config_.entries;++i)if(entries_[i].allocation&&(!oldest||entries_[i].age<oldest->age))oldest=&entries_[i];
            if(!oldest)return finish(hr);
            evict(*oldest);
        }
    }
    *destination={candidate,actual_bytes,size_t(key_bytes),size_t(value_bytes),hash,++age_,outgoing_fp.x87.status,outgoing_fp.mxcsr};
    candidate=nullptr;retained_+=actual_bytes;++count_;
    counters_.admissions.fetch_add(1,std::memory_order_relaxed);
    counters_.retained_bytes.store(retained_,std::memory_order_relaxed);
    counters_.retained_entries.store(count_,std::memory_order_relaxed);
    return finish(hr);
}
}
