#include "loading_trace.h"
#include "capture.h"
#include "mesh_adjacency_cache.h"
#include "../ownership/d3d9_ownership.h"
#include <new>
#include <d3dx9.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <type_traits>

namespace x3m::loading_trace {
namespace {
constexpr unsigned count=static_cast<unsigned>(Operation::Count);
struct Counter {
    std::atomic<uint64_t> calls{0}, failures{0}, pending{0}, ambiguous{0}, bytes{0};
    std::atomic<uint64_t> inclusive{0}, exclusive{0}, maximum{0}, overhead{0};
};
Counter counters[count];
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
bool cache_fp_detail_reported=false;
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
uint64_t tick() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }

// Thread-local nesting prevents double counting another loading hook's entire
// inclusive interval. Other telemetry categories (e.g. backend CreateShader)
// are not part of this tree: their totals must not be added to Effect/Texture.
struct Span;
thread_local Span* parent_span=nullptr;
struct Span {
    DWORD caller_error; Operation op; Span* parent; uint64_t begin, children=0;
    explicit Span(Operation value):caller_error(GetLastError()),op(value),parent(parent_span),begin(tick()) {
        parent_span=this;
        SetLastError(caller_error);
    }
    void finish(uint64_t end,DWORD result_error,bool failed=false,uint64_t bytes=0,bool pending=false,bool ambiguous=false) {
        auto& c=counters[static_cast<unsigned>(op)];
        const uint64_t elapsed=end-begin;
        c.calls.fetch_add(1,std::memory_order_relaxed);
        c.failures.fetch_add(failed,std::memory_order_relaxed);
        c.pending.fetch_add(pending,std::memory_order_relaxed);
        c.ambiguous.fetch_add(ambiguous,std::memory_order_relaxed);
        c.bytes.fetch_add(bytes,std::memory_order_relaxed);
        c.inclusive.fetch_add(elapsed,std::memory_order_relaxed);
        c.exclusive.fetch_add(elapsed>children ? elapsed-children : 0,std::memory_order_relaxed);
        auto old=c.maximum.load(std::memory_order_relaxed);
        while(old<elapsed&&!c.maximum.compare_exchange_weak(old,elapsed,std::memory_order_relaxed)) {}
        parent_span=parent;
        const uint64_t tail=tick();
        // Own measured tail excludes the last accounting stores and SetLastError.
        // Backend duration begins before the final caller-error restoration, so
        // it contains that tiny wrapper entry cost; report this limitation.
        c.overhead.fetch_add(tail-end,std::memory_order_relaxed);
        if(parent)parent->children+=tail-begin;
        SetLastError(result_error);
    }
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
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    Span span(Operation::MeshPointReps);
    const HRESULT hr=reinterpret_cast<PointRepsFn>(mesh_tables[Index].slots[0].original)(mesh,reps,adjacency);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(hr));return hr;
}
template<unsigned Index> HRESULT WINAPI mesh_adjacency(ID3DXMesh* mesh,FLOAT epsilon,DWORD* adjacency) {
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    auto original=reinterpret_cast<AdjacencyFn>(mesh_tables[Index].slots[1].original);
    if(!cache_requested||!cache_enabled.load(std::memory_order_acquire)){
        Span span(Operation::MeshAdjacency);const HRESULT hr=original(mesh,epsilon,adjacency);
        const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(hr));return hr;
    }
    const DWORD incoming_error=GetLastError();const auto incoming=computational_state();
    Span span(Operation::MeshAdjacency);const auto gate_begin=tick();
    auto* const instance=cache_instance.load(std::memory_order_acquire);
    const bool eligible=!instance?reject_gate(GateReason::Unavailable):(!mesh||!adjacency)?reject_gate(GateReason::Input):cache_buffer_contract(mesh);
    cache_gate_ticks.fetch_add(tick()-gate_begin,std::memory_order_relaxed);
    restore_computational_state(incoming);SetLastError(incoming_error);
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
    const DWORD error=GetLastError();const auto outgoing=computational_state();const auto end=tick();
    if(outcome.origin==adjacency_cache::Origin::CacheHit)cache_hit_outcomes.fetch_add(1,std::memory_order_relaxed);
    else if(outcome.origin==adjacency_cache::Origin::Native)cache_native_outcomes.fetch_add(1,std::memory_order_relaxed);
    else cache_fault(outcome.hr);
    span.finish(end,error,FAILED(outcome.hr));restore_computational_state(outgoing);SetLastError(error);return outcome.hr;
}
template<unsigned Index> HRESULT WINAPI mesh_optimize(ID3DXMesh* mesh,DWORD flags,const DWORD* in,DWORD* out,DWORD* faces,ID3DXBuffer** vertices) {
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    Span span(Operation::MeshOptimize);
    const HRESULT hr=reinterpret_cast<OptimizeFn>(mesh_tables[Index].slots[2].original)(mesh,flags,in,out,faces,vertices);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(hr));return hr;
}
#define MESH_THUNKS(i) {reinterpret_cast<PVOID>(mesh_point_reps<i>),reinterpret_cast<PVOID>(mesh_adjacency<i>),reinterpret_cast<PVOID>(mesh_optimize<i>)}
PVOID mesh_replacements[mesh_table_limit][3]={MESH_THUNKS(0),MESH_THUNKS(1),MESH_THUNKS(2),MESH_THUNKS(3),MESH_THUNKS(4),MESH_THUNKS(5),MESH_THUNKS(6),MESH_THUNKS(7)};
#undef MESH_THUNKS

