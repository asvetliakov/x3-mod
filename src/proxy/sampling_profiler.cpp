#include "sampling_profiler.h"
#include "capture.h"
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>

// Safety contract of the suspended window (SuspendThread .. ResumeThread): only
// GetThreadContext plus plain reads of memory whose mapping was verified in the
// refresh step (the target's own stack between its TEB limits, and executable
// ranges of modules pinned at first sight, recorded with VirtualQuery inside the
// pinned allocation, so nothing the tick reads can be unmapped while the
// sampler runs) and GetLastError (a TEB field of the sampler itself). No
// allocation, logging, CRT I/O or other Wine/Win32 calls: a suspended thread
// may own the heap, loader or log lock. Every table is fixed-size static storage owned by the sampler thread;
// other threads only read the atomic counters.
namespace x3m::sampling_profiler {
namespace {
constexpr unsigned max_threads=32, max_modules=128, max_ranges=4, max_sorted=max_modules*max_ranges;
constexpr unsigned chain_limit=32, scan_dwords=1024, scan_limit=64, merged_limit=chain_limit+scan_limit;
constexpr unsigned leaf_capacity=65536, frame_capacity=65536, pair_capacity=16384, probe_limit=64;
constexpr unsigned delta_tops=48, delta_pair_tops=32, cumulative_tops=256, cumulative_every=12;
constexpr uintptr_t stack_limit_bytes=64u<<20;
constexpr uint32_t no_frame=0; // RVA 0 is the DOS header, never code: "no main-module frame".
enum Kind : unsigned { KindX3ap, KindNtdll, KindWine, KindD3dx, KindZlib, KindXml, KindProxy, KindOther, KindCount };
constexpr const char* kind_names[KindCount]={"x3ap","ntdll","wine","d3dx","zlib","xml","proxy","other"};

struct Range { uintptr_t begin=0, end=0; };
struct Module {
    uintptr_t base=0; uint32_t size=0; Range ranges[max_ranges]; unsigned range_count=0;
    Kind kind=KindOther; char name[64]{}; bool used=false, loaded=false, seen=false, pinned=false;
};
struct SortedRange { uintptr_t begin, end; unsigned module; };
struct Thread {
    HANDLE handle=nullptr; DWORD tid=0; uintptr_t teb=0; uint64_t creation=0;
    bool used=false, seen=false, retired=false;
    unsigned start_module=~0u; uint32_t start_rva=0;
    uint64_t samples=0, total=0, leaf[KindCount]{}, leaf_total[KindCount]{};
    uint64_t suspend_failures=0, context_failures=0, stack_unknown=0;
    uint64_t suspend_failures_delta=0, context_failures_delta=0;   // per report period
};
struct LeafKey { uint32_t rva; uint16_t module; uint16_t slot; };   // slot stored +1: zero means empty
struct FrameKey { uint32_t rva; uint32_t slot; };
struct PairKey { uint32_t rva, caller; uint32_t slot; };
bool same(const LeafKey& a,const LeafKey& b){return a.rva==b.rva&&a.module==b.module&&a.slot==b.slot;}
bool same(const FrameKey& a,const FrameKey& b){return a.rva==b.rva&&a.slot==b.slot;}
bool same(const PairKey& a,const PairKey& b){return a.rva==b.rva&&a.caller==b.caller&&a.slot==b.slot;}
uint32_t mix(uint32_t h){h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;return h;}
uint32_t hash(const LeafKey& k){return mix(k.rva*0x9e3779b1u^(uint32_t(k.module)<<20)^(uint32_t(k.slot)*0x85ebca77u));}
uint32_t hash(const FrameKey& k){return mix(k.rva*0x9e3779b1u^(k.slot*0x85ebca77u));}
uint32_t hash(const PairKey& k){return mix(k.rva*0x9e3779b1u^(k.caller*0xc2b2ae3du)^(k.slot*0x85ebca77u));}
template<typename Key,unsigned Capacity> struct Table {
    static_assert((Capacity&(Capacity-1))==0);
    Key keys[Capacity]; uint32_t counts[Capacity]; unsigned used=0; uint64_t dropped=0;
    // Linear probing bounded by probe_limit; a full neighbourhood drops the sample.
    void add(const Key& key,uint32_t n){
        uint32_t index=hash(key)&(Capacity-1);
        for(unsigned probe=0;probe<probe_limit;++probe,index=(index+1)&(Capacity-1)){
            if(!keys[index].slot){keys[index]=key;counts[index]=n;++used;return;}
            if(same(keys[index],key)){counts[index]+=n;return;}
        }
        dropped+=n;
    }
    void clear(){if(!used)return;std::memset(keys,0,sizeof keys);std::memset(counts,0,sizeof counts);used=0;}
};
struct Ranked { uint32_t index, count; };

Table<LeafKey,leaf_capacity> leaf_delta, leaf_total;
Table<FrameKey,frame_capacity> frame_delta, frame_total;
Table<PairKey,pair_capacity> pair_delta, pair_total;
Ranked ranked[leaf_capacity];
Module modules[max_modules];
SortedRange sorted[max_sorted]; unsigned sorted_count=0;
Thread threads[max_threads];
unsigned main_module=~0u;

std::atomic<bool> running{false}, stop_requested{false};
bool started=false, finished=false;
HANDLE sampler=nullptr, stop_event=nullptr;
DWORD sampler_tid=0, init_tid=0;
uintptr_t main_base=0, proxy_base=0;
wchar_t windows_directory[MAX_PATH]{}; size_t windows_directory_length=0;
using QueryThreadFn=LONG (WINAPI*)(HANDLE,int,PVOID,ULONG,PULONG);
QueryThreadFn query_thread=nullptr;
Settings settings;
uint64_t frequency=0, startup=0, interval_ticks=0, report_interval_ticks=0;
std::atomic<uint64_t> tick_count{0}, sample_count{0}, drop_count{0}, report_count{0};
std::atomic<uint64_t> tick_total{0}, tick_maximum{0}, report_total{0}, refresh_total{0};
std::atomic<unsigned> thread_count{0}, module_count{0}, unsampled_count{0};
// Per-report accumulators owned by the sampler thread.
uint64_t delta_ticks=0, delta_tick_total=0, delta_tick_max=0, delta_samples=0, delta_refresh=0, delta_dropped=0;
uint64_t last_report=0, last_refresh=0;

uint64_t qpc(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return uint64_t(v.QuadPart);}
double us(uint64_t ticks){return frequency?double(ticks)*1e6/double(frequency):0;}

// Module lookup: the sorted executable ranges serve the scan; whole-image
// lookup classifies a leaf that sits outside a text section.
unsigned module_of(uintptr_t address){
    for(unsigned i=0;i<max_modules;++i){const auto& m=modules[i];if(m.used&&m.loaded&&address>=m.base&&address-m.base<m.size)return i;}
    return ~0u;
}
const SortedRange* range_of(uintptr_t address){
    unsigned lo=0,hi=sorted_count;
    while(lo<hi){const unsigned mid=(lo+hi)/2;if(sorted[mid].begin<=address)lo=mid+1;else hi=mid;}
    if(!lo)return nullptr;
    const auto& r=sorted[lo-1];return address<r.end?&r:nullptr;
}
// Return-address heuristic: the bytes before the candidate decode as a call
// (E8 rel32, or the FF /2 register, memory, disp8, disp32 and SIB forms).
bool call_precedes(uintptr_t address,const SortedRange& r){
    if(address<r.begin+8)return false;
    const auto* b=reinterpret_cast<const volatile unsigned char*>(address);
    if(b[-5]==0xe8)return true;
    if(b[-2]==0xff){const unsigned m=b[-1];if((m&0xf8)==0xd0)return true;if((m&0xf8)==0x10&&(m&7)!=4&&(m&7)!=5)return true;}
    if(b[-3]==0xff){const unsigned m=b[-2];if((m&0xf8)==0x50&&(m&7)!=4)return true;if(m==0x14)return true;}
    if(b[-4]==0xff&&b[-3]==0x54)return true;
    if(b[-6]==0xff){const unsigned m=b[-5];if((m&0xf8)==0x90&&(m&7)!=4)return true;if(m==0x15)return true;}
    if(b[-7]==0xff&&b[-6]==0x94)return true;
    return false;
}

struct Walk {
    uintptr_t eip=0; unsigned leaf_module=~0u; bool stack_known=false;
    unsigned count=0; struct Entry{uintptr_t slot,address;} merged[merged_limit];
};
// Inside the suspended window. Returns false when the context is unavailable.
bool capture(const Thread& t,Walk& w){
    CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
    if(!GetThreadContext(t.handle,&context))return false;
    const uintptr_t eip=context.Eip,esp=context.Esp,ebp=context.Ebp;
    w.eip=eip;w.leaf_module=module_of(eip);w.count=0;w.stack_known=false;
    if(!t.teb||t.teb%4)return true;
    const auto* tib=reinterpret_cast<const volatile uintptr_t*>(t.teb);
    const uintptr_t base=tib[1],limit=tib[2]; // NT_TIB: ExceptionList, StackBase, StackLimit
    if(!(limit<base&&base-limit<=stack_limit_bytes&&limit%4==0&&base%4==0&&esp>=limit&&esp<base&&esp%4==0))return true;
    w.stack_known=true;
    Walk::Entry chain[chain_limit];unsigned chain_count=0;
    uintptr_t frame=ebp;
    while(chain_count<chain_limit){
        if(frame%4||frame<esp||frame+8>base)break;
        const auto* slots=reinterpret_cast<const volatile uintptr_t*>(frame);
        const uintptr_t next=slots[0],address=slots[1];
        if(!range_of(address))break;
        chain[chain_count++]={frame+4,address};
        if(next<=frame)break;
        frame=next;
    }
    Walk::Entry scan[scan_limit];unsigned scan_count=0;
    const uintptr_t stop=std::min(base,esp+uintptr_t(scan_dwords)*4);
    for(uintptr_t slot=esp;slot<stop&&scan_count<scan_limit;slot+=4){
        const uintptr_t value=*reinterpret_cast<const volatile uintptr_t*>(slot);
        const auto* r=range_of(value);
        if(r&&call_precedes(value,*r))scan[scan_count++]={slot,value};
    }
    // Merge by stack slot: a scan hit below the chain's next slot is a frame the
    // chain skipped (frame-pointer-omitting code); an equal slot is the same frame.
    unsigned a=0,b=0;
    while((a<chain_count||b<scan_count)&&w.count<merged_limit){
        if(b>=scan_count||(a<chain_count&&chain[a].slot<=scan[b].slot)){
            if(a<chain_count&&b<scan_count&&chain[a].slot==scan[b].slot)++b;
            w.merged[w.count++]=chain[a++];
        } else w.merged[w.count++]=scan[b++];
    }
    return true;
}
void aggregate(unsigned slot,Thread& t,const Walk& w){
    ++t.samples;++t.total;++delta_samples;
    const Kind kind=w.leaf_module==~0u?KindOther:modules[w.leaf_module].kind;
    ++t.leaf[kind];++t.leaf_total[kind];
    if(!w.stack_known)++t.stack_unknown;
    const uint32_t leaf_rva=w.leaf_module==~0u?uint32_t(w.eip):uint32_t(w.eip-modules[w.leaf_module].base);
    leaf_delta.add({leaf_rva,uint16_t(w.leaf_module==~0u?0xffff:w.leaf_module),uint16_t(slot+1)},1);
    uint32_t frame_rva=no_frame,caller_rva=no_frame;unsigned i=0;
    const uintptr_t main_size=main_module==~0u?0:modules[main_module].size;
    const auto in_main=[&](uintptr_t address){return main_size&&address>=main_base&&address-main_base<main_size;};
    if(main_size&&w.leaf_module==main_module)frame_rva=leaf_rva;
    else for(;i<w.count;++i){const auto& e=w.merged[i];if(in_main(e.address)){frame_rva=uint32_t(e.address-main_base);++i;break;}}
    if(frame_rva!=no_frame)for(;i<w.count;++i){const auto& e=w.merged[i];if(in_main(e.address)){caller_rva=uint32_t(e.address-main_base);break;}}
    frame_delta.add({frame_rva,slot+1},1);
    if(caller_rva!=no_frame)pair_delta.add({frame_rva,caller_rva,slot+1},1);
}

Kind classify(const MODULEENTRY32W& entry,uintptr_t base){
    if(base==main_base)return KindX3ap;
    if(base==proxy_base)return KindProxy;
    if(!_wcsicmp(entry.szModule,L"ntdll.dll"))return KindNtdll;
    if(!_wcsnicmp(entry.szModule,L"d3dx9",5))return KindD3dx;
    if(!_wcsicmp(entry.szModule,L"zlib1.dll"))return KindZlib;
    if(!_wcsicmp(entry.szModule,L"libxml2.dll"))return KindXml;
    if(windows_directory_length&&!_wcsnicmp(entry.szExePath,windows_directory,windows_directory_length))return KindWine;
    return KindOther;
}
void refresh_modules(){
    for(auto& m:modules)m.seen=false;
    // A failed snapshot (transient ERROR_BAD_LENGTH on Windows) keeps the
    // previous tables rather than marking every module unloaded.
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,0);
    if(snapshot==INVALID_HANDLE_VALUE)return;
    {
        MODULEENTRY32W entry{};entry.dwSize=sizeof entry;
        for(BOOL more=Module32FirstW(snapshot,&entry);more;more=Module32NextW(snapshot,&entry)){
            const uintptr_t base=reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            unsigned index=~0u,free_index=~0u;
            for(unsigned i=0;i<max_modules;++i){
                if(modules[i].used&&modules[i].base==base&&modules[i].size==entry.modBaseSize){index=i;break;}
                if(!modules[i].used&&free_index==~0u)free_index=i;
            }
            const bool fresh=index==~0u;
            if(fresh){if(free_index==~0u)continue;index=free_index;}
            auto& m=modules[index];
            if(fresh){
                m=Module{};m.base=base;m.size=entry.modBaseSize;m.kind=classify(entry,base);m.used=true;
                unsigned n=0;for(;n<sizeof m.name-1&&entry.szModule[n];++n)m.name[n]=entry.szModule[n]<0x80?char(entry.szModule[n]):'?';m.name[n]=0;
            }
            if(!m.loaded&&!m.pinned){
                // The tick reads code bytes of this mapping while a thread is
                // suspended, so the mapping must outlive the table: pin it for
                // the process lifetime (diagnostics only). A module that cannot
                // be pinned (already unloading) is never read.
                HMODULE pinned=nullptr;
                m.pinned=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(base),&pinned)&&reinterpret_cast<uintptr_t>(pinned)==base;
                if(!m.pinned&&fresh)log("profile_module index=%u name=%s base=0x%08lx pinned=0 error=%lu",index,m.name,static_cast<unsigned long>(base),GetLastError());
            }
            if(m.pinned&&!m.loaded){
                // Executable ranges from the live mapping, never from headers,
                // and only inside the pinned image allocation.
                m.range_count=0;
                uintptr_t cursor=base;
                while(cursor<base+m.size){
                    MEMORY_BASIC_INFORMATION info{};
                    if(!VirtualQuery(reinterpret_cast<void*>(cursor),&info,sizeof info)||!info.RegionSize)break;
                    if(reinterpret_cast<uintptr_t>(info.AllocationBase)!=base)break;
                    const uintptr_t begin=reinterpret_cast<uintptr_t>(info.BaseAddress),end=std::min<uintptr_t>(begin+info.RegionSize,base+uintptr_t(m.size));
                    const bool executable=info.State==MEM_COMMIT&&(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))&&!(info.Protect&PAGE_GUARD);
                    if(executable){
                        if(m.range_count&&m.ranges[m.range_count-1].end==begin)m.ranges[m.range_count-1].end=end;
                        else if(m.range_count<max_ranges)m.ranges[m.range_count++]={begin,end};
                    }
                    if(end<=cursor)break;
                    cursor=end;
                }
                m.loaded=true;
                uintptr_t text_begin=m.range_count?m.ranges[0].begin:base,text_end=m.range_count?m.ranges[m.range_count-1].end:base;
                log("profile_module index=%u name=%s kind=%s base=0x%08lx size=0x%lx text_rva=0x%lx text_size=0x%lx ranges=%u pinned=1",index,m.name,kind_names[m.kind],
                    static_cast<unsigned long>(base),static_cast<unsigned long>(m.size),static_cast<unsigned long>(text_begin-base),static_cast<unsigned long>(text_end-text_begin),m.range_count);
                if(base==main_base)main_module=index;
            }
            m.seen=true;
        }
        CloseHandle(snapshot);
    }
    for(unsigned i=0;i<max_modules;++i){auto& m=modules[i];if(m.used&&!m.seen&&m.loaded){m.loaded=false;m.range_count=0;log("profile_module index=%u unloaded=1",i);}}
    sorted_count=0;unsigned count=0;
    for(unsigned i=0;i<max_modules;++i){const auto& m=modules[i];if(!m.used||!m.loaded)continue;++count;for(unsigned r=0;r<m.range_count;++r)sorted[sorted_count++]={m.ranges[r].begin,m.ranges[r].end,i};}
    std::sort(sorted,sorted+sorted_count,[](const SortedRange& a,const SortedRange& b){return a.begin<b.begin;});
    module_count.store(count,std::memory_order_relaxed);
}
struct Candidate { DWORD tid; uint64_t creation; };
void refresh_threads(){
    for(auto& t:threads)t.seen=false;
    Candidate candidates[256]{};unsigned candidate_count=0;
    const DWORD pid=GetCurrentProcessId();
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE)return; // keep the known threads; retry next refresh
    {
        THREADENTRY32 entry{};entry.dwSize=sizeof entry;
        for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry)){
            if(entry.th32OwnerProcessID!=pid||entry.th32ThreadID==sampler_tid)continue;
            bool known=false;
            for(auto& t:threads)if(t.used&&!t.retired&&t.tid==entry.th32ThreadID){t.seen=true;known=true;break;}
            if(!known&&candidate_count<256)candidates[candidate_count++]={entry.th32ThreadID,0};
        }
        CloseHandle(snapshot);
    }
    for(auto& t:threads)if(t.used&&!t.retired&&!t.seen){CloseHandle(t.handle);t.handle=nullptr;t.retired=true;}
    // Rank: the initializing thread first, then creation order; slots free after the next report.
    for(unsigned c=0;c<candidate_count;++c){
        HANDLE h=OpenThread(THREAD_QUERY_INFORMATION,FALSE,candidates[c].tid);
        if(!h)continue;
        FILETIME creation{},exit{},kernel{},user{};
        if(GetThreadTimes(h,&creation,&exit,&kernel,&user))candidates[c].creation=(uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
        CloseHandle(h);
    }
    std::sort(candidates,candidates+candidate_count,[](const Candidate& a,const Candidate& b){
        if((a.tid==init_tid)!=(b.tid==init_tid))return a.tid==init_tid;
        return a.creation!=b.creation?a.creation<b.creation:a.tid<b.tid;});
    unsigned unsampled=0;
    for(unsigned c=0;c<candidate_count;++c){
        unsigned slot=~0u;for(unsigned i=0;i<max_threads;++i)if(!threads[i].used){slot=i;break;}
        if(slot==~0u){++unsampled;continue;}
        HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,candidates[c].tid);
        if(!h)continue;
        auto& t=threads[slot];t=Thread{};t.handle=h;t.tid=candidates[c].tid;t.creation=candidates[c].creation;t.used=t.seen=true;
        if(query_thread){
            struct { LONG status; PVOID teb; DWORD pid,tid; ULONG_PTR affinity; LONG priority,base_priority; } basic{};
            if(query_thread(h,0/*ThreadBasicInformation*/,&basic,sizeof basic,nullptr)>=0)t.teb=reinterpret_cast<uintptr_t>(basic.teb);
            PVOID start=nullptr;
            if(query_thread(h,9/*ThreadQuerySetWin32StartAddress*/,&start,sizeof start,nullptr)>=0&&start){
                const uintptr_t address=reinterpret_cast<uintptr_t>(start);t.start_module=module_of(address);
                t.start_rva=uint32_t(t.start_module==~0u?address:address-modules[t.start_module].base);
            }
        }
        log("profile_thread_seen slot=%u tid=%lu teb=0x%08lx start_module=%u start_rva=0x%lx creation=%llu",slot,t.tid,static_cast<unsigned long>(t.teb),t.start_module,static_cast<unsigned long>(t.start_rva),static_cast<unsigned long long>(t.creation));
    }
    unsigned live=0;for(const auto& t:threads)live+=t.used&&!t.retired;
    thread_count.store(live,std::memory_order_relaxed);unsampled_count.store(unsampled,std::memory_order_relaxed);
}
void refresh(){
    const uint64_t begin=qpc();
    refresh_modules();refresh_threads();
    const uint64_t cost=qpc()-begin;delta_refresh+=cost;refresh_total.fetch_add(cost,std::memory_order_relaxed);
}

