// Fast resource reader and engine-probe fixture (docs/verification/resource-reader.md,
// docs/verification/loading-probes.md). Links the production core
// (src/proxy/resource_reader_core.cpp, no-SSE build), the hook module, the
// patch arena and the probe machinery, and loads the game's real zlib1.dll
// (copied next to the executable by run_resource_reader.py). Builds synthetic
// loose .pck files (gzip + XOR key) and catalogue .dat slices (XOR 0x33 over
// concatenated records) and reads every one of them with:
//   * the reference decoder: the original 0x004e8880 algorithm re-implemented
//     from the disassembly (3-byte probe, trailer seek, memset, byte-wise header
//     getc, 1 KiB fread + byte XOR + inflate loop, dispatcher clamp/cursor);
//   * the core in fast mode (game_buffer=true: env malloc, counters, globals);
//   * the core in verify mode through the generated stub on a fixture site
//     whose prologue is the real one (sub esp,0x454), with the reference as
//     the "original", so the production comparison runs;
// and checks bytes, size globals, counters, cursor and stream position, the
// fallback restoration, the .dat handle pool, and the probe trampolines
// (entry/exit counters, ret n, nesting, longjmp desync recovery, two threads,
// probe + reader chained on one site). No game launch.
#include "../../src/proxy/resource_reader.h"
#include "../../src/proxy/loading_probes.h"
#include "../../src/proxy/loading_trace_light.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <csetjmp>
#include <string>
#include <vector>

