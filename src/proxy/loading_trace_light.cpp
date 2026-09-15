#include "loading_trace_light.h"
#include <cstring>

// Compiled with -mno-sse -mno-mmx -mfpmath=387 and free of floating-point work:
// see loading_trace_light.h for the rules. Every function here runs inside a
// game thread with no CPU-state boundary; only the last error is transported.

namespace x3m::loading_trace::light {
namespace {
alignas(8) Row rows[row_count];
PVOID originals[row_count];
DWORD span_slot=TLS_OUT_OF_INDEXES;
bool initialized=false;
template<typename T>T original(unsigned op) noexcept { return reinterpret_cast<T>(originals[op]); }
// ---- write-side path records ----
PathRecord path_records[path_record_limit];
volatile LONG path_count=0;
void record_path(unsigned op,LPCSTR path) noexcept {
    if(!path)return;
    const LONG index=InterlockedIncrement(&path_count)-1;
    if(index<0||index>=LONG(path_record_limit))return;
    auto& record=path_records[index];record.op=op;
    unsigned i=0;for(;i<path_record_length-1&&path[i];++i)record.path[i]=path[i];
    record.path[i]=0;
}
}
bool initialize() noexcept {
    if(initialized)return true;
    span_slot=TlsAlloc();
    initialized=true;
    return span_slot!=TLS_OUT_OF_INDEXES;
}
bool nesting_available() noexcept { return span_slot!=TLS_OUT_OF_INDEXES; }
void set_original(unsigned op,PVOID value) noexcept { if(op<row_count)originals[op]=value; }
PVOID get_original(unsigned op) noexcept { return op<row_count?originals[op]:nullptr; }
uint64_t tick() noexcept { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return uint64_t(value.QuadPart); }
void take(unsigned op,Sample& out) noexcept {
    out=Sample{};
    if(op>=row_count)return;
    auto& r=rows[op];
    out.count=exchange64(&r.calls,0);out.failures=exchange64(&r.failures,0);out.pending=exchange64(&r.pending,0);
    out.ambiguous=exchange64(&r.ambiguous,0);out.bytes=exchange64(&r.bytes,0);out.inclusive_ticks=exchange64(&r.inclusive,0);
    out.exclusive_ticks=exchange64(&r.exclusive,0);out.maximum_ticks=exchange64(&r.maximum,0);out.overhead_ticks=exchange64(&r.overhead,0);
}
unsigned take_paths(PathRecord* out,unsigned capacity) noexcept {
    const LONG count=InterlockedExchange(&path_count,0);
    const unsigned n=count<0?0:count>LONG(path_record_limit)?path_record_limit:unsigned(count);
    unsigned copied=0;
    for(unsigned i=0;i<n&&copied<capacity;++i)out[copied++]=path_records[i];
    return copied;
}
// ---- bounded process-lifetime interval recorder ----
namespace {
struct Atomic32 {
    volatile LONG value=0;
    LONG read() noexcept {return InterlockedCompareExchange(&value,0,0);}
    LONG exchange(LONG n) noexcept {return InterlockedExchange(&value,n);}
    LONG increment() noexcept {return InterlockedIncrement(&value);}
    LONG compare(LONG n,LONG expected) noexcept {return InterlockedCompareExchange(&value,n,expected);}
    LONG decrement() noexcept {return InterlockedDecrement(&value);}
};
intervals::Gate<Atomic32> interval_gate;
Atomic32 interval_phase,interval_next,interval_flags; // 1=recording, 2=frozen, 3=claimed
DWORD interval_tls=TLS_OUT_OF_INDEXES;
intervals::Record* interval_records=nullptr;
intervals::Ring interval_rings[intervals::slot_limit];
intervals::Header interval_header{};
#ifdef X3M_LOADING_TRACE_FIXTURE
unsigned interval_fixture_failures=0;
#endif
void interval_fault(LONG flag) noexcept {InterlockedOr(&interval_flags.value,flag);}
intervals::Ring* interval_enter() noexcept {
    if(!interval_gate.enter())return nullptr;
    auto* slot=static_cast<intervals::Ring*>(TlsGetValue(interval_tls));
    if(!slot){
        if(GetLastError()!=ERROR_SUCCESS){interval_fault(intervals::Tls);interval_gate.leave();return nullptr;}
        LONG index=interval_next.read();
        for(;;){
            if(index>=LONG(intervals::slot_limit)){interval_fault(intervals::Threads);interval_gate.leave();return nullptr;}
            const LONG old=InterlockedCompareExchange(&interval_next.value,index+1,index);
            if(old==index)break;
            index=old;
        }
        slot=&interval_rings[index];slot->tid=GetCurrentThreadId();slot->generation=unsigned(index)+1;
#ifdef X3M_LOADING_TRACE_FIXTURE
        if(interval_fixture_failures&8)slot->tid=42;
        if(interval_fixture_failures&4){interval_fault(intervals::Tls);interval_gate.leave();return nullptr;}
#endif
        if(!TlsSetValue(interval_tls,slot)){interval_fault(intervals::Tls);interval_gate.leave();return nullptr;}
    }
    return slot;
}
void interval_finish(intervals::Ring* slot,uint64_t begin,uint64_t end,unsigned op) noexcept {
    if(!slot)return;
    slot->append(interval_records+(slot-interval_rings)*intervals::capacity,intervals::capacity,begin,end,op);
    interval_gate.leave();
}
}
#ifdef X3M_LOADING_TRACE_FIXTURE
void fixture_interval_failures(unsigned mask) noexcept {interval_fixture_failures=mask;}
#endif
unsigned intervals_initialize(bool requested,uint64_t frequency) noexcept {
    if(!requested||interval_phase.read())return unsigned(interval_flags.read());
    interval_header.schema=1;interval_header.header_bytes=sizeof(interval_header);
    const char magic[8]={'X','3','M','I','N','T','0','1'};
    for(unsigned i=0;i<8;++i)interval_header.magic[i]=magic[i];
    interval_header.record_bytes=sizeof(intervals::Record);interval_header.slots=intervals::slot_limit;
    interval_header.ring_capacity=intervals::capacity;interval_header.frequency=frequency;
    interval_header.initialized=tick();
    bool fail_allocation=false,fail_tls=false;
#ifdef X3M_LOADING_TRACE_FIXTURE
    fail_allocation=interval_fixture_failures&1;fail_tls=interval_fixture_failures&2;
#endif
    interval_records=fail_allocation?nullptr:static_cast<intervals::Record*>(VirtualAlloc(nullptr,intervals::slot_limit*intervals::capacity*sizeof(intervals::Record),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    interval_tls=fail_tls?TLS_OUT_OF_INDEXES:TlsAlloc();
    if(!interval_records)interval_fault(intervals::Allocation);
    if(interval_tls==TLS_OUT_OF_INDEXES)interval_fault(intervals::Tls);
    if(!interval_header.initialized||!frequency)interval_fault(intervals::Clock);
    interval_phase.exchange(1);
    if(!interval_flags.read())interval_gate.enabled.exchange(1);
    return unsigned(interval_flags.read());
}
void intervals_freeze(uint64_t begin,uint64_t end,uint64_t device,uint64_t reset,uint64_t frame,DWORD tid) noexcept {
    if(interval_phase.read()!=1)return;
    // Called once by the capture-serialized marker producer; publication of
    // phase=2 follows all metadata stores. Never wait under its capture lock.
    interval_gate.close();
    interval_header.begin=begin;interval_header.end=end;interval_header.device=device;
    interval_header.reset=reset;interval_header.frame=frame;interval_header.present_tid=tid;
    if(!begin||end<=begin||begin<interval_header.initialized)interval_fault(intervals::Clock);
    interval_phase.exchange(2);
}
unsigned intervals_snapshot(const intervals::Header*& header,const intervals::Ring*& rings,const intervals::Record*& records,LONG& outstanding) noexcept {
    const LONG phase=interval_phase.read();
    if(!phase||phase==3)return 0;
    if(phase==1)return 1;
    outstanding=interval_gate.active.read();
    if(!interval_gate.drained())return 2;
    if(InterlockedCompareExchange(&interval_phase.value,3,2)!=2)return 0;
    interval_header.flags=unsigned(interval_flags.read());interval_header.registered=unsigned(interval_next.read());
    header=&interval_header;rings=interval_rings;records=interval_records;return 3;
}
// ---- span ----
void Span::begin(unsigned operation) noexcept {
    caller_error=GetLastError();op=operation;children=0;
    interval_token=interval_enter();
    begin_ticks=tick();
    if(span_slot!=TLS_OUT_OF_INDEXES){parent=static_cast<Span*>(TlsGetValue(span_slot));TlsSetValue(span_slot,this);}
    else parent=nullptr;
}
void Span::before_call() const noexcept { SetLastError(caller_error); }
void Span::finish(bool failed,uint64_t bytes,bool pending,bool ambiguous) noexcept {
    const DWORD result_error=GetLastError();
    const uint64_t end=tick();
    interval_finish(interval_token,begin_ticks,end,op);interval_token=nullptr;
    auto& r=rows[op<row_count?op:0];
    const uint64_t elapsed=end-begin_ticks;
    add64(&r.calls,1);
    if(failed)add64(&r.failures,1);
    if(pending)add64(&r.pending,1);
    if(ambiguous)add64(&r.ambiguous,1);
    if(bytes)add64(&r.bytes,bytes);
    add64(&r.inclusive,elapsed);
    add64(&r.exclusive,elapsed>children?elapsed-children:0);
    max64(&r.maximum,elapsed);
    if(span_slot!=TLS_OUT_OF_INDEXES)TlsSetValue(span_slot,parent);
    // Own measured tail excludes the last accounting stores and SetLastError,
    // as before (loading_metric wrapper_tail_ticks).
    const uint64_t tail=tick();
    add64(&r.overhead,tail-end);
    if(parent)parent->children+=tail-begin_ticks;
    SetLastError(result_error);
}
namespace {
constexpr unsigned index(Operation op){return static_cast<unsigned>(op);}
}
// ---- light import wrappers: count, time, forward; last error preserved exactly ----
HANDLE WINAPI file_open(LPCSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templ) {
    Span span;span.begin(index(Operation::FileOpen));span.before_call();
    HANDLE result=original<decltype(&CreateFileA)>(span.op)(name,access,share,security,creation,flags,templ);
    span.finish(result==INVALID_HANDLE_VALUE);return result;
}
BOOL WINAPI file_read(HANDLE file,LPVOID buffer,DWORD requested,LPDWORD read,LPOVERLAPPED overlapped) {
    Span span;span.begin(index(Operation::FileRead));span.before_call();
    BOOL result=original<decltype(&ReadFile)>(span.op)(file,buffer,requested,read,overlapped);
    const DWORD error=GetLastError();const bool pending=!result&&error==ERROR_IO_PENDING;
    span.finish(!result&&!pending,result&&read?*read:0,pending);return result;
}
DWORD WINAPI file_seek(HANDLE file,LONG distance,PLONG high,DWORD method) {
    Span span;span.begin(index(Operation::FileSeek));span.before_call();
    DWORD result=original<decltype(&SetFilePointer)>(span.op)(file,distance,high,method);
    const DWORD error=GetLastError();
    // The sentinel can be a successful offset; classified as ambiguous, never forced.
    span.finish(false,0,false,result==INVALID_SET_FILE_POINTER&&error!=NO_ERROR);return result;
}
HCURSOR WINAPI cursor_set(HCURSOR value) {
    Span span;span.begin(index(Operation::CursorSet));span.before_call();
    HCURSOR result=original<decltype(&SetCursor)>(span.op)(value);
    span.finish();return result;
}
BOOL WINAPI cursor_position(int x,int y) {
    Span span;span.begin(index(Operation::CursorPosition));span.before_call();
    BOOL result=original<decltype(&SetCursorPos)>(span.op)(x,y);
    span.finish(!result);return result;
}
// Directory enumeration of the resource resolver: a miss (invalid handle with
// ERROR_FILE_NOT_FOUND / ERROR_NO_MORE_FILES) and FindNextFileA's termination
// are ambiguous, not failures. Patterns are never recorded.
HANDLE WINAPI find_first(LPCSTR pattern,LPWIN32_FIND_DATAA data) {
    Span span;span.begin(index(Operation::FindFirst));span.before_call();
    HANDLE result=original<decltype(&FindFirstFileA)>(span.op)(pattern,data);
    const DWORD error=GetLastError();
    const bool missing=result==INVALID_HANDLE_VALUE&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_NO_MORE_FILES);
    span.finish(result==INVALID_HANDLE_VALUE&&!missing,0,false,missing);return result;
}
BOOL WINAPI find_next(HANDLE find,LPWIN32_FIND_DATAA data) {
    Span span;span.begin(index(Operation::FindNext));span.before_call();
    BOOL result=original<decltype(&FindNextFileA)>(span.op)(find,data);
    const DWORD error=GetLastError();const bool exhausted=!result&&error==ERROR_NO_MORE_FILES;
    span.finish(!result&&!exhausted,0,false,exhausted);return result;
}
BOOL WINAPI find_close(HANDLE find) {
    Span span;span.begin(index(Operation::FindClose));span.before_call();
    BOOL result=original<decltype(&FindClose)>(span.op)(find);
    span.finish(!result);return result;
}
using GzOpenFn=void* (__cdecl*)(const char*,const char*);
using GzReadFn=int (__cdecl*)(void*,void*,unsigned);
using GzSeekFn=LONG (__cdecl*)(void*,LONG,int);
using GzTellFn=LONG (__cdecl*)(void*);
using GzGetcFn=int (__cdecl*)(void*);
using GzCloseFn=int (__cdecl*)(void*);
using GzWriteFn=int (__cdecl*)(void*,const void*,unsigned);
using InflateFn=int (__cdecl*)(void*,int);
using InflateInitFn=int (__cdecl*)(void*,int,const char*,int);
using InflateEndFn=int (__cdecl*)(void*);
using XmlReadFn=void* (__cdecl*)(const char*,int,const char*,const char*,int);
void* __cdecl gz_open_traced(const char* path,const char* mode) {
    Span span;span.begin(index(Operation::GzOpen));span.before_call();
    void* result=original<GzOpenFn>(span.op)(path,mode);
    span.finish(!result);return result;
}
// With the read-ahead buffer on, this row counts the buffer's chunk reads.
int __cdecl gz_read_traced(void* file,void* data,unsigned size) {
    Span span;span.begin(index(Operation::GzRead));span.before_call();
    int result=original<GzReadFn>(span.op)(file,data,size);
    span.finish(result<0,result>0?unsigned(result):0);return result;
}
LONG __cdecl gz_seek_traced(void* file,LONG offset,int whence) {
    Span span;span.begin(index(Operation::GzSeek));span.before_call();
    LONG result=original<GzSeekFn>(span.op)(file,offset,whence);
    span.finish(result<0);return result;
}
int __cdecl gz_getc_traced(void* file) {
    Span span;span.begin(index(Operation::GzGetc));span.before_call();
    int result=original<GzGetcFn>(span.op)(file);
    span.finish(result<0,result>=0?1:0);return result;
}
LONG __cdecl gz_tell_traced(void* file) {
    Span span;span.begin(index(Operation::GzTell));span.before_call();
    LONG result=original<GzTellFn>(span.op)(file);
    span.finish(result<0);return result;
}
int __cdecl gz_close_traced(void* file) {
    Span span;span.begin(index(Operation::GzClose));span.before_call();
    int result=original<GzCloseFn>(span.op)(file);
    span.finish(result!=0);return result;
}
int __cdecl gz_write(void* file,const void* data,unsigned size) {
    Span span;span.begin(index(Operation::GzWrite));span.before_call();
    int result=original<GzWriteFn>(span.op)(file,data,size);
    span.finish(result<=0&&size>0,result>0?unsigned(result):0);return result;
}
int __cdecl inflate_stream(void* stream,int flush) {
    Span span;span.begin(index(Operation::Inflate));span.before_call();
    int result=original<InflateFn>(span.op)(stream,flush);
    // Z_BUF_ERROR (-5) is nonfatal no-progress: ambiguous, not failed. No z_stream member is read.
    span.finish(result<0&&result!=-5,0,false,result==-5);return result;
}
void* __cdecl xml_read(const char* data,int size,const char* url,const char* encoding,int options) {
    Span span;span.begin(index(Operation::XmlRead));span.before_call();
    void* result=original<XmlReadFn>(span.op)(data,size,url,encoding,options);
    span.finish(!result,size>0?unsigned(size):0);return result;
}
int __cdecl inflate_init2(void* stream,int bits,const char* version,int size) {
    Span span;span.begin(index(Operation::InflateInit));span.before_call();
    int result=original<InflateInitFn>(span.op)(stream,bits,version,size);
    span.finish(result!=0);return result;
}
int __cdecl inflate_end(void* stream) {
    Span span;span.begin(index(Operation::InflateEnd));span.before_call();
    int result=original<InflateEndFn>(span.op)(stream);
    span.finish(result!=0);return result;
}
BOOL WINAPI crypt_acquire_context(HCRYPTPROV* provider,LPCSTR container,LPCSTR name,DWORD type,DWORD flags) {
    Span span;span.begin(index(Operation::CryptAcquire));span.before_call();
    BOOL result=original<decltype(&CryptAcquireContextA)>(span.op)(provider,container,name,type,flags);
    span.finish(!result);return result;
}
BOOL WINAPI crypt_release_context(HCRYPTPROV provider,DWORD flags) {
    Span span;span.begin(index(Operation::CryptRelease));span.before_call();
    BOOL result=original<decltype(&CryptReleaseContext)>(span.op)(provider,flags);
    span.finish(!result);return result;
}
BOOL WINAPI crypt_import_key(HCRYPTPROV provider,const BYTE* data,DWORD length,HCRYPTKEY key,DWORD flags,HCRYPTKEY* out) {
    Span span;span.begin(index(Operation::CryptImport));span.before_call();
    BOOL result=original<decltype(&CryptImportKey)>(span.op)(provider,data,length,key,flags,out);
    span.finish(!result,length);return result;
}
BOOL WINAPI crypt_create_hash(HCRYPTPROV provider,ALG_ID algorithm,HCRYPTKEY key,DWORD flags,HCRYPTHASH* out) {
    Span span;span.begin(index(Operation::CryptHashCreate));span.before_call();
    BOOL result=original<decltype(&CryptCreateHash)>(span.op)(provider,algorithm,key,flags,out);
    span.finish(!result);return result;
}
BOOL WINAPI crypt_hash_data(HCRYPTHASH hash,const BYTE* data,DWORD length,DWORD flags) {
    Span span;span.begin(index(Operation::CryptHash));span.before_call();
    BOOL result=original<decltype(&CryptHashData)>(span.op)(hash,data,length,flags);
    span.finish(!result,length);return result;
}
BOOL WINAPI crypt_verify_signature(HCRYPTHASH hash,const BYTE* signature,DWORD length,HCRYPTKEY key,LPCSTR description,DWORD flags) {
    Span span;span.begin(index(Operation::CryptVerify));span.before_call();
    BOOL result=original<decltype(&CryptVerifySignatureA)>(span.op)(hash,signature,length,key,description,flags);
    span.finish(!result,length);return result;
}
BOOL WINAPI crypt_get_hash_param(HCRYPTHASH hash,DWORD parameter,BYTE* data,DWORD* length,DWORD flags) {
    Span span;span.begin(index(Operation::CryptHashParam));span.before_call();
    BOOL result=original<decltype(&CryptGetHashParam)>(span.op)(hash,parameter,data,length,flags);
    span.finish(!result);return result;
}
BOOL WINAPI crypt_destroy_hash(HCRYPTHASH hash) {
    Span span;span.begin(index(Operation::CryptHashDestroy));span.before_call();
    BOOL result=original<decltype(&CryptDestroyHash)>(span.op)(hash);
    span.finish(!result);return result;
}
BOOL WINAPI crypt_destroy_key(HCRYPTKEY key) {
    Span span;span.begin(index(Operation::CryptKeyDestroy));span.before_call();
    BOOL result=original<decltype(&CryptDestroyKey)>(span.op)(key);
    span.finish(!result);return result;
}
BOOL WINAPI create_directory(LPCSTR path,LPSECURITY_ATTRIBUTES security) {
    Span span;span.begin(index(Operation::DirectoryCreate));record_path(span.op,path);span.before_call();
    BOOL result=original<decltype(&CreateDirectoryA)>(span.op)(path,security);
    span.finish(!result);return result;
}
BOOL WINAPI delete_file(LPCSTR path) {
    Span span;span.begin(index(Operation::FileDelete));record_path(span.op,path);span.before_call();
    BOOL result=original<decltype(&DeleteFileA)>(span.op)(path);
    span.finish(!result);return result;
}
BOOL WINAPI move_file(LPCSTR from,LPCSTR to) {
    Span span;span.begin(index(Operation::FileMove));record_path(span.op,to);span.before_call();
    BOOL result=original<decltype(&MoveFileA)>(span.op)(from,to);
    span.finish(!result);return result;
}
BOOL WINAPI move_file_ex(LPCSTR from,LPCSTR to,DWORD flags) {
    Span span;span.begin(index(Operation::FileMoveEx));record_path(span.op,to);span.before_call();
    BOOL result=original<decltype(&MoveFileExA)>(span.op)(from,to,flags);
    span.finish(!result);return result;
}
BOOL WINAPI write_file(HANDLE file,LPCVOID data,DWORD size,LPDWORD written,LPOVERLAPPED overlapped) {
    Span span;span.begin(index(Operation::FileWrite));span.before_call();
    BOOL result=original<decltype(&WriteFile)>(span.op)(file,data,size,written,overlapped);
    const DWORD error=GetLastError();const bool pending=!result&&error==ERROR_IO_PENDING;
    span.finish(!result&&!pending,result&&written?*written:0,pending);return result;
}
DWORD WINAPI get_file_type(HANDLE file) {
    Span span;span.begin(index(Operation::FileType));span.before_call();
    DWORD result=original<decltype(&GetFileType)>(span.op)(file);
    const DWORD error=GetLastError();
    span.finish(result==FILE_TYPE_UNKNOWN&&error!=NO_ERROR);return result;
}
BOOL WINAPI close_handle(HANDLE handle) {
    Span span;span.begin(index(Operation::HandleClose));span.before_call();
    BOOL result=original<decltype(&CloseHandle)>(span.op)(handle);
    span.finish(!result);return result;
}

// ---- engine probes ----
namespace {
struct ShadowEntry { uint32_t site,slot,ret,ctx; uint64_t begin; };
struct Shadow { unsigned depth; ShadowEntry entries[probe_shadow_depth]; };
alignas(8) ProbeRow probe_rows[probe_site_limit];
ProbeConfig probe_configs[probe_site_limit];
const void* exit_stub=nullptr;
const uint32_t* size_global=nullptr;
DWORD shadow_slot=TLS_OUT_OF_INDEXES;
volatile LONG shadow_slot_state=0; // 0 unallocated, 1 allocating, 2 ready, 3 failed
volatile LONG shadow_block_count=0;
// Readable-page cache for the file-object dereferences: page bases validated by
// VirtualQuery (committed, readable, not guarded) with the tick of validation.
// Entries are single aligned 32-bit words written racily; a torn pair only costs
// a repeated query. A page is trusted for readable_page_ttl_ms, the bound
// engine_memory.cpp uses when no frame advances (loading runs between frames).
constexpr unsigned readable_pages=16;
constexpr DWORD readable_page_ttl_ms=100;
constexpr DWORD readable_protection=PAGE_READONLY|PAGE_READWRITE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY;
struct ReadablePage { uint32_t page; DWORD tick; };
ReadablePage readable[readable_pages];
volatile LONG readable_victim=0;
bool page_readable(uint32_t address) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof info)!=sizeof info)return false;
    if(info.State!=MEM_COMMIT||(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))||!(info.Protect&readable_protection))return false;
    const uint32_t begin=uint32_t(reinterpret_cast<uintptr_t>(info.BaseAddress));
    return address>=begin&&address-begin<uint32_t(info.RegionSize);
}
Shadow* shadow() noexcept {
    if(shadow_slot_state!=2){
        if(InterlockedCompareExchange(&shadow_slot_state,1,0)==0){
            shadow_slot=TlsAlloc();
            InterlockedExchange(&shadow_slot_state,shadow_slot==TLS_OUT_OF_INDEXES?3:2);
        }
        while(shadow_slot_state==1)YieldProcessor();
        if(shadow_slot_state!=2)return nullptr;
    }
    auto* s=static_cast<Shadow*>(TlsGetValue(shadow_slot));
    if(!s){
        s=static_cast<Shadow*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Shadow)));
        if(!s||!TlsSetValue(shadow_slot,s)){if(s)HeapFree(GetProcessHeap(),0,s);return nullptr;}
        InterlockedIncrement(&shadow_block_count);
    }
    return s;
}
// A register that should hold the file object: only an aligned user-space
// pointer above the first 64 KB whose page is committed and readable is
// dereferenced (the game passes heap objects; a fixture or a foreign caller may
// leave anything in the register). The aligned dword never crosses a page.
inline bool plausible(uint32_t pointer) noexcept { return pointer>=0x10000u&&pointer<0x7fff0000u&&(pointer&3)==0; }
inline bool object_flags(uint32_t object,uint32_t& flags) noexcept { return plausible(object)&&probe_read32(object+4,flags); }
void note_caller(ProbeRow& row,uint32_t address) noexcept {
    for(unsigned i=0;i<probe_caller_limit;++i){
        if(row.caller_address[i]==address){add64(&row.caller_calls[i],1);return;}
        if(!row.caller_address[i]){
            if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&row.caller_address[i]),LONG(address),0)==0||row.caller_address[i]==address){add64(&row.caller_calls[i],1);return;}
        }
    }
}
}
unsigned shadow_blocks() noexcept { return unsigned(shadow_block_count); }
unsigned shadow_block_bytes() noexcept { return unsigned(sizeof(Shadow)); }
bool probe_read32(uint32_t address,uint32_t& out) noexcept {
    if(address<0x10000u||address>0x7fff0000u-4||(address&3))return false;
    const uint32_t page=address&~uint32_t(0xfff);
    const DWORD now=GetTickCount();
    bool trusted=false;
    for(unsigned i=0;i<readable_pages&&!trusted;++i){
        const uint32_t entry_page=readable[i].page;const DWORD entry_tick=readable[i].tick; // two 32-bit reads, no 64-bit copy
        trusted=entry_page==page&&now-entry_tick<=readable_page_ttl_ms;
    }
    if(!trusted){
        if(!page_readable(address))return false;
        const unsigned slot=unsigned(InterlockedIncrement(&readable_victim)-1)%readable_pages;
        readable[slot].tick=now;readable[slot].page=page; // page last: a reader that sees it also sees a tick no older than now
    }
    out=*reinterpret_cast<const volatile uint32_t*>(address);
    return true;
}
void probe_configure(unsigned site,const ProbeConfig& config) noexcept { if(site<probe_site_limit)probe_configs[site]=config; }
void probe_set_exit_stub(const void* stub) noexcept { exit_stub=stub; }
void probe_set_size_global(const uint32_t* address) noexcept { size_global=address; }
void probe_take(unsigned site,ProbeRow& out) noexcept {
    out=ProbeRow{};
    if(site>=probe_site_limit)return;
    auto& r=probe_rows[site];
    out.calls=exchange64(&r.calls,0);out.exits=exchange64(&r.exits,0);out.inclusive=exchange64(&r.inclusive,0);out.maximum=exchange64(&r.maximum,0);
    out.overflow=exchange64(&r.overflow,0);out.desync=exchange64(&r.desync,0);out.bytes=exchange64(&r.bytes,0);
    for(unsigned i=0;i<probe_extra_count;++i)out.extra[i]=exchange64(&r.extra[i],0);
    for(unsigned i=0;i<probe_caller_limit;++i){out.caller_address[i]=r.caller_address[i];out.caller_calls[i]=exchange64(&r.caller_calls[i],0);}
}
extern "C" void __cdecl x3m_probe_enter(unsigned site,uint32_t* regs) {
    if(site>=probe_site_limit)return;
    const DWORD error=GetLastError();
    auto& row=probe_rows[site];const auto& config=probe_configs[site];
    add64(&row.calls,1);
    uint32_t ctx=0;
    switch(config.kind){
    case ProbeKind::ResourceOpen: ctx=regs[1]; break;               // ESI = file object
    case ProbeKind::ResourceRead: {                                  // EAX = file object; branch by its flags
        ctx=regs[7];
        uint32_t flags=0;
        if(object_flags(ctx,flags))add64(&row.extra[!(flags&1)?2:(flags&3)==3?1:(flags&5)==5?2:0],1);
        break; }
    case ProbeKind::ReadDispatch: {                                  // ECX = element size, EAX = count, ESI = object
        add64(&row.bytes,uint64_t(regs[6])*regs[7]);
        uint32_t flags=0;
        if(object_flags(regs[1],flags))add64(&row.extra[!(flags&1)?3:(flags&3)==3?1:(flags&5)==5?2:0],1);
        break; }
    case ProbeKind::FindWrapper: note_caller(row,regs[9]); break;    // bucket by return address
    default: break;
    }
    if(config.kind!=ProbeKind::CountOnly&&config.kind!=ProbeKind::ReadDispatch&&exit_stub){ // the dispatcher is counted at entry only (calls, bytes, branch)
        Shadow* s=shadow();
        if(!s||s->depth>=probe_shadow_depth)add64(&row.overflow,1);
        else{
            auto& e=s->entries[s->depth++];
            e.site=site;e.slot=uint32_t(reinterpret_cast<uintptr_t>(&regs[9]));e.ret=regs[9];e.ctx=ctx;e.begin=tick();
            regs[9]=uint32_t(reinterpret_cast<uintptr_t>(exit_stub));
        }
    }
    SetLastError(error);
}
extern "C" uint32_t __cdecl x3m_probe_exit(uint32_t* regs) {
    const DWORD error=GetLastError();
    const uint64_t end=tick();
    Shadow* s=shadow();
    // regs[9] is the placeholder the exit stub pushed; the callee's return left
    // ESP at &regs[10], so the matching entry's slot is that minus 4 minus the
    // bytes the callee popped (ret n).
    const uint32_t here=uint32_t(reinterpret_cast<uintptr_t>(&regs[10]));
    uint32_t ret=0;
    if(s&&s->depth){
        // Entries below the current stack belong to frames unwound past without returning.
        while(s->depth>1){
            const auto& top=s->entries[s->depth-1];
            const uint32_t expected=here-4-probe_configs[top.site<probe_site_limit?top.site:0].ret_pop;
            if(top.slot<expected){add64(&probe_rows[top.site<probe_site_limit?top.site:0].desync,1);--s->depth;}
            else break;
        }
        const auto& e=s->entries[--s->depth];
        auto& row=probe_rows[e.site<probe_site_limit?e.site:0];
        const uint32_t expected=here-4-probe_configs[e.site<probe_site_limit?e.site:0].ret_pop;
        if(e.slot!=expected)add64(&row.desync,1);
        const uint64_t elapsed=end-e.begin;
        add64(&row.exits,1);add64(&row.inclusive,elapsed);max64(&row.maximum,elapsed);
        switch(probe_configs[e.site<probe_site_limit?e.site:0].kind){
        case ProbeKind::ResourceOpen: {                          // AL = success; +0x04 flags after return
            if(!(regs[7]&0xff))add64(&row.extra[2],1);
            else {uint32_t flags=0;if(object_flags(e.ctx,flags))add64(&row.extra[(flags&3)==3?1:0],1);}
            break; }
        case ProbeKind::Resolve: add64(&row.extra[(regs[7]&0xff)?0:1],1); break; // AL = hit
        case ProbeKind::ResourceRead:                            // EAX = buffer or null; DAT_00596988 = size
            if(!regs[7])add64(&row.extra[3],1);
            else if(size_global)add64(&row.bytes,*size_global);
            break;
        default: break;
        }
        ret=e.ret;
    }
    SetLastError(error);
    return ret;
}
}
