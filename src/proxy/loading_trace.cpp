#include "loading_trace.h"
#include "capture.h"
#include "mesh_adjacency_cache.h"
#include "mesh_adjacency_fast.h"
#include "../ownership/d3d9_ownership.h"
#include "../ownership/application_admission_abi.h"
#include "cpu_state.h"
#include "gz_buffer.h"
#include "loading_trace_light.h"
#include "loading_probes.h"
#include "resource_reader.h"
#include <new>
#include <d3dx9.h>
#include <cpuid.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <type_traits>

namespace x3m::loading_trace {
namespace {
constexpr unsigned count=static_cast<unsigned>(Operation::Count);
struct Hook { const char* dll; const char* name; PVOID replacement; PVOID original=nullptr; PVOID* slot=nullptr; };
std::atomic<bool> installed{false};
bool installation_started=false; // One installation generation; originals never rebound.
std::atomic<unsigned> mesh_owned_slots{0}, protection_debts{0};
uint64_t clock_frequency=0, coverage_start=0;
unsigned hook_count=0;
constexpr unsigned mesh_table_limit=8;
constexpr unsigned mesh_slot_indices[3]={20,22,27};
constexpr unsigned mesh_contract_slots[]={4,5,7,8,9,13,14,15,16,17,18};
struct MeshTable { PVOID* table=nullptr; Hook slots[3]{}; PVOID contract[11]{}; int last_result=-1,last_owned=-1; };
MeshTable mesh_tables[mesh_table_limit];
SRWLOCK mesh_lock=SRWLOCK_INIT;
std::atomic<bool> mesh_observation_enabled{false};
HMODULE mesh_module=nullptr; // Factory implementation lifetime is pinned, not version qualified.
bool mesh_module_checked=false;
namespace adjacency_cache=mesh_adjacency_cache;
// Off is the existing direct dispatch path: no construction or acquisition.
bool cache_requested=false;
std::atomic<bool> cache_enabled{false},cache_faulted{false};
alignas(adjacency_cache::Cache) unsigned char cache_storage[sizeof(adjacency_cache::Cache)];
// Immutable process-lifetime publication; reports need not acquire mesh_lock.
std::atomic<adjacency_cache::Cache*> cache_instance{nullptr};
adjacency_cache::RuntimeIdentity cache_runtime;
std::atomic<uint64_t> cache_native_outcomes{0},cache_hit_outcomes{0},cache_cleanup_outcomes{0},cache_blocked{0};
std::atomic<uint64_t> cache_gate_rejections{0},cache_gate_ticks{0};
std::atomic<HRESULT> cache_cleanup_hr{S_OK};
uint64_t cache_last_report_calls=0,cache_last_report_blocked=0,cache_last_report_rejected=0;
enum class GateReason : unsigned {
    Unavailable, Input, MeshObject, MeshTable, MeshMethod, MeshPool, MeshOptions,
    VertexAcquire, IndexAcquire, BufferMissing, Tracker,
    DescriptorCall, DescriptorPool, DescriptorUsage, DescriptorFormat, DescriptorSize, Count
};
constexpr unsigned gate_reason_count=static_cast<unsigned>(GateReason::Count);
const char* gate_reason_name(unsigned i){
    static constexpr const char* names[]={"unavailable","input","mesh_object","mesh_table","mesh_method","mesh_pool","mesh_options","vertex_acquire","index_acquire","buffer_missing","tracker","descriptor_call","descriptor_pool","descriptor_usage","descriptor_format","descriptor_size"};
    return i<gate_reason_count?names[i]:"unknown";
}
struct GateDetail {
    unsigned scope=0; // 0 mesh, 1 vertex buffer, 2 index buffer.
    DWORD options=0,slot=0,actual_entry=0,expected_entry=0;
    HRESULT status=S_OK;DWORD pool=0,usage=0,format=0;
    uint64_t required_bytes=0,size_bytes=0,pending=0;
    bool known=false;
};
struct GateCounter {std::atomic<uint64_t> count{0};std::atomic<unsigned> publication{0};GateDetail first{};};
GateCounter cache_gate_reasons[gate_reason_count];
uint64_t cache_gate_last_counts[gate_reason_count]{};
bool cache_gate_detail_reported[gate_reason_count]{};
uint64_t cache_bypass_last_counts[adjacency_cache::bypass_reason_count]{};
bool cache_fp_detail_reported=false,cache_fp_incoming_reported=false;
bool reject_gate(GateReason reason,const GateDetail& detail={}){
    auto& row=cache_gate_reasons[static_cast<unsigned>(reason)];row.count.fetch_add(1,std::memory_order_relaxed);
    unsigned empty=0;if(row.publication.compare_exchange_strong(empty,1,std::memory_order_acquire)){
        row.first=detail;row.publication.store(2,std::memory_order_release);
    }
    return false;
}
struct X87Environment {DWORD control,status,tag,ip,cs,dp,ds;};
struct ComputationalState {X87Environment x87;DWORD mxcsr;};
ComputationalState computational_state(){ComputationalState value{};
    asm volatile("fnstenv %0\n\tfldenv %0\n\tstmxcsr %1":"=m"(value.x87),"=m"(value.mxcsr)::"memory");return value;
}
void restore_computational_state(const ComputationalState& value){
    asm volatile("fldenv %0\n\tldmxcsr %1"::"m"(value.x87),"m"(value.mxcsr):"memory");
}
// Our SSE2 arithmetic runs under the default MXCSR (masked, nearest, no FTZ/DAZ)
// whatever the caller's state (the game enters with 0x9fc0); callers restore theirs.
void set_default_mxcsr(){static const DWORD value=0x1f80;asm volatile("ldmxcsr %0"::"m"(value):"memory");}
bool cache_buffer_contract(ID3DXMesh* mesh);
void cache_fault(HRESULT hr);
void cache_report();
#ifdef X3M_LOADING_TRACE_FIXTURE
unsigned fail_mesh_patch=0,fail_protection_restore=0;
std::atomic<HRESULT> forced_cache_cleanup{S_OK};
thread_local bool cache_reentry_once=false;
#endif
void observe_mesh(ID3DXMesh* mesh);
void restore_mesh_hooks();
uint64_t tick() { return light::tick(); }
// Timing span of the heavy (D3DX/mesh) rows: the accounting and the nesting
// live in loading_trace_light.cpp (light::Span); this adapter keeps the former
// call shape (finish after the CpuCallBoundary's after_original()).
struct Span {
    light::Span inner; Operation op;
    explicit Span(Operation value):op(value){inner.begin(static_cast<unsigned>(value));inner.before_call();}
    void finish(uint64_t,DWORD,bool failed=false,uint64_t bytes=0,bool pending=false,bool ambiguous=false){inner.finish(failed,bytes,pending,ambiguous);}
};

// Each bounded table has distinct trampolines. Dispatch therefore continues to
// the correct original even if another interceptor later clones/replaces a
// mesh vptr and chains to us. No object registry/refcounts or lock around calls.
using PointRepsFn=HRESULT (WINAPI*)(ID3DXMesh*,const DWORD*,DWORD*);
using AdjacencyFn=HRESULT (WINAPI*)(ID3DXMesh*,FLOAT,DWORD*);
using OptimizeFn=HRESULT (WINAPI*)(ID3DXMesh*,DWORD,const DWORD*,DWORD*,DWORD*,ID3DXBuffer**);
static_assert(std::is_same_v<decltype(&ID3DXMesh::GenerateAdjacency),HRESULT (STDMETHODCALLTYPE ID3DXMesh::*)(FLOAT,DWORD*)>);
static_assert(std::is_same_v<decltype(&ID3DXMesh::ConvertPointRepsToAdjacency),HRESULT (STDMETHODCALLTYPE ID3DXMesh::*)(const DWORD*,DWORD*)>);
static_assert(std::is_same_v<decltype(&ID3DXMesh::OptimizeInplace),HRESULT (STDMETHODCALLTYPE ID3DXMesh::*)(DWORD,const DWORD*,DWORD*,DWORD*,ID3DXBuffer**)>);
#ifdef X3M_LOADING_TRACE_FIXTURE
thread_local AdjacencyFn cache_reentry_original=nullptr;
HRESULT WINAPI fixture_reentry_call(ID3DXMesh* mesh,FLOAT epsilon,DWORD* adjacency){
    const DWORD error=GetLastError();const auto fp=computational_state();
    if(cache_reentry_once){cache_reentry_once=false;DWORD nested[12]{};if(mesh->GetNumFaces()<=4)mesh->GenerateAdjacency(epsilon,nested);}
    restore_computational_state(fp);SetLastError(error);return cache_reentry_original(mesh,epsilon,adjacency);
}
#endif
template<unsigned Index> HRESULT WINAPI mesh_point_reps(ID3DXMesh* mesh,const DWORD* reps,DWORD* adjacency) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    Span span(Operation::MeshPointReps);
    cpu.before_original();
    const HRESULT hr=reinterpret_cast<PointRepsFn>(mesh_tables[Index].slots[0].original)(mesh,reps,adjacency);cpu.after_original();
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(hr));return hr;
}
// X3M_MESH_ADJACENCY=native|verify|fast: exact-equality replacement of the
// D3DX epsilon welding (docs/verification/mesh-adjacency-fast.md). Verify runs
// the native method, recomputes and compares; fast answers from the module and
// falls through to native on any qualification failure. Both reuse the cache
// hook's public buffer qualification and serve as the "original" callable of
// the adjacency cache, so the order stays cache lookup -> compute -> admission.
namespace adjacency_fast=mesh_adjacency_fast;
enum class AdjacencyMode : unsigned { Native, Verify, Fast };
std::atomic<AdjacencyMode> adjacency_mode{AdjacencyMode::Native};
std::atomic<bool> adjacency_faulted{false};
enum class AdjacencyFallback : unsigned { Input, Gate, Declaration, Size, Lock, Module, MathTable, Count };
constexpr unsigned adjacency_fallback_count=static_cast<unsigned>(AdjacencyFallback::Count);
// d3dx9_37's math-table dispatch (0x00587e6b), mirrored from its documented
// inputs: DisablePSGP / DisableD3DXPSGP (HKLM\Software\Microsoft\Direct3D,
// DWORD; 1 keeps the generic table, 2 skips 3DNow); then, unless 2, the 3DNow
// check (IsProcessorFeaturePresent(7) on NT 5+, D3DX's 0x00587e06) selects the
// 3DNow installer (0x0074a996), which installs only when CPUID reports MMX (leaf
// 1 EDX bit 23) and 3DNow (leaf 0x80000001 EDX bit 31) and otherwise leaves the
// generic table (the FEX case: 3DNow reported, bit absent); without 3DNow, SSE2
// (CPUID leaf 1 EDX bit 26, 0x00587c94) selects the SSE2 table, else
// IsProcessorFeaturePresent(6) the SSE table.
D3dxMathTable d3dx_math_table_detect(){
    auto registry=[](const char* value,DWORD& out){
        HKEY key=nullptr;if(RegOpenKeyA(HKEY_LOCAL_MACHINE,"Software\\Microsoft\\Direct3D",&key)!=ERROR_SUCCESS)return false;
        DWORD size=sizeof out;const LSTATUS status=RegQueryValueExA(key,value,nullptr,nullptr,reinterpret_cast<BYTE*>(&out),&size);RegCloseKey(key);
        return status==ERROR_SUCCESS&&size==sizeof out;
    };
    DWORD psgp=0;if(!registry("DisablePSGP",psgp))psgp=0;
    DWORD d3dx_psgp=0;if(registry("DisableD3DXPSGP",d3dx_psgp))psgp=d3dx_psgp;
    if(psgp==1)return D3dxMathTable::Generic;
    unsigned edx1=0,ext_edx=0;
    {
        unsigned a=0,b=0,c=0,d=0;
        if(__get_cpuid(0,&a,&b,&c,&d)&&a!=0){__get_cpuid(1,&a,&b,&c,&d);edx1=d;}
        if(__get_cpuid(0x80000000u,&a,&b,&c,&d)&&a>0x80000000u){__get_cpuid(0x80000001u,&a,&b,&c,&d);ext_edx=d;}
    }
    if(psgp!=2&&IsProcessorFeaturePresent(PF_3DNOW_INSTRUCTIONS_AVAILABLE))
        return ((edx1&(1u<<23))&&(ext_edx&(1u<<31)))?D3dxMathTable::ThreeDNow:D3dxMathTable::Generic;
    if(edx1&(1u<<26))return D3dxMathTable::Sse2;
    if(IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE))return D3dxMathTable::Sse;
    return D3dxMathTable::Generic;
}
}
D3dxMathTable d3dx_math_table(){static const D3dxMathTable table=d3dx_math_table_detect();return table;}
const char* d3dx_math_table_name(D3dxMathTable table){
    static constexpr const char* names[]={"generic","3dnow","sse2","sse"};
    return unsigned(table)<4?names[unsigned(table)]:"unknown";
}
namespace {
const char* adjacency_mode_name(AdjacencyMode mode){return mode==AdjacencyMode::Fast?"fast":mode==AdjacencyMode::Verify?"verify":"native";}
struct AdjacencyCounters {
    std::atomic<uint64_t> calls{0},computed{0},fallbacks{0},faults{0},faces{0},vertices{0};
    std::atomic<uint64_t> fast_ticks{0},fast_max_ticks{0},native_ticks{0},native_max_ticks{0};
    std::atomic<uint64_t> verify_meshes{0},verify_equal{0},verify_mismatched{0},verify_entries{0},verify_mismatch_entries{0},verify_native_failures{0};
    std::atomic<uint64_t> quantized{0},unquantized{0},welded_vertices{0},multi_candidate_meshes{0},normal_selected{0},degenerate_faces{0};
    std::atomic<uint64_t> mismatch_lines{0};
    std::atomic<unsigned> fp_publication{0};ComputationalState first_fp{};
    std::array<std::atomic<uint64_t>,adjacency_fallback_count> fallback_reasons{};
    std::array<std::atomic<uint64_t>,adjacency_fast::status_count> module_status{};
} adjacency_counters;
uint64_t adjacency_last_report_calls=0;bool adjacency_fp_reported=false;
void adjacency_note_fp(const ComputationalState& state){
    unsigned empty=0;if(adjacency_counters.fp_publication.compare_exchange_strong(empty,1,std::memory_order_acquire)){adjacency_counters.first_fp=state;adjacency_counters.fp_publication.store(2,std::memory_order_release);}
}
constexpr uint64_t adjacency_mismatch_line_limit=64;
// X3M_MESH_ADJACENCY_DUMP=1: verify mode writes each mismatching mesh (bounded)
// into the capture folder for offline replay (loading_trace.h, format).
std::atomic<bool> adjacency_dump_requested{false};
std::atomic<uint64_t> adjacency_dump_index{0};
constexpr uint64_t adjacency_dump_limit=256;
thread_local AdjacencyFn adjacency_thread_original=nullptr;
struct AdjacencyCompute {
    bool computed=false,fault=false;HRESULT fault_hr=S_OK;AdjacencyFallback fallback=AdjacencyFallback::Input;
    adjacency_fast::Report report{};uint64_t ticks=0;DWORD faces=0,vertices=0;
};
void adjacency_note_max(std::atomic<uint64_t>& slot,uint64_t value){uint64_t old=slot.load(std::memory_order_relaxed);while(old<value&&!slot.compare_exchange_weak(old,value,std::memory_order_relaxed)){}}
// Public-interface path only: the cache gate (mesh table/method pointers,
// SYSTEMMEM pool, descriptors), then GetDeclaration and READONLY Lock/Unlock.
AdjacencyCompute adjacency_compute(ID3DXMesh* mesh,FLOAT epsilon,DWORD* output) {
    AdjacencyCompute r;
    auto fall=[&](AdjacencyFallback reason){r.fallback=reason;adjacency_counters.fallbacks.fetch_add(1,std::memory_order_relaxed);adjacency_counters.fallback_reasons[unsigned(reason)].fetch_add(1,std::memory_order_relaxed);return r;};
    if(!mesh||!output)return fall(AdjacencyFallback::Input);
    if(!cache_buffer_contract(mesh))return fall(AdjacencyFallback::Gate);
    const DWORD options=mesh->GetOptions(),vertices=mesh->GetNumVertices(),faces=mesh->GetNumFaces(),stride=mesh->GetNumBytesPerVertex();
    r.faces=faces;r.vertices=vertices;
    if(!vertices||!faces||stride<12)return fall(AdjacencyFallback::Declaration);
    D3DVERTEXELEMENT9 declaration[MAX_FVF_DECL_SIZE]{};
    if(FAILED(mesh->GetDeclaration(declaration)))return fall(AdjacencyFallback::Declaration);
    DWORD position_offset=0;bool position_found=false;
    for(unsigned i=0;i<MAX_FVF_DECL_SIZE&&declaration[i].Stream!=0xff;++i){
        const auto& e=declaration[i];
        if(e.Stream!=0)return fall(AdjacencyFallback::Declaration); // D3DX's parse ignores the stream number; only single-stream declarations are mirrored here
        if(e.Usage==D3DDECLUSAGE_POSITION&&e.UsageIndex==0&&e.Type==D3DDECLTYPE_FLOAT3){position_offset=e.Offset;position_found=true;} // D3DX keeps the last such element
    }
    if(!position_found||position_offset>stride-12)return fall(AdjacencyFallback::Declaration);
    // D3DX walks the faces of the attribute table's ranges when a table exists;
    // the module walks all faces in order, which is the same only for a table
    // covering the faces contiguously in order (none on the engine's fresh meshes).
    DWORD attribute_count=0;
    if(FAILED(mesh->GetAttributeTable(nullptr,&attribute_count)))return fall(AdjacencyFallback::Declaration);
    if(attribute_count){
        D3DXATTRIBUTERANGE ranges[16]{};
        if(attribute_count>16||FAILED(mesh->GetAttributeTable(ranges,&attribute_count)))return fall(AdjacencyFallback::Declaration);
        DWORD next_face=0;
        for(DWORD i=0;i<attribute_count;++i){if(ranges[i].FaceStart!=next_face)return fall(AdjacencyFallback::Declaration);next_face+=ranges[i].FaceCount;}
        if(next_face!=faces)return fall(AdjacencyFallback::Declaration);
    }
    // The candidate selection's normals must come from the D3DXVec3Normalize this
    // process's D3DX installed (loading_trace.h, d3dx_math_table); the module
    // reproduces the generic and the SSE2 tables, the 3DNow and SSE ones are native's.
    adjacency_fast::Policy policy;
    switch(d3dx_math_table()){
    case D3dxMathTable::Sse2:policy.normalize=adjacency_fast::Normalize::Sse2;break;
    case D3dxMathTable::Generic:policy.normalize=adjacency_fast::Normalize::Generic;break;
    default:return fall(AdjacencyFallback::MathTable);
    }
    const uint64_t vb_bytes=uint64_t(vertices)*stride,ib_bytes=uint64_t(faces)*3*((options&D3DXMESH_32BIT)?4:2),value_bytes=uint64_t(faces)*3*sizeof(DWORD);
    if(vb_bytes>0x7fffffffu||ib_bytes>0x7fffffffu||value_bytes>0x7fffffffu||uint64_t(reinterpret_cast<uintptr_t>(output))+value_bytes>(uint64_t(1)<<32))return fall(AdjacencyFallback::Size);
    void* vertex_data=nullptr;void* index_data=nullptr;
    if(FAILED(mesh->LockVertexBuffer(D3DLOCK_READONLY,&vertex_data))||!vertex_data)return fall(AdjacencyFallback::Lock);
    auto unlock=[&](bool vertex){
        HRESULT hr=vertex?mesh->UnlockVertexBuffer():mesh->UnlockIndexBuffer();
        if(FAILED(hr))hr=vertex?mesh->UnlockVertexBuffer():mesh->UnlockIndexBuffer(); // one recovery attempt, as the cache
        if(FAILED(hr)){r.fault=true;r.fault_hr=hr;}
    };
    if(FAILED(mesh->LockIndexBuffer(D3DLOCK_READONLY,&index_data))||!index_data){unlock(true);if(r.fault)return r;return fall(AdjacencyFallback::Lock);}
    const uintptr_t out_begin=reinterpret_cast<uintptr_t>(output),out_end=out_begin+size_t(value_bytes);
    auto overlaps=[&](const void* p,uint64_t n){const uintptr_t b=reinterpret_cast<uintptr_t>(p);return b+n>(uint64_t(1)<<32)||(b<out_end&&out_begin<b+n);};
    bool ran=false;
    if(!overlaps(vertex_data,vb_bytes)&&!overlaps(index_data,ib_bytes)){
        adjacency_fast::Input in;in.vertices=vertex_data;in.vertex_count=vertices;in.stride=stride;in.position_offset=position_offset;
        in.indices=index_data;in.indices_32bit=(options&D3DXMESH_32BIT)!=0;in.face_count=faces;in.epsilon=epsilon;
        static_assert(sizeof(DWORD)==sizeof(uint32_t));
        const auto begin=tick();r.report=adjacency_fast::generate(in,reinterpret_cast<uint32_t*>(output),policy);r.ticks=tick()-begin;ran=true;
    }
    unlock(false);unlock(true);
    if(r.fault)return r;
    if(!ran)return fall(AdjacencyFallback::Size);
    adjacency_counters.module_status[unsigned(r.report.status)].fetch_add(1,std::memory_order_relaxed);
    if(r.report.status!=adjacency_fast::Status::Ok)return fall(AdjacencyFallback::Module);
    r.computed=true;
    adjacency_counters.computed.fetch_add(1,std::memory_order_relaxed);
    adjacency_counters.faces.fetch_add(faces,std::memory_order_relaxed);adjacency_counters.vertices.fetch_add(vertices,std::memory_order_relaxed);
    adjacency_counters.fast_ticks.fetch_add(r.ticks,std::memory_order_relaxed);adjacency_note_max(adjacency_counters.fast_max_ticks,r.ticks);
    (r.report.quantized?adjacency_counters.quantized:adjacency_counters.unquantized).fetch_add(1,std::memory_order_relaxed);
    adjacency_counters.welded_vertices.fetch_add(r.report.welded,std::memory_order_relaxed);
    adjacency_counters.multi_candidate_meshes.fetch_add(r.report.multi_candidates!=0,std::memory_order_relaxed);
    adjacency_counters.normal_selected.fetch_add(r.report.normal_selected,std::memory_order_relaxed);
    adjacency_counters.degenerate_faces.fetch_add(r.report.degenerate_faces,std::memory_order_relaxed);
    return r;
}
void adjacency_fault(HRESULT hr){
    adjacency_counters.faults.fetch_add(1,std::memory_order_relaxed);
    bool expected=false;
    if(adjacency_faulted.compare_exchange_strong(expected,true,std::memory_order_acq_rel)){
        adjacency_mode.store(AdjacencyMode::Native,std::memory_order_release);
        log("mesh_adjacency_fault origin=acquisition_cleanup_failure hr=%08lx mode=native restart_required=1 native_fallback=0 lock_repaired=0",hr);
    }
}
// Serves as the adjacency cache's "original": preserves LastError and the
// computational state of a state-transparent call on the computed path.
HRESULT WINAPI adjacency_fast_service(ID3DXMesh* mesh,FLOAT epsilon,DWORD* adjacency){
    const AdjacencyFn original=adjacency_thread_original;
    const DWORD error=GetLastError();const auto fp=computational_state();adjacency_note_fp(fp);set_default_mxcsr();
    adjacency_counters.calls.fetch_add(1,std::memory_order_relaxed);
    const AdjacencyCompute r=adjacency_compute(mesh,epsilon,adjacency);
    restore_computational_state(fp);SetLastError(error);
    if(r.fault){adjacency_fault(r.fault_hr);return r.fault_hr;}
    if(r.computed)return S_OK;
    return original?original(mesh,epsilon,adjacency):E_POINTER;
}
}
// The dump folder is the capture folder next to this module (capture.cpp's
// initialize_log creates the same directory); resolved here so that the
// fixtures, which do not link capture.cpp, share the writer.
static bool adjacency_dump_path(wchar_t* path,size_t capacity,uint64_t index){
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&adjacency_dump_path),&self))return false;
    wchar_t module[32768]{};const DWORD length=GetModuleFileNameW(self,module,32768);
    if(!length||length>=32768)return false;
    size_t cut=length;while(cut&&module[cut-1]!=L'\\'&&module[cut-1]!=L'/')--cut;
    module[cut]=0;
    const int written=_snwprintf(path,capacity,L"%sx3-modern-captures",module);
    if(written<=0||size_t(written)>=capacity)return false;
    CreateDirectoryW(path,nullptr);
    const int full=_snwprintf(path,capacity,L"%sx3-modern-captures\\mesh-adjacency-%llu.bin",module,index);
    return full>0&&size_t(full)<capacity;
}
bool adjacency_write_dump(const wchar_t* path,ID3DXMesh* mesh,FLOAT epsilon,const DWORD* native,const DWORD* module,uint64_t mismatches,DWORD first,DWORD x87_control,DWORD mxcsr){
    if(!path||!mesh||!native||!module)return false;
    const DWORD options=mesh->GetOptions(),vertices=mesh->GetNumVertices(),faces=mesh->GetNumFaces(),stride=mesh->GetNumBytesPerVertex();
    if(!vertices||!faces||stride<12)return false;
    D3DVERTEXELEMENT9 declaration[MAX_FVF_DECL_SIZE]{};
    if(FAILED(mesh->GetDeclaration(declaration)))return false;
    AdjacencyDumpHeader header{};std::memcpy(header.magic,"X3MADJ01",8);header.reserved[0]=1u+unsigned(d3dx_math_table());
    header.header_size=sizeof header;header.faces=faces;header.vertices=vertices;header.stride=stride;header.options=options;
    bool position_found=false;
    for(unsigned i=0;i<MAX_FVF_DECL_SIZE&&declaration[i].Stream!=0xff;++i){
        const auto& e=declaration[i];++header.declaration_count;
        if(e.Stream==0&&e.Usage==D3DDECLUSAGE_POSITION&&e.UsageIndex==0&&e.Type==D3DDECLTYPE_FLOAT3){header.position_offset=e.Offset;position_found=true;} // D3DX keeps the last such element
    }
    if(!position_found)return false;
    static_assert(sizeof(D3DVERTEXELEMENT9)==8);
    std::memcpy(&header.epsilon_bits,&epsilon,sizeof header.epsilon_bits);header.x87_control=x87_control;header.mxcsr=mxcsr;
    header.mismatches=mismatches>0xffffffffu?0xffffffffu:DWORD(mismatches);header.first=first;
    const uint64_t vb_bytes=uint64_t(vertices)*stride,ib_bytes=uint64_t(faces)*3*((options&D3DXMESH_32BIT)?4:2),adjacency_bytes=uint64_t(faces)*3*sizeof(DWORD);
    if(vb_bytes>0x7fffffffu||ib_bytes>0x7fffffffu||adjacency_bytes>0x7fffffffu)return false;
    void* vertex_data=nullptr;void* index_data=nullptr;
    if(FAILED(mesh->LockVertexBuffer(D3DLOCK_READONLY,&vertex_data))||!vertex_data)return false;
    if(FAILED(mesh->LockIndexBuffer(D3DLOCK_READONLY,&index_data))||!index_data){mesh->UnlockVertexBuffer();return false;}
    bool written=false;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){
        auto put=[&](const void* data,uint64_t bytes){DWORD done=0;return WriteFile(file,data,DWORD(bytes),&done,nullptr)&&done==DWORD(bytes);};
        written=put(&header,sizeof header)&&put(declaration,uint64_t(header.declaration_count)*sizeof(D3DVERTEXELEMENT9))&&put(vertex_data,vb_bytes)&&put(index_data,ib_bytes)&&put(native,adjacency_bytes)&&put(module,adjacency_bytes);
        CloseHandle(file);
    }
    mesh->UnlockIndexBuffer();mesh->UnlockVertexBuffer();
    return written;
}
namespace {
HRESULT WINAPI adjacency_verify_service(ID3DXMesh* mesh,FLOAT epsilon,DWORD* adjacency){
    const AdjacencyFn original=adjacency_thread_original;
    if(!original)return E_POINTER;
    adjacency_counters.calls.fetch_add(1,std::memory_order_relaxed);adjacency_note_fp(computational_state());
    const auto native_begin=tick();const HRESULT hr=original(mesh,epsilon,adjacency);const uint64_t native_ticks=tick()-native_begin;
    const DWORD error=GetLastError();const auto fp=computational_state();set_default_mxcsr();
    adjacency_counters.native_ticks.fetch_add(native_ticks,std::memory_order_relaxed);adjacency_note_max(adjacency_counters.native_max_ticks,native_ticks);
    if(FAILED(hr)){adjacency_counters.verify_native_failures.fetch_add(1,std::memory_order_relaxed);restore_computational_state(fp);SetLastError(error);return hr;}
    const DWORD faces=mesh?mesh->GetNumFaces():0;
    DWORD* scratch=faces&&faces<=0x0fffffffu?static_cast<DWORD*>(HeapAlloc(GetProcessHeap(),0,size_t(faces)*3*sizeof(DWORD))):nullptr;
    if(scratch){
        const AdjacencyCompute r=adjacency_compute(mesh,epsilon,scratch);
        if(r.fault){HeapFree(GetProcessHeap(),0,scratch);restore_computational_state(fp);SetLastError(error);adjacency_fault(r.fault_hr);return r.fault_hr;}
        if(r.computed){
            const size_t entries=size_t(faces)*3;uint64_t mismatches=0;size_t first=entries;
            for(size_t i=0;i<entries;++i)if(scratch[i]!=adjacency[i]){if(!mismatches)first=i;++mismatches;}
            adjacency_counters.verify_meshes.fetch_add(1,std::memory_order_relaxed);
            adjacency_counters.verify_entries.fetch_add(entries,std::memory_order_relaxed);
            if(!mismatches)adjacency_counters.verify_equal.fetch_add(1,std::memory_order_relaxed);
            else{
                adjacency_counters.verify_mismatched.fetch_add(1,std::memory_order_relaxed);
                adjacency_counters.verify_mismatch_entries.fetch_add(mismatches,std::memory_order_relaxed);
                if(adjacency_counters.mismatch_lines.fetch_add(1,std::memory_order_relaxed)<adjacency_mismatch_line_limit)
                    log("mesh_adjacency verify faces=%lu vertices=%lu equal=0 mismatches=%llu first=%lu native=%08lx fast=%08lx native_us=%.3f fast_us=%.3f quantized=%u welded=%lu multi_candidates=%lu normal_selected=%lu degenerate_faces=%lu welded_degenerate_faces=%lu refused_welds=%lu repeated_neighbours=%lu",
                        faces,r.vertices,mismatches,DWORD(first),adjacency[first],scratch[first],double(native_ticks)*1e6/clock_frequency,double(r.ticks)*1e6/clock_frequency,unsigned(r.report.quantized),DWORD(r.report.welded),DWORD(r.report.multi_candidates),DWORD(r.report.normal_selected),DWORD(r.report.degenerate_faces),DWORD(r.report.welded_degenerate_faces),DWORD(r.report.refused_welds),DWORD(r.report.repeated_neighbours));
                if(adjacency_dump_requested.load(std::memory_order_relaxed)){
                    const uint64_t index=adjacency_dump_index.fetch_add(1,std::memory_order_relaxed);
                    if(index<adjacency_dump_limit){
                        wchar_t path[32768+64]{};
                        const bool written=adjacency_dump_path(path,32768+64,index)&&adjacency_write_dump(path,mesh,epsilon,adjacency,scratch,mismatches,DWORD(first),fp.x87.control,fp.mxcsr);
                        log("mesh_adjacency_dump index=%llu faces=%lu vertices=%lu written=%u path=mesh-adjacency-%llu.bin",index,faces,r.vertices,unsigned(written),index);
                    }
                }
            }
        }
        HeapFree(GetProcessHeap(),0,scratch);
    }
    restore_computational_state(fp);SetLastError(error);return hr;
}
void adjacency_report(){
    const auto mode=adjacency_mode.load(std::memory_order_acquire);
    if(mode==AdjacencyMode::Native&&!adjacency_faulted.load(std::memory_order_acquire))return;
    auto& c=adjacency_counters;const auto calls=c.calls.load(std::memory_order_relaxed);
    if(calls==adjacency_last_report_calls)return;
    adjacency_last_report_calls=calls;
    log("mesh_adjacency_metric cumulative=1 qpc=%llu mode=%s faulted=%u calls=%llu computed=%llu fallbacks=%llu faults=%llu faces=%llu vertices=%llu fast_ticks=%llu fast_max_ticks=%llu fast_us=%.3f fast_max_us=%.3f native_ticks=%llu native_max_ticks=%llu native_us=%.3f native_max_us=%.3f verify_meshes=%llu verify_equal=%llu verify_mismatched=%llu verify_entries=%llu verify_mismatch_entries=%llu verify_native_failures=%llu quantized=%llu unquantized=%llu welded_vertices=%llu multi_candidate_meshes=%llu normal_selected=%llu degenerate_faces=%llu fallback_input=%llu fallback_gate=%llu fallback_declaration=%llu fallback_size=%llu fallback_lock=%llu fallback_module=%llu fallback_math_table=%llu module_input=%llu module_index_range=%llu module_non_finite=%llu module_magnitude=%llu module_epsilon_neighbour=%llu module_allocation=%llu",
        tick(),adjacency_mode_name(mode),unsigned(adjacency_faulted.load(std::memory_order_relaxed)),calls,c.computed.load(),c.fallbacks.load(),c.faults.load(),c.faces.load(),c.vertices.load(),
        c.fast_ticks.load(),c.fast_max_ticks.load(),double(c.fast_ticks.load())*1e6/clock_frequency,double(c.fast_max_ticks.load())*1e6/clock_frequency,
        c.native_ticks.load(),c.native_max_ticks.load(),double(c.native_ticks.load())*1e6/clock_frequency,double(c.native_max_ticks.load())*1e6/clock_frequency,
        c.verify_meshes.load(),c.verify_equal.load(),c.verify_mismatched.load(),c.verify_entries.load(),c.verify_mismatch_entries.load(),c.verify_native_failures.load(),
        c.quantized.load(),c.unquantized.load(),c.welded_vertices.load(),c.multi_candidate_meshes.load(),c.normal_selected.load(),c.degenerate_faces.load(),
        c.fallback_reasons[0].load(),c.fallback_reasons[1].load(),c.fallback_reasons[2].load(),c.fallback_reasons[3].load(),c.fallback_reasons[4].load(),c.fallback_reasons[5].load(),c.fallback_reasons[6].load(),
        c.module_status[1].load(),c.module_status[2].load(),c.module_status[3].load(),c.module_status[4].load(),c.module_status[5].load(),c.module_status[6].load());
    if(!adjacency_fp_reported&&c.fp_publication.load(std::memory_order_acquire)==2){adjacency_fp_reported=true;const auto& f=c.first_fp;
        log("mesh_adjacency_fp_first control=%08lx status=%08lx tag=%08lx mxcsr=%08lx compute_mxcsr=00001f80 restored=1",f.x87.control,f.x87.status,f.x87.tag,f.mxcsr);
    }
    if(!cache_requested)for(unsigned i=0;i<gate_reason_count;++i){ // otherwise cache_report prints the shared gate counters
        auto& row=cache_gate_reasons[i];const auto count=row.count.load(std::memory_order_relaxed);
        if(count!=cache_gate_last_counts[i]){cache_gate_last_counts[i]=count;log("mesh_adjacency_gate cumulative=1 reason=%s count=%llu",gate_reason_name(i),count);}
    }
}
template<unsigned Index> HRESULT WINAPI mesh_adjacency(ID3DXMesh* mesh,FLOAT epsilon,DWORD* adjacency) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    const auto native=reinterpret_cast<AdjacencyFn>(mesh_tables[Index].slots[1].original);
    // The fast/verify services fall through to this table's own original; the
    // thread-local carries it because the cache calls the service as "original".
    const auto mode=adjacency_mode.load(std::memory_order_acquire);
    adjacency_thread_original=native;
    const AdjacencyFn original=mode==AdjacencyMode::Fast?&adjacency_fast_service:mode==AdjacencyMode::Verify?&adjacency_verify_service:native;
    if(!cache_requested||!cache_enabled.load(std::memory_order_acquire)){
        Span span(Operation::MeshAdjacency);cpu.before_original();
        const HRESULT hr=original(mesh,epsilon,adjacency);cpu.after_original();
        const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(hr));return hr;
    }
    Span span(Operation::MeshAdjacency);const auto gate_begin=tick();
    auto* const instance=cache_instance.load(std::memory_order_acquire);
    const bool eligible=!instance?reject_gate(GateReason::Unavailable):(!mesh||!adjacency)?reject_gate(GateReason::Input):cache_buffer_contract(mesh);
    cache_gate_ticks.fetch_add(tick()-gate_begin,std::memory_order_relaxed);
    cpu.before_original();
    adjacency_cache::Outcome outcome;
    if(eligible){
#ifdef X3M_LOADING_TRACE_FIXTURE
        const HRESULT forced=forced_cache_cleanup.exchange(S_OK);
        if(FAILED(forced))outcome={forced,adjacency_cache::Origin::AcquisitionCleanupFailure};
        else{
            auto invoke=original;if(cache_reentry_once){cache_reentry_original=original;invoke=fixture_reentry_call;}
            outcome=instance->generate(mesh,epsilon,adjacency,invoke,cache_runtime);
        }
#else
        outcome=instance->generate(mesh,epsilon,adjacency,original,cache_runtime);
#endif
    }
    else{cache_gate_rejections.fetch_add(1,std::memory_order_relaxed);outcome={original(mesh,epsilon,adjacency),adjacency_cache::Origin::Native};}
    cpu.after_original();
    const DWORD error=GetLastError();const auto end=tick();
    if(outcome.origin==adjacency_cache::Origin::CacheHit)cache_hit_outcomes.fetch_add(1,std::memory_order_relaxed);
    else if(outcome.origin==adjacency_cache::Origin::Native)cache_native_outcomes.fetch_add(1,std::memory_order_relaxed);
    else cache_fault(outcome.hr);
    span.finish(end,error,FAILED(outcome.hr));return outcome.hr;
}
template<unsigned Index> HRESULT WINAPI mesh_optimize(ID3DXMesh* mesh,DWORD flags,const DWORD* in,DWORD* out,DWORD* faces,ID3DXBuffer** vertices) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    Span span(Operation::MeshOptimize);
    cpu.before_original();
    const HRESULT hr=reinterpret_cast<OptimizeFn>(mesh_tables[Index].slots[2].original)(mesh,flags,in,out,faces,vertices);cpu.after_original();
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(hr));return hr;
}
#define MESH_THUNKS(i) {reinterpret_cast<PVOID>(mesh_point_reps<i>),reinterpret_cast<PVOID>(mesh_adjacency<i>),reinterpret_cast<PVOID>(mesh_optimize<i>)}
PVOID mesh_replacements[mesh_table_limit][3]={MESH_THUNKS(0),MESH_THUNKS(1),MESH_THUNKS(2),MESH_THUNKS(3),MESH_THUNKS(4),MESH_THUNKS(5),MESH_THUNKS(6),MESH_THUNKS(7)};
#undef MESH_THUNKS