HANDLE WINAPI file_open(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
BOOL WINAPI file_read(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
DWORD WINAPI file_seek(HANDLE,LONG,PLONG,DWORD);
HRESULT WINAPI effect(IDirect3DDevice9*,const void*,UINT,const D3DXMACRO*,ID3DXInclude*,DWORD,ID3DXEffectPool*,ID3DXEffect**,ID3DXBuffer**);
HRESULT WINAPI texture(IDirect3DDevice9*,const void*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,D3DXIMAGE_INFO*,PALETTEENTRY*,IDirect3DTexture9**);
HRESULT WINAPI cube(IDirect3DDevice9*,const void*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,D3DXIMAGE_INFO*,PALETTEENTRY*,IDirect3DCubeTexture9**);
HRESULT WINAPI surface(IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,const void*,UINT,const RECT*,DWORD,D3DCOLOR,D3DXIMAGE_INFO*);
HRESULT WINAPI mesh_create(DWORD,DWORD,DWORD,const D3DVERTEXELEMENT9*,IDirect3DDevice9*,ID3DXMesh**);
HRESULT WINAPI mesh_clean(D3DXCLEANTYPE,ID3DXMesh*,const DWORD*,ID3DXMesh**,DWORD*,ID3DXBuffer**);
HCURSOR WINAPI cursor_set(HCURSOR);
BOOL WINAPI cursor_position(int,int);
// cdecl and argument widths corroborated by target callsites; local zlib/libxml
// SDK prototypes supply semantics. Opaque pointers avoid importing struct layouts.
using GzOpenFn=void* (__cdecl*)(const char*,const char*);
using GzReadFn=int (__cdecl*)(void*,void*,unsigned);
using GzSeekFn=LONG (__cdecl*)(void*,LONG,int);
using InflateFn=int (__cdecl*)(void*,int);
using XmlReadFn=void* (__cdecl*)(const char*,int,const char*,const char*,int);
static_assert(sizeof(LONG)==4&&sizeof(int)==4&&sizeof(void*)==4);
void* __cdecl gz_open(const char*,const char*);
int __cdecl gz_read(void*,void*,unsigned);
LONG __cdecl gz_seek(void*,LONG,int);
int __cdecl inflate_stream(void*,int);
void* __cdecl xml_read(const char*,int,const char*,const char*,int);
Hook hooks[]={
    {"KERNEL32.dll","CreateFileA",reinterpret_cast<PVOID>(file_open)},
    {"KERNEL32.dll","ReadFile",reinterpret_cast<PVOID>(file_read)},
    {"KERNEL32.dll","SetFilePointer",reinterpret_cast<PVOID>(file_seek)},
    {"d3dx9_37.dll","D3DXCreateEffect",reinterpret_cast<PVOID>(effect)},
    {"d3dx9_37.dll","D3DXCreateTextureFromFileInMemoryEx",reinterpret_cast<PVOID>(texture)},
    {"d3dx9_37.dll","D3DXCreateCubeTextureFromFileInMemoryEx",reinterpret_cast<PVOID>(cube)},
    {"d3dx9_37.dll","D3DXLoadSurfaceFromFileInMemory",reinterpret_cast<PVOID>(surface)},
    {"USER32.dll","SetCursor",reinterpret_cast<PVOID>(cursor_set)},
    {"USER32.dll","SetCursorPos",reinterpret_cast<PVOID>(cursor_position)},
    {"zlib1.dll","gzopen",reinterpret_cast<PVOID>(gz_open)},
    {"zlib1.dll","gzread",reinterpret_cast<PVOID>(gz_read)},
    {"zlib1.dll","gzseek",reinterpret_cast<PVOID>(gz_seek)},
    {"zlib1.dll","inflate",reinterpret_cast<PVOID>(inflate_stream)},
    {"libxml2.dll","xmlReadMemory",reinterpret_cast<PVOID>(xml_read)},
    {"d3dx9_37.dll","D3DXCreateMesh",reinterpret_cast<PVOID>(mesh_create)},
    {"d3dx9_37.dll","D3DXCleanMesh",reinterpret_cast<PVOID>(mesh_clean)}
};
constexpr unsigned import_count=sizeof hooks/sizeof *hooks;
static_assert(import_count==static_cast<unsigned>(Operation::MeshPointReps));
const char* method_names[]={"ID3DXMesh::ConvertPointRepsToAdjacency","ID3DXMesh::GenerateAdjacency","ID3DXMesh::OptimizeInplace"};
const char* operation_name(unsigned index){return index<import_count?hooks[index].name:method_names[index-import_count];}
static_assert(std::is_same_v<decltype(&file_open),decltype(&CreateFileA)>);
static_assert(std::is_same_v<decltype(&file_read),decltype(&ReadFile)>);
static_assert(std::is_same_v<decltype(&file_seek),decltype(&SetFilePointer)>);
static_assert(std::is_same_v<decltype(&effect),decltype(&D3DXCreateEffect)>);
static_assert(std::is_same_v<decltype(&texture),decltype(&D3DXCreateTextureFromFileInMemoryEx)>);
static_assert(std::is_same_v<decltype(&cube),decltype(&D3DXCreateCubeTextureFromFileInMemoryEx)>);
static_assert(std::is_same_v<decltype(&surface),decltype(&D3DXLoadSurfaceFromFileInMemory)>);
static_assert(std::is_same_v<decltype(&mesh_create),decltype(&D3DXCreateMesh)>);
static_assert(std::is_same_v<decltype(&mesh_clean),decltype(&D3DXCleanMesh)>);
static_assert(std::is_same_v<decltype(&cursor_set),decltype(&SetCursor)>);
static_assert(std::is_same_v<decltype(&cursor_position),decltype(&SetCursorPos)>);
template<typename T>T original(Operation op){return reinterpret_cast<T>(hooks[static_cast<unsigned>(op)].original);}

HANDLE WINAPI file_open(LPCSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templ) {
    Span span(Operation::FileOpen);
    HANDLE result=original<decltype(&CreateFileA)>(span.op)(name,access,share,security,creation,flags,templ);
    const DWORD error=GetLastError(); const auto end=tick();
    span.finish(end,error,result==INVALID_HANDLE_VALUE);return result;
}
BOOL WINAPI file_read(HANDLE file,LPVOID buffer,DWORD requested,LPDWORD read,LPOVERLAPPED overlapped) {
    Span span(Operation::FileRead);
    BOOL result=original<decltype(&ReadFile)>(span.op)(file,buffer,requested,read,overlapped);
    const DWORD error=GetLastError(); const auto end=tick();
    const bool pending=!result&&error==ERROR_IO_PENDING;
    span.finish(end,error,!result&&!pending,result&&read?*read:0,pending);return result;
}
DWORD WINAPI file_seek(HANDLE file,LONG distance,PLONG high,DWORD method) {
    Span span(Operation::FileSeek);
    DWORD result=original<decltype(&SetFilePointer)>(span.op)(file,distance,high,method);
    const DWORD error=GetLastError(); const auto end=tick();
    // The sentinel can be a successful offset. Preserve caller LastError rather
    // than forcing it to zero just to make our failure classification convenient.
    span.finish(end,error,false,0,false,result==INVALID_SET_FILE_POINTER&&error!=NO_ERROR);return result;
}
HRESULT WINAPI effect(IDirect3DDevice9* d,const void* data,UINT size,const D3DXMACRO* defines,ID3DXInclude* include,DWORD flags,ID3DXEffectPool* pool,ID3DXEffect** out,ID3DXBuffer** errors) {
    Span span(Operation::Effect);
    HRESULT result=original<decltype(&D3DXCreateEffect)>(span.op)(d,data,size,defines,include,flags,pool,out,errors);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI texture(IDirect3DDevice9* d,const void* data,UINT size,UINT width,UINT height,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,DWORD filter,DWORD mipfilter,D3DCOLOR key,D3DXIMAGE_INFO* info,PALETTEENTRY* palette,IDirect3DTexture9** out) {
    Span span(Operation::Texture);
    HRESULT result=original<decltype(&D3DXCreateTextureFromFileInMemoryEx)>(span.op)(d,data,size,width,height,levels,usage,format,pool,filter,mipfilter,key,info,palette,out);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI cube(IDirect3DDevice9* d,const void* data,UINT size,UINT edge,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,DWORD filter,DWORD mipfilter,D3DCOLOR key,D3DXIMAGE_INFO* info,PALETTEENTRY* palette,IDirect3DCubeTexture9** out) {
    Span span(Operation::CubeTexture);
    HRESULT result=original<decltype(&D3DXCreateCubeTextureFromFileInMemoryEx)>(span.op)(d,data,size,edge,levels,usage,format,pool,filter,mipfilter,key,info,palette,out);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI surface(IDirect3DSurface9* dest,const PALETTEENTRY* palette,const RECT* destrect,const void* data,UINT size,const RECT* srcrect,DWORD filter,D3DCOLOR key,D3DXIMAGE_INFO* info) {
    Span span(Operation::Surface);
    HRESULT result=original<decltype(&D3DXLoadSurfaceFromFileInMemory)>(span.op)(dest,palette,destrect,data,size,srcrect,filter,key,info);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,FAILED(result),size);return result;
}
HRESULT WINAPI mesh_create(DWORD faces,DWORD vertices,DWORD options,const D3DVERTEXELEMENT9* declaration,IDirect3DDevice9* device,ID3DXMesh** out) {
    const bool preserve_fp=cache_requested;DWORD incoming_error=0;ComputationalState incoming;
    if(preserve_fp){incoming_error=GetLastError();incoming=computational_state();}
    Span span(Operation::MeshCreate);
    if(preserve_fp){restore_computational_state(incoming);SetLastError(incoming_error);}
    HRESULT result=original<decltype(&D3DXCreateMesh)>(span.op)(faces,vertices,options,declaration,device,out);
    const DWORD error=GetLastError();
    if(preserve_fp){const auto outgoing=computational_state();const auto end=tick();if(SUCCEEDED(result)&&out&&*out)observe_mesh(*out);span.finish(end,error,FAILED(result));restore_computational_state(outgoing);SetLastError(error);return result;}
    const auto end=tick();
    if(SUCCEEDED(result)&&out&&*out)observe_mesh(*out);
    span.finish(end,error,FAILED(result));return result;
}
HRESULT WINAPI mesh_clean(D3DXCLEANTYPE type,ID3DXMesh* input,const DWORD* adjacency_in,ID3DXMesh** output,DWORD* adjacency_out,ID3DXBuffer** errors) {
    if(cache_requested&&cache_faulted.load(std::memory_order_acquire)){cache_blocked.fetch_add(1);return E_FAIL;}
    const bool preserve_fp=cache_requested;DWORD incoming_error=0;ComputationalState incoming;
    if(preserve_fp){incoming_error=GetLastError();incoming=computational_state();}
    Span span(Operation::MeshClean);
    if(preserve_fp){restore_computational_state(incoming);SetLastError(incoming_error);}
    HRESULT result=original<decltype(&D3DXCleanMesh)>(span.op)(type,input,adjacency_in,output,adjacency_out,errors);
    const DWORD error=GetLastError();
    if(preserve_fp){const auto outgoing=computational_state();const auto end=tick();if(SUCCEEDED(result)&&output&&*output)observe_mesh(*output);span.finish(end,error,FAILED(result));restore_computational_state(outgoing);SetLastError(error);return result;}
    const auto end=tick();
    if(SUCCEEDED(result)&&output&&*output)observe_mesh(*output);
    span.finish(end,error,FAILED(result));return result;
}
HCURSOR WINAPI cursor_set(HCURSOR value) {
    Span span(Operation::CursorSet);HCURSOR result=original<decltype(&SetCursor)>(span.op)(value);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error);return result;
}
BOOL WINAPI cursor_position(int x,int y) {
    Span span(Operation::CursorPosition);BOOL result=original<decltype(&SetCursorPos)>(span.op)(x,y);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,!result);return result;
}

void* __cdecl gz_open(const char* path,const char* mode) {
    Span span(Operation::GzOpen);void* result=original<GzOpenFn>(span.op)(path,mode);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,!result);return result;
}
int __cdecl gz_read(void* file,void* data,unsigned size) {
    Span span(Operation::GzRead);int result=original<GzReadFn>(span.op)(file,data,size);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,result<0,result>0?result:0);return result;
}
LONG __cdecl gz_seek(void* file,LONG offset,int whence) {
    Span span(Operation::GzSeek);LONG result=original<GzSeekFn>(span.op)(file,offset,whence);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,result<0);return result;
}
int __cdecl inflate_stream(void* stream,int flush) {
    Span span(Operation::Inflate);int result=original<InflateFn>(span.op)(stream,flush);
    const DWORD error=GetLastError();const auto end=tick();
    // Z_BUF_ERROR (-5) is nonfatal no-progress, recorded as ambiguous instead of
    // treating it as failed decompression. No z_stream member is dereferenced.
    span.finish(end,error,result<0&&result!=-5,0,false,result==-5);return result;
}
void* __cdecl xml_read(const char* data,int size,const char* url,const char* encoding,int options) {
    Span span(Operation::XmlRead);void* result=original<XmlReadFn>(span.op)(data,size,url,encoding,options);
    const DWORD error=GetLastError();const auto end=tick();span.finish(end,error,!result,size>0?size:0);return result;
}

