// Actual transition handle() extracted by its paired Python test. Platform
// reads/thread/locks are controlled; both production lifetime/timing cores run.
#include "chase_transition_core.h"
#include "chase_native_timing_core.h"
#include <atomic>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void check(bool okay,const char* why){++checks;if(!okay){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static constexpr std::uint32_t owner_thread=11,other_thread=22,unrelated_thread=33;
static constexpr std::uint32_t own_cockpit=0x1000,decoded_cockpit=0x2000,unrelated_cockpit=0x3000;
namespace x3m::chase_lead {
native_timing::State native;
void native_timing_invalidate(std::uintptr_t cockpit,std::uint32_t thread) noexcept {native.revoke(std::uint32_t(cockpit),thread);}
}
static bool pending(std::uint32_t thread){
 for(const auto& slot:x3m::chase_lead::native.slots)if(slot.thread==thread)
  for(const auto& start:slot.starts)if(start.stamp)return true;
 return false;
}
namespace x3m::chase_transition {
namespace {
constexpr unsigned mandatory=6,site_count=9;
std::atomic<bool> enabled{true},diagnostic{true};
bool timing=false,read_ok=false;
unsigned argument_reads=0;
detail::State state;
detail::HandlerTiming handler_timing[site_count]{};
struct Guard { Guard(){}~Guard(){} };
std::uint64_t now(){return 100;}
std::uint32_t GetCurrentThreadId(){return owner_thread;}
bool field(std::uintptr_t,unsigned offset,std::uint32_t& out){
 if(offset==4){
  ++argument_reads;
  check(!pending(owner_thread),"callback thread revoked before attempted argument read");
  check(pending(other_thread),"decoded owner not assumed before reading argument");
  if(!read_ok)return false;
  out=decoded_cockpit;return true;
 }
 out=0x12345678;return true;
}
void record(unsigned,std::uintptr_t,std::uint32_t,std::uint32_t=0,std::uint32_t=0,std::uint32_t=0){}
#include "chase_transition_handle_under_test_inc.h"
}
}
static void setup(){
 using namespace x3m::chase_transition;
 state={};argument_reads=0;x3m::chase_lead::native={};
 const std::uint32_t threads[]={owner_thread,other_thread,unrelated_thread};
 const std::uint32_t cockpits[]={own_cockpit,decoded_cockpit,unrelated_cockpit};
 for(unsigned i=0;i<3;++i){
  auto g=state.construct(cockpits[i],threads[i]);state.complete(cockpits[i],threads[i]);
  x3m::chase_lead::native.begin({g,i+1,threads[i],cockpits[i],258,0},0,0x8000+i*4,100);
 }
}
int main(){
 using namespace x3m::chase_transition;
 std::uint32_t regs[9]{};regs[3]=0x8000;regs[2]=0x8000;
 for(unsigned kind:{0u,2u}){
  setup();read_ok=false;auto previous=state.generation(own_cockpit);
  handle(kind,regs);
  check(argument_reads==1,"failed argument read attempted once");
  check(!pending(owner_thread)&&pending(other_thread)&&pending(unrelated_thread),"failed read revokes only callback thread");
  check(x3m::chase_lead::native.window.abandoned==1,"failed-read span counted abandoned");
  check(state.generation(own_cockpit)==previous,"unreadable identity does not fabricate lifetime mutation");
  setup();read_ok=true;handle(kind,regs);
  check(argument_reads==1,"successful argument read attempted once");
  check(!pending(owner_thread)&&!pending(other_thread)&&pending(unrelated_thread),"decoded cockpit revokes other thread without touching unrelated owner");
  check(x3m::chase_lead::native.window.abandoned==2,"thread and cockpit spans both counted abandoned");
  check(!state.generation(decoded_cockpit),"decoded constructor/destructor invalidates completed lifetime");
 }
 // Existing updater boundaries continue to invalidate unconditionally.
 for(unsigned kind:{3u,4u,5u}){
  setup();read_ok=false;handle(kind,regs);
  check(!pending(owner_thread)&&pending(other_thread),"update entry and both exits retire only current thread spans");
 }
 std::printf("chase_transition_handle_host scenarios=7 checks=%u failures=0\n",checks);
}