// The counting/timing forwarders live in loading_trace_light.cpp (no SSE, no
// x87, no CpuCallBoundary); only the D3DX rows below, whose wrappers observe
// meshes and log, keep the full boundary.
namespace light=loading_trace::light;
HRESULT WINAPI effect(IDirect3DDevice9*,const void*,UINT,const D3DXMACRO*,ID3DXInclude*,DWORD,ID3DXEffectPool*,ID3DXEffect**,ID3DXBuffer**);
HRESULT WINAPI texture(IDirect3DDevice9*,const void*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,D3DXIMAGE_INFO*,PALETTEENTRY*,IDirect3DTexture9**);
HRESULT WINAPI cube(IDirect3DDevice9*,const void*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,D3DXIMAGE_INFO*,PALETTEENTRY*,IDirect3DCubeTexture9**);
HRESULT WINAPI surface(IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,const void*,UINT,const RECT*,DWORD,D3DCOLOR,D3DXIMAGE_INFO*);
HRESULT WINAPI mesh_create(DWORD,DWORD,DWORD,const D3DVERTEXELEMENT9*,IDirect3DDevice9*,ID3DXMesh**);
HRESULT WINAPI mesh_clean(D3DXCLEANTYPE,ID3DXMesh*,const DWORD*,ID3DXMesh**,DWORD*,ID3DXBuffer**);
// cdecl and argument widths corroborated by target callsites; local zlib/libxml
// SDK prototypes supply semantics. Opaque pointers avoid importing struct layouts.
using GzOpenFn=void* (__cdecl*)(const char*,const char*);
using GzReadFn=int (__cdecl*)(void*,void*,unsigned);
using GzSeekFn=LONG (__cdecl*)(void*,LONG,int);
static_assert(sizeof(LONG)==4&&sizeof(int)==4&&sizeof(void*)==4);
using GzTellFn=LONG (__cdecl*)(void*);
using GzGetcFn=int (__cdecl*)(void*);
using GzCloseFn=int (__cdecl*)(void*);
void* __cdecl gz_open(const char*,const char*);
int __cdecl gz_read(void*,void*,unsigned);
LONG __cdecl gz_seek(void*,LONG,int);
int __cdecl gz_getc(void*);
LONG __cdecl gz_tell(void*);
int __cdecl gz_close(void*);
// X3M_GZ_BUFFER=1: the gz hooks route through gz_buffer (set before patching, never
// cleared); its real functions are the traced wrappers with telemetry, else the originals.
bool gz_buffer_active=false;
Hook hooks[]={
    {"KERNEL32.dll","CreateFileA",reinterpret_cast<PVOID>(light::file_open)},
    {"KERNEL32.dll","ReadFile",reinterpret_cast<PVOID>(light::file_read)},
    {"KERNEL32.dll","SetFilePointer",reinterpret_cast<PVOID>(light::file_seek)},
    {"d3dx9_37.dll","D3DXCreateEffect",reinterpret_cast<PVOID>(effect)},
    {"d3dx9_37.dll","D3DXCreateTextureFromFileInMemoryEx",reinterpret_cast<PVOID>(texture)},
    {"d3dx9_37.dll","D3DXCreateCubeTextureFromFileInMemoryEx",reinterpret_cast<PVOID>(cube)},
    {"d3dx9_37.dll","D3DXLoadSurfaceFromFileInMemory",reinterpret_cast<PVOID>(surface)},
    {"USER32.dll","SetCursor",reinterpret_cast<PVOID>(light::cursor_set)},
    {"USER32.dll","SetCursorPos",reinterpret_cast<PVOID>(light::cursor_position)},
    {"zlib1.dll","gzopen",reinterpret_cast<PVOID>(gz_open)},
    {"zlib1.dll","gzread",reinterpret_cast<PVOID>(gz_read)},
    {"zlib1.dll","gzseek",reinterpret_cast<PVOID>(gz_seek)},
    {"zlib1.dll","inflate",reinterpret_cast<PVOID>(light::inflate_stream)},
    {"libxml2.dll","xmlReadMemory",reinterpret_cast<PVOID>(light::xml_read)},
    {"d3dx9_37.dll","D3DXCreateMesh",reinterpret_cast<PVOID>(mesh_create)},
    {"d3dx9_37.dll","D3DXCleanMesh",reinterpret_cast<PVOID>(mesh_clean)},
    {"KERNEL32.dll","FindFirstFileA",reinterpret_cast<PVOID>(light::find_first)},
    {"KERNEL32.dll","FindNextFileA",reinterpret_cast<PVOID>(light::find_next)},
    {"KERNEL32.dll","FindClose",reinterpret_cast<PVOID>(light::find_close)},
    {"zlib1.dll","gzgetc",reinterpret_cast<PVOID>(gz_getc)},
    {"zlib1.dll","gztell",reinterpret_cast<PVOID>(gz_tell)},
    {"zlib1.dll","gzclose",reinterpret_cast<PVOID>(gz_close)},
    {"zlib1.dll","gzwrite",reinterpret_cast<PVOID>(light::gz_write)},
    // Probe batch 2 rows (patched only with X3M_LOADING_PROBES=1).
    {"zlib1.dll","inflateInit2_",reinterpret_cast<PVOID>(light::inflate_init2)},
    {"zlib1.dll","inflateEnd",reinterpret_cast<PVOID>(light::inflate_end)},
    {"ADVAPI32.dll","CryptAcquireContextA",reinterpret_cast<PVOID>(light::crypt_acquire_context)},
    {"ADVAPI32.dll","CryptReleaseContext",reinterpret_cast<PVOID>(light::crypt_release_context)},
    {"ADVAPI32.dll","CryptImportKey",reinterpret_cast<PVOID>(light::crypt_import_key)},
    {"ADVAPI32.dll","CryptCreateHash",reinterpret_cast<PVOID>(light::crypt_create_hash)},
    {"ADVAPI32.dll","CryptHashData",reinterpret_cast<PVOID>(light::crypt_hash_data)},
    {"ADVAPI32.dll","CryptVerifySignatureA",reinterpret_cast<PVOID>(light::crypt_verify_signature)},
    {"ADVAPI32.dll","CryptGetHashParam",reinterpret_cast<PVOID>(light::crypt_get_hash_param)},
    {"ADVAPI32.dll","CryptDestroyHash",reinterpret_cast<PVOID>(light::crypt_destroy_hash)},
    {"ADVAPI32.dll","CryptDestroyKey",reinterpret_cast<PVOID>(light::crypt_destroy_key)},
    {"KERNEL32.dll","CreateDirectoryA",reinterpret_cast<PVOID>(light::create_directory)},
    {"KERNEL32.dll","DeleteFileA",reinterpret_cast<PVOID>(light::delete_file)},
    {"KERNEL32.dll","MoveFileA",reinterpret_cast<PVOID>(light::move_file)},
    {"KERNEL32.dll","MoveFileExA",reinterpret_cast<PVOID>(light::move_file_ex)},
    {"KERNEL32.dll","WriteFile",reinterpret_cast<PVOID>(light::write_file)},
    {"KERNEL32.dll","GetFileType",reinterpret_cast<PVOID>(light::get_file_type)},
    {"KERNEL32.dll","CloseHandle",reinterpret_cast<PVOID>(light::close_handle)}
};
bool probe_row(unsigned index){return index>=probe_row_begin&&index<probe_row_end;}
// Rows the read-ahead buffer needs; patched alone when telemetry is off.
bool gz_buffer_row(const Hook& hook){
    return !std::strcmp(hook.dll,"zlib1.dll")&&std::strcmp(hook.name,"inflate")&&std::strcmp(hook.name,"gzwrite");
}
constexpr unsigned import_count=sizeof hooks/sizeof *hooks;
static_assert(import_count==static_cast<unsigned>(Operation::MeshPointReps));
const char* method_names[]={"ID3DXMesh::ConvertPointRepsToAdjacency","ID3DXMesh::GenerateAdjacency","ID3DXMesh::OptimizeInplace"};
const char* operation_name(unsigned index){return index<import_count?hooks[index].name:method_names[index-import_count];}
static_assert(std::is_same_v<decltype(&light::file_open),decltype(&CreateFileA)>);
static_assert(std::is_same_v<decltype(&light::file_read),decltype(&ReadFile)>);
static_assert(std::is_same_v<decltype(&light::file_seek),decltype(&SetFilePointer)>);
static_assert(std::is_same_v<decltype(&effect),decltype(&D3DXCreateEffect)>);
static_assert(std::is_same_v<decltype(&texture),decltype(&D3DXCreateTextureFromFileInMemoryEx)>);
static_assert(std::is_same_v<decltype(&cube),decltype(&D3DXCreateCubeTextureFromFileInMemoryEx)>);
static_assert(std::is_same_v<decltype(&surface),decltype(&D3DXLoadSurfaceFromFileInMemory)>);
static_assert(std::is_same_v<decltype(&mesh_create),decltype(&D3DXCreateMesh)>);
static_assert(std::is_same_v<decltype(&mesh_clean),decltype(&D3DXCleanMesh)>);
static_assert(std::is_same_v<decltype(&light::cursor_set),decltype(&SetCursor)>);
static_assert(std::is_same_v<decltype(&light::cursor_position),decltype(&SetCursorPos)>);
static_assert(std::is_same_v<decltype(&light::find_first),decltype(&FindFirstFileA)>);
static_assert(std::is_same_v<decltype(&light::find_next),decltype(&FindNextFileA)>);
static_assert(std::is_same_v<decltype(&light::find_close),decltype(&FindClose)>);
static_assert(std::is_same_v<decltype(&light::crypt_acquire_context),decltype(&CryptAcquireContextA)>);
static_assert(std::is_same_v<decltype(&light::crypt_verify_signature),decltype(&CryptVerifySignatureA)>);
static_assert(std::is_same_v<decltype(&light::create_directory),decltype(&CreateDirectoryA)>);
static_assert(std::is_same_v<decltype(&light::move_file_ex),decltype(&MoveFileExA)>);
static_assert(std::is_same_v<decltype(&light::write_file),decltype(&WriteFile)>);
static_assert(std::is_same_v<decltype(&light::close_handle),decltype(&CloseHandle)>);
template<typename T>T original(Operation op){return reinterpret_cast<T>(hooks[static_cast<unsigned>(op)].original);}