bool requested() { wchar_t setting[8]{};return GetEnvironmentVariableW(L"X3M_TELEMETRY",setting,8)==1&&setting[0]==L'1'; }
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
    if(!requested())return false;
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
    for(auto& hook:hooks){const auto i=&hook-hooks;hook.slot=slots[i];hook.original=originals[i];}
    installation_started=true;
    wchar_t cache_setting[8]{};cache_requested=GetEnvironmentVariableW(L"X3M_MESH_CACHE",cache_setting,8)==1&&cache_setting[0]==L'1';
    log("mesh_cache requested=%u enabled=0 activation=await_public_mesh_and_buffer_contract adjacency_metric_scope=hook_service restart_on_cleanup_failure=1",cache_requested);
    clock_frequency=frequency.QuadPart;
    mesh_observation_enabled.store(true,std::memory_order_release);
    for(auto& hook:hooks) {
        if(hook.slot&&patch(hook,false)){++hook_count;log("loading_hook name=%s installed=1",hook.name);}
        else {log("loading_hook name=%s installed=0",hook.name);if(hook.slot&&!has_protection_debt(hook.slot))hook.slot=nullptr;}
    }
    coverage_start=tick();installed.store(hook_count!=0);
    log("loading_trace coverage_begin=%llu frequency=%llu hooks=%u module=main inclusive=1 paths=0 qualification=named_pe32_imports",coverage_start,clock_frequency,hook_count);
    return installed.load();
}
}