void tick(){
    const uint64_t begin=qpc();
    Walk walk;
    for(unsigned slot=0;slot<max_threads;++slot){
        auto& t=threads[slot];
        if(!t.used||t.retired||!t.handle)continue;
        if(SuspendThread(t.handle)==DWORD(-1)){
            ++t.suspend_failures;++t.suspend_failures_delta;
            continue;
        }
        const bool ok=capture(t,walk);
        ResumeThread(t.handle);   // every path resumes, including a failed context read
        if(ok)aggregate(slot,t,walk);
        else{++t.context_failures;++t.context_failures_delta;}
    }
    const uint64_t cost=qpc()-begin;
    ++delta_ticks;delta_tick_total+=cost;delta_tick_max=std::max(delta_tick_max,cost);
    tick_count.fetch_add(1,std::memory_order_relaxed);tick_total.fetch_add(cost,std::memory_order_relaxed);
    uint64_t old=tick_maximum.load(std::memory_order_relaxed);while(old<cost&&!tick_maximum.compare_exchange_weak(old,cost,std::memory_order_relaxed)){}
}

template<typename Key,unsigned Capacity> unsigned rank(const Table<Key,Capacity>& table,unsigned top){
    unsigned n=0;
    for(unsigned i=0;i<Capacity;++i)if(table.keys[i].slot)ranked[n++]={i,table.counts[i]};
    const unsigned keep=std::min(n,top);
    std::partial_sort(ranked,ranked+keep,ranked+n,[](const Ranked& a,const Ranked& b){return a.count!=b.count?a.count>b.count:a.index<b.index;});
    return keep;
}
template<typename Key,unsigned Capacity> void merge(Table<Key,Capacity>& into,const Table<Key,Capacity>& from){
    for(unsigned i=0;i<Capacity;++i)if(from.keys[i].slot)into.add(from.keys[i],from.counts[i]);
}
void report_tables(const char* scope,uint64_t stamp,const Table<LeafKey,leaf_capacity>& leaves,const Table<FrameKey,frame_capacity>& frames,const Table<PairKey,pair_capacity>& pairs,unsigned tops,unsigned pair_tops){
    unsigned n=rank(leaves,tops);
    for(unsigned i=0;i<n;++i){const auto& k=leaves.keys[ranked[i].index];
        log("profile_leaf scope=%s qpc=%llu slot=%u module=%u name=%s rva=0x%lx count=%lu",scope,static_cast<unsigned long long>(stamp),unsigned(k.slot-1),unsigned(k.module),k.module==0xffff?"-":modules[k.module].name,static_cast<unsigned long>(k.rva),static_cast<unsigned long>(ranked[i].count));}
    n=rank(frames,tops);
    for(unsigned i=0;i<n;++i){const auto& k=frames.keys[ranked[i].index];
        log("profile_frame scope=%s qpc=%llu slot=%u rva=0x%lx count=%lu",scope,static_cast<unsigned long long>(stamp),k.slot-1,static_cast<unsigned long>(k.rva),static_cast<unsigned long>(ranked[i].count));}
    n=rank(pairs,pair_tops);
    for(unsigned i=0;i<n;++i){const auto& k=pairs.keys[ranked[i].index];
        log("profile_pair scope=%s qpc=%llu slot=%u rva=0x%lx caller=0x%lx count=%lu",scope,static_cast<unsigned long long>(stamp),k.slot-1,static_cast<unsigned long>(k.rva),static_cast<unsigned long>(k.caller),static_cast<unsigned long>(ranked[i].count));}
}
void report_threads(const char* scope,uint64_t stamp,bool cumulative){
    for(unsigned slot=0;slot<max_threads;++slot){const auto& t=threads[slot];if(!t.used)continue;
        const uint64_t* leaf=cumulative?t.leaf_total:t.leaf;const uint64_t samples=cumulative?t.total:t.samples;
        if(!samples&&!cumulative&&!t.retired)continue;
        log("profile_thread scope=%s qpc=%llu slot=%u tid=%lu samples=%llu total=%llu leaf_x3ap=%llu leaf_ntdll=%llu leaf_wine=%llu leaf_d3dx=%llu leaf_zlib=%llu leaf_xml=%llu leaf_proxy=%llu leaf_other=%llu stack_unknown=%llu suspend_failures=%llu context_failures=%llu start_module=%u start_rva=0x%lx retired=%u suspend_failures_delta=%llu context_failures_delta=%llu",
            scope,static_cast<unsigned long long>(stamp),slot,t.tid,static_cast<unsigned long long>(samples),static_cast<unsigned long long>(t.total),
            static_cast<unsigned long long>(leaf[KindX3ap]),static_cast<unsigned long long>(leaf[KindNtdll]),static_cast<unsigned long long>(leaf[KindWine]),static_cast<unsigned long long>(leaf[KindD3dx]),
            static_cast<unsigned long long>(leaf[KindZlib]),static_cast<unsigned long long>(leaf[KindXml]),static_cast<unsigned long long>(leaf[KindProxy]),static_cast<unsigned long long>(leaf[KindOther]),
            static_cast<unsigned long long>(t.stack_unknown),static_cast<unsigned long long>(t.suspend_failures),static_cast<unsigned long long>(t.context_failures),t.start_module,static_cast<unsigned long>(t.start_rva),t.retired,
            static_cast<unsigned long long>(t.suspend_failures_delta),static_cast<unsigned long long>(t.context_failures_delta));
    }
}
// Delta report, then (every cumulative_every reports, or at shutdown) the
// cumulative tables. Report cost is measured and kept out of the tick cost.
void report(bool final){
    const uint64_t begin=qpc();
    const uint64_t dropped=leaf_delta.dropped+frame_delta.dropped+pair_delta.dropped;delta_dropped+=dropped;
    const unsigned live=thread_count.load(std::memory_order_relaxed);
    log("profile_report scope=delta qpc=%llu frequency=%llu since_start_us=%.3f elapsed_us=%.3f interval_us=%u samples=%llu threads=%u threads_unsampled=%u modules=%u ticks=%llu tick_us_mean=%.3f tick_us_max=%.3f refresh_us=%.3f dropped=%llu table_used=%u,%u,%u",
        static_cast<unsigned long long>(begin),static_cast<unsigned long long>(frequency),us(begin-startup),us(begin-last_report),settings.interval_us,static_cast<unsigned long long>(delta_samples),live,unsampled_count.load(std::memory_order_relaxed),module_count.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(delta_ticks),delta_ticks?us(delta_tick_total)/double(delta_ticks):0.0,us(delta_tick_max),us(delta_refresh),static_cast<unsigned long long>(dropped),leaf_delta.used,frame_delta.used,pair_delta.used);
    report_threads("delta",begin,false);
    report_tables("delta",begin,leaf_delta,frame_delta,pair_delta,delta_tops,delta_pair_tops);
    merge(leaf_total,leaf_delta);merge(frame_total,frame_delta);merge(pair_total,pair_delta);
    leaf_delta.clear();frame_delta.clear();pair_delta.clear();
    leaf_delta.dropped=frame_delta.dropped=pair_delta.dropped=0;
    sample_count.fetch_add(delta_samples,std::memory_order_relaxed);drop_count.fetch_add(dropped,std::memory_order_relaxed);
    const uint64_t reports=report_count.fetch_add(1,std::memory_order_relaxed)+1;
    for(auto& t:threads){
        t.samples=0;std::memset(t.leaf,0,sizeof t.leaf);t.suspend_failures_delta=t.context_failures_delta=0;
        if(t.retired){t.used=false;t.retired=false;}
    }
    delta_ticks=delta_tick_total=delta_tick_max=delta_samples=delta_refresh=0;
    if(final||reports%cumulative_every==0){
        log("profile_report scope=cumulative qpc=%llu frequency=%llu since_start_us=%.3f samples=%llu ticks=%llu tick_us_mean=%.3f tick_us_max=%.3f refresh_us=%.3f report_us=%.3f dropped=%llu cumulative_dropped=%llu table_used=%u,%u,%u final=%u",
            static_cast<unsigned long long>(begin),static_cast<unsigned long long>(frequency),us(begin-startup),static_cast<unsigned long long>(sample_count.load(std::memory_order_relaxed)),static_cast<unsigned long long>(tick_count.load(std::memory_order_relaxed)),
            tick_count.load()?us(tick_total.load())/double(tick_count.load()):0.0,us(tick_maximum.load()),us(refresh_total.load()),us(report_total.load()),
            static_cast<unsigned long long>(drop_count.load(std::memory_order_relaxed)),static_cast<unsigned long long>(leaf_total.dropped+frame_total.dropped+pair_total.dropped),leaf_total.used,frame_total.used,pair_total.used,final);
        report_threads("cumulative",begin,true);
        report_tables("cumulative",begin,leaf_total,frame_total,pair_total,cumulative_tops,cumulative_tops);
    }
    const uint64_t cost=qpc()-begin;report_total.fetch_add(cost,std::memory_order_relaxed);
    log("profile_report_end qpc=%llu report_us=%.3f",static_cast<unsigned long long>(begin+cost),us(cost));
    last_report=begin+cost;
}