HRESULT WINAPI effect(IDirect3DDevice9* d,const void* data,UINT size,const D3DXMACRO* defines,ID3DXInclude* include,DWORD flags,ID3DXEffectPool* pool,ID3DXEffect** out,ID3DXBuffer** errors) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::Effect);
    cpu.before_original();
    HRESULT result=original<decltype(&D3DXCreateEffect)>(span.op)(d,data,size,defines,include,flags,pool,out,errors);cpu.after_original();
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI texture(IDirect3DDevice9* d,const void* data,UINT size,UINT width,UINT height,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,DWORD filter,DWORD mipfilter,D3DCOLOR key,D3DXIMAGE_INFO* info,PALETTEENTRY* palette,IDirect3DTexture9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::Texture);
    cpu.before_original();
    HRESULT result=original<decltype(&D3DXCreateTextureFromFileInMemoryEx)>(span.op)(d,data,size,width,height,levels,usage,format,pool,filter,mipfilter,key,info,palette,out);cpu.after_original();
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI cube(IDirect3DDevice9* d,const void* data,UINT size,UINT edge,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,DWORD filter,DWORD mipfilter,D3DCOLOR key,D3DXIMAGE_INFO* info,PALETTEENTRY* palette,IDirect3DCubeTexture9** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::CubeTexture);
    cpu.before_original();
    HRESULT result=original<decltype(&D3DXCreateCubeTextureFromFileInMemoryEx)>(span.op)(d,data,size,edge,levels,usage,format,pool,filter,mipfilter,key,info,palette,out);cpu.after_original();
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI surface(IDirect3DSurface9* dest,const PALETTEENTRY* palette,const RECT* destrect,const void* data,UINT size,const RECT* srcrect,DWORD filter,D3DCOLOR key,D3DXIMAGE_INFO* info) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::Surface);
    cpu.before_original();
    HRESULT result=original<decltype(&D3DXLoadSurfaceFromFileInMemory)>(span.op)(dest,palette,destrect,data,size,srcrect,filter,key,info);cpu.after_original();
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI mesh_create(DWORD faces,DWORD vertices,DWORD options,const D3DVERTEXELEMENT9* declaration,IDirect3DDevice9* device,ID3DXMesh** out) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    Span span(Operation::MeshCreate);
    cpu.before_original();
    HRESULT result=original<decltype(&D3DXCreateMesh)>(span.op)(faces,vertices,options,declaration,device,out);cpu.after_original();
    const DWORD error=GetLastError();
    const auto end=tick();
    if(SUCCEEDED(result)&&out&&*out)observe_mesh(*out);
    span.finish(end,error,FAILED(result));return result;
}
HRESULT WINAPI mesh_clean(D3DXCLEANTYPE type,ID3DXMesh* input,const DWORD* adjacency_in,ID3DXMesh** output,DWORD* adjacency_out,ID3DXBuffer** errors) {
    CpuCallBoundary cpu;
    ownership::ApplicationAdmissionAbi admission(ownership::process_admission_monitor());
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    Span span(Operation::MeshClean);
    cpu.before_original();
    HRESULT result=original<decltype(&D3DXCleanMesh)>(span.op)(type,input,adjacency_in,output,adjacency_out,errors);cpu.after_original();
    const DWORD error=GetLastError();
    const auto end=tick();
    if(SUCCEEDED(result)&&output&&*output)observe_mesh(*output);
    span.finish(end,error,FAILED(result));return result;
}


