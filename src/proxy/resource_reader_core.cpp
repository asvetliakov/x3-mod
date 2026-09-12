#include "resource_reader.h"
#include "loading_trace_light.h"
#include <cstring>

// Compiled with -mno-sse -mno-mmx -mfpmath=387; no floating-point work, no
// logging, no 64-bit atomic loads (see loading_trace_light.h). Runs inside the
// engine's loader thread at the entry of 0x004e8880 with no CPU-state boundary.

namespace x3m::resource_reader {
namespace {
using loading_trace::light::add64;
using loading_trace::light::max64;
using loading_trace::light::load64;
// zlib 1.2.3 z_stream, 32-bit layout (0x38 bytes, the size the game passes to inflateInit2_).
struct ZStream {
    const unsigned char* next_in; uint32_t avail_in,total_in;
    unsigned char* next_out; uint32_t avail_out,total_out;
    const char* msg; void* state; void* zalloc; void* zfree; void* opaque; int data_type; uint32_t adler,reserved;
};
static_assert(sizeof(ZStream)==0x38);
constexpr int z_no_flush=0,z_ok=0,z_stream_end=1,seek_set=0,seek_end=2;
constexpr uint32_t flag_open=1,flag_catalogue=2,flag_gz=4,flag_progress=0x10;
constexpr unsigned char catalogue_key=0x33,magic_key=0xc8;
inline uint64_t tick() noexcept { LARGE_INTEGER v{}; QueryPerformanceCounter(&v); return uint64_t(v.QuadPart); }
inline uint32_t le32(const unsigned char* p) noexcept { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
struct RestoreGuard { // puts the stream back where the original expects it when we bail out
    const Environment* env; FileObject* object; long position; bool armed;
    ~RestoreGuard(){ if(armed){env->fseek(object->file,position,seek_set);} }
};
Result fail(Result& r,Reason reason,RestoreGuard* guard=nullptr,void* scratch=nullptr) noexcept {
    r.outcome=Outcome::Fallback;r.reason=reason;
    if(scratch)HeapFree(GetProcessHeap(),0,scratch);
    (void)guard;
    return r;
}
}
const char* reason_name(unsigned reason) noexcept {
    static const char* const names[]={"none","unopened","gz_handle","progress","tell","state","length","scratch","short_read","not_gzip",
                                      "method","reserved","header","empty","alloc","init","inflate","size"};
    return reason<static_cast<unsigned>(Reason::Count)?names[reason]:"unknown";
}
void xor_bytes(void* data,size_t size,unsigned char key) noexcept {
    if(!key||!size)return;
    auto* p=static_cast<unsigned char*>(data);
    const uint32_t pattern=uint32_t(key)*0x01010101u;
    // Word-wide with unaligned loads (x86), four words per iteration; the
    // compiler keeps this on the integer unit (no SSE in this unit).
    while(size>=16){
        uint32_t w0,w1,w2,w3;
        std::memcpy(&w0,p,4);std::memcpy(&w1,p+4,4);std::memcpy(&w2,p+8,4);std::memcpy(&w3,p+12,4);
        w0^=pattern;w1^=pattern;w2^=pattern;w3^=pattern;
        std::memcpy(p,&w0,4);std::memcpy(p+4,&w1,4);std::memcpy(p+8,&w2,4);std::memcpy(p+12,&w3,4);
        p+=16;size-=16;
    }
    while(size>=4){uint32_t w;std::memcpy(&w,p,4);w^=pattern;std::memcpy(p,&w,4);p+=4;size-=4;}
    while(size--){*p++^=key;}
}
Result decode(const Environment& env,FileObject* object,bool game_buffer) noexcept {
    Result r;
    const uint64_t begin=tick();
    if(!object||!object->file||!(object->flags&flag_open))return fail(r,Reason::Unopened);
    if((object->flags&(flag_open|flag_gz))==(flag_open|flag_gz))return fail(r,Reason::GzHandle);
    if(object->flags&flag_progress)return fail(r,Reason::Progress);
    const bool catalogue=(object->flags&(flag_open|flag_catalogue))==(flag_open|flag_catalogue);
    r.catalogue=catalogue;
    void* const file=object->file;
    const long position=env.ftell(file);
    if(position<0)return fail(r,Reason::Tell);
    long begin_offset=0,length=0;
    if(catalogue){
        // The open body seeks to the record and zeroes the cursor; anything else is not the contract we know.
        if(object->cursor!=0||object->length<0||object->offset<0||position!=object->offset)return fail(r,Reason::State);
        begin_offset=object->offset;length=object->length;
    } else {
        if(position!=0)return fail(r,Reason::State);
        if(env.fseek(file,0,seek_end)!=0)return fail(r,Reason::Tell);
        length=env.ftell(file);
        if(length<0){env.fseek(file,position,seek_set);return fail(r,Reason::Tell);}
    }
    RestoreGuard guard{&env,object,position,true};
    if(length<18)return fail(r,Reason::Length);
    auto* scratch=static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(),0,size_t(length)));
    if(!scratch)return fail(r,Reason::Scratch);
    if(env.fseek(file,begin_offset,seek_set)!=0)return fail(r,Reason::ShortRead,&guard,scratch);
    const size_t got=env.fread(scratch,1,size_t(length),file);
    if(got!=size_t(length))return fail(r,Reason::ShortRead,&guard,scratch);
    r.extent=uint32_t(length);
    if(catalogue)xor_bytes(scratch,size_t(length),catalogue_key);
    // Magic: plain gzip at 0, or scrambled: byte 0 ^ 0xc8 is the key of every following byte.
    unsigned start=0;unsigned char key=0;
    if(scratch[0]==0x1f&&scratch[1]==0x8b){start=0;}
    else{
        key=static_cast<unsigned char>(scratch[0]^magic_key);
        if(((scratch[1]^key)&0xff)!=0x1f||((scratch[2]^key)&0xff)!=0x8b)return fail(r,Reason::NotGzip,&guard,scratch);
        start=1;r.scrambled=true;
        xor_bytes(scratch+1,size_t(length)-1,key);
    }
    const unsigned char* gz=scratch+start;const uint32_t gzlen=uint32_t(length)-start;
    if(gzlen<18)return fail(r,Reason::Length,&guard,scratch);
    const uint32_t isize=le32(gz+gzlen-4);
    if(gz[2]!=8)return fail(r,Reason::Method,&guard,scratch);
    const unsigned char flg=gz[3];
    if(flg&0xe0)return fail(r,Reason::Reserved,&guard,scratch);
    uint32_t p=10;const uint32_t limit=gzlen-8;
    if(flg&4){if(p+2>limit)return fail(r,Reason::Header,&guard,scratch);const uint32_t xlen=uint32_t(gz[p])|uint32_t(gz[p+1])<<8;p+=2+xlen;}
    if(flg&8){while(p<limit&&gz[p])++p;++p;}
    if(flg&0x10){while(p<limit&&gz[p])++p;++p;}
    if(flg&2)p+=2;
    if(p>limit)return fail(r,Reason::Header,&guard,scratch);
    if(isize==0)return fail(r,Reason::Empty,&guard,scratch);
    unsigned char* out=static_cast<unsigned char*>(game_buffer?env.malloc(isize):HeapAlloc(GetProcessHeap(),0,isize));
    if(!out)return fail(r,Reason::Alloc,&guard,scratch);
    auto release=[&]{ if(game_buffer)env.free(out); else HeapFree(GetProcessHeap(),0,out); };
    ZStream z{};
    if(env.inflateInit2_(&z,-15,"1.2.3",int(sizeof z))!=z_ok){release();return fail(r,Reason::Init,&guard,scratch);}
    z.next_in=gz+p;z.avail_in=gzlen-p;z.next_out=out;z.avail_out=isize;
    const int status=env.inflate(&z,z_no_flush);
    const uint32_t produced=isize-z.avail_out;
    env.inflateEnd(&z);
    if(status!=z_stream_end){release();return fail(r,Reason::Inflate,&guard,scratch);}
    if(produced!=isize){release();return fail(r,Reason::Size,&guard,scratch);}
    HeapFree(GetProcessHeap(),0,scratch);
    // Fast mode leaves the stream at the end of the extent, where the original
    // leaves it; a scratch decode (verify) puts it back so the original can run.
    guard.armed=!game_buffer;
    r.outcome=Outcome::Handled;r.buffer=out;r.size=isize;r.expected_cursor=catalogue?object->length:object->cursor;
    if(game_buffer){
        // Exactly the original's bookkeeping: three byte counters, the allocation count, the two size globals, the record cursor.
        for(unsigned i=0;i<3;++i)if(env.counters[i])*env.counters[i]+=isize;
        if(env.counters[3])*env.counters[3]+=1;
        for(unsigned i=0;i<2;++i)if(env.size_globals[i])*env.size_globals[i]=isize;
        if(catalogue)object->cursor=object->length;
    }
    r.ticks=tick()-begin;
    return r;
}

