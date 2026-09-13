#include "../../src/proxy/chase_transition_core.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace x3m::chase_transition;
using namespace x3m::chase_transition::detail;
static unsigned checks=0;
static void check(bool yes,const char* why){++checks;if(!yes){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static void lifetimes(){
 State s;
 check(!s.generation(0x1000),"unknown refuses");
 check(!s.begin(0x1000,0x8000,1).serial,"unknown update refuses");
 const auto gen=s.construct(0x1000,1);
 check(gen&&!s.generation(0x1000),"partial cannot witness");
 check(!s.complete(0x1000,2),"completion must use constructor thread");
 check(s.complete(0x1000,1)&&s.generation(0x1000)==gen,"successful constructor witnesses");
 check(!s.complete(0x1000,1),"duplicate completion refuses");
 auto one=s.begin(0x1000,0x8000,1);
 check(one.serial&&one.generation==gen&&one.thread==1,"entry token");
 check(!s.current(0x1000,2).serial&&!s.current(0x1004,1).serial,"token exact cockpit/thread");
 s.end(0x7000,1);check(s.current(0x1000,1).serial==one.serial,"unrelated exit does not retire");
 auto two=s.begin(0x1000,0x7000,1);check(two.serial>one.serial,"nested entry new serial");
 s.end(0x8000,1);check(s.current(0x1000,1).serial==two.serial,"outer exit never clears nested frame");
 s.end(0x7000,1);check(!s.current(0x1000,1).serial,"nested exit does not restore outer");
 s.begin(0x1000,0x8000,1);s.begin(0,0x7000,1);check(!s.current(0x1000,1).serial,"invalid nested entry revokes prior");
 s.begin(0x1000,0x8000,1);s.destroy(0x1000);check(!s.current(0x1000,1).serial&&!s.generation(0x1000),"destructor retires token immediately");
 auto reused=s.construct(0x1000,1);s.complete(0x1000,1);check(reused>gen,"same address gets new generation");
 s.begin(0x1000,0x8000,1);s.construct(0x1000,1);check(!s.current(0x1000,1).serial&&!s.generation(0x1000),"new constructor revokes even missed destructor");
 check(!s.construct(0x1001,1)&&!s.construct(0,1)&&!s.construct(0x2000,0),"invalid identities refuse");
 State full;
 for(unsigned i=0;i<lifetime_capacity;++i){check(full.construct(0x1000+4*i,1)!=0,"fill lifetime");full.complete(0x1000+4*i,1);}
 check(!full.construct(0x2000,1)&&full.lifetime_overflow==1,"lifetime capacity refuses without eviction");
 check(full.generation(0x1000)!=0,"overflow preserves existing witness");full.destroy(0x1000);check(full.construct(0x2000,1)!=0,"destructor frees capacity");
 for(unsigned i=0;i<thread_capacity;++i)check(full.begin(0x1004,0x8000+i*4,i+1).serial!=0,"fill thread");
 check(!full.begin(0x1004,0x9000,100).serial&&full.thread_overflow==1,"active threads cannot be evicted");
 full.end(0x8000,1);check(full.begin(0x1004,0x9000,100).serial!=0,"inactive thread capacity reusable");
 State exhausted;exhausted.next_generation=UINT64_MAX;check(!exhausted.construct(0x1000,1),"generation never wraps");
 exhausted.next_generation=0;exhausted.construct(0x1000,1);exhausted.complete(0x1000,1);exhausted.next_serial=UINT64_MAX;
 check(!exhausted.begin(0x1000,0x8000,1).serial,"serial never wraps");
}
static void windows(){
 Window<unsigned,2,3> w;for(unsigned i=1;i<=8;++i)w.push(i);
 check(w.first_used==2&&w.first[0]==1&&w.first[1]==2,"first evidence retained");
 check(w.last_used==3&&w.dropped==3,"exact middle drops");
 for(unsigned i=0;i<3;++i)check(w.last[(w.next+i)%3]==6+i,"last evidence ordered");
}
struct Memory {
 std::array<unsigned char,0x20000> data{};
 unsigned reads=0;std::uintptr_t refused=0;
 bool read(std::uintptr_t base,unsigned offset,void* out,unsigned n){++reads;const auto at=base+offset;
  if(at<base||at==refused||at<0x1000||at>data.size()||n>data.size()-at)return false;
  std::memcpy(out,data.data()+at,n);return true;
 }
 template<class T>void put(unsigned at,T value){std::memcpy(data.data()+at,&value,sizeof value);}
 void init(){
  data.fill(0);reads=0;refused=0;
  // VM global is aliased in the reader; everything else is deterministic memory.
  put(0x2004,0x4a3909u);put(0x200c,0x3000u);put(0x2010,0x30u);
  put(0x4008,0x10000u);put(0x4048,0x42d340u); // group1 ->(1+2)*24
  put(0x301c,0x105u);put(0x303c,0x5000u);put(0x5000,0x200u);
  data[0x10100]=0x82;data[0x10101]=1;data[0x10103]=0x30;
  put(0x3014,0x8000u);put(0x3010,128u);put(0x3018,std::int32_t(-4));
  data[0x8000-20]=10;put(0x8000-19,0x5000u);data[0x8000-15]=3;put(0x8000-14,0x300u);
 }
 Origin capture(){Origin o;
  auto reader=[&](std::uintptr_t b,unsigned off,void* out,unsigned n){if(b==0x6085e4&&off==0&&n==4){std::uint32_t vm=0x4000;std::memcpy(out,&vm,4);return true;}return read(b,off,out,n);};
  auto range=[](std::uint32_t code,std::uint32_t off,unsigned n,std::uintptr_t& at){if(code!=0x10000||off>0xffff||n>0x10000-off)return false;at=code+off;return true;};
  provenance(0x2000,o,reader,range);return o;
 }
};
static void origins(){
 Memory m;m.init();auto o=m.capture();check(o.valid==15&&!o.flags&&o.count==1&&o.contexts[0]==0x5000&&o.returns[0]==0x300,"exact native-call origin and raw context-return candidates");
 m.put(0x2004,0x1234u);check(!m.capture().valid,"non VM native caller refuses");m.init();
 m.put(0x301c,4u);check(m.capture().valid==1&&m.capture().flags==1,"PC underflow refuses");m.init();
 m.data[0x10100]=0x83;check(m.capture().valid==1,"VM return opcode refuses native provenance");m.init();
 m.data[0x10103]=0x31;check(m.capture().valid==1,"wrong command refuses");m.init();
 m.put(0x4048,0x1234u);check(m.capture().valid==1,"wrong native group handler refuses");m.init();
 m.put(0x3018,std::int32_t(1));check(!(m.capture().valid&8),"positive stack index refuses");m.init();
 m.put(0x3018,std::int32_t(-129));check(!(m.capture().valid&8),"stack outside capacity refuses");m.init();
 m.put(0x3018,INT32_MIN);check(!(m.capture().valid&8),"INT_MIN arithmetic refuses");m.init();
 m.put(0x3018,std::int32_t(-128));auto big=m.capture();check((big.valid&8)&&(big.flags&2),"bounded stack marks truncation");
 m.init();m.refused=0x8000-20;check(!(m.capture().valid&8)&&m.capture().flags,"unreadable stack explicit");m.refused=0;
 m.init();m.put(0x3018,std::int32_t(-10));for(unsigned i=0;i<5;++i){unsigned a=0x8000-50+i*10;m.data[a]=10;m.put(a+1,0x5000u);m.data[a+5]=3;m.put(a+6,0x300u);}
 auto many=m.capture();check(many.count==4&&(many.flags&4),"context-return candidates cap four and report excess");
 m.init();m.put(0x5000,0x10000u);auto raw=m.capture();
 check((raw.valid&4)&&raw.context_word0==0x10000u&&raw.count==1,"context word is raw data, not a CODE offset");
 m.init();m.put(0x8000-19,0u);auto null_context=m.capture();
 check(null_context.count==1&&!null_context.flags&&null_context.contexts[0]==0,"native null saved context is valid");
 m.init();m.put(0x8000-14,0x10000u);check(!m.capture().count&&(m.capture().flags&1),"return offset must remain in CODE");
 m.init();m.put(0x8000-19,0x20000u);check(!m.capture().count&&(m.capture().flags&1),"unreadable nonnull saved context refuses");
 m.init();m.put(0x303c,0x20000u);check(!(m.capture().valid&4)&&(m.capture().flags&1),"unreadable current context refuses");
}
static void handler_timings(){
 HandlerTiming t;
 t.add(100,130);t.add(200,210);t.add(400,400);
 check(t.calls==3&&t.samples==3&&t.ticks==40&&t.max_ticks==30&&!t.invalid,"handler aggregates total/max including genuine zero elapsed");
 t.add(0,500);t.add(600,0);t.add(800,700);
 check(t.calls==6&&t.samples==3&&t.invalid==3&&t.ticks==40&&t.max_ticks==30,"failed or backwards QPC cannot appear as zero-cost success");
 t.add(0x100000000ull,0x300000001ull);
 check(t.samples==4&&t.ticks==0x200000029ull&&t.max_ticks==0x200000001ull,"handler counters retain full 64-bit durations");
 t={};check(!t.calls&&!t.samples&&!t.invalid&&!t.ticks&&!t.max_ticks,"report reset clears complete timing window");
}
int main(){lifetimes();windows();origins();handler_timings();std::printf("chase transition host: %u checks PASS\n",checks);}
