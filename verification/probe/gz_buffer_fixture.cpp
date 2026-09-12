// gz read-ahead buffer against the game's real zlib1.dll (zlib 1.2.3).
//
// The runner copies the bottle's zlib1.dll next to this executable. The fixture
// loads it, generates gz files with its gzwrite, then drives random sequences
// of gzread/gzgetc/gztell/gzseek through src/proxy/gz_buffer.cpp on one handle
// and through the raw DLL on a second handle of the same file, comparing every
// return value and every returned byte. Cases cover small and huge read lengths,
// seeks inside and outside the buffered range in both directions, SEEK_END and an
// unknown whence, EOF, truncated / corrupted / bad-CRC streams (including a stream
// whose length is a multiple of the chunk size), a transparent (non-gz) file, a
// two-member file, interleaved handles with a write handle among them, a full
// slot table, the decoder's exact 3-byte loop, and a timing case of 10 M 3-byte
// reads unbuffered, inside the loading-trace hook envelope, and buffered.
// usage: gz_buffer_fixture.exe [timing_calls] [case-name-substring]
#include "../../src/proxy/gz_buffer.h"
#include "../../src/proxy/cpu_state.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace x3m {void log(const char* format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');}}
namespace gzb=x3m::gz_buffer;
static unsigned checks=0,failures=0,case_checks=0,case_failures=0;
static const char* case_filter=nullptr; // optional argv[2]: run only cases whose name contains it, with an op trace on failure
static char trace_ring[16][96];static unsigned trace_next=0;
static void trace(const char* text){snprintf(trace_ring[trace_next%16],sizeof trace_ring[0],"%s",text);++trace_next;}
static void check(bool ok,const char* what,const char* detail=""){
    ++checks;++case_checks;
    if(!ok){++failures;++case_failures;printf("FAIL %s %s\n",what,detail);
        if(case_filter&&case_failures<=3){for(unsigned i=trace_next>16?trace_next-16:0;i<trace_next;++i)printf("  trace %s\n",trace_ring[i%16]);}}
}
static bool case_selected(const char* name){return !case_filter||strstr(name,case_filter)!=nullptr;}

struct Raw {
    gzb::OpenFn open;gzb::ReadFn read;gzb::WriteFn write;gzb::SeekFn seek;gzb::TellFn tell;gzb::GetcFn getc;gzb::CloseFn close;gzb::RewindFn rewind;
} raw{};

static uint32_t rng_state=1;
static uint32_t rnd(){uint32_t s=rng_state;s^=s<<13;s^=s>>17;s^=s<<5;rng_state=s;return s;}
static uint32_t rnd(uint32_t n){return n?rnd()%n:0;}
// Compressible mix: letters in runs with random bytes sprinkled in.
static void fill(std::vector<unsigned char>& out,size_t n,uint32_t seed){
    uint32_t s=seed?seed:1;out.resize(n);
    for(size_t i=0;i<n;++i){s^=s<<13;s^=s>>17;s^=s<<5;const uint32_t r=s>>8;out[i]=(r&0x70)?static_cast<unsigned char>('a'+(r>>8)%12):static_cast<unsigned char>(r&0xff);}
}
static bool write_gz(const char* name,const std::vector<unsigned char>& data,const char* mode="wb"){
    void* f=raw.open(name,mode);if(!f)return false;
    size_t done=0;bool ok=true;
    while(done<data.size()){const unsigned n=static_cast<unsigned>(data.size()-done<65536?data.size()-done:65536);ok&=raw.write(f,data.data()+done,n)==int(n);done+=n;}
    return raw.close(f)==0&&ok;
}
static std::vector<unsigned char> read_file(const char* name){
    std::vector<unsigned char> out;FILE* f=fopen(name,"rb");if(!f)return out;
    unsigned char chunk[65536];size_t n;while((n=fread(chunk,1,sizeof chunk,f))>0)out.insert(out.end(),chunk,chunk+n);fclose(f);return out;
}
static bool write_file(const char* name,const std::vector<unsigned char>& data){FILE* f=fopen(name,"wb");if(!f)return false;const bool ok=fwrite(data.data(),1,data.size(),f)==data.size();return fclose(f)==0&&ok;}

struct Handle { void* buffered=nullptr;void* reference=nullptr;const char* name="";LONG size=0; };
static std::vector<unsigned char> out_a(2u<<20),out_b(2u<<20);
static unsigned long long total_ops=0;

