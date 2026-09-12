#include "resource_reader.h"
#include "engine_patch.h"
#include "loading_probes.h"
#include "capture.h"
#include "cpu_state.h"
#include "object_trace.h"
#include <cstring>

// Hook side of the fast resource reader (SSE build): reads the switches,
// verifies the executable and the site, binds the game's CRT/zlib functions
// into the core's Environment, generates the entry stub and chains it in
// front of 0x004e8880 (after the probe stub when both are on), logs, reports.
// Addresses: docs/reverse-engineering/resource-reader.md.
namespace x3m::resource_reader {
namespace {
constexpr uintptr_t reader_va=0x004e8880;
const unsigned char reader_bytes[6]={0x81,0xec,0x54,0x04,0x00,0x00}; // sub esp,0x454
constexpr uintptr_t fread_va=0x00512213,fseek_va=0x00510343,ftell_va=0x005128e6,malloc_va=0x005112c4,free_va=0x0050e1b0;
constexpr uintptr_t counter_va[4]={0x006085f4,0x006085f8,0x006089f8,0x006089fc};
constexpr uintptr_t size_global_va[2]={0x00596988,0x0059698c};
// First bytes of the CRT callees (belt and braces next to the executable hash).
struct Callee { uintptr_t va; unsigned char bytes[5]; };
const Callee callees[]={
    {fread_va,{0xff,0x74,0x24,0x10,0xff}},   // push [esp+0x10] ... (fread -> _fread_s)
    {fseek_va,{0x6a,0x0c,0x68,0x80,0xd0}},   // SEH prolog of _fseek
    {ftell_va,{0x6a,0x0c,0x68,0x40,0xd1}},   // SEH prolog of _ftell
    {malloc_va,{0x55,0x8b,0x6c,0x24,0x08}},  // _malloc
    {free_va,{0x6a,0x0c,0x68,0x98,0xce}},    // SEH prolog of _free
};
// .dat handle pool call sites: _fopen in the catalogue branch of 0x004e8780 and the _fclose of 0x004e9360.
constexpr uintptr_t pool_fopen_site=0x004e87ff,pool_fclose_site=0x004e9392,fopen_va=0x00512a18,fclose_va=0x0050f8d4;
Mode mode_=Mode::Native;
bool initialized_=false,installed_=false,pool_requested_=false,pool_installed_=false;
const char* status_="disabled";
engine_patch::Site own_site;
engine_patch::Site* site=nullptr;
engine_patch::CallSite pool_open_site,pool_close_site;
void** next_slot=nullptr;
uint64_t frequency=1,last_report_calls=0,last_pool_opens=0;
unsigned mismatch_lines=0;
constexpr unsigned mismatch_line_limit=64;
const char* mode_name(Mode m){return m==Mode::Fast?"fast":m==Mode::Verify?"verify":"native";}
void verify_sink(const VerifyEvent& e){
    PreserveCpuState cpu;
    if(mismatch_lines++>=mismatch_line_limit)return;
    log("resource_reader verify equal=%u size=%lu original_size=%lu mismatches=%lu first=%lu original_null=%u globals_ok=%u counters_ok=%u cursor_ok=%u catalogue=%u scrambled=%u our_us=%.3f original_us=%.3f",
        unsigned(e.equal),static_cast<unsigned long>(e.size),static_cast<unsigned long>(e.original_size),static_cast<unsigned long>(e.mismatches),static_cast<unsigned long>(e.first_mismatch),
        unsigned(e.original_null),unsigned(e.globals_ok),unsigned(e.counters_ok),unsigned(e.cursor_ok),unsigned(e.catalogue),unsigned(e.scrambled),
        double(e.our_ticks)*1e6/double(frequency),double(e.original_ticks)*1e6/double(frequency));
}
// Reader entry stub:
//   pushfd ; pushad ; mov eax,esp ; push eax ; call x3m_resource_read_entry ; add esp,4
//   test eax,eax ; jz continue ; popad ; popfd ; ret
//   continue: popad ; popfd ; jmp [next]
void* emit_reader_stub(void*** next_out) {
    engine_patch::Emitter e(48);
    if(!e.ok())return nullptr;
    void* start=e.here();
    e.byte(0x9c);e.byte(0x60);e.byte(0x89);e.byte(0xe0);e.byte(0x50);
    e.byte(0xe8);e.rel32(reinterpret_cast<const void*>(&x3m_resource_read_entry));
    e.byte(0x83);e.byte(0xc4);e.byte(0x04);e.byte(0x85);e.byte(0xc0);e.byte(0x74);e.byte(0x03);
    e.byte(0x61);e.byte(0x9d);e.byte(0xc3);
    e.byte(0x61);e.byte(0x9d);
    unsigned char* jmp=static_cast<unsigned char*>(e.here());
    uintptr_t next=reinterpret_cast<uintptr_t>(jmp)+6;next=(next+3)&~uintptr_t(3);
    e.byte(0xff);e.byte(0x25);e.dword(uint32_t(next));
    while(reinterpret_cast<uintptr_t>(e.here())<next)e.byte(0xcc);
    *next_out=reinterpret_cast<void**>(next);e.dword(0);
    return e.finish()?start:nullptr;
}
bool install_stub(engine_patch::Site& target,const Environment& env,Mode mode) {
    void** next=nullptr;void* stub=emit_reader_stub(&next);
    if(!stub||!next){status_="stub_failed";return false;}
    if(!engine_patch::store_pointer(next,*target.entry)){status_="chain_write_failed";return false;}
    core_bind(env,mode,next,&verify_sink);
    if(!engine_patch::push_front(target,stub)){status_="chain_failed";return false;}
    next_slot=next;site=&target;installed_=true;status_="active";
    return true;
}
Environment game_environment() {
    Environment env;
    env.fread=reinterpret_cast<decltype(env.fread)>(fread_va);env.fseek=reinterpret_cast<decltype(env.fseek)>(fseek_va);
    env.ftell=reinterpret_cast<decltype(env.ftell)>(ftell_va);env.malloc=reinterpret_cast<decltype(env.malloc)>(malloc_va);
    env.free=reinterpret_cast<decltype(env.free)>(free_va);
    for(unsigned i=0;i<4;++i)env.counters[i]=reinterpret_cast<uint32_t*>(counter_va[i]);
    for(unsigned i=0;i<2;++i)env.size_globals[i]=reinterpret_cast<uint32_t*>(size_global_va[i]);
    if(HMODULE zlib=GetModuleHandleW(L"zlib1.dll")){
        env.inflateInit2_=reinterpret_cast<decltype(env.inflateInit2_)>(GetProcAddress(zlib,"inflateInit2_"));
        env.inflate=reinterpret_cast<decltype(env.inflate)>(GetProcAddress(zlib,"inflate"));
        env.inflateEnd=reinterpret_cast<decltype(env.inflateEnd)>(GetProcAddress(zlib,"inflateEnd"));
    }
    return env;
}
}
Mode mode(){return mode_;}
bool installed(){return installed_;}
const char* status(){return status_;}
bool pool_requested(){return pool_requested_;}
bool pool_installed(){return pool_installed_;}
bool initialize() {
    const DWORD error=GetLastError();
    if(initialized_){SetLastError(error);return installed_;}
    initialized_=true;
    LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart>0?uint64_t(f.QuadPart):1;
    wchar_t setting[16]{};
    const DWORD length=GetEnvironmentVariableW(L"X3M_RESOURCE_READ",setting,16);
    if(length&&length<16){if(!lstrcmpiW(setting,L"fast"))mode_=Mode::Fast;else if(!lstrcmpiW(setting,L"verify"))mode_=Mode::Verify;}
    wchar_t pool_setting[8]{};
    pool_requested_=GetEnvironmentVariableW(L"X3M_DAT_HANDLES",pool_setting,8)==1&&pool_setting[0]==L'1';
    if(mode_==Mode::Native&&!pool_requested_){status_="disabled";SetLastError(error);return false;}
    if(!object_trace::executable_verified()){status_="executable_mismatch";log("resource_reader mode=%s installed=0 status=%s",mode_name(mode_),status_);SetLastError(error);return false;}
    if(mode_!=Mode::Native){
        bool callees_ok=true;
        for(const auto& c:callees)callees_ok&=engine_patch::verify_bytes(c.va,c.bytes,5);
        const Environment env=game_environment();
        if(!callees_ok)status_="crt_bytes_mismatch";
        else if(!env.inflateInit2_||!env.inflate||!env.inflateEnd)status_="zlib_exports_missing";
        else{
            engine_patch::Site* target=loading_probes::resource_read_site();
            if(!target){
                engine_patch::SiteSpec spec{"resource_read",reader_va,{},6,0};std::memcpy(spec.expected,reader_bytes,6);
                if(engine_patch::claim(own_site,spec))target=&own_site;else status_=own_site.status;
            }
            if(target)install_stub(*target,env,mode_);
        }
        log("resource_reader mode=%s installed=%u status=%s site=0x%08lx chained_after_probe=%u scope=whole_extent_fread,word_xor,single_inflate,game_malloc,no_memset fallback=original arena_used=%u",
            mode_name(mode_),unsigned(installed_),status_,static_cast<unsigned long>(reader_va),unsigned(loading_probes::resource_read_site()!=nullptr),engine_patch::arena_used());
    }
    if(pool_requested_){
        PoolEnvironment pool;
        pool.fopen=reinterpret_cast<decltype(pool.fopen)>(fopen_va);pool.fclose=reinterpret_cast<decltype(pool.fclose)>(fclose_va);
        pool_bind(pool);
        const bool open_ok=engine_patch::claim_call(pool_open_site,pool_fopen_site,fopen_va,reinterpret_cast<void*>(&x3m_pool_fopen));
        const bool close_ok=open_ok&&engine_patch::claim_call(pool_close_site,pool_fclose_site,fclose_va,reinterpret_cast<void*>(&x3m_pool_fclose));
        if(open_ok&&!close_ok)engine_patch::restore_call(pool_open_site);
        pool_installed_=open_ok&&close_ok;
        log("dat_handle_pool requested=1 installed=%u open_site=0x%08lx open_status=%s close_site=0x%08lx close_status=%s entries=32 scope=catalogue_fopen_reuse",
            unsigned(pool_installed_),static_cast<unsigned long>(pool_fopen_site),pool_open_site.status,static_cast<unsigned long>(pool_fclose_site),pool_close_site.status);
    }
    SetLastError(error);
    return installed_||pool_installed_;
}
void report() {
    if(!installed_&&!pool_installed_)return;
    const DWORD error=GetLastError();const PreserveCpuState cpu;
    if(installed_){
        const Statistics s=statistics();
        if(s.calls!=last_report_calls){
            last_report_calls=s.calls;
            log("resource_reader_metric cumulative=1 mode=%s calls=%llu handled=%llu fallbacks=%llu bytes_in=%llu bytes_out=%llu fast_us=%.3f fast_max_us=%.3f original_us=%.3f verify_files=%llu verify_equal=%llu verify_mismatched=%llu verify_original_null=%llu catalogue=%llu scrambled=%llu "
                "fallback_unopened=%llu fallback_gz_handle=%llu fallback_progress=%llu fallback_tell=%llu fallback_state=%llu fallback_length=%llu fallback_scratch=%llu fallback_short_read=%llu fallback_not_gzip=%llu fallback_method=%llu fallback_reserved=%llu fallback_header=%llu fallback_empty=%llu fallback_alloc=%llu fallback_init=%llu fallback_inflate=%llu fallback_size=%llu",
                mode_name(mode_),s.calls,s.handled,s.fallbacks,s.bytes_in,s.bytes_out,double(s.ticks)*1e6/double(frequency),double(s.max_ticks)*1e6/double(frequency),double(s.original_ticks)*1e6/double(frequency),
                s.verify_files,s.verify_equal,s.verify_mismatched,s.verify_original_null,s.catalogue,s.scrambled,
                s.reasons[1],s.reasons[2],s.reasons[3],s.reasons[4],s.reasons[5],s.reasons[6],s.reasons[7],s.reasons[8],s.reasons[9],s.reasons[10],s.reasons[11],s.reasons[12],s.reasons[13],s.reasons[14],s.reasons[15],s.reasons[16],s.reasons[17]);
        }
    }
    if(pool_installed_){
        const PoolStatistics p=pool_statistics();
        if(p.opens!=last_pool_opens){
            last_pool_opens=p.opens;
            log("dat_handle_pool_metric cumulative=1 opens=%llu reused=%llu real_opens=%llu closes=%llu kept=%llu real_closes=%llu errors=%llu full=%llu held=%u",p.opens,p.reused,p.real_opens,p.closes,p.kept,p.real_closes,p.errors,p.full,p.held);
        }
    }
    SetLastError(error);
}
void shutdown() {
    report();
    if(pool_installed_){engine_patch::restore_call(pool_open_site);engine_patch::restore_call(pool_close_site);pool_drain();pool_installed_=false;}
    if(site==&own_site&&own_site.patched_in)engine_patch::restore(own_site);
    installed_=false;
}
#ifdef X3M_RESOURCE_READER_FIXTURE
void fixture_bind(const Environment& env,Mode mode) { core_bind(env,mode,next_slot,&verify_sink);mode_=mode;initialized_=true; }
void fixture_pool_bind(const PoolEnvironment& env) { pool_bind(env); }
bool fixture_install(uintptr_t site_address,const unsigned char* expected,unsigned length) {
    LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart>0?uint64_t(f.QuadPart):1;
    engine_patch::SiteSpec spec{"resource_read",site_address,{},length,0};std::memcpy(spec.expected,expected,length);
    engine_patch::Site* target=loading_probes::resource_read_site();
    if(!target){if(!engine_patch::claim(own_site,spec)){status_=own_site.status;return false;}target=&own_site;}
    Environment env;return install_stub(*target,env,mode_);
}
void fixture_shutdown(){shutdown();initialized_=false;mismatch_lines=0;}
#endif
}
