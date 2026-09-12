#include "gz_buffer.h"
#include "capture.h"
#include "cpu_state.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Model (docs/verification/gz-buffer.md): the real stream sits at R = base + fill;
// the caller's logical position is L = base + pos <= R. A real error (sticky in
// zlib) becomes visible to the caller only once L reaches R, because the
// unbuffered stream would have detected it in the call that produced its last
// byte; until then reads, tells and in-range seeks behave as on a healthy stream
// and a backward seek clears the real error with gzrewind (as zlib's own rewind
// would). No floating point anywhere on the call paths: the game's x87 state is
// untouched by construction (the close-time log runs under PreserveCpuState).
namespace x3m::gz_buffer {
namespace {
struct State {
    unsigned char* buf=nullptr; unsigned capacity=0,fill=0,pos=0; LONG base=0;
    bool error=false,at_end=false; unsigned index=0;
    uint32_t calls=0,small_calls=0,real_reads=0,direct_reads=0,getcs=0,tells=0,seeks=0,seeks_served=0,seeks_real=0; // per file: 32-bit keeps the hot path to single adds
    uint64_t served_bytes=0,real_bytes=0;
};
struct Slot { std::atomic<void*> file{nullptr}; State state; };
Slot slots[slot_count];
std::atomic<unsigned> hint{0},registered{0};
SRWLOCK table_lock=SRWLOCK_INIT;
Originals real;
unsigned capacity_bytes=0;
std::atomic<bool> is_enabled{false};
struct Totals {
    std::atomic<uint64_t> opens{0},buffered_opens{0},passthrough_opens{0},closes{0};
    std::atomic<uint64_t> calls{0},small_calls{0},served_bytes{0},real_reads{0},real_bytes{0},direct_reads{0};
    std::atomic<uint64_t> getcs{0},tells{0},seeks{0},seeks_served{0},seeks_real{0},errors{0};
} totals;

State* lookup(const void* file) {
    if(!file)return nullptr;
    const unsigned h=hint.load(std::memory_order_relaxed);
    if(slots[h].file.load(std::memory_order_acquire)==file)return &slots[h].state;
    for(unsigned i=0;i<slot_count;++i)if(slots[i].file.load(std::memory_order_acquire)==file){hint.store(i,std::memory_order_relaxed);return &slots[i].state;}
    return nullptr;
}
// zlib's gz_open keeps the last of r/w/a in the mode string; anything else is
// refused by zlib itself (the real open then returns null and nothing is registered).
bool read_mode(const char* mode) {
    char m=0;
    for(const char* p=mode;p&&*p;++p){if(*p=='r')m='r';else if(*p=='w'||*p=='a')m='w';}
    return m=='r';
}
void drop(State& s){s.base+=LONG(s.fill);s.fill=s.pos=0;}
// After every real read: the arithmetic position is checked against the real
// stream (gztell = gzseek(0, SEEK_CUR) in zlib 1.2.3, -1 in its sticky error state).
void probe(State& s,void* file) {
    const LONG t=real.tell(file);
    if(t<0)s.error=true;
    else if(t!=s.base+LONG(s.fill))s.base=t-LONG(s.fill);
}
void resync(State& s,void* file,LONG result) {
    if(result>=0){s.base=result;s.error=false;s.at_end=false;return;}
    const LONG t=real.tell(file);
    if(t<0){s.error=true;return;}
    s.base=t;s.error=false;s.at_end=false; // e.g. a forward seek that ran into the end: moved, not in error
}
int buffered_read(State& s,void* file,unsigned char* out,unsigned len) {
    ++s.calls;s.small_calls+=len<=8;
    unsigned done=0;
    for(;;) {
        const unsigned avail=s.fill-s.pos;
        if(avail) {
            const unsigned n=avail<len-done?avail:len-done;
            if(n<=16){const unsigned char* src=s.buf+s.pos;for(unsigned i=0;i<n;++i)out[done+i]=src[i];}
            else std::memcpy(out+done,s.buf+s.pos,n);
            s.pos+=n;done+=n;
            if(done==len){s.served_bytes+=done;return int(done);}
            continue;
        }
        if(s.error){s.served_bytes+=done;return done?int(done):-1;}
        if(s.at_end){s.served_bytes+=done;return int(done);}
        if(done==len)return int(done); // zero-length request on a healthy stream
        const unsigned want=len-done;
        int r;
        if(want>=s.capacity) { // large remainder: straight into the caller's buffer
            r=real.read(file,out+done,want);++s.direct_reads;
            if(r>0){s.base+=LONG(s.fill)+r;s.fill=s.pos=0;done+=unsigned(r);s.real_bytes+=uint64_t(r);}
        } else {
            r=real.read(file,s.buf,s.capacity);++s.real_reads;
            s.base+=LONG(s.fill);s.pos=0;s.fill=r>0?unsigned(r):0;
            if(r>0)s.real_bytes+=uint64_t(r);
        }
        if(r<0)s.error=true;
        else{if(r==0)s.at_end=true;probe(s,file);}
    }
}
LONG buffered_seek(State& s,void* file,LONG offset,int whence) {
    ++s.seeks;
    if(s.error&&s.pos==s.fill)return -1; // zlib refuses before moving in its error state
    if(whence==SEEK_SET||whence==SEEK_CUR) {
        const LONG target=whence==SEEK_CUR?s.base+LONG(s.pos)+offset:offset;
        if(target<0)return -1;
        if(target>=s.base&&target<=s.base+LONG(s.fill)){s.pos=unsigned(target-s.base);++s.seeks_served;return target;}
        if(target<s.base&&s.error&&real.rewind){real.rewind(file);s.error=false;s.at_end=false;}
        drop(s);++s.seeks_real;
        const LONG result=real.seek(file,target,SEEK_SET);
        resync(s,file,result);return result;
    }
    // Other whence values (SEEK_END, refused by every zlib): put the real stream
    // at the logical position, then let the real function answer.
    if(s.pos!=s.fill) {
        if(s.error&&real.rewind){real.rewind(file);s.error=false;s.at_end=false;}
        const LONG logical=s.base+LONG(s.pos);
        const LONG result=real.seek(file,logical,SEEK_SET);
        s.fill=s.pos=0;
        if(result>=0)s.base=result;else resync(s,file,result);
    } else drop(s);
    ++s.seeks_real;
    const LONG result=real.seek(file,offset,whence);
    resync(s,file,result);return result;
}
void accumulate(const State& s) {
    totals.calls.fetch_add(s.calls,std::memory_order_relaxed);totals.small_calls.fetch_add(s.small_calls,std::memory_order_relaxed);
    totals.served_bytes.fetch_add(s.served_bytes,std::memory_order_relaxed);totals.real_reads.fetch_add(s.real_reads,std::memory_order_relaxed);
    totals.real_bytes.fetch_add(s.real_bytes,std::memory_order_relaxed);totals.direct_reads.fetch_add(s.direct_reads,std::memory_order_relaxed);
    totals.getcs.fetch_add(s.getcs,std::memory_order_relaxed);totals.tells.fetch_add(s.tells,std::memory_order_relaxed);
    totals.seeks.fetch_add(s.seeks,std::memory_order_relaxed);totals.seeks_served.fetch_add(s.seeks_served,std::memory_order_relaxed);
    totals.seeks_real.fetch_add(s.seeks_real,std::memory_order_relaxed);totals.errors.fetch_add(s.error,std::memory_order_relaxed);
}
}

bool requested(){wchar_t setting[8]{};return GetEnvironmentVariableW(L"X3M_GZ_BUFFER",setting,8)==1&&setting[0]==L'1';}
unsigned requested_capacity() {
    wchar_t setting[16]{};const DWORD n=GetEnvironmentVariableW(L"X3M_GZ_BUFFER_KB",setting,16);
    // Decimal digits only: wcstoul would accept a sign or leading blanks, and a
    // negative value would wrap to the maximum instead of the default.
    unsigned long kb=0;bool digits=n>0&&n<16;
    for(DWORD i=0;digits&&i<n;++i){if(setting[i]<L'0'||setting[i]>L'9')digits=false;else if(kb<=maximum_capacity_kb)kb=kb*10+unsigned(setting[i]-L'0');}
    if(!digits)kb=0;
    if(!kb)kb=default_capacity_kb;
    if(kb<minimum_capacity_kb)kb=minimum_capacity_kb;
    if(kb>maximum_capacity_kb)kb=maximum_capacity_kb;
    return unsigned(kb)*1024u;
}
bool initialize(const Originals& originals,unsigned capacity) {
    if(!originals.open||!originals.read||!originals.seek||!originals.tell||!originals.getc||!originals.close)return false;
    if(capacity<minimum_capacity_kb*1024u)capacity=minimum_capacity_kb*1024u;
    if(capacity>maximum_capacity_kb*1024u)capacity=maximum_capacity_kb*1024u;
    AcquireSRWLockExclusive(&table_lock);
    const bool free_table=registered.load(std::memory_order_relaxed)==0;
    if(free_table){real=originals;capacity_bytes=capacity;is_enabled.store(true,std::memory_order_release);}
    ReleaseSRWLockExclusive(&table_lock);
    return free_table;
}
bool enabled(){return is_enabled.load(std::memory_order_acquire);}
bool buffered(const void* file){return lookup(file)!=nullptr;}

void* open(const char* path,const char* mode) {
    void* file=real.open(path,mode);
    totals.opens.fetch_add(1,std::memory_order_relaxed);
    if(!file||!read_mode(mode)||!enabled())return file;
    auto* buf=static_cast<unsigned char*>(std::malloc(capacity_bytes));
    Slot* slot=nullptr;unsigned char* stale_buf=nullptr;
    if(buf) {
        AcquireSRWLockExclusive(&table_lock);
        // A slot still holding this pointer means the previous handle was closed
        // behind the hooks (a gzclose that did not pass through the patched
        // import); zlib has reused the allocation, so the state is stale and is
        // reset in place rather than left to shadow the fresh registration.
        for(auto& candidate:slots)if(candidate.file.load(std::memory_order_relaxed)==file){slot=&candidate;stale_buf=candidate.state.buf;break;}
        if(!slot)for(auto& candidate:slots)if(!candidate.file.load(std::memory_order_relaxed)){slot=&candidate;break;}
        if(slot) {
            slot->state=State{};slot->state.buf=buf;slot->state.capacity=capacity_bytes;
            slot->state.index=unsigned(slot-slots);
            slot->file.store(file,std::memory_order_release);
            if(!stale_buf)registered.fetch_add(1,std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&table_lock);
    }
    std::free(stale_buf);
    if(slot)totals.buffered_opens.fetch_add(1,std::memory_order_relaxed);
    else{std::free(buf);totals.passthrough_opens.fetch_add(1,std::memory_order_relaxed);}
    return file;
}
int read(void* file,void* data,unsigned size) {
    State* s=lookup(file);
    if(!s)return real.read(file,data,size);
    return buffered_read(*s,file,static_cast<unsigned char*>(data),size);
}
int getc(void* file) {
    State* s=lookup(file);
    if(!s)return real.getc(file);
    ++s->getcs;
    if(s->pos<s->fill){++s->calls;++s->small_calls;++s->served_bytes;return s->buf[s->pos++];}
    unsigned char c=0;
    return buffered_read(*s,file,&c,1)==1?int(c):-1;
}
LONG tell(void* file) {
    State* s=lookup(file);
    if(!s)return real.tell(file);
    ++s->tells;
    if(s->error&&s->pos==s->fill)return -1;
    return s->base+LONG(s->pos);
}
LONG seek(void* file,LONG offset,int whence) {
    State* s=lookup(file);
    if(!s)return real.seek(file,offset,whence);
    return buffered_seek(*s,file,offset,whence);
}
int close(void* file) {
    State* s=lookup(file);
    if(!s)return real.close(file);
    Slot& slot=slots[s->index];
    AcquireSRWLockExclusive(&table_lock);
    const State copy=*s;
    slot.file.store(nullptr,std::memory_order_release);
    registered.fetch_sub(1,std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&table_lock);
    std::free(copy.buf);
    const int result=real.close(file);
    accumulate(copy);totals.closes.fetch_add(1,std::memory_order_relaxed);
    PreserveCpuState fp; // the formatter is x87 code; the caller's state and last error survive
    log("gz_buffer_file slot=%u capacity=%u calls=%llu small_calls=%llu served_bytes=%llu real_reads=%llu real_bytes=%llu direct_reads=%llu getcs=%llu tells=%llu seeks=%llu seeks_served=%llu seeks_real=%llu error=%u end=%u close=%d",
        copy.index,copy.capacity,(unsigned long long)copy.calls,(unsigned long long)copy.small_calls,(unsigned long long)copy.served_bytes,
        (unsigned long long)copy.real_reads,(unsigned long long)copy.real_bytes,(unsigned long long)copy.direct_reads,(unsigned long long)copy.getcs,
        (unsigned long long)copy.tells,(unsigned long long)copy.seeks,(unsigned long long)copy.seeks_served,(unsigned long long)copy.seeks_real,
        unsigned(copy.error),unsigned(copy.at_end),result);
    return result;
}
Statistics statistics() {
    Statistics s;
    s.opens=totals.opens.load();s.buffered_opens=totals.buffered_opens.load();s.passthrough_opens=totals.passthrough_opens.load();s.closes=totals.closes.load();
    s.calls=totals.calls.load();s.small_calls=totals.small_calls.load();s.served_bytes=totals.served_bytes.load();s.real_reads=totals.real_reads.load();
    s.real_bytes=totals.real_bytes.load();s.direct_reads=totals.direct_reads.load();s.getcs=totals.getcs.load();s.tells=totals.tells.load();
    s.seeks=totals.seeks.load();s.seeks_served=totals.seeks_served.load();s.seeks_real=totals.seeks_real.load();s.errors=totals.errors.load();
    return s;
}
}