static void op_read(Handle& h,unsigned len){
    char detail[96];
    const size_t room=out_a.size();
    if(len>room&&len!=0xFFFFFFFFu)len=static_cast<unsigned>(room);
    out_a[0]=0x11;out_b[0]=0x22;
    const int a=gzb::read(h.buffered,out_a.data(),len),b=raw.read(h.reference,out_b.data(),len);
    snprintf(detail,sizeof detail,"%s len=%u buffered=%d reference=%d",h.name,len,a,b);trace(detail);
    check(a==b,"read_result",detail);
    if(a>0&&a==b)check(!memcmp(out_a.data(),out_b.data(),static_cast<size_t>(a)),"read_bytes",detail);
    ++total_ops;
}
static void op_getc(Handle& h){
    char detail[64];const int a=gzb::getc(h.buffered),b=raw.getc(h.reference);
    snprintf(detail,sizeof detail,"getc %s buffered=%d reference=%d",h.name,a,b);trace(detail);check(a==b,"getc",detail);++total_ops;
}
static void op_tell(Handle& h){
    char detail[64];const LONG a=gzb::tell(h.buffered),b=raw.tell(h.reference);
    snprintf(detail,sizeof detail,"tell %s buffered=%ld reference=%ld",h.name,a,b);trace(detail);check(a==b,"tell",detail);++total_ops;
}
static void op_seek(Handle& h,LONG offset,int whence){
    char detail[96];const LONG a=gzb::seek(h.buffered,offset,whence),b=raw.seek(h.reference,offset,whence);
    snprintf(detail,sizeof detail,"seek %s offset=%ld whence=%d buffered=%ld reference=%ld",h.name,offset,whence,a,b);trace(detail);check(a==b,"seek",detail);++total_ops;
}
static void random_op(Handle& h,unsigned capacity,bool huge=true){
    const uint32_t k=rnd(100);
    if(k<50){
        uint32_t len;const uint32_t shape=rnd(100);
        if(shape<70)len=1+rnd(8);else if(shape<90)len=1+rnd(5000);else if(shape<94)len=capacity-2+rnd(5);else if(shape<98)len=capacity*2+rnd(capacity);else if(shape<99)len=0;else len=huge?0xFFFFFFFFu:capacity*3;
        op_read(h,len);
    } else if(k<65)op_getc(h);
    else if(k<78)op_tell(h);
    else {
        const uint32_t kind=rnd(100);
        if(kind<45){const LONG span=h.size+100;op_seek(h,static_cast<LONG>(rnd(static_cast<uint32_t>(span+10)))-10,SEEK_SET);}
        else if(kind<90){const LONG reach=static_cast<LONG>(capacity)*2;op_seek(h,static_cast<LONG>(rnd(static_cast<uint32_t>(reach*2+1)))-reach,SEEK_CUR);}
        else if(kind<97)op_seek(h,rnd(2)?0:-8,SEEK_END);
        else op_seek(h,static_cast<LONG>(rnd(static_cast<uint32_t>(h.size+1))),3);
    }
}
static bool open_pair(Handle& h,const char* name,LONG size,const char* mode="rb"){
    h.name=name;h.size=size;h.buffered=gzb::open(name,mode);h.reference=raw.open(name,mode);
    check(h.buffered&&h.reference,"open",name);check(gzb::buffered(h.buffered),"registered",name);return h.buffered&&h.reference;
}
static void close_pair(Handle& h){
    const int a=gzb::close(h.buffered),b=raw.close(h.reference);char detail[64];snprintf(detail,sizeof detail,"%s buffered=%d reference=%d",h.name,a,b);
    check(a==b,"close",detail);check(!gzb::buffered(h.buffered),"unregistered",h.name);h.buffered=h.reference=nullptr;
}
static void begin_case(const char* name){case_checks=case_failures=0;total_ops=0;printf("GZ_CASE_BEGIN name=%s\n",name);}
static void end_case(const char* name,unsigned capacity){
    printf("GZ_CASE name=%s capacity=%u ops=%llu checks=%u failures=%u\n",name,capacity,total_ops,case_checks,case_failures);
}
static void set_capacity(unsigned capacity){
    gzb::Originals o;o.open=raw.open;o.read=raw.read;o.seek=raw.seek;o.tell=raw.tell;o.getc=raw.getc;o.close=raw.close;o.rewind=raw.rewind;
    check(gzb::initialize(o,capacity),"initialize");
}
static void random_case(const char* label,const char* file,LONG size,unsigned capacity,unsigned ops,uint32_t seed,const char* mode="rb",bool huge=true){
    if(!case_selected(label))return;
    begin_case(label);set_capacity(capacity);rng_state=seed;
    Handle h;if(open_pair(h,file,size,mode)){for(unsigned i=0;i<ops;++i)random_op(h,capacity,huge);close_pair(h);}
    end_case(label,capacity);
}