// ---- entry handler: mode dispatch, verify comparison, statistics ----
namespace {
Environment bound_env;
Mode bound_mode=Mode::Native;
void** continuation=nullptr;              // the stub's next word (the original entry)
void (*verify_sink)(const VerifyEvent&)=nullptr;
alignas(8) Statistics stats;
inline void* call_original(void* fn,FileObject* object) noexcept {
    void* result=object; // EAX in: the file object; EAX out: the buffer. ECX/EDX are caller-saved.
    asm volatile("call *%1" : "+a"(result) : "r"(fn) : "ecx","edx","memory","cc");
    return result;
}
}
void core_bind(const Environment& env,Mode mode,void** next_slot,void (*sink)(const VerifyEvent&)) noexcept {
    bound_env=env;bound_mode=mode;continuation=next_slot;verify_sink=sink;
}
// Cumulative snapshot: atomic 64-bit loads (cmpxchg8b of zero against zero),
// never an exchange with a stale plain read, which would drop a concurrent add.
Statistics statistics() {
    Statistics s;
    s.calls=load64(&stats.calls);s.handled=load64(&stats.handled);s.fallbacks=load64(&stats.fallbacks);
    s.bytes_in=load64(&stats.bytes_in);s.bytes_out=load64(&stats.bytes_out);s.ticks=load64(&stats.ticks);s.max_ticks=load64(&stats.max_ticks);
    s.verify_files=load64(&stats.verify_files);s.verify_equal=load64(&stats.verify_equal);s.verify_mismatched=load64(&stats.verify_mismatched);
    s.verify_original_null=load64(&stats.verify_original_null);s.original_ticks=load64(&stats.original_ticks);
    s.catalogue=load64(&stats.catalogue);s.scrambled=load64(&stats.scrambled);
    for(unsigned i=0;i<static_cast<unsigned>(Reason::Count);++i)s.reasons[i]=load64(&stats.reasons[i]);
    return s;
}
extern "C" uint32_t __cdecl x3m_resource_read_entry(uint32_t* regs) {
    const DWORD error=GetLastError();
    auto* object=reinterpret_cast<FileObject*>(regs[7]);
    add64(&stats.calls,1);
    if(bound_mode==Mode::Fast){
        const Result r=decode(bound_env,object,true);
        if(r.outcome==Outcome::Handled){
            add64(&stats.handled,1);add64(&stats.bytes_in,r.extent);add64(&stats.bytes_out,r.size);add64(&stats.ticks,r.ticks);max64(&stats.max_ticks,r.ticks);
            if(r.catalogue)add64(&stats.catalogue,1);
            if(r.scrambled)add64(&stats.scrambled,1);
            regs[7]=uint32_t(reinterpret_cast<uintptr_t>(r.buffer));
            SetLastError(error);return 1;
        }
        add64(&stats.fallbacks,1);add64(&stats.reasons[static_cast<unsigned>(r.reason)],1);
        SetLastError(error);return 0;
    }
    if(bound_mode==Mode::Verify&&continuation&&*continuation){
        Result r=decode(bound_env,object,false);
        if(r.outcome!=Outcome::Handled){add64(&stats.fallbacks,1);add64(&stats.reasons[static_cast<unsigned>(r.reason)],1);SetLastError(error);return 0;}
        add64(&stats.handled,1);add64(&stats.bytes_in,r.extent);add64(&stats.bytes_out,r.size);add64(&stats.ticks,r.ticks);max64(&stats.max_ticks,r.ticks);
        if(r.catalogue)add64(&stats.catalogue,1);
        if(r.scrambled)add64(&stats.scrambled,1);
        uint32_t counters_before[4]{},globals_before[2]{};
        for(unsigned i=0;i<4;++i)counters_before[i]=bound_env.counters[i]?*bound_env.counters[i]:0;
        for(unsigned i=0;i<2;++i)globals_before[i]=bound_env.size_globals[i]?*bound_env.size_globals[i]:0;
        const uint64_t t0=tick();
        void* original=call_original(*continuation,object);
        const uint64_t t1=tick();const DWORD original_error=GetLastError();
        add64(&stats.original_ticks,t1-t0);add64(&stats.verify_files,1);
        VerifyEvent e{};
        e.size=r.size;e.catalogue=r.catalogue;e.scrambled=r.scrambled;e.our_ticks=r.ticks;e.original_ticks=t1-t0;
        e.original_null=original==nullptr;
        e.original_size=bound_env.size_globals[0]?*bound_env.size_globals[0]:0;
        e.globals_ok=bound_env.size_globals[0]&&bound_env.size_globals[1]?(*bound_env.size_globals[0]==r.size&&*bound_env.size_globals[1]==r.size):true;
        e.counters_ok=true;
        for(unsigned i=0;i<3;++i)if(bound_env.counters[i]&&*bound_env.counters[i]-counters_before[i]!=r.size)e.counters_ok=false;
        if(bound_env.counters[3]&&*bound_env.counters[3]-counters_before[3]!=1)e.counters_ok=false;
        e.cursor_ok=!r.catalogue||object->cursor==r.expected_cursor;
        e.mismatches=0;e.first_mismatch=0;
        if(original){
            const auto* a=static_cast<const unsigned char*>(r.buffer);const auto* b=static_cast<const unsigned char*>(original);
            const uint32_t n=e.original_size<r.size?e.original_size:r.size;
            for(uint32_t i=0;i<n;++i)if(a[i]!=b[i]){if(!e.mismatches)e.first_mismatch=i;++e.mismatches;}
            if(e.original_size!=r.size){if(!e.mismatches)e.first_mismatch=n;e.mismatches+=e.original_size>r.size?e.original_size-r.size:r.size-e.original_size;}
        } else add64(&stats.verify_original_null,1);
        e.equal=!e.original_null&&!e.mismatches&&e.globals_ok&&e.counters_ok&&e.cursor_ok;
        if(e.equal)add64(&stats.verify_equal,1);else add64(&stats.verify_mismatched,1);
        (void)globals_before;
        HeapFree(GetProcessHeap(),0,r.buffer);
        if(verify_sink&&!e.equal)verify_sink(e);
        regs[7]=uint32_t(reinterpret_cast<uintptr_t>(original));
        SetLastError(original_error);return 1;
    }
    SetLastError(error);return 0;
}