DWORD WINAPI sampler_main(LPVOID){
    running.store(true,std::memory_order_release);
    last_report=last_refresh=qpc();refresh();
    while(!stop_requested.load(std::memory_order_acquire)){
        const uint64_t begin=qpc();
        if(begin-last_refresh>=frequency){refresh();last_refresh=begin;}
        tick();
        const uint64_t after=qpc();
        if(after-last_report>=report_interval_ticks)report(false);
        // Duty cycle at most one half: never sleep less than the tick just cost.
        const uint64_t cost=after-begin,rest=std::max(interval_ticks>cost?interval_ticks-cost:0,cost);
        const uint64_t ms=std::max<uint64_t>(1,(rest*1000+frequency-1)/frequency);
        if(WaitForSingleObject(stop_event,DWORD(ms))==WAIT_OBJECT_0)break;
    }
    running.store(false,std::memory_order_release);
    return 0;
}
unsigned env_number(const wchar_t* name,unsigned fallback,unsigned lo,unsigned hi){
    wchar_t value[32]{};if(GetEnvironmentVariableW(name,value,32)==0)return fallback;
    const unsigned long n=wcstoul(value,nullptr,10);return n>=lo&&n<=hi?unsigned(n):fallback;
}
}

bool initialize(){
    if(started)return active();
    wchar_t value[8]{};
    if(!(GetEnvironmentVariableW(L"X3M_PROFILE",value,8)==1&&value[0]==L'1'))return false;
    started=true;
    LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0){log("profile_start enabled=0 reason=no_qpc");return false;}
    frequency=uint64_t(f.QuadPart);
    settings.interval_us=env_number(L"X3M_PROFILE_INTERVAL_US",2000,100,1000000);
    settings.report_s=env_number(L"X3M_PROFILE_REPORT_S",5,1,3600);
    interval_ticks=frequency*settings.interval_us/1000000;report_interval_ticks=frequency*settings.report_s;
    init_tid=GetCurrentThreadId();
    main_base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    HMODULE self=nullptr;
    // Pin this module while the sampler thread executes its code; the process
    // never unloads its d3d9 in practice, so this is not application-visible.
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&initialize),&self))proxy_base=reinterpret_cast<uintptr_t>(self);
    if(HMODULE ntdll=GetModuleHandleW(L"ntdll.dll"))query_thread=reinterpret_cast<QueryThreadFn>(reinterpret_cast<void*>(GetProcAddress(ntdll,"NtQueryInformationThread")));
    windows_directory_length=GetWindowsDirectoryW(windows_directory,MAX_PATH);
    if(windows_directory_length>=MAX_PATH)windows_directory_length=0;
    stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stop_event){log("profile_start enabled=0 reason=no_event error=%lu",GetLastError());return false;}
    startup=qpc();last_report=startup;
    sampler=CreateThread(nullptr,0,sampler_main,nullptr,CREATE_SUSPENDED,&sampler_tid);
    if(!sampler){log("profile_start enabled=0 reason=no_thread error=%lu",GetLastError());CloseHandle(stop_event);stop_event=nullptr;return false;}
    SetThreadPriority(sampler,THREAD_PRIORITY_ABOVE_NORMAL);
    log("profile_start schema=1 enabled=1 qpc=%llu frequency=%llu interval_us=%u report_s=%u sampler_tid=%lu init_tid=%lu main_base=0x%08lx proxy_base=0x%08lx query_thread=%u chain_limit=%u scan_dwords=%u threads_max=%u modules_max=%u tables=%u,%u,%u",
        static_cast<unsigned long long>(startup),static_cast<unsigned long long>(frequency),settings.interval_us,settings.report_s,sampler_tid,init_tid,static_cast<unsigned long>(main_base),static_cast<unsigned long>(proxy_base),query_thread!=nullptr,chain_limit,scan_dwords,max_threads,max_modules,leaf_capacity,frame_capacity,pair_capacity);
    ResumeThread(sampler);
    return true;
}
bool active(){return sampler!=nullptr&&!finished;}
void shutdown(){
    if(!sampler||finished)return;
    finished=true;
    const uint64_t begin=qpc();
    stop_requested.store(true,std::memory_order_release);SetEvent(stop_event);
    const DWORD wait=WaitForSingleObject(sampler,10000);
    if(wait!=WAIT_OBJECT_0){log("profile_stop joined=0 wait=%lu",wait);return;} // Tables stay owned by the live thread.
    report(true);
    unsigned closed=0;for(auto& t:threads){if(t.handle){CloseHandle(t.handle);++closed;}t=Thread{};}
    CloseHandle(sampler);sampler=nullptr;CloseHandle(stop_event);stop_event=nullptr;
    log("profile_stop joined=1 qpc=%llu stop_us=%.3f handles_closed=%u",static_cast<unsigned long long>(qpc()),us(qpc()-begin),closed);
}
Status status(){
    Status s;s.active=active();
    s.ticks=tick_count.load(std::memory_order_relaxed);s.samples=sample_count.load(std::memory_order_relaxed);s.dropped=drop_count.load(std::memory_order_relaxed);s.reports=report_count.load(std::memory_order_relaxed);
    s.tick_ticks=tick_total.load(std::memory_order_relaxed);s.tick_max_ticks=tick_maximum.load(std::memory_order_relaxed);s.report_ticks=report_total.load(std::memory_order_relaxed);s.refresh_ticks=refresh_total.load(std::memory_order_relaxed);
    s.threads=thread_count.load(std::memory_order_relaxed);s.modules=module_count.load(std::memory_order_relaxed);s.threads_unsampled=unsampled_count.load(std::memory_order_relaxed);
    return s;
}
}
