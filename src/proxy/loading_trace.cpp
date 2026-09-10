#include "loading_trace.h"
#include "capture.h"
#include "mesh_adjacency_cache.h"
#include "../ownership/d3d9_ownership.h"
#include <wincrypt.h>
#include <new>
#include <d3dx9.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <type_traits>

namespace x3m::loading_trace {
namespace {
constexpr uint64_t expected_x3_hash=0x96f0b2777c624f6dull;
constexpr DWORD expected_x3_size=2153984;
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
struct MeshTable { PVOID* table=nullptr; Hook slots[3]{}; int last_result=-1,last_owned=-1; };
MeshTable mesh_tables[mesh_table_limit];
SRWLOCK mesh_lock=SRWLOCK_INIT;
std::atomic<bool> mesh_observation_enabled{false};
HMODULE mesh_module=nullptr; // Exact verified module is process-lifetime pinned.
bool mesh_module_checked=false;
namespace adjacency_cache=mesh_adjacency_cache;
// Off is the existing direct dispatch path: no construction/hash/acquisition.
bool cache_requested=false;
std::atomic<bool> cache_enabled{false},cache_faulted{false};
alignas(adjacency_cache::Cache) unsigned char cache_storage[sizeof(adjacency_cache::Cache)];
// Immutable process-lifetime publication; reports need not acquire mesh_lock.
std::atomic<adjacency_cache::Cache*> cache_instance{nullptr};
adjacency_cache::RuntimeIdentity cache_runtime;
HMODULE cache_backend=nullptr,cache_wined3d=nullptr;
std::atomic<bool> cache_backend_verified{false};
bool cache_backend_checked=false;
std::atomic<uint64_t> cache_native_outcomes{0},cache_hit_outcomes{0},cache_cleanup_outcomes{0},cache_blocked{0};
std::atomic<uint64_t> cache_gate_rejections{0},cache_gate_ticks{0};
std::atomic<HRESULT> cache_cleanup_hr{S_OK};
uint64_t cache_last_report_calls=0,cache_last_report_blocked=0,cache_last_report_rejected=0;
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
    const bool eligible=instance&&mesh&&adjacency&&cache_buffer_contract(mesh);
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
uint64_t fingerprint(HMODULE module,DWORD* size_out) {
    wchar_t path[32768]{};
    const DWORD n=GetModuleFileNameW(module,path,32768);
    if(!n||n>=32768)return 0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>64*1024*1024){CloseHandle(file);return 0;}
    uint64_t hash=14695981039346656037ull;DWORD total=0,got=0;unsigned char bytes[16384];bool okay=true;
    while(total<static_cast<uint64_t>(size.QuadPart)) {
        if(!ReadFile(file,bytes,sizeof bytes,&got,nullptr)||!got){okay=false;break;}
        for(DWORD i=0;i<got;++i){hash^=bytes[i];hash*=1099511628211ull;}
        total+=got;
    }
    CloseHandle(file);if(size_out)*size_out=total;return okay?hash:0;
}

// Only validated module memory is traversed. Reject unterminated names, missing
// OriginalFirstThunk and RVAs that escape SizeOfImage; never infer names from IAT.
struct Image {
    unsigned char* base;DWORD size;
    bool range(DWORD rva,size_t length)const{return rva<size&&length<=size-rva;}
    template<typename T>T* at(DWORD rva,size_t n=1)const{return range(rva,sizeof(T)*n)?reinterpret_cast<T*>(base+rva):nullptr;}
    const char* string(DWORD rva)const {
        if(!range(rva,1))return nullptr;
        const char* s=reinterpret_cast<char*>(base+rva);
        return std::memchr(s,0,size-rva)?s:nullptr;
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
bool readable(const void* address,size_t bytes,HMODULE owner=nullptr,bool executable=false) {
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
constexpr unsigned char native_mesh_sha[32]={0xc2,0xcc,0xb8,0x4c,0x67,0x2a,0x9d,0x89,0x66,0xe8,0x2a,0x28,0x00,0x5a,0x42,0x69,0x88,0x6e,0xe3,0x04,0x97,0x2a,0xc3,0x59,0x0c,0x0b,0x8a,0x9c,0x16,0x22,0xa3,0xd8};
constexpr unsigned char buffer_backend_sha[32]={0x58,0xcc,0x36,0xcf,0x74,0x12,0x8a,0xe4,0xb6,0x21,0x11,0x00,0x43,0x0d,0x14,0x6c,0x36,0x92,0x80,0x81,0x46,0xd8,0xd2,0x07,0x5e,0x6c,0x5d,0x84,0x61,0x62,0xf8,0xcf};

bool sha256_module(HMODULE module,const unsigned char expected[32]) {
    wchar_t path[32768]{};const DWORD length=GetModuleFileNameW(module,path,32768);
    if(!length||length>=32768)return false;
    // This executes inside x86 Wine: System32 is redirected to the actual x86
    // module. Host tooling must fingerprint SysWOW64, not the x64 host path.
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    bool okay=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=64*1024*1024&&
        CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&CryptCreateHash(provider,CALG_SHA_256,0,0,&hash);
    unsigned char buffer[16384];DWORD got=0;uint64_t total=0;
    while(okay&&total<uint64_t(size.QuadPart)){
        if(!ReadFile(file,buffer,sizeof buffer,&got,nullptr)||!got){okay=false;break;}
        total+=got;okay=CryptHashData(hash,buffer,got,0)!=FALSE;
    }
    unsigned char digest[32]{};DWORD bytes=sizeof digest;
    okay=okay&&total==uint64_t(size.QuadPart)&&CryptGetHashParam(hash,HP_HASHVAL,digest,&bytes,0)&&bytes==32&&!std::memcmp(digest,expected,32);
    if(hash)CryptDestroyHash(hash);
    if(provider)CryptReleaseContext(provider,0);
    CloseHandle(file);return okay;
}
constexpr unsigned char wined3d_backend_sha[32]={0xf4,0x99,0x7b,0xc0,0x46,0x5d,0xe7,0xe8,0x7b,0xac,0x99,0x21,0xbf,0x02,0x74,0xdb,0x00,0xac,0x3b,0x3b,0xa0,0x75,0x4f,0xa0,0x3f,0x1f,0x33,0xe3,0x09,0xa8,0xe8,0x63};

bool backend_imports_match(HMODULE backend,HMODULE wined3d) {
    constexpr unsigned slots[]={0x23460,0x235b8,0x235c4};
    constexpr unsigned targets[]={0x1ac00,0x7b690,0x7b700};
    const char* names[]={"wined3d_buffer_get_resource","wined3d_resource_map","wined3d_resource_unmap"};
    for(unsigned i=0;i<3;++i){
        auto slot=reinterpret_cast<PVOID*>(reinterpret_cast<uintptr_t>(backend)+slots[i]);
        auto target=reinterpret_cast<PVOID>(reinterpret_cast<uintptr_t>(wined3d)+targets[i]);
        if(!readable(slot,sizeof *slot,backend)||*slot!=target||reinterpret_cast<PVOID>(GetProcAddress(wined3d,names[i]))!=target)return false;
    }
    return true;
}
bool verify_wined3d(HMODULE backend) {
    auto slot=reinterpret_cast<PVOID*>(reinterpret_cast<uintptr_t>(backend)+0x235c4);
    if(!readable(slot,sizeof *slot,backend))return false;
    HMODULE candidate=nullptr,pinned=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCSTR>(*slot),&candidate))return false;
    bool okay=sha256_module(candidate,wined3d_backend_sha)&&backend_imports_match(backend,candidate)&&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCSTR>(*slot),&pinned);
    FreeLibrary(candidate);if(okay)cache_wined3d=pinned;return okay;
}

bool backend_module_for(PVOID endpoint) {
    // Setup is serialized independently of cache workspace/native method calls.
    AcquireSRWLockExclusive(&mesh_lock);
    if(!cache_backend_checked){
        cache_backend_checked=true;HMODULE candidate=nullptr,pinned=nullptr;
        bool okay=GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCSTR>(endpoint),&candidate)!=0;
        if(okay){
            auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(candidate);
            okay=readable(dos,sizeof *dos,candidate)&&dos->e_magic==IMAGE_DOS_SIGNATURE&&dos->e_lfanew>0&&dos->e_lfanew<0x100000;
            if(okay){auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(reinterpret_cast<unsigned char*>(candidate)+dos->e_lfanew);
                okay=readable(nt,sizeof *nt,candidate)&&nt->Signature==IMAGE_NT_SIGNATURE&&nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC;}
            okay=okay&&sha256_module(candidate,buffer_backend_sha)&&verify_wined3d(candidate)&&GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCSTR>(endpoint),&pinned);
        }
        if(candidate)FreeLibrary(candidate);
        if(okay)cache_backend=pinned;
        cache_backend_verified.store(okay,std::memory_order_release);
        log("mesh_cache_backend verified=%u pinned=%u pe32=1 sha256=58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf wined3d_sha256=f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863 contract=serialized_nonreentrant_application_calls",okay,okay);
    }
    const bool okay=cache_backend!=nullptr;ReleaseSRWLockExclusive(&mesh_lock);return okay;
}
template<class Buffer> bool buffer_contract(Buffer* application,unsigned lock_rva,unsigned unlock_rva,unsigned desc_rva,uint64_t required_bytes,D3DFORMAT format) {
    if(!application)return false;
    Buffer* native=ownership::borrowed_native_buffer_for_lock_contract(application);
    if(native){
        ownership::BufferContentView view{};
        if(FAILED(ownership::get_buffer_content_view(application,&view))||
           (view.requested&&(!view.known||view.pending_locks||FAILED(view.status))))return false;
    }else native=application;
    if(!readable(native,sizeof(PVOID)))return false;
    auto table=*reinterpret_cast<PVOID**>(native);
    if(!readable(table,14*sizeof(PVOID)))return false;
    HMODULE owner=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(table[12]),&owner)||
       table[11]!=reinterpret_cast<PVOID>(reinterpret_cast<uintptr_t>(owner)+lock_rva)||
       table[12]!=reinterpret_cast<PVOID>(reinterpret_cast<uintptr_t>(owner)+unlock_rva)||!backend_module_for(table[12]))return false;
    const auto base=reinterpret_cast<uintptr_t>(cache_backend);
    if(table[11]!=reinterpret_cast<PVOID>(base+lock_rva)||table[12]!=reinterpret_cast<PVOID>(base+unlock_rva)||table[13]!=reinterpret_cast<PVOID>(base+desc_rva)||
       !readable(table[11],1,cache_backend,true)||!readable(table[12],1,cache_backend,true)||!readable(table[13],1,cache_backend,true)||!backend_imports_match(cache_backend,cache_wined3d))return false;
    std::conditional_t<std::is_same_v<Buffer,IDirect3DVertexBuffer9>,D3DVERTEXBUFFER_DESC,D3DINDEXBUFFER_DESC> desc{};
    return SUCCEEDED(native->GetDesc(&desc))&&desc.Pool==D3DPOOL_SYSTEMMEM&&desc.Format==format&&
        !(desc.Usage&(D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY))&&required_bytes&&required_bytes<=desc.Size;
}
bool cache_buffer_contract(ID3DXMesh* mesh) {
    if(!readable(mesh,sizeof(PVOID)))return false;
    auto table=*reinterpret_cast<PVOID**>(mesh);if(!readable(table,19*sizeof(PVOID)))return false;
    constexpr unsigned slots[]={4,5,7,8,9,13,14,15,16,17,18};
    constexpr unsigned rvas[]={0x18c626,0x18c63d,0x191987,0x18c280,0x18c297,0x18c2ae,0x18c2e0,0x18c57d,0x18c1a3,0x18c1c0,0x18c1ee};
    for(unsigned i=0;i<11;++i)if(table[slots[i]]!=reinterpret_cast<PVOID>(reinterpret_cast<uintptr_t>(mesh_module)+rvas[i]))return false;
    const DWORD options=mesh->GetOptions();
    if((options&D3DXMESH_SYSTEMMEM)!=D3DXMESH_SYSTEMMEM||(options&(D3DXMESH_WRITEONLY|D3DXMESH_DYNAMIC|D3DXMESH_VB_SHARE)))return false;
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    bool okay=SUCCEEDED(mesh->GetVertexBuffer(&vb))&&vb;
    if(okay)okay=SUCCEEDED(mesh->GetIndexBuffer(&ib))&&ib;
    if(okay){const uint64_t vertices=uint64_t(mesh->GetNumVertices())*mesh->GetNumBytesPerVertex();
        const uint64_t indices=uint64_t(mesh->GetNumFaces())*3*((options&D3DXMESH_32BIT)?4:2);
        okay=buffer_contract(vb,0x1f90,0x2060,0x20c0,vertices,D3DFMT_VERTEXDATA)&&
             buffer_contract(ib,0x2a70,0x2b40,0x2ba0,indices,(options&D3DXMESH_32BIT)?D3DFMT_INDEX32:D3DFMT_INDEX16);
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
    const auto blocked=cache_blocked.load(),rejected=cache_gate_rejections.load();
    if(stats.calls==cache_last_report_calls&&blocked==cache_last_report_blocked&&rejected==cache_last_report_rejected)return;
    cache_last_report_calls=stats.calls;cache_last_report_blocked=blocked;cache_last_report_rejected=rejected;
    log("mesh_cache_metric cumulative=1 qpc=%llu dispatch_enabled=%u backend_verified=%u faulted=%u calls=%llu hits=%llu misses=%llu bypasses=%llu contention=%llu admissions=%llu evictions=%llu allocation_failures=%llu acquisition_failures=%llu unrecoverable_unlocks=%llu native_calls=%llu native_failures=%llu retained_bytes=%llu retained_entries=%llu acquired_bytes=%llu copied_bytes=%llu evicted_bytes=%llu acquisition_ticks=%llu lookup_ticks=%llu copy_ticks=%llu native_ticks=%llu total_ticks=%llu gate_rejections=%llu gate_ticks=%llu native_outcomes=%llu hit_outcomes=%llu cleanup_outcomes=%llu blocked=%llu cleanup_hr=%08lx",
        tick(),unsigned(cache_enabled.load()),unsigned(cache_backend_verified.load()),unsigned(cache_faulted.load()),stats.calls,stats.hits,stats.misses,stats.bypasses,stats.contention,stats.admissions,stats.evictions,stats.allocation_failures,stats.acquisition_failures,stats.unrecoverable_unlocks,stats.native_calls,stats.native_failures,stats.retained_bytes,stats.retained_entries,stats.acquired_bytes,stats.copied_bytes,stats.evicted_bytes,stats.acquisition_ticks,stats.lookup_ticks,stats.copy_ticks,stats.native_ticks,stats.total_ticks,rejected,cache_gate_ticks.load(),cache_native_outcomes.load(),cache_hit_outcomes.load(),cache_cleanup_outcomes.load(),blocked,cache_cleanup_hr.load());
}

bool verify_mesh_module() {
    if(mesh_module_checked)return mesh_module!=nullptr;
    mesh_module_checked=true;
    HMODULE candidate=nullptr;
    const auto create=hooks[static_cast<unsigned>(Operation::MeshCreate)].original;
    if(!create||!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCSTR>(create),&candidate))return false;
    DWORD size=0;const uint64_t hash=fingerprint(candidate,&size);
    bool valid=hash==0x49cc52632ef762d4ull&&size==3786760;
    // Pin only the exact verified native implementation. No mesh/device refs
    // are acquired; callbacks/originals remain callable after owned-slot restore.
    HMODULE pinned=nullptr;
    if(valid)valid=GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCSTR>(create),&pinned)!=0;
    FreeLibrary(candidate);
    if(valid)mesh_module=pinned;
    if(valid&&cache_requested&&sha256_module(mesh_module,native_mesh_sha)){
        std::copy_n(native_mesh_sha,32,cache_runtime.sha256.begin());cache_runtime.generation=1;
        cache_runtime.verified=true;cache_runtime.success_preserves_last_error=true;
        auto* const instance=new(cache_storage) adjacency_cache::Cache();
        cache_instance.store(instance,std::memory_order_release);
        cache_enabled.store(true,std::memory_order_release);
        log("mesh_cache ready=1 buffer_contract_required=1 sha256=c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8 generation=1 retained_budget=16777216 scratch_budget=4194304 entries=512");
    }else if(cache_requested)log("mesh_cache ready=0 reason=native_runtime_not_verified");
    log("mesh_trace module_verified=%u pinned=%u hash=%016llx bytes=%lu scope=shared_native_vtables table_limit=%u objects_retained=0",valid,valid,hash,size,mesh_table_limit);
    return valid;
}
void observe_mesh(ID3DXMesh* mesh) {
    if(!mesh_observation_enabled.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&mesh_lock);
    if(!mesh_observation_enabled.load(std::memory_order_relaxed)||!verify_mesh_module()||!readable(mesh,sizeof(PVOID))){ReleaseSRWLockExclusive(&mesh_lock);return;}
    auto table=*reinterpret_cast<PVOID**>(mesh);
    if(reinterpret_cast<uintptr_t>(table)%alignof(PVOID)||!readable(table,29*sizeof(PVOID),mesh_module)){ReleaseSRWLockExclusive(&mesh_lock);return;}
    // IUnknown and all three target methods must execute in this exact module.
    for(unsigned slot:{0u,1u,2u,20u,22u,27u}) {
        bool ours=false;
        for(const auto& row:mesh_tables)for(const auto& h:row.slots)ours|=h.replacement&&table[slot]==h.replacement;
        if(!ours&&!readable(table[slot],1,mesh_module,true)){ReleaseSRWLockExclusive(&mesh_lock);return;}
    }
    unsigned index=0;
    while(index<mesh_table_limit&&mesh_tables[index].table&&mesh_tables[index].table!=table)++index;
    if(index==mesh_table_limit){ReleaseSRWLockExclusive(&mesh_lock);return;}
    auto& row=mesh_tables[index];
    if(!row.table){
        row.table=table;
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
bool install(HMODULE target,uint64_t expected,bool production) {
    if(installed.load())return true;
    if(installation_started){log("loading_trace disabled=reinitialization_not_supported");return false;}
    if(!requested())return false;
    DWORD bytes=0;const uint64_t hash=fingerprint(target,&bytes);
    if(!hash||hash!=expected||(production&&bytes!=expected_x3_size)) {
        log("loading_trace disabled=fingerprint_mismatch hash=%016llx bytes=%lu",hash,bytes);return false;
    }
    auto base=reinterpret_cast<unsigned char*>(target);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))||dos->e_lfanew>0x100000)return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    if(production&&(nt->OptionalHeader.SizeOfImage!=0x2f5000||nt->OptionalHeader.ImageBase!=0x400000))return false;
    Image image{base,nt->OptionalHeader.SizeOfImage};
    const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress||!image.range(directory.VirtualAddress,directory.Size))return false;
    hook_count=0;
    for(DWORD pos=0;pos+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=directory.Size;pos+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        auto descriptor=image.at<IMAGE_IMPORT_DESCRIPTOR>(directory.VirtualAddress+pos);
        if(!descriptor||!descriptor->Name)break;
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
                if(hook.slot)continue;
                if(!slot->u1.Function||reinterpret_cast<uintptr_t>(&slot->u1.Function)%alignof(PVOID))continue;
                hook.slot=reinterpret_cast<PVOID*>(&slot->u1.Function);
                hook.original=reinterpret_cast<PVOID>(slot->u1.Function);
            }
        }
    }
    LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return false;
    installation_started=true;
    wchar_t cache_setting[8]{};cache_requested=GetEnvironmentVariableW(L"X3M_MESH_CACHE",cache_setting,8)==1&&cache_setting[0]==L'1';
    log("mesh_cache requested=%u enabled=0 activation=await_verified_mesh_and_buffer_contract adjacency_metric_scope=hook_service restart_on_cleanup_failure=1",cache_requested);
    clock_frequency=frequency.QuadPart;
    mesh_observation_enabled.store(true,std::memory_order_release);
    for(auto& hook:hooks) {
        if(hook.slot&&patch(hook,false)){++hook_count;log("loading_hook name=%s installed=1",hook.name);}
        else {log("loading_hook name=%s installed=0",hook.name);if(hook.slot&&!has_protection_debt(hook.slot))hook.slot=nullptr;}
    }
    coverage_start=tick();installed.store(hook_count!=0);
    log("loading_trace coverage_begin=%llu frequency=%llu hooks=%u module=main inclusive=1 paths=0 hash=%016llx",coverage_start,clock_frequency,hook_count,hash);
    return installed.load();
}
}

bool initialize(){const DWORD error=GetLastError();bool result=install(GetModuleHandleW(nullptr),expected_x3_hash,true);SetLastError(error);return result;}
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
uint64_t fixture_fingerprint(HMODULE target){return fingerprint(target,nullptr);}
bool fixture_initialize(HMODULE target,uint64_t expected){return install(target,expected,false);}
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
#endif
}