namespace x3m { void log(const char* format,...){ va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n'); } }
namespace x3m::object_trace { bool executable_verified(){ return false; } } // the fixture never patches game addresses; production gates on the real check
namespace rr=x3m::resource_reader;
namespace lp=x3m::loading_probes;
namespace light=x3m::loading_trace::light;
namespace ep=x3m::engine_patch;
static unsigned checks=0,failures=0;
static void check(bool okay,const char* label,const char* detail=""){ ++checks; if(!okay){ ++failures; printf("FAIL %s %s\n",label,detail); } }
// ---- zlib1.dll ----
using GzOpenFn=void* (__cdecl*)(const char*,const char*);
using GzWriteFn=int (__cdecl*)(void*,const void*,unsigned);
using GzCloseFn=int (__cdecl*)(void*);
using InflateInitFn=int (__cdecl*)(void*,int,const char*,int);
using InflateFn=int (__cdecl*)(void*,int);
using InflateEndFn=int (__cdecl*)(void*);
static GzOpenFn gz_open;static GzWriteFn gz_write;static GzCloseFn gz_close;
static InflateInitFn inflate_init;static InflateFn inflate_fn;static InflateEndFn inflate_end;
// ---- fixture CRT environment (msvcrt FILE*) and game-like globals ----
static uint32_t counters[4],size_globals[2];
static unsigned malloc_failures_armed=0;
static void* __cdecl env_malloc(size_t n){ if(malloc_failures_armed){--malloc_failures_armed;return nullptr;} return malloc(n); }
static void __cdecl env_free(void* p){ free(p); }
static size_t __cdecl env_fread(void* b,size_t s,size_t n,void* f){ return fread(b,s,n,static_cast<FILE*>(f)); }
static int __cdecl env_fseek(void* f,long o,int w){ return fseek(static_cast<FILE*>(f),o,w); }
static long __cdecl env_ftell(void* f){ return ftell(static_cast<FILE*>(f)); }
static rr::Environment environment(){
    rr::Environment env;env.fread=env_fread;env.fseek=env_fseek;env.ftell=env_ftell;env.malloc=env_malloc;env.free=env_free;
    env.inflateInit2_=inflate_init;env.inflate=inflate_fn;env.inflateEnd=inflate_end;
    for(unsigned i=0;i<4;++i)env.counters[i]=&counters[i];for(unsigned i=0;i<2;++i)env.size_globals[i]=&size_globals[i];
    return env;
}
// ---- synthetic payloads ----
static uint32_t rng=0x2545f491u;
static uint32_t rnd(){ rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static std::vector<unsigned char> text_payload(size_t n){ std::vector<unsigned char> v(n); for(size_t i=0;i<n;++i){ const uint32_t r=rnd(); v[i]=(r%23)<3?'\n':static_cast<unsigned char>('a'+(r>>8)%26); } return v; }
static std::vector<unsigned char> read_file(const char* name){ std::vector<unsigned char> v; FILE* f=fopen(name,"rb"); if(!f)return v; fseek(f,0,SEEK_END); const long n=ftell(f); fseek(f,0,SEEK_SET); v.resize(n>0?size_t(n):0); if(n>0)fread(v.data(),1,v.size(),f); fclose(f); return v; }
static bool write_file(const char* name,const std::vector<unsigned char>& v){ FILE* f=fopen(name,"wb"); if(!f)return false; const bool ok=v.empty()||fwrite(v.data(),1,v.size(),f)==v.size(); fclose(f); return ok; }
// gzip through the real DLL, then optional header flags rewritten by hand.
struct HeaderOptions { bool fname=false,fextra=false,fcomment=false,fhcrc=false; };
static std::vector<unsigned char> gzip(const std::vector<unsigned char>& payload,const HeaderOptions& options={},int level=6){
    char mode[8];snprintf(mode,sizeof mode,"wb%d",level);
    void* f=gz_open("rr_tmp.gz",mode);check(f!=nullptr,"gzopen_write");
    size_t done=0;while(f&&done<payload.size()){const unsigned n=unsigned(payload.size()-done<65536?payload.size()-done:65536);gz_write(f,payload.data()+done,n);done+=n;}
    if(f)gz_close(f);
    std::vector<unsigned char> gz=read_file("rr_tmp.gz");
    if(gz.size()<18)return gz;
    // zlib 1.2.3 gzopen writes a 10-byte header with FLG=0; rebuild the header with the requested fields.
    std::vector<unsigned char> head(gz.begin(),gz.begin()+10);
    unsigned char flg=0;std::vector<unsigned char> extra;
    if(options.fextra){flg|=4;extra.push_back(5);extra.push_back(0);for(unsigned i=0;i<5;++i)extra.push_back(static_cast<unsigned char>('A'+i));}
    if(options.fname){flg|=8;const char* name="synthetic.xml";extra.insert(extra.end(),name,name+strlen(name)+1);}
    if(options.fcomment){flg|=16;const char* comment="fixture comment";extra.insert(extra.end(),comment,comment+strlen(comment)+1);}
    if(options.fhcrc){flg|=2;extra.push_back(0x12);extra.push_back(0x34);}
    head[3]=flg;
    std::vector<unsigned char> out(head);out.insert(out.end(),extra.begin(),extra.end());out.insert(out.end(),gz.begin()+10,gz.end());
    return out;
}
static std::vector<unsigned char> scramble(const std::vector<unsigned char>& gz,unsigned char key){
    std::vector<unsigned char> out;out.push_back(static_cast<unsigned char>(key^0xc8));
    for(unsigned char b:gz)out.push_back(static_cast<unsigned char>(b^key));
    return out;
}
// ---- the reference decoder: the original algorithm (docs/reverse-engineering/resource-reader.md) ----
static bool reference_tamper=false;
static int ref_dispatch_read(rr::FileObject* o,void* buf,int size,int count){ // 0x004e9210
    if(!(o->flags&1))return 0;
    FILE* f=static_cast<FILE*>(o->file);
    if((o->flags&3)!=3)return int(fread(buf,size_t(size),size_t(count),f));
    unsigned n=unsigned(size)*unsigned(count);
    if(unsigned(o->length)<unsigned(o->cursor)+n)n=unsigned(o->length-o->cursor);
    const int got=int(fread(buf,1,n,f));
    for(unsigned i=0;i<n;++i)static_cast<unsigned char*>(buf)[i]^=0x33;
    o->cursor+=int(n);
    return got;
}
static int ref_getc(rr::FileObject* o){ // 0x004e91b0
    if(!(o->flags&1))return -1;
    FILE* f=static_cast<FILE*>(o->file);
    if((o->flags&3)!=3)return fgetc(f);
    o->cursor+=1;if(o->cursor<=o->length){const int c=fgetc(f);return c<0?c:(c&0xff)^0x33;}
    return -1;
}
static void ref_seek(rr::FileObject* o,long offset,int whence){ // the fseek sites of 0x004e8880 (catalogue: cursor clamp + absolute seek)
    FILE* f=static_cast<FILE*>(o->file);
    if((o->flags&3)==3){ long cursor=whence==SEEK_END?o->length+offset:offset; if(cursor>o->length)cursor=o->length; if(cursor<0)cursor=0; o->cursor=int32_t(cursor); fseek(f,o->offset+cursor,SEEK_SET); }
    else fseek(f,offset,whence);
}
static void* reference_read(rr::FileObject* o){ // 0x004e8880 (mode 5 gz handles are not modelled: the fixture never builds them)
    unsigned char magic[3]{};
    if(ref_dispatch_read(o,magic,1,3)!=3)return nullptr;
    unsigned char key=0;bool scrambled=false;
    bool gz=magic[0]==0x1f&&magic[1]==0x8b;
    if(!gz){key=static_cast<unsigned char>(magic[0]^0xc8);if(((magic[1]^key)&0xff)==0x1f&&((magic[2]^key)&0xff)==0x8b){gz=true;scrambled=true;}}
    if(!gz){
        long size=0;
        if(o->flags&1){ if((o->flags&3)==3){ref_seek(o,0,SEEK_END);size=o->cursor;} else {ref_seek(o,0,SEEK_END);size=ftell(static_cast<FILE*>(o->file));} }
        size_globals[0]=size_globals[1]=uint32_t(size);
        void* buf=env_malloc(size_t(size));if(!buf)return nullptr;
        counters[0]+=uint32_t(size);counters[1]+=uint32_t(size);counters[2]+=uint32_t(size);counters[3]+=1;
        memset(buf,0,size_t(size));
        if(o->flags&1)ref_seek(o,0,SEEK_SET);
        const int got=ref_dispatch_read(o,buf,1,int(size));
        if(uint32_t(got)==size_globals[0])return buf;
        if(size_globals[0]){env_free(buf);counters[0]-=size_globals[0];counters[1]-=size_globals[0];}
        return nullptr;
    }
    if(o->flags&1)ref_seek(o,-8,SEEK_END);
    unsigned char crc[4]{},isize_bytes[4]{};
    ref_dispatch_read(o,crc,1,4);if(scrambled)for(auto& b:crc)b^=key;
    ref_dispatch_read(o,isize_bytes,1,4);if(scrambled)for(auto& b:isize_bytes)b^=key;
    const uint32_t isize=uint32_t(isize_bytes[0])|uint32_t(isize_bytes[1])<<8|uint32_t(isize_bytes[2])<<16|uint32_t(isize_bytes[3])<<24;
    void* buf=env_malloc(isize);if(!buf)return nullptr;
    counters[0]+=isize;counters[1]+=isize;counters[2]+=isize;counters[3]+=1;
    memset(buf,0,isize);
    if(o->flags&1)ref_seek(o,scrambled?3:2,SEEK_SET);
    const int cm=ref_getc(o)^key,flg=ref_getc(o)^key;
    bool okay=(cm&0xff)==8&&!(flg&0xe0);
    if(okay){
        for(int i=0;i<6;++i)ref_getc(o);
        if(flg&4){int xlen=(ref_getc(o)^key)&0xff;xlen+=((ref_getc(o)^key)&0xff)<<8;for(int i=0;i<xlen;++i)ref_getc(o);}
        if(flg&8)while(((ref_getc(o)^key)&0xff)!=0){}
        if(flg&16)while(((ref_getc(o)^key)&0xff)!=0){}
        if(flg&2){ref_getc(o);ref_getc(o);}
        struct { const unsigned char* next_in;uint32_t avail_in,total_in;unsigned char* next_out;uint32_t avail_out,total_out;const char* msg;void* state;void* zalloc;void* zfree;void* opaque;int data_type;uint32_t adler,reserved; } z{};
        if(inflate_init(&z,-15,"1.2.3",0x38)!=0)okay=false;
        else{
            unsigned char chunk[1028];uint32_t produced=0;
            for(;;){
                const int n=ref_dispatch_read(o,chunk,1,1024);
                if(scrambled)for(int i=0;i<n;++i)chunk[i]^=key;
                if(n==0)break;
                z.next_in=chunk;z.avail_in=uint32_t(n);z.next_out=static_cast<unsigned char*>(buf)+produced;z.avail_out=isize-produced;
                const int status=inflate_fn(&z,0);
                produced=isize-z.avail_out;
                if(status==1)break;
            }
            inflate_end(&z);
            size_globals[0]=size_globals[1]=isize;
            if(reference_tamper&&isize)static_cast<unsigned char*>(buf)[0]^=1; // fixture knob: a deliberately wrong "original"
            return buf;
        }
    }
    if(isize){env_free(buf);counters[0]-=isize;counters[1]-=isize;}
    return nullptr;
}
// A fixture site with the real prologue of 0x004e8880 (sub esp,0x454) and the
// EAX-in/EAX-out convention, forwarding to the reference decoder.
extern "C" void* __cdecl reference_entry_c(rr::FileObject* o){ return reference_read(o); }
extern "C" void reference_site();
asm(".text\n.globl _reference_site\n_reference_site:\n"
    "\t.byte 0x81,0xec,0x54,0x04,0x00,0x00\n"   // sub esp,0x454 (the displaced instruction)
    "\tadd $0x454,%esp\n\tpush %eax\n\tcall _reference_entry_c\n\tadd $4,%esp\n\tret\n");
static void* call_site(void (*site)(),rr::FileObject* o){ void* r=o; asm volatile("call *%1":"+a"(r):"r"(site):"ecx","edx","memory","cc"); return r; }
// ---- cases ----
struct Source { std::string name; std::vector<unsigned char> payload; std::vector<unsigned char> stored; bool gz; unsigned char key; bool scrambled; };
static std::vector<Source> sources;
static std::vector<unsigned char> catalogue_image;struct Record{size_t index;int32_t offset,length;};static std::vector<Record> records;
static std::vector<Source> cursor_sources;static std::vector<unsigned char> cursor_image;static std::vector<Record> cursor_records;
static rr::FileObject loose_object(const Source& s){ rr::FileObject o{}; o.file=fopen(s.name.c_str(),"rb"); o.flags=o.file?1:0; return o; }
static rr::FileObject record_object(const Record& r){ rr::FileObject o{}; o.file=fopen("rr_catalogue.dat","rb"); o.flags=3; o.offset=r.offset; o.length=r.length; o.cursor=0; if(o.file)fseek(static_cast<FILE*>(o.file),r.offset,SEEK_SET); return o; }
static rr::FileObject cursor_record_object(const Record& r){ rr::FileObject o{}; o.file=fopen("rr_cursor.dat","rb"); o.flags=3; o.offset=r.offset; o.length=r.length; o.cursor=0; if(o.file)fseek(static_cast<FILE*>(o.file),r.offset,SEEK_SET); return o; }
static void close_object(rr::FileObject& o){ if(o.file)fclose(static_cast<FILE*>(o.file)); o.file=nullptr; }
static bool same(const void* a,size_t n,const std::vector<unsigned char>& b){ return n==b.size()&&(n==0||!memcmp(a,b.data(),n)); }
struct Outcome { bool handled; rr::Reason reason; };
// The original's chunk loop (0x004e8880): from the first deflate byte (start+header) it reads 1 KiB
// chunks and stops on Z_STREAM_END, so cursor/position end at the chunk boundary after the last
// deflate byte: length itself unless (length-first) mod 1024 is 1..8 (the trailer's tail alone in
// a chunk). Computed from the stored image (unscrambled) independently of the core and the reference.
static long original_final(const std::vector<unsigned char>& stored){
    const unsigned start=stored[0]==0x1f&&stored[1]==0x8b?0:1;const unsigned char key=start?static_cast<unsigned char>(stored[0]^0xc8):0;
    auto at=[&](size_t i){ return static_cast<unsigned char>(stored[start+i]^key); };
    size_t p=10;const unsigned char flg=at(3);
    if(flg&4){p+=2+at(10)+(at(11)<<8);}
    if(flg&8){while(at(p))++p;++p;}
    if(flg&0x10){while(at(p))++p;++p;}
    if(flg&2)p+=2;
    const long first=long(start+p),length=long(stored.size());const long deflate=length-first-8;
    long final=first+((deflate-1)/1024+1)*1024;return final>length?length:final;
}
static Outcome run_fast(rr::FileObject& o,const std::vector<unsigned char>& expect,const char* label,bool catalogue,long expected_position,int32_t expected_cursor){
    const rr::Environment env=environment();
    counters[0]=counters[1]=counters[2]=100;counters[3]=7;size_globals[0]=size_globals[1]=0xdeadbeef;
    const rr::Result r=rr::decode(env,&o,true);
    char detail[160];
    if(r.outcome==rr::Outcome::Handled){
        snprintf(detail,sizeof detail,"%s size=%lu expect=%lu",label,static_cast<unsigned long>(r.size),static_cast<unsigned long>(expect.size()));
        check(same(r.buffer,r.size,expect),"fast_bytes",detail);
        check(size_globals[0]==r.size&&size_globals[1]==r.size,"fast_size_globals",label);
        check(counters[0]==100+r.size&&counters[1]==100+r.size&&counters[2]==100+r.size&&counters[3]==8,"fast_counters",label);
        check(r.catalogue==catalogue,"fast_catalogue_flag",label);
        if(catalogue)check(o.cursor==expected_cursor,"fast_cursor",label);
        check(r.expected_cursor==(catalogue?expected_cursor:0)&&r.expected_position==expected_position,"fast_predicted_state",label);
        check(ftell(static_cast<FILE*>(o.file))==expected_position,"fast_position",label);
        env_free(r.buffer);
    }
    return {r.outcome==rr::Outcome::Handled,r.reason};
}
// The fallback contract: after a Fallback the reference must read the same bytes it reads from a fresh object.
static void check_fallback_restores(rr::FileObject& o,const char* label){
    const long expected_position=ftell(static_cast<FILE*>(o.file));
    const int32_t expected_cursor=o.cursor;
    const rr::Environment env=environment();
    const rr::Result r=rr::decode(env,&o,true);
    check(r.outcome==rr::Outcome::Fallback,"fallback_outcome",label);
    check(ftell(static_cast<FILE*>(o.file))==expected_position&&o.cursor==expected_cursor,"fallback_restored",label);
    printf("RR_FALLBACK case=%s reason=%s\n",label,rr::reason_name(unsigned(r.reason)));
}
static uint64_t qpc(){ LARGE_INTEGER v{};QueryPerformanceCounter(&v);return uint64_t(v.QuadPart); }
static double us(uint64_t ticks){ LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return double(ticks)*1e6/double(f.QuadPart); }
// ---- probe fixture sites ----
extern "C" int __cdecl probe_body_c(int depth);
extern "C" void probe_site_a(); extern "C" void probe_site_b(); extern "C" void probe_site_c();
// a: prologue `push ebp; mov ebp,esp; and esp,-8` (6 bytes, the resource_load shape), cdecl, ret
asm(".text\n.globl _probe_site_a\n_probe_site_a:\n\t.byte 0x55,0x8b,0xec,0x83,0xe4,0xf8\n\tmov 8(%ebp),%eax\n\tpush %eax\n\tcall _probe_body_c\n\tmov %ebp,%esp\n\tpop %ebp\n\tret\n");
// b: prologue `push ebx; mov ebx,[esp+8]` (5 bytes, the resource_open shape), stack arg popped by ret 4
asm(".text\n.globl _probe_site_b\n_probe_site_b:\n\t.byte 0x53,0x8b,0x5c,0x24,0x08\n\tpush %ebx\n\tcall _probe_body_c\n\tadd $4,%esp\n\tpop %ebx\n\tret $4\n");
// c: prologue `test byte [esi+4],1; push ebx` (5 bytes, the read_dispatch shape, count-only), ret 4
asm(".text\n.globl _probe_site_c\n_probe_site_c:\n\t.byte 0xf6,0x46,0x04,0x01,0x53\n\tmov 8(%esp),%ebx\n\tpop %ebx\n\tmov $7,%eax\n\tret $4\n");
static jmp_buf escape;static int escape_depth=0,catch_depth=0;
static int call_a(int depth);
extern "C" int __cdecl probe_body_c(int depth){
    if(depth==escape_depth&&escape_depth>0)longjmp(escape,1);   // unwinds through the probed frames above the catcher without returning
    if(depth==catch_depth&&catch_depth>0){ if(setjmp(escape)==0)call_a(depth-1); return 100; } // the catcher's own frame then returns normally
    if(depth>0)return call_a(depth-1)+1;
    return 1;
}
static int call_a(int depth){ int r; asm volatile("push %1\n\tcall _probe_site_a\n\tadd $4,%%esp":"=a"(r):"r"(depth):"ecx","edx","memory","cc"); return r; }
static int call_b(int depth){ int r; rr::FileObject o{}; o.flags=3; asm volatile("push %1\n\tcall _probe_site_b":"=a"(r):"r"(depth),"S"(&o):"ecx","edx","memory","cc"); return r; } // ESI = a file object, as at the real site
static int call_c(){ int r; rr::FileObject o{}; o.flags=3; asm volatile("push $0\n\tmov $3,%%ecx\n\tmov $5,%%eax\n\tcall _probe_site_c":"=a"(r):"S"(&o):"ecx","edx","memory","cc"); return r; }
static DWORD WINAPI probe_thread(LPVOID){ for(int i=0;i<200;++i)call_a(3); return 0; }
static unsigned char site_bytes(void (*fn)(),unsigned char* out,unsigned n){ memcpy(out,reinterpret_cast<const void*>(fn),n); return n; }

int main(int argc,char** argv){
    const bool quick=argc>1&&!strcmp(argv[1],"quick");
    HMODULE zlib=LoadLibraryA("zlib1.dll");
    check(zlib!=nullptr,"zlib1_loaded");
    if(!zlib){printf("RESOURCE READER RESULT checks=%u failures=%u\n",checks,failures+1);return 1;}
    gz_open=reinterpret_cast<GzOpenFn>(GetProcAddress(zlib,"gzopen"));gz_write=reinterpret_cast<GzWriteFn>(GetProcAddress(zlib,"gzwrite"));gz_close=reinterpret_cast<GzCloseFn>(GetProcAddress(zlib,"gzclose"));
    inflate_init=reinterpret_cast<InflateInitFn>(GetProcAddress(zlib,"inflateInit2_"));inflate_fn=reinterpret_cast<InflateFn>(GetProcAddress(zlib,"inflate"));inflate_end=reinterpret_cast<InflateEndFn>(GetProcAddress(zlib,"inflateEnd"));
    auto version=reinterpret_cast<const char* (__cdecl*)()>(GetProcAddress(zlib,"zlibVersion"));
    check(gz_open&&gz_write&&gz_close&&inflate_init&&inflate_fn&&inflate_end,"zlib_exports");
    printf("RR_ZLIB version=%s\n",version?version():"?");
    // ---- sources: loose files ----
    struct Spec { const char* name; size_t size; unsigned char key; bool scrambled; HeaderOptions header; int level; };
    const Spec specs[]={
        {"rr_small.pck",700,0x5a,true,{},6},{"rr_medium.pck",31000,0x00,true,{},6},{"rr_fname.pck",5000,0x91,true,{true,false,false,false},6},
        {"rr_extra.pck",5000,0x33,true,{false,true,false,false},6},{"rr_comment.pck",4000,0xc8,true,{false,false,true,false},6},{"rr_hcrc.pck",4000,0x7e,true,{false,false,false,true},6},
        {"rr_allflags.pck",9000,0x10,true,{true,true,true,true},6},{"rr_large.pck",size_t(quick?300000:3000000),0x4b,true,{},1},{"rr_plain.gz",20000,0,false,{},6},{"rr_plain_fname.gz",2500,0,false,{true,false,false,false},9},
    };
    for(const auto& s:specs){
        Source src;src.name=s.name;src.payload=text_payload(s.size);src.gz=true;src.key=s.key;src.scrambled=s.scrambled;
        std::vector<unsigned char> gz=gzip(src.payload,s.header,s.level);
        src.stored=s.scrambled?scramble(gz,s.key):gz;
        check(write_file(src.name.c_str(),src.stored),"write_source",s.name);
        sources.push_back(src);
    }
    { Source src;src.name="rr_text.txt";src.payload=text_payload(3000);src.stored=src.payload;src.gz=false;src.key=0;src.scrambled=false;check(write_file(src.name.c_str(),src.stored),"write_text");sources.push_back(src); }
    // ---- catalogue: every source as a record, XOR 0x33, plus a plain-text record ----
    for(size_t i=0;i<sources.size();++i){
        Record r;r.index=i;r.offset=int32_t(catalogue_image.size());r.length=int32_t(sources[i].stored.size());
        for(unsigned char b:sources[i].stored)catalogue_image.push_back(static_cast<unsigned char>(b^0x33));
        catalogue_image.push_back(0x33);catalogue_image.push_back(0x33); // padding between records (a stray NUL pair after XOR)
        records.push_back(r);
    }
    check(write_file("rr_catalogue.dat",catalogue_image),"write_catalogue");
    unsigned handled_loose=0,handled_records=0;
    // ---- case 1: fast mode against the reference, loose ----
    for(const auto& s:sources){
        rr::FileObject o=loose_object(s);check(o.file!=nullptr,"open_loose",s.name.c_str());if(!o.file)continue;
        rr::FileObject ref=loose_object(s);
        void* expect=reference_read(&ref);const uint32_t expect_size=size_globals[0];
        std::vector<unsigned char> expected(expect?static_cast<unsigned char*>(expect):nullptr,expect?static_cast<unsigned char*>(expect)+expect_size:nullptr);
        check(expect!=nullptr&&same(expect,expect_size,s.payload),"reference_matches_payload",s.name.c_str());
        const long ref_position=ftell(static_cast<FILE*>(ref.file));
        if(s.gz)check(ref_position==original_final(s.stored),"reference_loose_position_formula",s.name.c_str());
        const Outcome out=run_fast(o,expected,s.name.c_str(),false,ref_position,0);
        check(out.handled==s.gz,"fast_handled_iff_gzip",s.name.c_str());
        if(!s.gz)check(out.reason==rr::Reason::NotGzip,"text_falls_back_not_gzip",s.name.c_str());
        handled_loose+=out.handled;
        if(expect)env_free(expect);close_object(o);close_object(ref);
    }
    // ---- case 2: fast mode against the reference, catalogue records (random order) ----
    for(unsigned pass=0;pass<2;++pass)for(size_t k=0;k<records.size();++k){
        const Record& rec=records[(k*7+pass*3)%records.size()];const Source& s=sources[rec.index];
        rr::FileObject o=record_object(rec);rr::FileObject ref=record_object(rec);
        void* expect=reference_read(&ref);const uint32_t expect_size=size_globals[0];
        std::vector<unsigned char> expected(expect?static_cast<unsigned char*>(expect):nullptr,expect?static_cast<unsigned char*>(expect)+expect_size:nullptr);
        check(expect!=nullptr&&same(expect,expect_size,s.payload),"reference_record_matches_payload",s.name.c_str());
        if(s.gz)check(ref.cursor==original_final(s.stored),"reference_record_cursor_formula",s.name.c_str());
        const Outcome out=run_fast(o,expected,s.name.c_str(),true,ftell(static_cast<FILE*>(ref.file)),ref.cursor);
        check(out.handled==s.gz,"fast_record_handled_iff_gzip",s.name.c_str());
        handled_records+=out.handled;
        if(expect)env_free(expect);close_object(o);close_object(ref);
    }
    printf("RR_CASE name=fast loose_handled=%u record_handled=%u sources=%zu\n",handled_loose,handled_records,sources.size());
    // ---- case 2b: the record-cursor class (run C: 20 verify lines with cursor_ok=0, all records whose
    // (length-first) mod 1024 is 1..8). One source per remainder 0..9 at one and at three-plus chunks,
    // alternating scrambled/plain, one with an FNAME header; rr_cursor.dat holds them with a class
    // record last (no padding after it). Checked: the formula, the reference, fast mode, then the verify
    // stub (case 5) and a pooled handle sequence (case 6).
    unsigned cursor_class=0,cursor_handled=0;
    {
        // Payload sizes are searched (text compresses to ~60 %): grow n by one character from a base
        // below the target until (length-first)/1024 == q and the remainder == k; restart from the
        // base when the target was skipped (the payload text differs per attempt, so a retry can land).
        for(unsigned q=1;q<=3;q+=2){ size_t base=q==1?1400:4600;
        for(unsigned k=0;k<10;++k){
            const unsigned idx=unsigned(cursor_sources.size());
            Source src;src.name="rr_cursor_"+std::to_string(q)+"_"+std::to_string(k)+".pck";src.gz=true;src.scrambled=(idx%2)==0;src.key=src.scrambled?static_cast<unsigned char>(0x40+idx):0;
            HeaderOptions header;header.fname=(idx%5)==3;
            bool found=false;size_t n=base;
            for(unsigned attempt=0;attempt<8000&&!found;++attempt){
                src.payload=text_payload(n);std::vector<unsigned char> gz=gzip(src.payload,header);src.stored=src.scrambled?scramble(gz,src.key):gz;
                const long length=long(src.stored.size()),start=src.scrambled?1:0;
                long p=10;if(header.fname){while(gz[size_t(p)])++p;++p;} // FNAME: name bytes to NUL
                const long rem=(length-start-p)%1024,chunks=(length-start-p)/1024;
                found=rem==long(k)&&chunks==long(q);
                if(chunks>long(q)||(chunks==long(q)&&rem>long(k)))n=base;else ++n;
            }
            check(found,"cursor_source_built",src.name.c_str());
            if(found)base=n>12?n-12:n;
            if(!found)continue;
            check(write_file(src.name.c_str(),src.stored),"write_cursor_source",src.name.c_str());
            cursor_sources.push_back(src);
        }}
        // the .dat: the class record (rem 5, three chunks) last, nothing after it
        std::vector<size_t> order;for(size_t i=0;i<cursor_sources.size();++i)if(i!=15)order.push_back(i);order.push_back(15);
        for(size_t i:order){
            Record r;r.index=i;r.offset=int32_t(cursor_image.size());r.length=int32_t(cursor_sources[i].stored.size());
            for(unsigned char b:cursor_sources[i].stored)cursor_image.push_back(static_cast<unsigned char>(b^0x33));
            if(i!=15){cursor_image.push_back(0x33);cursor_image.push_back(0x33);}
            cursor_records.push_back(r);
        }
        check(write_file("rr_cursor.dat",cursor_image),"write_cursor_catalogue");
        for(const auto& s:cursor_sources){
            const long final=original_final(s.stored),length=long(s.stored.size());
            const bool short_class=final<length;cursor_class+=short_class;
            rr::FileObject o=loose_object(s),ref=loose_object(s);
            void* expect=reference_read(&ref);const uint32_t expect_size=size_globals[0];
            std::vector<unsigned char> expected(expect?static_cast<unsigned char*>(expect):nullptr,expect?static_cast<unsigned char*>(expect)+expect_size:nullptr);
            check(expect!=nullptr&&same(expect,expect_size,s.payload),"cursor_reference_matches_payload",s.name.c_str());
            const long ref_position=ftell(static_cast<FILE*>(ref.file));
            check(ref_position==final,"cursor_reference_loose_position_formula",s.name.c_str());
            const Outcome out=run_fast(o,expected,s.name.c_str(),false,ref_position,0);
            check(out.handled,"cursor_loose_handled",s.name.c_str());
            if(expect)env_free(expect);close_object(o);close_object(ref);
        }
        for(const auto& rec:cursor_records){
            const Source& s=cursor_sources[rec.index];const long final=original_final(s.stored);
            rr::FileObject o=cursor_record_object(rec),ref=cursor_record_object(rec);
            void* expect=reference_read(&ref);const uint32_t expect_size=size_globals[0];
            std::vector<unsigned char> expected(expect?static_cast<unsigned char*>(expect):nullptr,expect?static_cast<unsigned char*>(expect)+expect_size:nullptr);
            check(expect!=nullptr&&same(expect,expect_size,s.payload),"cursor_reference_record_matches_payload",s.name.c_str());
            check(ref.cursor==final&&ftell(static_cast<FILE*>(ref.file))==rec.offset+final,"cursor_reference_record_formula",s.name.c_str());
            const Outcome out=run_fast(o,expected,s.name.c_str(),true,ftell(static_cast<FILE*>(ref.file)),ref.cursor);
            check(out.handled,"cursor_record_handled",s.name.c_str());cursor_handled+=out.handled;
            // from the state both leave, the dispatcher's next clamped read behaves the same (n = length - cursor bytes, XOR 0x33, same bytes)
            { unsigned char a[16]{},b[16]{};const int na=ref_dispatch_read(&o,a,1,16),nb=ref_dispatch_read(&ref,b,1,16);
              check(na==nb&&o.cursor==ref.cursor&&!memcmp(a,b,16)&&(final<rec.length?na>0:na==0),"cursor_next_dispatch_read_same",s.name.c_str()); }
            if(expect)env_free(expect);close_object(o);close_object(ref);
        }
        const Record& last=cursor_records.back();
        check(last.offset+last.length==int32_t(cursor_image.size())&&original_final(cursor_sources[last.index].stored)<last.length,"cursor_last_record_is_class");
        printf("RR_CASE name=cursor sources=%zu class_records=%u loose_and_record_handled=%u last_record_class=1\n",cursor_sources.size(),cursor_class,unsigned(cursor_handled));
    }
    // ---- case 3: fallbacks with restoration ----
    {
        const Source& s=sources[0];
        { rr::FileObject o{};o.flags=0;o.file=nullptr;const rr::Result r=rr::decode(environment(),&o,true);check(r.outcome==rr::Outcome::Fallback&&r.reason==rr::Reason::Unopened,"fallback_unopened"); }
        { rr::FileObject o=loose_object(s);o.flags=5;check_fallback_restores(o,"gz_handle");close_object(o); }
        { rr::FileObject o=loose_object(s);o.flags=0x11;check_fallback_restores(o,"progress");close_object(o); }
        { rr::FileObject o=loose_object(sources.back());check_fallback_restores(o,"transparent");
          // ...and the reference then still decodes the transparent file from the restored state
          void* buf=reference_read(&o);check(buf&&same(buf,size_globals[0],sources.back().payload),"transparent_reference_after_fallback");if(buf)env_free(buf);close_object(o); }
        { rr::FileObject o=loose_object(s);fseek(static_cast<FILE*>(o.file),1,SEEK_SET);check_fallback_restores(o,"state_position");close_object(o); }
        { rr::FileObject o=record_object(records[0]);o.cursor=1;check_fallback_restores(o,"state_cursor");close_object(o); }
        { std::vector<unsigned char> bad=s.stored;bad[3]^=s.key^0x09;write_file("rr_bad_cm.pck",bad);Source b=s;b.name="rr_bad_cm.pck";rr::FileObject o=loose_object(b);check_fallback_restores(o,"method");close_object(o); }
        { std::vector<unsigned char> bad=s.stored;bad[4]^=s.key^0xe0;write_file("rr_bad_flg.pck",bad);Source b=s;b.name="rr_bad_flg.pck";rr::FileObject o=loose_object(b);check_fallback_restores(o,"reserved");close_object(o); }
        { std::vector<unsigned char> bad(s.stored.begin(),s.stored.begin()+12);write_file("rr_short.pck",bad);Source b=s;b.name="rr_short.pck";rr::FileObject o=loose_object(b);check_fallback_restores(o,"length");close_object(o); }
        // an invalid deflate block header (BFINAL=1, BTYPE=11) right after the 10-byte gzip header: raw inflate must report Z_DATA_ERROR
        { std::vector<unsigned char> bad=s.stored;bad[11]=static_cast<unsigned char>(0x07^s.key);write_file("rr_corrupt.pck",bad);Source b=s;b.name="rr_corrupt.pck";rr::FileObject o=loose_object(b);check_fallback_restores(o,"inflate");close_object(o); }
        { std::vector<unsigned char> bad=s.stored;bad[bad.size()-2]^=s.key^0x40;write_file("rr_isize.pck",bad);Source b=s;b.name="rr_isize.pck";rr::FileObject o=loose_object(b);check_fallback_restores(o,"size");close_object(o); }
        { std::vector<unsigned char> bad=s.stored;bad.resize(bad.size()-40);write_file("rr_trunc.pck",bad);Source b=s;b.name="rr_trunc.pck";rr::FileObject o=loose_object(b);check_fallback_restores(o,"truncated");close_object(o); }
        { std::vector<unsigned char> empty=scramble(gzip(std::vector<unsigned char>()),0x21);write_file("rr_empty.pck",empty);Source b=s;b.name="rr_empty.pck";rr::FileObject o=loose_object(b);check_fallback_restores(o,"empty");close_object(o); }
        { std::vector<unsigned char> bad=gzip(s.payload,{true,false,false,false});bad[3]=8;bad.resize(bad.size()-8);for(size_t i=10;i<bad.size();++i)bad[i]=1;bad.resize(bad.size()+8,1);write_file("rr_noname.gz",bad);Source b=s;b.name="rr_noname.gz";b.scrambled=false;rr::FileObject o=loose_object(b);check_fallback_restores(o,"header");close_object(o); }
        { rr::FileObject o=loose_object(s);malloc_failures_armed=1;check_fallback_restores(o,"alloc");malloc_failures_armed=0;
          void* buf=reference_read(&o);check(buf&&same(buf,size_globals[0],s.payload),"reference_after_alloc_fallback");if(buf)env_free(buf);close_object(o); }
    }
    printf("RR_CASE name=fallbacks\n");
    // ---- case 4: the probe machinery on fixture sites ----
    {
        ep::SiteSpec specs[lp::site_count];unsigned kinds[lp::site_count];
        void (*fns[lp::site_count])()={};unsigned lens[lp::site_count]={};unsigned pops[lp::site_count]={};
        // index 0 resource_load shape -> site a; 1 resource_open shape (ret 4) -> site b; 4 read_dispatch (count only) -> site c; 3 resource_read -> the reference site
        fns[0]=probe_site_a;lens[0]=6;pops[0]=0;fns[1]=probe_site_b;lens[1]=5;pops[1]=4;fns[4]=probe_site_c;lens[4]=5;pops[4]=4;fns[3]=reference_site;lens[3]=6;pops[3]=0;
        const unsigned production_kinds[lp::site_count]={0,1,2,3,4,6,0,5,0,0,0,0};
        for(unsigned i=0;i<lp::site_count;++i){
            specs[i]=ep::SiteSpec{"fixture",0,{},0,0,0};kinds[i]=production_kinds[i];
            if(fns[i]){specs[i].address=reinterpret_cast<uintptr_t>(fns[i]);specs[i].length=lens[i];specs[i].ret_pop=pops[i];site_bytes(fns[i],specs[i].expected,lens[i]);}
        }
        // a wrong byte must fail closed for that site alone
        specs[6]=specs[0];specs[6].address=reinterpret_cast<uintptr_t>(probe_site_a)+64;
        const bool installed=lp::fixture_install(specs,lp::site_count,kinds,&size_globals[0]);
        check(installed,"probes_installed");
        check(!strcmp(lp::site(0).status,"active")&&!strcmp(lp::site(1).status,"active")&&!strcmp(lp::site(4).status,"active")&&!strcmp(lp::site(3).status,"active"),"probe_sites_active");
        check(!strcmp(lp::site(6).status,"bytes_mismatch"),"probe_wrong_bytes_fail_closed",lp::site(6).status);
        check(!strcmp(lp::site(2).status,"invalid_spec")||!strcmp(lp::site(2).status,"unreadable"),"probe_unspecified_site_not_patched",lp::site(2).status);
        check(lp::installed_sites()==4,"probe_installed_count");
        // behaviour unchanged, nested calls counted with inclusive time
        check(call_a(3)==4,"probe_a_result");
        light::ProbeRow row{};light::probe_take(0,row);
        check(row.calls==4&&row.exits==4&&row.inclusive>0&&row.overflow==0&&row.desync==0,"probe_a_nested_counts");
        check(call_b(5)==6,"probe_b_result_ret4");
        light::probe_take(1,row);check(row.calls==1&&row.exits==1&&row.desync==0,"probe_b_counts_ret4");
        check(row.extra[1]==1&&row.extra[0]==0&&row.extra[2]==0,"probe_b_open_classified_catalogue");
        // a garbage ESI (no object) must be classified without a dereference
        { int r;asm volatile("push %1\n\tcall _probe_site_b":"=a"(r):"r"(2),"S"(0xb):"ecx","edx","memory","cc");check(r==3,"probe_b_garbage_esi_result"); }
        light::probe_take(1,row);check(row.calls==1&&row.exits==1&&row.extra[0]+row.extra[1]+row.extra[2]==0,"probe_b_garbage_esi_unclassified");
        light::probe_take(0,row);check(row.calls==7&&row.exits==7,"probe_a_nested_under_b");   // 5 + 2 nested a() calls
        check(call_c()==7,"probe_c_result_count_only");
        light::probe_take(4,row);check(row.calls==1&&row.exits==0&&row.bytes==15&&row.extra[1]==1,"probe_c_dispatch_bytes_and_branch");
        // longjmp from depth 1 to the catcher at depth 3 skips the exits of a(2) and a(1):
        // their shadow entries lie below the catcher's return slot and are discarded
        // (desync counted) when a(3) returns normally; a(4) then returns as usual.
        catch_depth=3;escape_depth=1;
        check(call_a(4)==101,"probe_a_longjmp_catcher_returns");
        catch_depth=escape_depth=0;
        light::probe_take(0,row);   // a(4) a(3) a(2) a(1) entered; a(2) and a(1) never returned
        check(row.calls==4&&row.exits==2&&row.desync==2,"probe_desync_recovered");
        check(call_a(0)==1,"probe_a_after_longjmp");
        light::probe_take(0,row);check(row.calls==1&&row.exits==1&&row.desync==0,"probe_clean_after_recovery");
        // two threads
        HANDLE t=CreateThread(nullptr,0,probe_thread,nullptr,0,nullptr);
        for(int i=0;i<200;++i)call_a(3);
        if(t){WaitForSingleObject(t,INFINITE);CloseHandle(t);}
        light::probe_take(0,row);check(row.calls==1600&&row.exits==1600&&row.desync==0&&row.overflow==0,"probe_two_threads");
        printf("RR_CASE name=probes installed=%u\n",lp::installed_sites());
        // ---- case 5: the reader stub chained behind the probe on the reference site: verify mode ----
        unsigned char expected[6];site_bytes(reference_site,expected,6);
        check(rr::fixture_install(reinterpret_cast<uintptr_t>(reference_site),expected,6),"reader_stub_installed",rr::status());
        check(lp::resource_read_site()!=nullptr,"reader_chained_after_probe");
        rr::fixture_bind(environment(),rr::Mode::Verify);
        unsigned verify_files=0;
        for(const auto& s:sources){
            rr::FileObject o=loose_object(s);if(!o.file)continue;
            counters[0]=counters[1]=counters[2]=0;counters[3]=0;
            void* buf=call_site(reference_site,&o);
            check(buf&&same(buf,size_globals[0],s.payload),"verify_result_is_original",s.name.c_str());
            if(buf)env_free(buf);close_object(o);++verify_files;
        }
        for(const auto& rec:records){
            rr::FileObject o=record_object(rec);
            void* buf=call_site(reference_site,&o);
            check(buf&&same(buf,size_globals[0],sources[rec.index].payload),"verify_record_result_is_original",sources[rec.index].name.c_str());
            if(buf)env_free(buf);close_object(o);++verify_files;
        }
        rr::Statistics st=rr::statistics();
        const unsigned gz_sources=unsigned(sources.size())-1;
        check(st.calls==verify_files&&st.verify_files==2*gz_sources&&st.verify_equal==2*gz_sources&&st.verify_mismatched==0,"verify_parity");
        check(st.fallbacks==2&&st.reasons[unsigned(rr::Reason::NotGzip)]==2,"verify_text_fallbacks");
        light::probe_take(3,row);check(row.calls==verify_files&&row.exits==verify_files&&row.bytes>0&&row.extra[3]==0,"probe_resource_read_through_reader_stub");
        printf("RR_STATS mode=verify calls=%llu handled=%llu fallbacks=%llu verify_files=%llu verify_equal=%llu verify_mismatched=%llu bytes_in=%llu bytes_out=%llu our_us=%.1f original_us=%.1f\n",
            st.calls,st.handled,st.fallbacks,st.verify_files,st.verify_equal,st.verify_mismatched,st.bytes_in,st.bytes_out,us(st.ticks),us(st.original_ticks));
        // a deliberately different original (first byte flipped) must be reported as a mismatch and still be what the caller gets
        { reference_tamper=true;rr::FileObject o=record_object(records[1]);
          void* buf=call_site(reference_site,&o);
          check(buf&&size_globals[0]==sources[1].payload.size()&&static_cast<unsigned char*>(buf)[0]==(sources[1].payload[0]^1),"verify_mismatch_returns_original");
          if(buf)env_free(buf);close_object(o);reference_tamper=false;
          st=rr::statistics();check(st.verify_mismatched==1&&st.verify_equal==2*gz_sources,"verify_mismatch_counted"); }
        // the cursor class through the production comparison: cursor_ok and position_ok for every source and record
        unsigned cursor_verified=0;
        for(const auto& s:cursor_sources){
            rr::FileObject o=loose_object(s);if(!o.file)continue;
            void* buf=call_site(reference_site,&o);
            check(buf&&same(buf,size_globals[0],s.payload),"verify_cursor_result_is_original",s.name.c_str());
            check(ftell(static_cast<FILE*>(o.file))==original_final(s.stored),"verify_cursor_loose_position",s.name.c_str());
            if(buf)env_free(buf);close_object(o);++cursor_verified;
        }
        for(const auto& rec:cursor_records){
            rr::FileObject o=cursor_record_object(rec);
            void* buf=call_site(reference_site,&o);
            check(buf&&same(buf,size_globals[0],cursor_sources[rec.index].payload),"verify_cursor_record_result_is_original",cursor_sources[rec.index].name.c_str());
            check(o.cursor==original_final(cursor_sources[rec.index].stored),"verify_cursor_record_cursor",cursor_sources[rec.index].name.c_str());
            if(buf)env_free(buf);close_object(o);++cursor_verified;
        }
        st=rr::statistics();
        check(st.verify_mismatched==1&&st.verify_equal==2*gz_sources+cursor_verified&&st.cursor_short==cursor_class*2,"verify_cursor_class_equal");
        printf("RR_STATS_CURSOR verify_files=%u cursor_short=%llu verify_equal=%llu verify_mismatched=%llu\n",cursor_verified,st.cursor_short,st.verify_equal,st.verify_mismatched);
        // ---- fast mode through the stub: the reference never runs for gzip sources ----
        rr::fixture_bind(environment(),rr::Mode::Fast);
        for(const auto& s:sources){
            rr::FileObject o=loose_object(s);if(!o.file)continue;
            void* buf=call_site(reference_site,&o);
            check(buf&&same(buf,size_globals[0],s.payload),"fast_stub_result",s.name.c_str());
            if(buf)env_free(buf);close_object(o);
        }
        st=rr::statistics();
        check(st.handled==3*gz_sources+1+cursor_verified&&st.fallbacks==3&&st.calls==verify_files+1+cursor_verified+sources.size(),"fast_stub_statistics");
        // timing: fast core vs the reference (hot cache), the 3 MB file (inflate-bound) and the
        // 31 KB one (a typical script .pck, where the per-file fixed costs matter). The reference
        // here runs on msvcrt, not the game's locked static CRT, so its per-call costs are a floor.
        // Phases (from the core's own QPC stamps): fseek+fread, XOR+magic+header, malloc, inflate; the
        // remainder is HeapAlloc/HeapFree of the scratch and bookkeeping. large_record reads the 3 MB
        // file as a catalogue record (adds the XOR 0x33 pass over the extent).
        for(unsigned which=0;which<3;++which){
          const Source& src=sources[which==1?1:7];uint64_t fast_ticks=0,ref_ticks=0,read=0,scan=0,alloc=0,inflate=0;const unsigned rounds=which==1?(quick?50:400):(quick?2:5);
          const Record& rec=records[7];
          for(unsigned i=0;i<rounds;++i){
              rr::FileObject o=which==2?record_object(rec):loose_object(src);const uint64_t t0=qpc();const rr::Result r=rr::decode(environment(),&o,true);const uint64_t t1=qpc();if(r.buffer)env_free(r.buffer);close_object(o);fast_ticks+=t1-t0;
              read+=r.read_ticks;scan+=r.scan_ticks;alloc+=r.alloc_ticks;inflate+=r.inflate_ticks;
              rr::FileObject ref=which==2?record_object(rec):loose_object(src);const uint64_t t2=qpc();void* buf=reference_read(&ref);const uint64_t t3=qpc();if(buf)env_free(buf);close_object(ref);ref_ticks+=t3-t2;
          }
          const char* name=which==0?"large":which==1?"medium":"large_record";
          printf("RR_TIMING name=%s bytes=%zu rounds=%u fast_us=%.1f reference_us=%.1f ratio=%.2f\n",name,src.payload.size(),rounds,us(fast_ticks)/rounds,us(ref_ticks)/rounds,double(ref_ticks)/double(fast_ticks?fast_ticks:1));
          printf("RR_PHASES name=%s extent=%zu read_us=%.1f scan_us=%.1f alloc_us=%.1f inflate_us=%.1f total_us=%.1f\n",name,src.stored.size(),us(read)/rounds,us(scan)/rounds,us(alloc)/rounds,us(inflate)/rounds,us(fast_ticks)/rounds); }
        rr::fixture_shutdown();
        lp::fixture_shutdown();
        check(call_a(2)==3&&call_b(1)==2,"probe_sites_restored");
        light::probe_take(0,row);check(row.calls==0,"probe_restored_not_counting");
    }
    // ---- case 6: the .dat handle pool ----
    {
        static unsigned real_opens=0,real_closes=0;
        rr::PoolEnvironment env;
        env.fopen=[](const char* p,const char* m)->void*{++real_opens;return fopen(p,m);};
        env.fclose=[](void* f)->int{++real_closes;return fclose(static_cast<FILE*>(f));};
        env.flag_offset=0x0c;env.error_flag=0x20; // msvcrt FILE layout (same as the game's VC8 CRT)
        rr::fixture_pool_bind(env);
        void* a=rr::x3m_pool_fopen("rr_catalogue.dat","rb");check(a!=nullptr&&real_opens==1,"pool_first_open_real");
        void* b=rr::x3m_pool_fopen("rr_catalogue.dat","rb");check(b!=nullptr&&b!=a&&real_opens==2,"pool_concurrent_open_distinct");
        check(rr::x3m_pool_fclose(a)==0&&real_closes==0,"pool_close_keeps");
        void* c=rr::x3m_pool_fopen("rr_catalogue.dat","rb");check(c==a&&real_opens==2,"pool_reopen_reuses");
        // a reused handle reads the record correctly after the caller's fseek (as the open body does)
        { rr::FileObject o{};o.file=c;o.flags=3;o.offset=records[2].offset;o.length=records[2].length;fseek(static_cast<FILE*>(c),o.offset,SEEK_SET);
          void* buf=reference_read(&o);check(buf&&same(buf,size_globals[0],sources[2].payload),"pool_reused_handle_reads_record");if(buf)env_free(buf); }
        void* w=rr::x3m_pool_fopen("rr_small.pck","wb+");check(w!=nullptr,"pool_write_mode_passthrough");check(rr::x3m_pool_fclose(w)==0&&real_closes==1,"pool_write_mode_real_close");
        void* other=rr::x3m_pool_fopen("rr_medium.pck","rb");check(other!=nullptr,"pool_other_path");
        check(rr::x3m_pool_fclose(other)==0&&real_closes==1,"pool_other_path_kept");
        void* again=rr::x3m_pool_fopen("rr_medium.pck","rb");check(again==other,"pool_other_path_reused");
        // an errored stream is closed for real
        { FILE* f=static_cast<FILE*>(again);*reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(f)+0x0c)|=0x20;check(rr::x3m_pool_fclose(again)==0&&real_closes==2,"pool_error_stream_closed_real"); }
        void* miss=rr::x3m_pool_fopen("rr_does_not_exist.dat","rb");check(miss==nullptr,"pool_missing_file_null");
        rr::x3m_pool_fclose(b);rr::x3m_pool_fclose(c);
        const rr::PoolStatistics ps=rr::pool_statistics();
        check(ps.held==2&&ps.reused==2&&ps.kept==4&&ps.errors==1,"pool_statistics");
        rr::pool_drain();
        check(rr::pool_statistics().held==0&&real_closes==4,"pool_drain_closes_all");
        // the game's sequence on a pooled handle: open, read a class record (fast), close (kept), reopen (reused),
        // fseek to the next record, read it -- against the reference on its own pooled handle
        {
            const Record& a=cursor_records[cursor_records.size()-2];const Record& b=cursor_records.back(); // b: the class record that ends the .dat
            void* fast_handle=rr::x3m_pool_fopen("rr_cursor.dat","rb");void* ref_handle=rr::x3m_pool_fopen("rr_cursor.dat","rb");
            check(fast_handle&&ref_handle&&fast_handle!=ref_handle&&real_opens==7,"pool_cursor_sequence_opens");
            auto object=[](void* f,const Record& r){ rr::FileObject o{};o.file=f;o.flags=3;o.offset=r.offset;o.length=r.length;o.cursor=0;fseek(static_cast<FILE*>(f),r.offset,SEEK_SET);return o; };
            for(const Record* rec:{&b,&a,&b}){
                rr::FileObject fo=object(fast_handle,*rec),ro=object(ref_handle,*rec);
                void* expect=reference_read(&ro);const uint32_t expect_size=size_globals[0];
                std::vector<unsigned char> expected(expect?static_cast<unsigned char*>(expect):nullptr,expect?static_cast<unsigned char*>(expect)+expect_size:nullptr);
                const Outcome out=run_fast(fo,expected,cursor_sources[rec->index].name.c_str(),true,ftell(static_cast<FILE*>(ref_handle)),ro.cursor);
                check(out.handled&&same(expected.data(),expected.size(),cursor_sources[rec->index].payload),"pool_cursor_sequence_read",cursor_sources[rec->index].name.c_str());
                if(expect)env_free(expect);
                check(rr::x3m_pool_fclose(fast_handle)==0&&rr::x3m_pool_fclose(ref_handle)==0,"pool_cursor_sequence_close_kept");
                void* f2=rr::x3m_pool_fopen("rr_cursor.dat","rb");void* r2=rr::x3m_pool_fopen("rr_cursor.dat","rb");
                check((f2==fast_handle&&r2==ref_handle)||(f2==ref_handle&&r2==fast_handle),"pool_cursor_sequence_reused");
                fast_handle=f2;ref_handle=r2;
            }
            rr::x3m_pool_fclose(fast_handle);rr::x3m_pool_fclose(ref_handle);
            rr::pool_drain();check(rr::pool_statistics().held==0&&real_closes==6,"pool_cursor_sequence_drained");
        }
        void* unknown=fopen("rr_small.pck","rb");check(rr::x3m_pool_fclose(unknown)==0&&real_closes==7,"pool_unknown_handle_real_close");
        printf("RR_CASE name=pool opens=%llu reused=%llu real_opens=%llu kept=%llu real_closes=%llu\n",ps.opens,ps.reused,ps.real_opens,ps.kept,ps.real_closes);
    }
    for(const char* name:{"rr_tmp.gz","rr_catalogue.dat","rr_bad_cm.pck","rr_bad_flg.pck","rr_short.pck","rr_corrupt.pck","rr_isize.pck","rr_trunc.pck","rr_empty.pck","rr_noname.gz"})remove(name);
    for(const auto& s:sources)remove(s.name.c_str());
    remove("rr_cursor.dat");for(const auto& s:cursor_sources)remove(s.name.c_str());
    printf("RESOURCE READER RESULT checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