// Import-slot entry points. With the buffer off these are the light traced
// wrappers; with it on, the buffer's fast path runs with no span of its own.
void* __cdecl gz_open(const char* path,const char* mode){return gz_buffer_active?gz_buffer::open(path,mode):light::gz_open_traced(path,mode);}
int __cdecl gz_read(void* file,void* data,unsigned size){return gz_buffer_active?gz_buffer::read(file,data,size):light::gz_read_traced(file,data,size);}
LONG __cdecl gz_seek(void* file,LONG offset,int whence){return gz_buffer_active?gz_buffer::seek(file,offset,whence):light::gz_seek_traced(file,offset,whence);}
int __cdecl gz_getc(void* file){return gz_buffer_active?gz_buffer::getc(file):light::gz_getc_traced(file);}
LONG __cdecl gz_tell(void* file){return gz_buffer_active?gz_buffer::tell(file):light::gz_tell_traced(file);}
int __cdecl gz_close(void* file){return gz_buffer_active?gz_buffer::close(file):light::gz_close_traced(file);}

bool requested() { wchar_t setting[8]{};return GetEnvironmentVariableW(L"X3M_TELEMETRY",setting,8)==1&&setting[0]==L'1'; }
}
bool probes_requested() { wchar_t setting[8]{};return GetEnvironmentVariableW(L"X3M_LOADING_PROBES",setting,8)==1&&setting[0]==L'1'; }
namespace {
bool readable(const void* address,size_t bytes,HMODULE owner=nullptr,bool executable=false);

// Only validated module memory is traversed. Reject unterminated names, missing
// OriginalFirstThunk and RVAs that escape SizeOfImage; never infer names from IAT.
struct Image {
    unsigned char* base;DWORD size;
    bool range(DWORD rva,size_t length)const{return rva<size&&length<=size-rva;}
    template<typename T>T* at(DWORD rva,size_t n=1)const{
        if(n>size/sizeof(T)||!range(rva,sizeof(T)*n)||!readable(base+rva,sizeof(T)*n,reinterpret_cast<HMODULE>(base)))return nullptr;
        return reinterpret_cast<T*>(base+rva);
    }
    const char* string(DWORD rva)const {
        if(!range(rva,1))return nullptr;
        const char* start=reinterpret_cast<char*>(base+rva);
        DWORD offset=rva;
        while(offset<size){
            MEMORY_BASIC_INFORMATION info{};const auto cursor=base+offset;
            if(!readable(cursor,1,reinterpret_cast<HMODULE>(base))||VirtualQuery(cursor,&info,sizeof info)!=sizeof info)return nullptr;
            const size_t count=std::min<size_t>(size-offset,info.RegionSize-(cursor-static_cast<unsigned char*>(info.BaseAddress)));
            if(std::memchr(cursor,0,count))return start;
            offset+=static_cast<DWORD>(count);
        }
        return nullptr;
    }
};
// Serializes page-permission changes, not API calls. A failed restore retains
// the original page protection until recovery; PAGE_READWRITE is never mistaken
// for a new baseline when a later slot on the same page is patched.
struct ProtectionDebt {void* page=nullptr;DWORD protection=0;};
ProtectionDebt protection_pages[64]; // at most 16 IAT + 8*3 mesh slot pages
SRWLOCK protection_lock=SRWLOCK_INIT;
void* page_of(const void* address){SYSTEM_INFO info{};GetSystemInfo(&info);return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(address)&~(uintptr_t(info.dwPageSize)-1));}
bool restore_protection(void* address,DWORD protection){
#ifdef X3M_LOADING_TRACE_FIXTURE
    if(fail_protection_restore){--fail_protection_restore;SetLastError(ERROR_ACCESS_DENIED);return false;}
#endif
    DWORD ignored=0;return VirtualProtect(address,sizeof(PVOID),protection,&ignored)!=0;
}
void update_debt_count(){unsigned n=0;for(const auto& debt:protection_pages)n+=debt.page!=nullptr;protection_debts.store(n,std::memory_order_release);}
bool has_protection_debt(const void* address){
    AcquireSRWLockShared(&protection_lock);const auto page=page_of(address);bool found=false;
    for(const auto& debt:protection_pages)found|=debt.page==page;
    ReleaseSRWLockShared(&protection_lock);return found;
}
void recover_protections(){
    AcquireSRWLockExclusive(&protection_lock);
    for(auto& debt:protection_pages)if(debt.page&&restore_protection(debt.page,debt.protection))debt={};
    update_debt_count();ReleaseSRWLockExclusive(&protection_lock);
}
bool patch(Hook& hook,bool restore) {
    if(!hook.slot)return false;
    AcquireSRWLockExclusive(&protection_lock);
    const auto page=page_of(hook.slot);ProtectionDebt* record=nullptr;
    for(auto& debt:protection_pages)if(debt.page==page){record=&debt;break;}
    if(!record)for(auto& debt:protection_pages)if(!debt.page){record=&debt;break;}
    if(!record){ReleaseSRWLockExclusive(&protection_lock);return false;}
    DWORD observed=0;
    if(!VirtualProtect(hook.slot,sizeof(PVOID),PAGE_READWRITE,&observed)){ReleaseSRWLockExclusive(&protection_lock);return false;}
    if(!record->page)*record={page,observed};
    const PVOID expected=restore?hook.replacement:hook.original;
    const PVOID desired=restore?hook.original:hook.replacement;
    const bool swapped=InterlockedCompareExchangePointer(hook.slot,desired,expected)==expected;
    bool protected_again=restore_protection(hook.slot,record->protection),rolled_back=false;
    if(!protected_again&&!restore&&swapped){
        InterlockedCompareExchangePointer(hook.slot,hook.original,hook.replacement);
        rolled_back=true;protected_again=restore_protection(hook.slot,record->protection);
    }
    if(protected_again)*record={};
    update_debt_count();ReleaseSRWLockExclusive(&protection_lock);
    return swapped&&protected_again&&!rolled_back;
}
bool readable(const void* address,size_t bytes,HMODULE owner,bool executable) {
    MEMORY_BASIC_INFORMATION info{};
    if(!address||VirtualQuery(address,&info,sizeof info)!=sizeof info||info.State!=MEM_COMMIT||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||(owner&&info.AllocationBase!=owner))return false;
    const auto start=reinterpret_cast<uintptr_t>(address),base=reinterpret_cast<uintptr_t>(info.BaseAddress);
    if(start<base||bytes>info.RegionSize-(start-base))return false;
    const DWORD protection=info.Protect&0xff;
    if(executable)return protection==PAGE_EXECUTE||protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
    return protection==PAGE_READONLY||protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||
           protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
}
bool verified_dynamic_options(DWORD options){
    // Bounded actual mesh variants; physical public descriptors must also agree.
    return options==0x990u||options==0x991u||options==0x18990u||options==0x18991u;
}
template<class Buffer> bool buffer_contract(Buffer* application,uint64_t required_bytes,D3DFORMAT format,DWORD options) {
    GateDetail detail{};detail.scope=std::is_same_v<Buffer,IDirect3DVertexBuffer9>?1:2;detail.options=options;detail.required_bytes=required_bytes;
    if(!application)return reject_gate(GateReason::BufferMissing,detail);
    // The typed reference returned by the verified mesh owns this interface.
    // Public descriptor and READONLY Lock/Unlock contracts require no backend
    // vtable, native object offset, implementation import or DLL fingerprint.
    ownership::BufferContentView view{};
    detail.status=ownership::get_buffer_content_view(application,&view);detail.known=view.known;detail.pending=view.pending_locks;
    if(SUCCEEDED(detail.status)){
        detail.status=view.status;
        if(FAILED(view.status)||(view.requested&&(!view.known||view.ambiguous||view.pending_locks)))return reject_gate(GateReason::Tracker,detail);
    }else if(detail.status!=E_INVALIDARG)return reject_gate(GateReason::Tracker,detail);
    std::conditional_t<std::is_same_v<Buffer,IDirect3DVertexBuffer9>,D3DVERTEXBUFFER_DESC,D3DINDEXBUFFER_DESC> desc{};
    detail.status=application->GetDesc(&desc);detail.pool=desc.Pool;detail.usage=desc.Usage;detail.format=desc.Format;detail.size_bytes=desc.Size;
    if(FAILED(detail.status))return reject_gate(GateReason::DescriptorCall,detail);
    if(desc.Pool!=D3DPOOL_SYSTEMMEM)return reject_gate(GateReason::DescriptorPool,detail);
    const auto expected_type=std::is_same_v<Buffer,IDirect3DVertexBuffer9>?D3DRTYPE_VERTEXBUFFER:D3DRTYPE_INDEXBUFFER;
    if(desc.Type!=expected_type||desc.Format!=format)return reject_gate(GateReason::DescriptorFormat,detail);
    const DWORD software_option=std::is_same_v<Buffer,IDirect3DVertexBuffer9>?D3DXMESH_VB_SOFTWAREPROCESSING:D3DXMESH_IB_SOFTWAREPROCESSING;
    const DWORD expected_usage=((options&D3DXMESH_DYNAMIC)?D3DUSAGE_DYNAMIC:0)|
        ((options&software_option)?D3DUSAGE_SOFTWAREPROCESSING:0);
    if(desc.Usage!=expected_usage)return reject_gate(GateReason::DescriptorUsage,detail);
    if(!required_bytes||required_bytes>desc.Size)return reject_gate(GateReason::DescriptorSize,detail);
    return true;
}
bool cache_buffer_contract(ID3DXMesh* mesh) {
    GateDetail detail{};
    if(!readable(mesh,sizeof(PVOID)))return reject_gate(GateReason::MeshObject,detail);
    auto table=*reinterpret_cast<PVOID**>(mesh);if(!readable(table,19*sizeof(PVOID)))return reject_gate(GateReason::MeshTable,detail);
    const MeshTable* recorded=nullptr;
    for(const auto& row:mesh_tables)if(row.table==table){recorded=&row;break;}
    if(!recorded)return reject_gate(GateReason::MeshTable,detail);
    for(unsigned i=0;i<11;++i){
        detail.slot=mesh_contract_slots[i];detail.actual_entry=DWORD(reinterpret_cast<uintptr_t>(table[detail.slot]));
        detail.expected_entry=DWORD(reinterpret_cast<uintptr_t>(recorded->contract[i]));
        if(table[detail.slot]!=recorded->contract[i])return reject_gate(GateReason::MeshMethod,detail);
    }
    const DWORD options=mesh->GetOptions();detail.options=options;
    if((options&D3DXMESH_SYSTEMMEM)!=D3DXMESH_SYSTEMMEM)return reject_gate(GateReason::MeshPool,detail);
    constexpr DWORD supported_options=D3DXMESH_SYSTEMMEM|D3DXMESH_32BIT|D3DXMESH_DYNAMIC|D3DXMESH_SOFTWAREPROCESSING;
    if(options&~supported_options)return reject_gate(GateReason::MeshOptions,detail);
    if((options&D3DXMESH_DYNAMIC)&&!verified_dynamic_options(options))return reject_gate(GateReason::MeshOptions,detail);
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    detail.status=mesh->GetVertexBuffer(&vb);bool okay=SUCCEEDED(detail.status)&&vb;
    if(!okay)reject_gate(GateReason::VertexAcquire,detail);
    if(okay){detail.status=mesh->GetIndexBuffer(&ib);okay=SUCCEEDED(detail.status)&&ib;if(!okay)reject_gate(GateReason::IndexAcquire,detail);}
    if(okay){const uint64_t vertices=uint64_t(mesh->GetNumVertices())*mesh->GetNumBytesPerVertex();
        const uint64_t indices=uint64_t(mesh->GetNumFaces())*3*((options&D3DXMESH_32BIT)?4:2);
        okay=buffer_contract(vb,vertices,D3DFMT_VERTEXDATA,options)&&
             buffer_contract(ib,indices,(options&D3DXMESH_32BIT)?D3DFMT_INDEX32:D3DFMT_INDEX16,options);
    }
    if(ib)ib->Release();
    if(vb)vb->Release();
    return okay;
}
void cache_fault(HRESULT hr) {
    cache_cleanup_outcomes.fetch_add(1,std::memory_order_relaxed);
    bool expected=false;
    if(cache_faulted.compare_exchange_strong(expected,true,std::memory_order_acq_rel)){
        cache_cleanup_hr.store(hr,std::memory_order_release);cache_enabled.store(false,std::memory_order_release);
        log("mesh_cache_fault origin=acquisition_cleanup_failure hr=%08lx restart_required=1 policy=reject_our_hooked_preparation_only native_fallback=0 lock_repaired=0",hr);
    }
}
void cache_report() {
    if(!cache_requested)return;
    auto* const instance=cache_instance.load(std::memory_order_acquire);
    const auto stats=instance?instance->statistics():adjacency_cache::Statistics{};
    for(unsigned i=0;i<gate_reason_count;++i){
        auto& row=cache_gate_reasons[i];const auto count=row.count.load(std::memory_order_relaxed);
        if(count!=cache_gate_last_counts[i]){cache_gate_last_counts[i]=count;log("mesh_cache_gate cumulative=1 reason=%s count=%llu",gate_reason_name(i),count);}
        if(!cache_gate_detail_reported[i]&&row.publication.load(std::memory_order_acquire)==2){
            cache_gate_detail_reported[i]=true;const auto& d=row.first;
            log("mesh_cache_gate_first reason=%s scope=%u options=%08lx slot=%lu actual_entry=%08lx expected_entry=%08lx status=%08lx pool=%lu usage=%08lx format=%lu required_bytes=%llu size_bytes=%llu known=%u pending=%llu",gate_reason_name(i),d.scope,d.options,d.slot,d.actual_entry,d.expected_entry,d.status,d.pool,d.usage,d.format,d.required_bytes,d.size_bytes,d.known,d.pending);
        }
    }
    for(unsigned i=0;i<adjacency_cache::bypass_reason_count;++i)if(stats.bypass_reasons[i]!=cache_bypass_last_counts[i]){
        cache_bypass_last_counts[i]=stats.bypass_reasons[i];log("mesh_cache_bypass cumulative=1 reason=%s count=%llu",adjacency_cache::bypass_reason_name(i),stats.bypass_reasons[i]);
    }
    if(stats.first_fp_available&&!cache_fp_incoming_reported){cache_fp_incoming_reported=true;const auto& f=stats.first_fp;
        log("mesh_cache_fp_incoming control=%08lx status=%08lx tag=%08lx mxcsr=%08lx supported=%u keyed=1 normalized=0",f.control,f.status,f.tag,f.mxcsr,unsigned(stats.first_fp_supported));
    }
    if(stats.unsupported_fp_available&&!cache_fp_detail_reported){cache_fp_detail_reported=true;const auto& f=stats.unsupported_fp;
        log("mesh_cache_fp_first control=%08lx status=%08lx tag=%08lx mxcsr=%08lx",f.control,f.status,f.tag,f.mxcsr);
    }
    const auto blocked=cache_blocked.load(),rejected=cache_gate_rejections.load();
    if(stats.calls==cache_last_report_calls&&blocked==cache_last_report_blocked&&rejected==cache_last_report_rejected)return;
    cache_last_report_calls=stats.calls;cache_last_report_blocked=blocked;cache_last_report_rejected=rejected;
    log("mesh_cache_metric cumulative=1 qpc=%llu dispatch_enabled=%u buffer_contract=public_systemmem_readonly faulted=%u calls=%llu hits=%llu misses=%llu bypasses=%llu contention=%llu admissions=%llu evictions=%llu allocation_failures=%llu acquisition_failures=%llu unrecoverable_unlocks=%llu native_calls=%llu native_failures=%llu retained_bytes=%llu retained_entries=%llu acquired_bytes=%llu copied_bytes=%llu evicted_bytes=%llu acquisition_ticks=%llu lookup_ticks=%llu copy_ticks=%llu native_ticks=%llu total_ticks=%llu gate_rejections=%llu gate_ticks=%llu native_outcomes=%llu hit_outcomes=%llu cleanup_outcomes=%llu blocked=%llu cleanup_hr=%08lx rejected_result=%llu rejected_last_error=%llu rejected_fp=%llu",
        tick(),unsigned(cache_enabled.load()),unsigned(cache_faulted.load()),stats.calls,stats.hits,stats.misses,stats.bypasses,stats.contention,stats.admissions,stats.evictions,stats.allocation_failures,stats.acquisition_failures,stats.unrecoverable_unlocks,stats.native_calls,stats.native_failures,stats.retained_bytes,stats.retained_entries,stats.acquired_bytes,stats.copied_bytes,stats.evicted_bytes,stats.acquisition_ticks,stats.lookup_ticks,stats.copy_ticks,stats.native_ticks,stats.total_ticks,rejected,cache_gate_ticks.load(),cache_native_outcomes.load(),cache_hit_outcomes.load(),cache_cleanup_outcomes.load(),blocked,cache_cleanup_hr.load(),stats.rejected_result,stats.rejected_last_error,stats.rejected_fp);
}