// ---- catalogue .dat handle pool ----
namespace {
constexpr unsigned pool_entries=32,pool_path_length=128;
struct PoolEntry { void* file; unsigned state; char path[pool_path_length]; }; // state 0 free slot, 1 in use, 2 kept
PoolEntry pool[pool_entries];
PoolEnvironment pool_env;
bool pool_bound=false;
SRWLOCK pool_lock=SRWLOCK_INIT;
alignas(8) PoolStatistics pool_stats;
inline bool same_path(const char* a,const char* b) noexcept { while(*a&&*a==*b){++a;++b;} return *a==*b; }
inline bool stream_error(void* file) noexcept { return (*reinterpret_cast<const uint32_t*>(static_cast<const char*>(file)+pool_env.flag_offset)&pool_env.error_flag)!=0; }
}
void pool_bind(const PoolEnvironment& env) noexcept { pool_env=env;pool_bound=env.fopen&&env.fclose; }
extern "C" void* __cdecl x3m_pool_fopen(const char* path,const char* mode) {
    if(!pool_bound)return nullptr;
    add64(&pool_stats.opens,1);
    const bool read_binary=path&&mode&&mode[0]=='r'&&mode[1]=='b'&&mode[2]==0;
    if(read_binary){
        AcquireSRWLockExclusive(&pool_lock);
        for(auto& e:pool)if(e.state==2&&same_path(e.path,path)){e.state=1;void* f=e.file;--pool_stats.held;ReleaseSRWLockExclusive(&pool_lock);add64(&pool_stats.reused,1);return f;}
        ReleaseSRWLockExclusive(&pool_lock);
    }
    add64(&pool_stats.real_opens,1);
    void* f=pool_env.fopen(path,mode);
    if(!f||!read_binary)return f;
    unsigned n=0;while(path[n])++n;
    if(n>=pool_path_length){add64(&pool_stats.full,1);return f;}
    AcquireSRWLockExclusive(&pool_lock);
    for(auto& e:pool)if(e.state==0){e.state=1;e.file=f;std::memcpy(e.path,path,n+1);ReleaseSRWLockExclusive(&pool_lock);return f;}
    ReleaseSRWLockExclusive(&pool_lock);
    add64(&pool_stats.full,1);
    return f;
}
extern "C" int __cdecl x3m_pool_fclose(void* file) {
    if(!pool_bound)return -1;
    add64(&pool_stats.closes,1);
    AcquireSRWLockExclusive(&pool_lock);
    for(auto& e:pool)if(e.state==1&&e.file==file){
        if(stream_error(file)){e.state=0;e.file=nullptr;ReleaseSRWLockExclusive(&pool_lock);add64(&pool_stats.errors,1);add64(&pool_stats.real_closes,1);return pool_env.fclose(file);}
        e.state=2;++pool_stats.held;ReleaseSRWLockExclusive(&pool_lock);add64(&pool_stats.kept,1);return 0;
    }
    ReleaseSRWLockExclusive(&pool_lock);
    add64(&pool_stats.real_closes,1);
    return pool_env.fclose(file);
}
PoolStatistics pool_statistics() noexcept {
    PoolStatistics s;
    s.opens=load64(&pool_stats.opens);s.reused=load64(&pool_stats.reused);s.real_opens=load64(&pool_stats.real_opens);
    s.closes=load64(&pool_stats.closes);s.kept=load64(&pool_stats.kept);s.real_closes=load64(&pool_stats.real_closes);
    s.errors=load64(&pool_stats.errors);s.full=load64(&pool_stats.full);
    AcquireSRWLockExclusive(&pool_lock);s.held=pool_stats.held;ReleaseSRWLockExclusive(&pool_lock); // held is maintained under the lock
    return s;
}
void pool_drain() noexcept {
    if(!pool_bound)return;
    AcquireSRWLockExclusive(&pool_lock);
    for(auto& e:pool)if(e.state==2){pool_env.fclose(e.file);e.state=0;e.file=nullptr;--pool_stats.held;add64(&pool_stats.real_closes,1);}
    ReleaseSRWLockExclusive(&pool_lock);
}
}
