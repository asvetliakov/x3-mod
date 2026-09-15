// Portable restore-ticket core (seam decode, consume proof, bounded live
// stack, transfer proof) on controlled memory. The X3 CPU fixture drives the
// same templates through the actual emitted stubs; this host run needs no Wine.
#include "../../src/proxy/chase_transition_restore_core.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace x3m::chase_transition::detail;
static unsigned checks=0;
static void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
struct Memory {
 std::array<unsigned char,0x120000> data{};
 std::uint32_t refused=0;
 template<class T>void put(unsigned at,T v){std::memcpy(data.data()+at,&v,sizeof v);}
 void cell(unsigned at,std::uint32_t value,unsigned char tag=1){data[at]=tag;put(at+1,value);}
 bool read(std::uintptr_t base,unsigned off,void* out,unsigned n){
  const auto at=std::uint64_t(base)+off;
  if(at==refused||at<0x1000||at>=data.size()||n>data.size()-at)return false;
  std::memcpy(out,data.data()+at,n);return true;
 }
 bool range(std::uint32_t code,std::uint32_t off,unsigned n,std::uintptr_t& at){if(code!=0x20000||off>0xfffff||n>0x100000-off)return false;at=code+off;return true;}
 // vm 0x2000, classes 0x3000 (global 0, warp 0x96, monitor 0x25e), code 0x20000, task 0x5000, stack base 0x5400, monitor context 0x6000, frame 0x7000.
 void init(){
  data.fill(0);refused=0;put(0x1000,0x2000u);put(0x2008,0x20000u);put(0x201c,3u);put(0x2020,0x3000u);
  const std::uint32_t ids[3]={0,0x96,0x25e},cells[3]={0x3100,0x3200,0};
  for(unsigned i=0;i<3;++i){put(0x3000+i*0x38,ids[i]);put(0x3008+i*0x38,0x3000u+i*0x38);put(0x300c+i*0x38,cells[i]);put(0x301c+i*0x38,12u);}
  cell(0x3100+45,0xfffefa78u);cell(0x3100+40,0xffff6eafu);cell(0x3200+15,1);cell(0x3200+30,0);
  put(0x6000,0xffff6eaeu);put(0x6008,0x3070u);put(0x600c,0x6100u);cell(0x6100,258);cell(0x6105,0);cell(0x6100+55,0xfffefa78u);
  put(0x5008,0x1234u);put(0x5010,128u);put(0x5014,0x5400u);put(0x503c,0x6000u);put(0x7008,0x5000u);
  put(0x7118,0x2000u);put(0x7120,0x6000u); // seam frame: [esp+18]=vm, [esp+20]=context
  const unsigned char store[4]={0x94,0,0,0x24};std::memcpy(data.data()+0x20000+0xf0c4b,store,4);
  cell(0x5400-35,1);const std::uint32_t ctx[3]={0x6200,0x6200,0x6200};
  for(unsigned i=0;i<3;++i){cell(0x5400-30+i*10,ctx[i],10);cell(0x5400-25+i*10,restore_reset_prefix[i],3);}
 }
 SeamRegs regs(){SeamRegs s;s.eax=0x6000;s.ebx=0x5400-35;s.esi=0;s.edi=0x20000+0xf0c4b+1;s.ebp=0x7000;s.esp=0x7100;s.thread=7;return s;}
 RestoreState pending(){RestoreState st;st.arm_player=0xfffefa78u;st.arm_controller=0xffff6eafu;st.transfer(0xffff6eaeu,0x5000,0x1234,7,1);return st;}
 unsigned proof(){auto r=[&](std::uintptr_t b,unsigned o,void* out,unsigned n){return read(b,o,out,n);};auto cr=[&](std::uint32_t c,std::uint32_t o,unsigned n,std::uintptr_t& at){return range(c,o,n,at);};
  SeamDecode d;if(!seam_decode(regs(),r,cr,0x1000,d))return 99u;return seam_consume_proof(pending(),regs(),d,1,r,cr,0x1000);}
};
int main(){
 Memory m;m.init();check(m.proof()==refuse_none,"complete proof admits the single write");
 m.init();m.put(0x1000,0x2004u);check(m.proof()==99,"VM root mismatch refuses at decode");
 m.init();m.data[0x20000+0xf0c4b]=0x16;check(m.proof()==99,"unoptimized opcode at the PC refuses at decode");
 m.init();m.data[0x20000+0xf0c4b+3]=0x25;check(m.proof()==99,"wrong discard byte refuses at decode");
 m.init();auto s=m.regs();s.esi=5;{auto r=[&](std::uintptr_t b,unsigned o,void* out,unsigned n){return m.read(b,o,out,n);};auto cr=[&](std::uint32_t c,std::uint32_t o,unsigned n,std::uintptr_t& at){return m.range(c,o,n,at);};SeamDecode d;seam_decode(s,r,cr,0x1000,d);
  check(seam_consume_proof(m.pending(),s,d,1,r,cr,0x1000)==refuse_opcode,"destination index other than variable0 refuses");
  check(seam_consume_proof(m.pending(),m.regs(),d,2,r,cr,0x1000)==refuse_epoch,"stale epoch refuses first");
  auto other=m.regs();other.thread=8;check(seam_consume_proof(m.pending(),other,d,1,r,cr,0x1000)==refuse_thread,"other thread refuses");
  m.put(0x7008,0x5040u);seam_decode(m.regs(),r,cr,0x1000,d);check(seam_consume_proof(m.pending(),m.regs(),d,1,r,cr,0x1000)==refuse_task,"other task refuses");m.put(0x7008,0x5000u);}
 m.init();m.put(0x5008,0x1235u);check(m.proof()==refuse_task_id,"reused task address with new ID refuses");
 m.init();m.put(0x503c,0x6100u);check(m.proof()==refuse_context,"task/context incoherence refuses");
 m.init();m.put(0x6008,0u);check(m.proof()==refuse_monitor_class,"dead class pointer refuses");
 m.init();m.put(0x6000,0xffff6eadu);check(m.proof()==refuse_monitor_identity,"different current monitor refuses");
 m.init();m.cell(0x6100,1);check(m.proof()==refuse_mode_cell,"persistent mode 1 refuses");
 m.init();m.cell(0x6105,0x77);check(m.proof()==refuse_handle_cell,"live native handle refuses");
 m.init();m.cell(0x6100+55,0xfffefa79u);check(m.proof()==refuse_ref_cell,"monitor ref not player refuses");
 m.init();m.cell(0x3100+45,0xfffefa79u);check(m.proof()==refuse_globals,"changed global player refuses");
 m.init();m.cell(0x3200+15,0);check(m.proof()==refuse_warp,"warp 0 refuses");
 m.init();m.cell(0x3200+30,1);check(m.proof()==refuse_warp,"killed refuses");
 m.init();m.cell(0x5400-35,1,2);check(m.proof()==refuse_source,"string-tagged source refuses");
 m.init();m.cell(0x5400-35,258);check(m.proof()==refuse_source,"same-valued request refuses");
 m.init();m.put(0x5400-25+1,0xedc90u);check(m.proof()==refuse_stack,"wrong SelectMode caller refuses");
 m.init();m.cell(0x5400-45,1);m.cell(0x5400-40,0x6200,10);m.cell(0x5400-35,0xedc91,3);
 {auto r=[&](std::uintptr_t b,unsigned o,void* out,unsigned n){return m.read(b,o,out,n);};auto cr=[&](std::uint32_t c,std::uint32_t o,unsigned n,std::uintptr_t& at){return m.range(c,o,n,at);};
  auto deep=m.regs();deep.ebx=0x5400-45;SeamDecode d;check(seam_decode(deep,r,cr,0x1000,d)&&seam_consume_proof(m.pending(),deep,d,1,r,cr,0x1000)==refuse_stack,"extra live frame pair refuses");}
 m.init();m.put(0x5010,6u);check(m.proof()==refuse_stack,"stack beyond capacity refuses");
 m.init();m.refused=0x6200;check(m.proof()==refuse_stack,"unreadable frame context refuses");
 {Memory big;big.init();auto r=[&](std::uintptr_t b,unsigned o,void* out,unsigned n){return big.read(b,o,out,n);};auto cr=[&](std::uint32_t c,std::uint32_t o,unsigned n,std::uintptr_t& at){return big.range(c,o,n,at);};
  check(!live_stack_prefix(0x5000,0x5400-65*5,0x20000,r,cr,restore_reset_prefix,3),"65 live cells is truncated proof");
  check(!live_stack_prefix(0x5000,0x5400-36,0x20000,r,cr,restore_reset_prefix,3),"five-byte misalignment refuses");
  check(!live_stack_prefix(0x5000,0x5401,0x20000,r,cr,restore_reset_prefix,3),"EBX above base refuses");}
 RestoreState st;check(st.filter_mode()==0,"idle filter");st.armed=true;check(st.filter_mode()==1,"armed filter");st.transfer(1,2,3,4,5);check(!st.armed&&st.pending&&st.filter_mode()==2&&st.transfers==1,"transfer moves arm to pending");
 st.clear_pending(cancel_task_complete);check(!st.pending&&st.cancels[cancel_task_complete]==1&&st.filter_mode()==0,"clear counts the reason once");st.clear_pending(cancel_task_complete);check(st.cancels[cancel_task_complete]==1,"idle clear counts nothing");
 std::printf("chase restore host: %u checks PASS\n",checks);
}