bool pin_address(const void* address,bool executable) {
    if(!readable(address,1,nullptr,executable))return false;
    HMODULE pinned=nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCSTR>(address),&pinned)!=FALSE;
}
bool pin_mesh_module() {
    if(mesh_module_checked)return mesh_module!=nullptr;
    mesh_module_checked=true;
    const auto create=hooks[static_cast<unsigned>(Operation::MeshCreate)].original;
    if(!create||!readable(create,1,nullptr,true)||
       !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
           reinterpret_cast<LPCSTR>(create),&mesh_module))return false;
    // This token separates this process-local adapter generation; it is not a
    // code fingerprint. Each key additionally includes the actual saved method.
    if(cache_requested){
        cache_runtime.algorithm_token=reinterpret_cast<uintptr_t>(mesh_module);cache_runtime.generation=1;
        cache_runtime.public_contract=true;
        cache_runtime.systemmem_dynamic_readonly_verified=true;
        auto* const instance=new(cache_storage) adjacency_cache::Cache();
        cache_instance.store(instance,std::memory_order_release);cache_enabled.store(true,std::memory_order_release);
        log("mesh_cache ready=1 buffer_contract_required=1 identity=process_local generation=1 retained_budget=16777216 scratch_budget=4194304 entries=512 buffer_contract=public_systemmem_readonly file_fingerprint_required=0 serialized_application_calls=1 dynamic_systemmem_contract=1 dynamic_options=990,991,18990,18991");
    }
    log("mesh_trace module_pinned=1 version_gate=0 scope=shared_public_com_vtables table_limit=%u objects_retained=0",mesh_table_limit);
    return true;
}
void observe_mesh(ID3DXMesh* mesh) {
    if(!mesh_observation_enabled.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&mesh_lock);
    if(!mesh_observation_enabled.load(std::memory_order_relaxed)||!pin_mesh_module()||!readable(mesh,sizeof(PVOID))){ReleaseSRWLockExclusive(&mesh_lock);return;}
    auto table=*reinterpret_cast<PVOID**>(mesh);
    if(reinterpret_cast<uintptr_t>(table)%alignof(PVOID)||!readable(table,29*sizeof(PVOID))){ReleaseSRWLockExclusive(&mesh_lock);return;}
    unsigned index=0;
    while(index<mesh_table_limit&&mesh_tables[index].table&&mesh_tables[index].table!=table)++index;
    if(index==mesh_table_limit){ReleaseSRWLockExclusive(&mesh_lock);return;}
    auto& row=mesh_tables[index];
    if(!row.table){
        // Once per distinct shared table: saved table/call targets must outlive
        // trampoline chains. Pin actual owning modules, without placement/RVAs.
        if(!pin_address(table,false)){ReleaseSRWLockExclusive(&mesh_lock);return;}
        for(unsigned slot:{0u,1u,2u,20u,22u,27u}){
            bool ours=false;
            for(const auto& prior:mesh_tables)for(const auto& h:prior.slots)ours|=h.replacement&&table[slot]==h.replacement;
            if(ours||!pin_address(table[slot],true)){ReleaseSRWLockExclusive(&mesh_lock);return;}
        }
        for(unsigned slot:mesh_contract_slots)if(!pin_address(table[slot],true)){ReleaseSRWLockExclusive(&mesh_lock);return;}
        row.table=table;
        for(unsigned i=0;i<11;++i)row.contract[i]=table[mesh_contract_slots[i]];
        for(unsigned i=0;i<3;++i)row.slots[i]={"d3dx9_37.dll",method_names[i],mesh_replacements[index][i],table[mesh_slot_indices[i]],&table[mesh_slot_indices[i]]};
    }
    bool already=true;for(const auto& h:row.slots)already&=*h.slot==h.replacement;
    if(already){ReleaseSRWLockExclusive(&mesh_lock);return;}
    bool okay=true;
    for(unsigned i=0;i<3&&okay;++i){
#ifdef X3M_LOADING_TRACE_FIXTURE
        if(fail_mesh_patch==i+1){fail_mesh_patch=0;okay=false;break;}
#endif
        auto& h=row.slots[i];okay=*h.slot==h.replacement||patch(h,false);
    }
    if(!okay)for(auto& h:row.slots)if(*h.slot==h.replacement)patch(h,true);
    unsigned owned=0;for(const auto& h:row.slots)owned+=*h.slot==h.replacement;
    unsigned total_owned=0;for(const auto& r:mesh_tables)if(r.table)for(const auto& h:r.slots)total_owned+=*h.slot==h.replacement;
    mesh_owned_slots.store(total_owned,std::memory_order_release);
    if(row.last_result!=int(okay)||row.last_owned!=int(owned)){
        log("mesh_hook table=%u installed=%u owned_slots=%u scope=shared_native_vtable",index,okay,owned);
        row.last_result=int(okay);row.last_owned=int(owned);
    }
    ReleaseSRWLockExclusive(&mesh_lock);
}
void restore_mesh_hooks() {
    mesh_observation_enabled.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&mesh_lock);
    for(unsigned i=0;i<mesh_table_limit;++i)if(mesh_tables[i].table){
        unsigned restored=0,foreign=0,remaining=0;
        for(auto& h:mesh_tables[i].slots){
            if(*h.slot==h.replacement){restored+=patch(h,true);remaining+=*h.slot==h.replacement;}
            else foreign+=*h.slot!=h.original;
        }
        log("mesh_hook table=%u restored_slots=%u foreign_slots=%u remaining_owned_slots=%u",i,restored,foreign,remaining);
    }
    unsigned total_owned=0;for(const auto& row:mesh_tables)if(row.table)for(const auto& h:row.slots)total_owned+=*h.slot==h.replacement;
    mesh_owned_slots.store(total_owned,std::memory_order_release);
    // Records/originals are intentionally kept: foreign interceptors may chain
    // to our trampoline after teardown. The native module remains pinned.
    ReleaseSRWLockExclusive(&mesh_lock);
}
bool install(HMODULE target) {
    if(installed.load())return true;
    if(installation_started){log("loading_trace disabled=reinitialization_not_supported");return false;}
    const bool trace=requested(),buffer=gz_buffer::requested();
    if(!trace&&!buffer)return false;
    auto base=reinterpret_cast<unsigned char*>(target);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(!readable(dos,sizeof *dos,target)||dos->e_magic!=IMAGE_DOS_SIGNATURE||
       dos->e_lfanew<static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))||dos->e_lfanew>0x100000)return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(!readable(nt,sizeof *nt,target)||nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_IMPORT||
       nt->OptionalHeader.SizeOfImage<sizeof(IMAGE_DOS_HEADER)||nt->OptionalHeader.SizeOfImage>0x40000000)return false;
    Image image{base,nt->OptionalHeader.SizeOfImage};
    const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress||!image.range(directory.VirtualAddress,directory.Size))return false;
    PVOID originals[count]{};PVOID* slots[count]{};
    hook_count=0;
    for(DWORD pos=0;pos+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=directory.Size;pos+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        auto descriptor=image.at<IMAGE_IMPORT_DESCRIPTOR>(directory.VirtualAddress+pos);
        if(!descriptor)return false;
        if(!descriptor->Name)break;
        const auto dll=image.string(descriptor->Name);if(!dll||!descriptor->OriginalFirstThunk)continue;
        for(DWORD i=0;i<image.size/sizeof(IMAGE_THUNK_DATA32);++i) {
            const uint64_t name_rva=uint64_t(descriptor->OriginalFirstThunk)+uint64_t(i)*sizeof(IMAGE_THUNK_DATA32);
            const uint64_t slot_rva=uint64_t(descriptor->FirstThunk)+uint64_t(i)*sizeof(IMAGE_THUNK_DATA32);
            if(name_rva>0xffffffffull||slot_rva>0xffffffffull)break;
            auto name=image.at<IMAGE_THUNK_DATA32>(static_cast<DWORD>(name_rva));
            auto slot=image.at<IMAGE_THUNK_DATA32>(static_cast<DWORD>(slot_rva));
            if(!name||!slot||!name->u1.AddressOfData)break;
            if(IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal)||name->u1.AddressOfData>0xfffffffd)continue;
            const auto symbol=image.string(name->u1.AddressOfData+2);if(!symbol)continue;
            for(auto& hook:hooks) {
                if(_stricmp(dll,hook.dll)||std::strcmp(symbol,hook.name))continue;
                const unsigned index=static_cast<unsigned>(&hook-hooks);
                if(slots[index])continue;
                if(!slot->u1.Function||reinterpret_cast<uintptr_t>(&slot->u1.Function)%alignof(PVOID)||
                   !readable(reinterpret_cast<PVOID>(slot->u1.Function),1,nullptr,true))continue;
                auto* proposed=reinterpret_cast<PVOID*>(&slot->u1.Function);
                for(auto* existing:slots)if(existing==proposed)return false;
                slots[index]=proposed;originals[index]=reinterpret_cast<PVOID>(slot->u1.Function);
            }
        }
    }
    bool found=false;for(auto* slot:slots)found|=slot!=nullptr;if(!found)return false;
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return false;
    for(auto& hook:hooks){const auto i=&hook-hooks;hook.slot=slots[i];hook.original=originals[i];light::set_original(unsigned(i),originals[i]);}
    const bool nesting=light::initialize();
    if(buffer) {
        gz_buffer::Originals real;
        real.open=trace?light::gz_open_traced:original<GzOpenFn>(Operation::GzOpen);
        real.read=trace?light::gz_read_traced:original<GzReadFn>(Operation::GzRead);
        real.seek=trace?light::gz_seek_traced:original<GzSeekFn>(Operation::GzSeek);
        real.getc=trace?light::gz_getc_traced:original<GzGetcFn>(Operation::GzGetc);
        real.tell=trace?light::gz_tell_traced:original<GzTellFn>(Operation::GzTell);
        real.close=trace?light::gz_close_traced:original<GzCloseFn>(Operation::GzClose);
        bool imports=true;
        for(const auto& hook:hooks)if(gz_buffer_row(hook)&&!hook.slot)imports=false;
        if(imports){
            if(HMODULE zlib=GetModuleHandleW(L"zlib1.dll"))real.rewind=reinterpret_cast<gz_buffer::RewindFn>(GetProcAddress(zlib,"gzrewind"));
            gz_buffer_active=gz_buffer::initialize(real,gz_buffer::requested_capacity());
        }
        log("gz_buffer requested=1 enabled=%u capacity_kb=%u telemetry=%u imports=%u rewind=%u slots=%u",gz_buffer_active,gz_buffer::requested_capacity()/1024,trace,imports,real.rewind!=nullptr,gz_buffer::slot_count);
        if(!gz_buffer_active&&!trace)return false;
    }
    installation_started=true;
    if(!trace) { // buffer only: no mesh observation, no cache/adjacency services
        LARGE_INTEGER buffer_frequency{};QueryPerformanceFrequency(&buffer_frequency);clock_frequency=buffer_frequency.QuadPart;
        for(auto& hook:hooks) {
            if(!gz_buffer_row(hook)){hook.slot=nullptr;continue;}
            if(hook.slot&&patch(hook,false)){++hook_count;log("loading_hook name=%s installed=1",hook.name);}
            else {log("loading_hook name=%s installed=0",hook.name);if(hook.slot&&!has_protection_debt(hook.slot))hook.slot=nullptr;}
        }
        coverage_start=tick();installed.store(hook_count!=0);
        log("loading_trace coverage_begin=%llu frequency=%llu hooks=%u module=main inclusive=0 paths=0 qualification=named_pe32_imports scope=gz_buffer",coverage_start,clock_frequency,hook_count);
        return installed.load();
    }
    wchar_t cache_setting[8]{};cache_requested=GetEnvironmentVariableW(L"X3M_MESH_CACHE",cache_setting,8)==1&&cache_setting[0]==L'1';
    log("mesh_cache requested=%u enabled=0 activation=await_public_mesh_and_buffer_contract adjacency_metric_scope=hook_service restart_on_cleanup_failure=1",cache_requested);
    wchar_t adjacency_setting[16]{};const DWORD adjacency_length=GetEnvironmentVariableW(L"X3M_MESH_ADJACENCY",adjacency_setting,16);
    AdjacencyMode requested_mode=AdjacencyMode::Native;
    if(adjacency_length&&adjacency_length<16){if(!lstrcmpiW(adjacency_setting,L"fast"))requested_mode=AdjacencyMode::Fast;else if(!lstrcmpiW(adjacency_setting,L"verify"))requested_mode=AdjacencyMode::Verify;}
    adjacency_mode.store(requested_mode,std::memory_order_release);
    wchar_t dump_setting[8]{};adjacency_dump_requested.store(GetEnvironmentVariableW(L"X3M_MESH_ADJACENCY_DUMP",dump_setting,8)==1&&dump_setting[0]==L'1',std::memory_order_relaxed);
    log("mesh_adjacency mode=%s scope=hook_service equivalence=d3dx_rules+exact_position_equality gate=public_systemmem_readonly+declaration_float3+no_attribute_table order=cache_lookup,compute,cache_store native_fallback=1 dump=%u rsqrt=%s math_table=%s normalize=%s",adjacency_mode_name(requested_mode),unsigned(adjacency_dump_requested.load(std::memory_order_relaxed)),adjacency_fast::rsqrt_implementation(),
        d3dx_math_table_name(d3dx_math_table()),d3dx_math_table()==D3dxMathTable::Sse2?"sse2":d3dx_math_table()==D3dxMathTable::Generic?"generic":"native_only");
    clock_frequency=frequency.QuadPart;
    mesh_observation_enabled.store(true,std::memory_order_release);
    const bool probes=probes_requested();
    for(auto& hook:hooks) {
        if(probe_row(unsigned(&hook-hooks))&&!probes){hook.slot=nullptr;continue;}
        if(hook.slot&&patch(hook,false)){++hook_count;log("loading_hook name=%s installed=1",hook.name);}
        else {log("loading_hook name=%s installed=0",hook.name);if(hook.slot&&!has_protection_debt(hook.slot))hook.slot=nullptr;}
    }
    coverage_start=tick();installed.store(hook_count!=0);
    log("loading_trace coverage_begin=%llu frequency=%llu hooks=%u module=main inclusive=1 paths=0 qualification=named_pe32_imports light_rows=1 nesting=%u probes=%u",coverage_start,clock_frequency,hook_count,unsigned(nesting),unsigned(probes));
    if(probes)loading_probes::initialize();
    return installed.load();
}
}