bool initialize(){const DWORD error=GetLastError();bool result=install(GetModuleHandleW(nullptr));SetLastError(error);return result;}
bool active(){return installed.load()||mesh_owned_slots.load()||protection_debts.load();}
Snapshot take_snapshot() {
    Snapshot result{};
    for(unsigned i=0;i<count;++i){auto& c=counters[i];auto& s=result[i];
        s.count=c.calls.exchange(0);s.failures=c.failures.exchange(0);s.pending=c.pending.exchange(0);s.ambiguous=c.ambiguous.exchange(0);
        s.bytes=c.bytes.exchange(0);s.inclusive_ticks=c.inclusive.exchange(0);s.exclusive_ticks=c.exclusive.exchange(0);
        s.maximum_ticks=c.maximum.exchange(0);s.overhead_ticks=c.overhead.exchange(0);
    }return result;
}
void report() {
    if(!active())return;
    const DWORD error=GetLastError();const auto fp=computational_state();const auto data=take_snapshot();const auto end=tick();
    for(unsigned i=0;i<count;++i){const auto& s=data[i];if(!s.count&&!s.failures&&!s.pending&&!s.ambiguous&&!s.bytes&&!s.inclusive_ticks&&!s.exclusive_ticks&&!s.maximum_ticks&&!s.overhead_ticks)continue;
        log("loading_metric op=%s qpc=%llu count=%llu failures=%llu pending=%llu ambiguous=%llu bytes=%llu inclusive_ticks=%llu exclusive_ticks=%llu max_ticks=%llu wrapper_tail_ticks=%llu total_us=%.3f exclusive_us=%.3f max_us=%.3f wrapper_tail_us=%.3f",
            operation_name(i),end,s.count,s.failures,s.pending,s.ambiguous,s.bytes,s.inclusive_ticks,s.exclusive_ticks,s.maximum_ticks,s.overhead_ticks,
            double(s.inclusive_ticks)*1e6/clock_frequency,double(s.exclusive_ticks)*1e6/clock_frequency,double(s.maximum_ticks)*1e6/clock_frequency,double(s.overhead_ticks)*1e6/clock_frequency);
    }cache_report();restore_computational_state(fp);SetLastError(error);
}
void shutdown() {
    const DWORD error=GetLastError();
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
#endif
}