int main(int argc,char** argv){
    const unsigned long long timing_calls=argc>1?strtoull(argv[1],nullptr,10):10000000ull;
    if(argc>2)case_filter=argv[2];
    HMODULE zlib=LoadLibraryA("zlib1.dll");
    if(!zlib){printf("FAIL load zlib1.dll error=%lu\n",GetLastError());printf("GZ BUFFER RESULT checks=0 failures=1\n");return 1;}
    raw.open=reinterpret_cast<gzb::OpenFn>(GetProcAddress(zlib,"gzopen"));raw.read=reinterpret_cast<gzb::ReadFn>(GetProcAddress(zlib,"gzread"));
    raw.write=reinterpret_cast<gzb::WriteFn>(GetProcAddress(zlib,"gzwrite"));raw.seek=reinterpret_cast<gzb::SeekFn>(GetProcAddress(zlib,"gzseek"));
    raw.tell=reinterpret_cast<gzb::TellFn>(GetProcAddress(zlib,"gztell"));raw.getc=reinterpret_cast<gzb::GetcFn>(GetProcAddress(zlib,"gzgetc"));
    raw.close=reinterpret_cast<gzb::CloseFn>(GetProcAddress(zlib,"gzclose"));raw.rewind=reinterpret_cast<gzb::RewindFn>(GetProcAddress(zlib,"gzrewind"));
    using VersionFn=const char* (__cdecl*)();
    auto version=reinterpret_cast<VersionFn>(GetProcAddress(zlib,"zlibVersion"));
    printf("GZ_ZLIB version=%s exports=%d rewind=%d\n",version?version():"?",raw.open&&raw.read&&raw.write&&raw.seek&&raw.tell&&raw.getc&&raw.close,raw.rewind!=nullptr);
    check(raw.open&&raw.read&&raw.write&&raw.seek&&raw.tell&&raw.getc&&raw.close,"zlib_exports");
    if(failures){printf("GZ BUFFER RESULT checks=%u failures=%u\n",checks,failures);return 1;}

    // Generated inputs (sizes in uncompressed bytes).
    std::vector<unsigned char> data;
    const LONG big_size=1000003,small_size=100,multiple_size=3*4096,plain_size=5000;
    fill(data,big_size,7);check(write_gz("gzb_big.gz",data),"write_big");
    fill(data,small_size,9);check(write_gz("gzb_small.gz",data),"write_small");
    data.clear();check(write_gz("gzb_empty.gz",data),"write_empty");
    fill(data,multiple_size,11);check(write_gz("gzb_multiple.gz",data),"write_multiple");
    { auto big=read_file("gzb_big.gz");check(big.size()>1000,"big_compressed");
      std::vector<unsigned char> cut(big.begin(),big.begin()+big.size()/2);check(write_file("gzb_trunc.gz",cut),"write_trunc");
      auto crc=big;crc[crc.size()-8]^=0x5a;check(write_file("gzb_crc.gz",crc),"write_crc");
      auto corrupt=big;corrupt[corrupt.size()/3]^=0x5a;check(write_file("gzb_corrupt.gz",corrupt),"write_corrupt");
      auto multiple=read_file("gzb_multiple.gz");multiple[multiple.size()-8]^=0x5a;check(write_file("gzb_multiple_crc.gz",multiple),"write_multiple_crc");
      auto small=read_file("gzb_small.gz");auto concat=small;concat.insert(concat.end(),big.begin(),big.end());check(write_file("gzb_concat.gz",concat),"write_concat"); }
    fill(data,plain_size,13);for(auto& byte:data)byte|=0x40;data[0]='P';data[1]='L';check(write_file("gzb_plain.bin",data),"write_plain");

    // Random sequences, one handle.
    random_case("big-cap4k","gzb_big.gz",big_size,4096,20000,101);
    random_case("big-cap64k","gzb_big.gz",big_size,65536,20000,102);
    random_case("big-cap256k-r","gzb_big.gz",big_size,262144,8000,103,"r");
    random_case("small","gzb_small.gz",small_size,4096,4000,104);
    random_case("empty","gzb_empty.gz",0,4096,1000,105);
    random_case("truncated","gzb_trunc.gz",big_size,4096,6000,106);
    random_case("crc","gzb_crc.gz",big_size,4096,6000,107);
    random_case("crc-cap64k","gzb_crc.gz",big_size,65536,6000,108);
    random_case("corrupt","gzb_corrupt.gz",big_size,4096,6000,109);
    random_case("corrupt-cap64k","gzb_corrupt.gz",big_size,65536,6000,110);
    random_case("multiple","gzb_multiple.gz",multiple_size,4096,4000,111);
    random_case("multiple-crc","gzb_multiple.gz",multiple_size,4096,4000,112);
    random_case("multiple-crc-bad","gzb_multiple_crc.gz",multiple_size,4096,6000,113);
    // Transparent (non-gzip) data: zlib maps reads to fread and gztell to fseek. A 4 GB-length
    // request is excluded here: Wine's msvcrt fread with that count returns one CRT buffer and
    // leaves the FILE unable to read after the next fseek (which gztell performs), while a
    // sequential fread still works, so the two streams diverge below zlib. The game never
    // issues such a request and its gz handles are gzip savegames (savegame-gz-stream.md).
    random_case("transparent","gzb_plain.bin",plain_size,4096,4000,114,"rb",false);
    random_case("concat","gzb_concat.gz",big_size+small_size,4096,8000,115);

    // Deterministic EOF and error paths.
    if(case_selected("eof-error-paths")){
    begin_case("eof-error-paths");set_capacity(4096);
    { Handle h;if(open_pair(h,"gzb_empty.gz",0)){op_read(h,3);op_getc(h);op_tell(h);op_seek(h,5,SEEK_SET);op_seek(h,0,SEEK_SET);op_read(h,0);op_seek(h,-1,SEEK_CUR);close_pair(h);}
      Handle t;if(open_pair(t,"gzb_trunc.gz",big_size)){for(unsigned i=0;i<400000;++i)op_read(t,3);op_tell(t);op_seek(t,0,SEEK_SET);op_read(t,10);op_seek(t,big_size,SEEK_SET);op_read(t,1);close_pair(t);}
      Handle c;if(open_pair(c,"gzb_crc.gz",big_size)){op_read(c,big_size-2);op_tell(c);op_seek(c,10,SEEK_SET);op_read(c,5);op_seek(c,big_size-1,SEEK_SET);op_read(c,1);op_tell(c);op_read(c,1);op_tell(c);op_seek(c,0,SEEK_SET);op_read(c,4);close_pair(c);}
      Handle k;if(open_pair(k,"gzb_corrupt.gz",big_size)){op_read(k,0xFFFFFFFFu);op_tell(k);op_read(k,1);op_seek(k,0,SEEK_SET);op_getc(k);op_seek(k,0,SEEK_END);op_seek(k,0,SEEK_CUR);close_pair(k);}
      Handle m;if(open_pair(m,"gzb_multiple_crc.gz",multiple_size)){op_read(m,4096);op_read(m,4096);op_read(m,4096);op_tell(m);op_read(m,1);op_tell(m);op_seek(m,100,SEEK_SET);close_pair(m);}
      Handle w;w.name="write";w.buffered=gzb::open("gzb_out.gz","wb");check(w.buffered&&!gzb::buffered(w.buffered),"write_handle_passthrough");
      if(w.buffered){check(gzb::read(w.buffered,out_a.data(),4)==-2,"write_handle_read_is_stream_error");check(gzb::tell(w.buffered)==0,"write_handle_tell");check(raw.write(w.buffered,"abcd",4)==4,"write_handle_write");check(gzb::close(w.buffered)==0,"write_handle_close");}
      check(gzb::read(nullptr,out_a.data(),4)==raw.read(nullptr,out_b.data(),4),"null_handle_read");
      check(gzb::seek(nullptr,0,SEEK_SET)==raw.seek(nullptr,0,SEEK_SET),"null_handle_seek"); }
    end_case("eof-error-paths",4096);
    }

    // The decoder's own pattern: 3-byte reads to the end (docs/reverse-engineering/savegame-gz-stream.md).
    if(case_selected("sequential-3byte")){
    begin_case("sequential-3byte");set_capacity(262144);
    { Handle h;if(open_pair(h,"gzb_big.gz",big_size)){unsigned zeros=0;while(zeros<3){out_a[0]=1;out_b[0]=2;const int a=gzb::read(h.buffered,out_a.data(),3),b=raw.read(h.reference,out_b.data(),3);++total_ops;if(a!=b||(a>0&&memcmp(out_a.data(),out_b.data(),a))){check(false,"sequential_read");break;}if(a<=0)++zeros;}check(zeros==3,"sequential_end");op_tell(h);close_pair(h);}
      const auto s=gzb::statistics();printf("GZ_STATS opens=%llu buffered=%llu passthrough=%llu closes=%llu calls=%llu small=%llu served=%llu real_reads=%llu real_bytes=%llu direct=%llu getcs=%llu tells=%llu seeks=%llu seeks_served=%llu seeks_real=%llu errors=%llu\n",
        (unsigned long long)s.opens,(unsigned long long)s.buffered_opens,(unsigned long long)s.passthrough_opens,(unsigned long long)s.closes,(unsigned long long)s.calls,(unsigned long long)s.small_calls,(unsigned long long)s.served_bytes,(unsigned long long)s.real_reads,(unsigned long long)s.real_bytes,(unsigned long long)s.direct_reads,(unsigned long long)s.getcs,(unsigned long long)s.tells,(unsigned long long)s.seeks,(unsigned long long)s.seeks_served,(unsigned long long)s.seeks_real,(unsigned long long)s.errors);
      check(s.closes==s.buffered_opens&&s.real_reads>0&&s.served_bytes>0,"statistics"); }
    end_case("sequential-3byte",262144);
    }

    // Interleaved handles with a write handle open among them.
    if(case_selected("mixed-handles")){
    begin_case("mixed-handles");set_capacity(4096);rng_state=201;
    { Handle hs[4];bool ok=open_pair(hs[0],"gzb_big.gz",big_size)&open_pair(hs[1],"gzb_small.gz",small_size)&open_pair(hs[2],"gzb_concat.gz",big_size+small_size)&open_pair(hs[3],"gzb_plain.bin",plain_size);
      void* writer=gzb::open("gzb_out2.gz","wb");check(writer&&!gzb::buffered(writer),"mixed_write_handle");
      if(ok){for(unsigned i=0;i<24000;++i){const unsigned pick=rnd(4);random_op(hs[pick],4096,pick!=3);if(i%1000==0&&writer)raw.write(writer,"xyz",3);}for(auto& h:hs)close_pair(h);}
      if(writer)check(gzb::close(writer)==0,"mixed_write_close"); }
    end_case("mixed-handles",4096);
    }

    // Slot table full: the 33rd read handle passes through and still reads correctly.
    if(case_selected("table-full")){
    begin_case("table-full");set_capacity(4096);
    { std::vector<Handle> hs(gzb::slot_count+1);unsigned registered=0;
      for(auto& h:hs){h.name="small";h.size=small_size;h.buffered=gzb::open("gzb_small.gz","rb");h.reference=raw.open("gzb_small.gz","rb");registered+=gzb::buffered(h.buffered);}
      check(registered==gzb::slot_count,"table_full_registered");check(!gzb::buffered(hs.back().buffered),"table_full_passthrough");
      for(unsigned i=0;i<hs.size();++i){op_read(hs[i],7);op_getc(hs[i]);op_tell(hs[i]);op_seek(hs[i],3,SEEK_SET);op_read(hs[i],200);}
      for(auto& h:hs){const int a=gzb::close(h.buffered),b=raw.close(h.reference);check(a==b&&a==0,"table_full_close");}
      check(gzb::statistics().passthrough_opens>=1,"table_full_statistics"); }
    end_case("table-full",4096);
    }

    // Timing: 10 M 3-byte reads (30 MB uncompressed, level 1) unbuffered, then buffered at 256 KB.
    if(case_selected("timing")){
    begin_case("timing");
    { const unsigned long long bytes=timing_calls*3;
      { void* f=raw.open("gzb_timing.gz","wb1");check(f!=nullptr,"timing_open_write");std::vector<unsigned char> chunk;uint32_t seed=17;unsigned long long done=0;
        while(f&&done<bytes){const size_t n=static_cast<size_t>(bytes-done<65536?bytes-done:65536);fill(chunk,n,seed);seed=seed*1664525u+1013904223u;raw.write(f,chunk.data(),static_cast<unsigned>(n));done+=n;}
        if(f)raw.close(f); }
      LARGE_INTEGER frequency{},t0{},t1{};QueryPerformanceFrequency(&frequency);
      unsigned char three[3];unsigned long long sum_ref=0,sum_buf=0,calls_ref=0,calls_buf=0;
      { void* f=raw.open("gzb_timing.gz","rb");check(f!=nullptr,"timing_open_reference");const ULONGLONG wall0=GetTickCount64();QueryPerformanceCounter(&t0);
        for(unsigned long long i=0;i<timing_calls;++i){const int n=raw.read(f,three,3);if(n!=3)break;++calls_ref;sum_ref+=three[0]+three[1]+three[2];}
        QueryPerformanceCounter(&t1);const ULONGLONG wall1=GetTickCount64();raw.close(f);
        const double seconds=double(t1.QuadPart-t0.QuadPart)/double(frequency.QuadPart);
        printf("GZ_TIMING mode=unbuffered calls=%llu bytes=%llu seconds=%.3f ns_per_call=%.1f wall_ms=%llu\n",calls_ref,calls_ref*3,seconds,seconds*1e9/double(calls_ref?calls_ref:1),(unsigned long long)(wall1-wall0)); }
      // The loading-trace hook's per-call envelope (CpuCallBoundary: fnsave/frstor twice, two
      // QueryPerformanceCounter reads, last-error transport) around the same raw call: what the
      // in-game "hooked" gzread interval of the loading profile contains besides zlib itself.
      unsigned long long calls_hooked=0,sum_hooked=0;
      { void* f=raw.open("gzb_timing.gz","rb");check(f!=nullptr,"timing_open_hooked");const ULONGLONG wall0=GetTickCount64();QueryPerformanceCounter(&t0);
        for(unsigned long long i=0;i<timing_calls;++i){
            x3m::CpuCallBoundary cpu;LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);cpu.before_original();
            const int n=raw.read(f,three,3);cpu.after_original();QueryPerformanceCounter(&end);
            if(n!=3)break;
            ++calls_hooked;sum_hooked+=three[0]+three[1]+three[2]+static_cast<unsigned>(end.QuadPart-begin.QuadPart>0);}
        QueryPerformanceCounter(&t1);const ULONGLONG wall1=GetTickCount64();raw.close(f);
        const double seconds=double(t1.QuadPart-t0.QuadPart)/double(frequency.QuadPart);
        printf("GZ_TIMING mode=hooked calls=%llu bytes=%llu seconds=%.3f ns_per_call=%.1f wall_ms=%llu\n",calls_hooked,calls_hooked*3,seconds,seconds*1e9/double(calls_hooked?calls_hooked:1),(unsigned long long)(wall1-wall0)); }
      set_capacity(262144);
      { void* f=gzb::open("gzb_timing.gz","rb");check(f&&gzb::buffered(f),"timing_open_buffered");const ULONGLONG wall0=GetTickCount64();QueryPerformanceCounter(&t0);
        for(unsigned long long i=0;i<timing_calls;++i){const int n=gzb::read(f,three,3);if(n!=3)break;++calls_buf;sum_buf+=three[0]+three[1]+three[2];}
        QueryPerformanceCounter(&t1);const ULONGLONG wall1=GetTickCount64();gzb::close(f);
        const double seconds=double(t1.QuadPart-t0.QuadPart)/double(frequency.QuadPart);
        printf("GZ_TIMING mode=buffered calls=%llu bytes=%llu seconds=%.3f ns_per_call=%.1f wall_ms=%llu\n",calls_buf,calls_buf*3,seconds,seconds*1e9/double(calls_buf?calls_buf:1),(unsigned long long)(wall1-wall0)); }
      check(calls_ref==timing_calls&&calls_buf==timing_calls&&sum_ref==sum_buf,"timing_equal_bytes");
      check(calls_hooked==timing_calls&&sum_hooked-calls_hooked<=sum_ref&&sum_hooked>=sum_ref,"timing_hooked_bytes"); }
    end_case("timing",262144);
    }
    printf("GZ BUFFER RESULT checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