bool initialize(){const DWORD error=GetLastError();bool result=install(GetModuleHandleW(nullptr));SetLastError(error);return result;}
bool active(){return installed.load()||mesh_owned_slots.load()||protection_debts.load();}
Snapshot take_snapshot() {
    Snapshot result{};
    for(unsigned i=0;i<count;++i)light::take(i,result[i]);
    return result;
}
void report() {
    if(!active())return;
    const DWORD error=GetLastError();const auto fp=computational_state();const auto data=take_snapshot();const auto end=tick();
    for(unsigned i=0;i<count;++i){const auto& s=data[i];if(!s.count&&!s.failures&&!s.pending&&!s.ambiguous&&!s.bytes&&!s.inclusive_ticks&&!s.exclusive_ticks&&!s.maximum_ticks&&!s.overhead_ticks)continue;
        log("loading_metric op=%s qpc=%llu count=%llu failures=%llu pending=%llu ambiguous=%llu bytes=%llu inclusive_ticks=%llu exclusive_ticks=%llu max_ticks=%llu wrapper_tail_ticks=%llu total_us=%.3f exclusive_us=%.3f max_us=%.3f wrapper_tail_us=%.3f",
            operation_name(i),end,s.count,s.failures,s.pending,s.ambiguous,s.bytes,s.inclusive_ticks,s.exclusive_ticks,s.maximum_ticks,s.overhead_ticks,
            double(s.inclusive_ticks)*1e6/clock_frequency,double(s.exclusive_ticks)*1e6/clock_frequency,double(s.maximum_ticks)*1e6/clock_frequency,double(s.overhead_ticks)*1e6/clock_frequency);
    }cache_report();adjacency_report();loading_probes::report();resource_reader::report();restore_computational_state(fp);SetLastError(error);
}
void shutdown() {
    const DWORD error=GetLastError();
    loading_probes::shutdown();
    cache_enabled.store(false,std::memory_order_release);
    if(auto* instance=cache_instance.load(std::memory_order_acquire))instance->clear();
    restore_mesh_hooks();
    for(auto& hook:hooks)if(hook.slot){
        const bool owned=*hook.slot==hook.replacement;
        const bool restored=owned&&patch(hook,true);
        log("loading_hook name=%s restored=%u owned=%u",hook.name,restored,owned);
        // Keep callable original pointers: a callback already dispatched to this
        // module must still be valid. Callers must ensure teardown is quiescent.
        if(restored||*hook.slot!=hook.replacement)hook.slot=nullptr;
    }
    hook_count=0;
    for(const auto& hook:hooks)if(hook.slot)++hook_count;
    installed.store(hook_count!=0);recover_protections();SetLastError(error);
}
#ifdef X3M_LOADING_TRACE_FIXTURE
bool fixture_initialize(HMODULE target){return install(target);}
void fixture_fail_mesh_patch(unsigned step){fail_mesh_patch=step;}
void fixture_fail_protection_restores(unsigned calls){fail_protection_restore=calls;}
unsigned fixture_protection_debts(){return protection_debts.load();}
mesh_adjacency_cache::Statistics fixture_cache_statistics(){auto* instance=cache_instance.load(std::memory_order_acquire);return instance?instance->statistics():mesh_adjacency_cache::Statistics{};}
bool fixture_cache_constructed(){return cache_instance.load(std::memory_order_acquire)!=nullptr;}
bool fixture_cache_faulted(){return cache_faulted.load();}
uint64_t fixture_cache_gate_rejections(){return cache_gate_rejections.load();}
uint64_t fixture_cache_blocked(){return cache_blocked.load();}
void fixture_cache_cleanup_failure(HRESULT hr){forced_cache_cleanup.store(hr);}
void fixture_cache_reenter_once(){cache_reentry_once=true;}
bool fixture_cache_contract(ID3DXMesh* mesh){return cache_buffer_contract(mesh);}
uint64_t fixture_cache_gate_reason(const char* reason){for(unsigned i=0;i<gate_reason_count;++i)if(!std::strcmp(reason,gate_reason_name(i)))return cache_gate_reasons[i].count.load();return 0;}
void fixture_adjacency_mode(unsigned mode){adjacency_mode.store(mode==2?AdjacencyMode::Fast:mode==1?AdjacencyMode::Verify:AdjacencyMode::Native,std::memory_order_release);}
AdjacencyStatistics fixture_adjacency_statistics(){
    const auto& c=adjacency_counters;AdjacencyStatistics s;
    s.calls=c.calls.load();s.computed=c.computed.load();s.fallbacks=c.fallbacks.load();s.faults=c.faults.load();s.fast_ticks=c.fast_ticks.load();s.native_ticks=c.native_ticks.load();
    s.verify_meshes=c.verify_meshes.load();s.verify_equal=c.verify_equal.load();s.verify_mismatched=c.verify_mismatched.load();s.verify_mismatch_entries=c.verify_mismatch_entries.load();
    s.quantized=c.quantized.load();s.unquantized=c.unquantized.load();s.multi_candidate_meshes=c.multi_candidate_meshes.load();
    for(unsigned i=0;i<adjacency_fallback_count;++i)s.fallback_reasons[i]=c.fallback_reasons[i].load();
    for(unsigned i=0;i<adjacency_fast::status_count;++i)s.module_status[i]=c.module_status[i].load();
    s.faulted=adjacency_faulted.load();return s;
}
void fixture_adjacency_report(){adjacency_report();}
#endif
}
