// Portable restore-ticket core (seam decode, consume proof, bounded live
// stack, transfer proof) on controlled memory. The X3 CPU fixture drives the
// same templates through the actual emitted stubs; this host run needs no Wine.
#include "../../src/proxy/chase_transition_restore_core.h"
#include <array>
#include <vector>
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
  for(unsigned i=0;i<3;++i){put(0x3000+i*0x38,ids[i]);put(0x3008+i*0x38,0x3000u+i*0x38);put(0x300c+i*0x38,cells[i]);put(0x301c+i*0x38,i==2?41u:12u);}
  cell(0x3100+45,0xfffefa78u);cell(0x3100+40,0xffff6eafu);cell(0x3200+15,1);cell(0x3200+30,0);
  put(0x6000,0xffff6eaeu);put(0x6008,0x3070u);put(0x600c,0x6100u);cell(0x6100,258);cell(0x6105,0);cell(0x6100+55,20);cell(0x6100+80,0);cell(0x6100+85,0xfffefa78u);
  put(0x5008,0x1234u);put(0x5010,128u);put(0x5014,0x5400u);put(0x503c,0x6000u);put(0x7008,0x5000u);
  put(0x7118,0x2000u);put(0x7120,0x6000u); // seam frame: [esp+18]=vm, [esp+20]=context
  const unsigned char store[4]={0x94,0,0,0x24};std::memcpy(data.data()+0x20000+0xf0c4b,store,4);
  cell(0x5400-35,1);const std::uint32_t ctx[3]={0x6200,0x6200,0x6200};
  for(unsigned i=0;i<3;++i){cell(0x5400-30+i*10,ctx[i],10);cell(0x5400-25+i*10,restore_reset_prefix[i],3);}
 }
 SeamRegs regs(){SeamRegs s;s.eax=0x6000;s.ebx=0x5400-35;s.esi=0;s.edi=0x20000+0xf0c4b+1;s.ebp=0x7000;s.esp=0x7100;s.thread=7;return s;}
 RestoreState pending(unsigned path=path_jump){RestoreState st;st.arm_player=0xfffefa78u;st.arm_controller=0xffff6eafu;st.transfer(0xffff6eaeu,0x5000,0x1234,7,1,path);return st;}
 unsigned proof(){auto r=[&](std::uintptr_t b,unsigned o,void* out,unsigned n){return read(b,o,out,n);};auto cr=[&](std::uint32_t c,std::uint32_t o,unsigned n,std::uintptr_t& at){return range(c,o,n,at);};
  SeamDecode d;if(!seam_decode(regs(),r,cr,0x1000,d))return 99u;return seam_consume_proof(pending(),regs(),d,1,r,cr,0x1000);}
};
// Verbatim pre-change reference (47d5e3d9 chase_transition_restore_core.h, jump-only
// seam_consume_proof and transfer_proof, unmodified bodies in their own namespace):
// the option-off equivalence below drives the same event sequences through both.
namespace legacy {
template<class Reader,class CodeRange>
unsigned seam_consume_proof(const RestoreState& st,const SeamRegs& s,const SeamDecode& d,std::uint32_t epoch,
                            Reader bytes,CodeRange code_address,std::uint32_t vm_root) {
    auto field=[&](std::uintptr_t base,unsigned offset,auto& out){return bytes(base,offset,&out,sizeof out);};
    if(st.pending_epoch!=epoch)return refuse_epoch;
    if(s.thread!=st.pending_thread)return refuse_thread;
    if(d.task!=st.pending_task)return refuse_task;
    std::uint32_t task_id=0,context=0,task_context=0;
    if(!field(d.task,8,task_id)||task_id!=st.pending_task_id)return refuse_task_id;
    if(!field(s.esp,0x20,context)||!context||context!=s.eax||!field(d.task,0x3c,task_context)||task_context!=context)return refuse_context;
    if(d.opcode!=0x94||d.index!=0||s.esi!=0||d.pc!=restore_mode_pc)return refuse_opcode;
    IdentityReader<Reader> r{bytes};r.vm_root=vm_root;
    if(!r.root()||r.vm!=d.vm)return refuse_globals;
    std::uint32_t monitor=0,desc[14]{};
    if(!borrowed_context(r,context,restore_monitor_class,monitor,desc))return refuse_monitor_class;
    if(monitor!=st.pending_monitor)return refuse_monitor_identity;
    std::uint32_t mode=0,handle=0,ref=0;
    if(!integer_cell(r,context,desc,0,mode)||mode!=restore_rear_mode)return refuse_mode_cell;
    if(!integer_cell(r,context,desc,1,handle)||handle!=0)return refuse_handle_cell;
    std::uint32_t player=0,controller=0,warp=0,killed=0;
    if(!global_scalars(r,player,controller,warp,killed)||player!=st.arm_player||controller!=st.arm_controller||!player)return refuse_globals;
    if(warp!=1||killed!=0)return refuse_warp;
    // run65: cell11 is a camera priority (20), never the player. cell16 is the
    // main-monitor number (0 from Create); cell17 is the player ref bound by
    // StartMainMonitor from the same global cell9 validated above.
    std::uint32_t number=0,ref_tag=0,g9_tag=0;
    if(!integer_cell(r,context,desc,16,number)||number!=0)return refuse_monitor_number;
    if(!global9_tag(r,g9_tag)||!raw_cell(r,context,desc,17,ref_tag,ref)||!ref_tag_ok(ref_tag,g9_tag))return refuse_ref_tag;
    if(ref!=player)return refuse_ref_cell;
    unsigned char source[5]{};std::uint32_t requested=0;
    if(!bytes(s.ebx,0,source,5))return refuse_source;
    std::memcpy(&requested,source+1,4);
    if(source[0]!=1||requested!=1)return refuse_source;
    if(!live_stack_prefix(d.task,s.ebx,d.code,bytes,code_address,restore_reset_prefix,3))return refuse_stack;
    return refuse_none;
}
template<class Reader>
unsigned transfer_proof(const RestoreState& st,const Origin& o,Reader bytes,std::uint32_t vm_root,
                        std::uint32_t& monitor,std::uint32_t& task_id) {
    auto field=[&](std::uintptr_t base,unsigned offset,auto& out){return bytes(base,offset,&out,sizeof out);};
    if(o.valid!=15||o.flags||o.count!=5)return refuse_provenance;
    for(unsigned i=0;i<5;++i)if(o.returns[i]!=restore_warp_prefix[i]||!o.contexts[i])return refuse_prefix;
    if(!o.task||(o.task&3)||!field(o.task,8,task_id))return refuse_task_id;
    IdentityReader<Reader> r{bytes};r.vm_root=vm_root;std::uint32_t desc[14]{};
    if(!r.root())return refuse_globals;
    if(!borrowed_context(r,o.context,restore_monitor_class,monitor,desc))return refuse_monitor_class;
    std::uint32_t player=0,controller=0,warp=0,killed=0;
    if(!global_scalars(r,player,controller,warp,killed)||player!=st.arm_player||controller!=st.arm_controller)return refuse_identity;
    if(warp!=1||killed!=0)return refuse_warp;
    return refuse_none;
}
}
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
 m.init();m.cell(0x6100+85,0xfffefa79u);check(m.proof()==refuse_ref_cell,"cell17 ref not player refuses");
 m.init();m.cell(0x6100+85,0xfffefa78u,2);check(m.proof()==refuse_ref_tag,"cell17 string tag refuses under its own code");
 m.init();m.cell(0x6100+85,0xfffefa78u,9);check(m.proof()==refuse_ref_tag,"cell17 tag differing from global cell9 refuses");
 m.init();m.cell(0x6100+80,2);check(m.proof()==refuse_monitor_number,"cell16 side monitor refuses");
 m.init();m.put(0x301c+2*0x38,17u);check(m.proof()==refuse_ref_tag,"cell17 beyond the class variable count refuses");
 m.init();m.cell(0x6100+55,0xfffefa78u);m.cell(0x6100+85,0xfffefa79u);check(m.proof()==refuse_ref_cell,"cell11 priority is never consulted");
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
 RestoreState st;check(st.filter_mode()==0,"idle filter");st.armed=true;check(st.filter_mode()==1,"armed filter");st.transfer(1,2,3,4,5,path_jump);check(!st.armed&&st.pending&&st.filter_mode()==2&&st.transfers==1,"transfer moves arm to pending");
 st.clear_pending(cancel_task_complete);check(!st.pending&&st.cancels[cancel_task_complete]==1&&st.filter_mode()==0,"clear counts the reason once");st.clear_pending(cancel_task_complete);check(st.cancels[cancel_task_complete]==1,"idle clear counts nothing");
 // ---- dock path (X3M_CHASE_VIEW_RESTORE_DOCK, docs/reverse-engineering/chase-view-docking.md) ----
 {
  Memory d;auto r=[&](std::uintptr_t b,unsigned o,void* out,unsigned n){return d.read(b,o,out,n);};
  auto cr=[&](std::uint32_t c,std::uint32_t o,unsigned n,std::uintptr_t& at){return d.range(c,o,n,at);};
  const std::uint32_t jump_chain[6]={0xefbff,0xedba0,0xedbe3,0x1661c,0,0};
  const std::uint32_t dock_chain[6]={0xefbff,0xedba0,0xedbe3,0x9be97,0x9bcd0,0}; // run315 event 52 (measured)
  auto origin=[](const std::uint32_t* chain,unsigned count,std::uint32_t flags=0){Origin o;o.valid=15;o.flags=flags;o.task=0x5000;o.context=0x6000;o.count=count;
   for(unsigned i=0;i<count&&i<6;++i){o.returns[i]=chain[i];o.contexts[i]=0x6200+i*0x10;}return o;};
  auto armed=[](bool dock){RestoreState st;st.dock=dock;st.armed=true;st.arm_cockpit=0x9000;st.arm_generation=3;st.arm_player=0xfffefa78u;st.arm_controller=0xffff6eafu;st.attempt_mode=258;st.retries=2;return st;};
  auto destroy=[&](RestoreState& st,const Origin& src,unsigned& why,unsigned& path,std::uint32_t caller=restore_destructor_caller){
   Origin o;return destroy_step(st,0x9000,3,caller,7,[]{return 1u;},r,0x1000,[&](Origin& out){out=src;},o,why,path);};
  auto legacy_gate=[&](const RestoreState& st,const Origin& o){std::uint32_t monitor=0,task_id=0;return legacy::transfer_proof(st,o,r,0x1000,monitor,task_id);};
  // Seam stacks: jump [source, 3 pairs] at base-35; dock [source, 4 pairs] at base-45.
  auto dock_stack=[&](const std::uint32_t* prefix,unsigned pairs){for(unsigned i=0;i<45;++i)d.data[0x5400-45+i]=0;
   const unsigned top=0x5400-5-pairs*10;d.cell(top,1);for(unsigned i=0;i<pairs;++i){d.cell(top+5+i*10,0x6200,10);d.cell(top+10+i*10,prefix[i],3);}return top;};
  unsigned writes=0;
  auto consume=[&](RestoreState& st,unsigned ebx,std::uint32_t epoch=1){SeamRegs s=d.regs();s.ebx=ebx;SeamDecode dec;if(!seam_decode(s,r,cr,0x1000,dec))return 99u;
   return consume_step(st,s,dec,epoch,r,cr,0x1000,[](std::uintptr_t,unsigned){return true;},
    [&](std::uintptr_t at,std::uint32_t v){++writes;d.put(unsigned(at),v);return true;},[](unsigned){});};
  auto payload=[&](unsigned ebx){std::uint32_t v=0;std::memcpy(&v,d.data.data()+ebx+1,4);return v;};
  unsigned why=0,path=0;

  // 1. Option off: the dock chain refuses with reason 18 and clears the arm, exactly as before.
  d.init();d.cell(0x3200+15,0);
  {RestoreState st=armed(false);const unsigned step=destroy(st,origin(dock_chain,6),why,path);
   check(step==destroy_refused&&why==refuse_provenance&&path==path_none,"option off: dock chain refuses with reason 18");
   check(!st.armed&&!st.pending&&st.transfers==0&&st.cancels[cancel_destructor]==1&&st.refusals[refuse_provenance]==1&&st.last_refusal==18&&st.attempt_mode==0&&st.retries==0&&st.filter_mode()==0,
    "option off: arm cleared, one destructor cancel, no pending");}
  // The option-off gate equals the pre-change function on every chain shape and warp value.
  {const std::uint32_t bad_dock[6]={0xefbff,0xedba0,0xedbe3,0x9be98,0x9bcd0,0};
   const Origin shapes[]={origin(jump_chain,5),origin(dock_chain,6),origin(dock_chain,5),origin(jump_chain,4),origin(bad_dock,6),origin(dock_chain,6,4),origin(jump_chain,5,4),origin(jump_chain,6)};
   unsigned same=0,total=0;
   for(unsigned warp=0;warp<2;++warp){d.init();d.cell(0x3200+15,warp);
    for(const Origin& o:shapes){RestoreState st=armed(false);std::uint32_t monitor=0,task_id=0;unsigned p=0;++total;
     if(transfer_proof(st,o,r,0x1000,monitor,task_id,p)==legacy_gate(st,o))++same;}}
   check(same==total&&total==16,"option off: transfer verdicts identical to the pre-change gate (16 shapes)");}

  // 2. Option on: the dock chain transfers only with warp 0, consume writes 258 once.
  d.init();d.cell(0x3200+15,0);
  {RestoreState st=armed(true);const unsigned step=destroy(st,origin(dock_chain,6),why,path);
   check(step==destroy_transferred&&why==0&&path==path_dock&&st.pending&&!st.armed&&st.pending_path==path_dock&&st.path_transfers[path_dock]==1&&st.transfers==1,
    "option on: dock chain transfers with path=dock");
   check(st.pending_monitor==0xffff6eaeu&&st.pending_task==0x5000&&st.pending_task_id==0x1234&&st.pending_thread==7&&st.pending_epoch==1,"dock pending keys thread, task, task ID and epoch");
   const unsigned top=dock_stack(restore_dock_reset_prefix,4);writes=0;
   check(consume(st,top)==refuse_none&&writes==1&&payload(top)==restore_rear_mode&&st.consumed==1&&st.path_consumed[path_dock]==1&&!st.pending&&st.pending_path==path_none,
    "dock consume writes 258 once and clears pending");
   check(destroy(st,origin(dock_chain,6),why,path)==destroy_idle&&writes==1,"a consumed ticket does not transfer or write again");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,1);
   check(destroy(st,origin(dock_chain,6),why,path)==destroy_refused&&why==refuse_warp&&path==path_dock&&!st.armed&&!st.pending,"dock chain with warp 1 refuses");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);
   check(destroy(st,origin(jump_chain,5),why,path)==destroy_refused&&why==refuse_warp&&path==path_jump,"jump chain with warp 0 refuses");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);const std::uint32_t bad[6]={0xefbff,0xedba0,0xedbe3,0x9be97,0x9bcd1,0};
   check(destroy(st,origin(bad,6),why,path)==destroy_refused&&why==refuse_prefix,"dock chain with one wrong return refuses");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);
   check(destroy(st,origin(dock_chain,6,4),why,path)==destroy_refused&&why==refuse_provenance,"origin_flags=4 (more than six pairs) refuses");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);
   check(destroy(st,origin(dock_chain,6),why,path,0x42d403)==destroy_cleared&&!st.armed&&st.transfers==0,"other destructor caller clears the arm");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);Origin o=origin(dock_chain,6);o.contexts[5]=0;
   check(destroy(st,o,why,path)==destroy_refused&&why==refuse_prefix,"dock chain with a null root context refuses");}

  // 3. A wrong live prefix, or the other path's seam, refuses without a write.
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);destroy(st,origin(dock_chain,6),why,path);
   const std::uint32_t wrong[4]={0xedc91,0x9bec3,0x9bcd1,0};const unsigned top=dock_stack(wrong,4);writes=0;
   check(consume(st,top)==refuse_stack&&writes==0&&payload(top)==1&&!st.pending&&st.cancels[cancel_proof]==1&&st.consumed==0,"dock pending with a wrong prefix refuses without writing");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);destroy(st,origin(dock_chain,6),why,path);
   const unsigned top=dock_stack(restore_reset_prefix,3);writes=0;
   check(consume(st,top)==refuse_stack&&writes==0&&payload(top)==1,"dock pending on the jump seam prefix refuses without writing");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);destroy(st,origin(dock_chain,6),why,path);
   const unsigned top=dock_stack(restore_dock_reset_prefix,4);d.cell(0x3200+15,1);writes=0;
   check(consume(st,top)==refuse_warp&&writes==0&&payload(top)==1,"dock pending with warp 1 at the seam refuses without writing");}
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,1);destroy(st,origin(jump_chain,5),why,path);
   const unsigned top=dock_stack(restore_dock_reset_prefix,4);d.cell(0x3200+15,0);writes=0;
   check(consume(st,top)==refuse_warp&&writes==0,"jump pending on a dock seam (warp 0) refuses without writing");
   RestoreState st2=armed(true);d.cell(0x3200+15,1);destroy(st2,origin(jump_chain,5),why,path);d.cell(0x3200+15,1);writes=0;
   check(consume(st2,top)==refuse_stack&&writes==0&&payload(top)==1,"jump pending with the dock prefix refuses without writing");}

  // 4. The jump chain is still handled, now with path=jump, with the option on and off.
  for(int dock=0;dock<2;++dock){RestoreState st=armed(dock);d.init();
   check(destroy(st,origin(jump_chain,5),why,path)==destroy_transferred&&path==path_jump&&st.pending_path==path_jump&&st.path_transfers[path_jump]==1,"jump chain transfers with path=jump");
   const unsigned top=0x5400-35;writes=0;
   check(consume(st,top)==refuse_none&&writes==1&&payload(top)==restore_rear_mode&&st.path_consumed[path_jump]==1&&st.path_consumed[path_dock]==0,"jump consume writes 258 once");}

  // 5. No double arming: a jump chain after a dock transfer cancels the pending ticket and transfers nothing.
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);destroy(st,origin(dock_chain,6),why,path);d.cell(0x3200+15,1);
   check(destroy(st,origin(jump_chain,5),why,path)==destroy_cancel_pending&&!st.pending&&!st.armed&&st.transfers==1&&st.cancels[cancel_second_destruction]==1,
    "jump chain after a dock transfer cancels pending, one transfer total");
   check(destroy(st,origin(jump_chain,5),why,path)==destroy_idle&&st.transfers==1,"no second pending without a fresh arm");
   check(st.filter_mode()==0,"the seam filter is idle after the cancel");}
  {RestoreState st=armed(true);d.init();destroy(st,origin(jump_chain,5),why,path);d.cell(0x3200+15,0);
   check(destroy(st,origin(dock_chain,6),why,path)==destroy_cancel_pending&&!st.pending&&st.transfers==1,"dock chain after a jump transfer cancels pending");}

  // 6. Save/load boundary: the epoch advance (load 0x004a0880, VM clear/construct, deserializer) cancels a dock pending.
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);destroy(st,origin(dock_chain,6),why,path);
   const unsigned top=dock_stack(restore_dock_reset_prefix,4);writes=0;
   check(consume(st,top,2)==refuse_epoch&&writes==0&&payload(top)==1&&!st.pending,"stale epoch at the dock seam refuses without writing");
   RestoreState st2=armed(true);destroy(st2,origin(dock_chain,6),why,path);st2.clear_all(cancel_epoch);
   check(!st2.pending&&!st2.armed&&st2.cancels[cancel_epoch]==1&&st2.dock&&st2.pending_path==path_none,"load boundary clears the dock pending once; the option survives");}

  // 7. F1: the pending epoch is read after the provenance walk (47d5e3d9 order),
  // so an EH epoch bump during the walk stamps the new epoch.
  {RestoreState st=armed(true);d.init();d.cell(0x3200+15,0);std::uint32_t epoch_now=1;Origin o;
   const unsigned step=destroy_step(st,0x9000,3,restore_destructor_caller,7,[&]{return epoch_now;},r,0x1000,
    [&](Origin& out){out=origin(dock_chain,6);epoch_now=2;},o,why,path);
   check(step==destroy_transferred&&st.pending&&st.pending_epoch==2,"epoch is read after the provenance walk");
   const unsigned top=dock_stack(restore_dock_reset_prefix,4);writes=0;
   check(consume(st,top,2)==refuse_none&&writes==1,"the post-walk epoch consumes");}
  {RestoreState st=armed(false);d.init();std::uint32_t epoch_now=1;Origin o;
   destroy_step(st,0x9000,3,restore_destructor_caller,7,[&]{return epoch_now;},r,0x1000,[&](Origin& out){out=origin(jump_chain,5);epoch_now=3;},o,why,path);
   check(st.pending&&st.pending_epoch==3,"jump path (option off) stamps the post-walk epoch");}

  // 8. F4: option off, destroy_step/consume_step sequencing equals the pre-change
  // sequencing (47d5e3d9 restore_on_destroy / restore_on_store consume branch,
  // driven through the verbatim legacy proofs) on every event sequence of length 4.
  {
   struct Ev{unsigned kind,shape,warp,epoch,io;};
   std::vector<Ev> alphabet;alphabet.push_back({0,0,0,0,0});                                   // re-arm
   for(unsigned shape=0;shape<6;++shape)for(unsigned w=0;w<2;++w)alphabet.push_back({1,shape,w,0,0}); // destructor
   for(unsigned stack=0;stack<3;++stack)for(unsigned w=0;w<2;++w)for(unsigned e=1;e<3;++e)alphabet.push_back({2,stack,w,e,0}); // seam
   alphabet.push_back({2,0,1,1,1});alphabet.push_back({2,0,1,1,2});                           // not writable, write fails
   const std::uint32_t bad_jump[5]={0xefbff,0xedba0,0xedbe3,0x1661d,0};
   const std::uint32_t wrong[4]={0xedc91,0x9bec3,0x9bcd1,0};
   auto shape_origin=[&](unsigned shape){return shape==0?origin(jump_chain,5):shape==1?origin(dock_chain,6):shape==2?origin(jump_chain,5,4):
    shape==3?origin(bad_jump,5):origin(jump_chain,shape==4?6:5);};
   auto arm=[](RestoreState& st){if(st.armed||st.pending)return;st.armed=true;st.arm_cockpit=0x9000;st.arm_generation=3;st.arm_player=0xfffefa78u;st.arm_controller=0xffff6eafu;};
   auto same_state=[](const RestoreState& a,const RestoreState& b){
    if(a.armed!=b.armed||a.pending!=b.pending||a.arm_cockpit!=b.arm_cockpit||a.arm_generation!=b.arm_generation||a.arm_player!=b.arm_player||a.arm_controller!=b.arm_controller)return false;
    if(a.pending_monitor!=b.pending_monitor||a.pending_task!=b.pending_task||a.pending_task_id!=b.pending_task_id||a.pending_thread!=b.pending_thread||a.pending_epoch!=b.pending_epoch||a.pending_updates!=b.pending_updates)return false;
    if(a.transfers!=b.transfers||a.consumed!=b.consumed||a.writes_failed!=b.writes_failed||a.last_refusal!=b.last_refusal||a.attempt_mode!=b.attempt_mode||a.retries!=b.retries||a.attempt_generation!=b.attempt_generation)return false;
    for(unsigned i=0;i<cancel_count;++i)if(a.cancels[i]!=b.cancels[i])return false;
    for(unsigned i=0;i<refuse_count;++i)if(a.refusals[i]!=b.refusals[i])return false;
    return true;};
   d.init();
   std::uint64_t sequences=0,mismatches=0,transfers=0,consumes=0;
   const unsigned n=unsigned(alphabet.size());
   for(unsigned code=0;code<n*n*n*n;++code){
    RestoreState now_st;now_st.dock=false;arm(now_st);RestoreState old_st=now_st;old_st.attempt_mode=now_st.attempt_mode=258;
    unsigned now_writes=0,old_writes=0;bool ok=true;unsigned c=code;
    for(unsigned step=0;step<4;++step,c/=n){
     const Ev& e=alphabet[c%n];
     if(e.kind==0){arm(now_st);arm(old_st);}
     else if(e.kind==1){
      d.cell(0x3200+15,e.warp);const Origin src=shape_origin(e.shape);const std::uint32_t caller=e.shape==5?0x42d403u:restore_destructor_caller;
      Origin o;unsigned w=0,p=0;
      destroy_step(now_st,0x9000,3,caller,7,[]{return 1u;},r,0x1000,[&](Origin& out){out=src;},o,w,p);
      [&]{ // 47d5e3d9 restore_on_destroy, verbatim order
       if(old_st.pending){old_st.clear_pending(cancel_second_destruction);return;}
       if(!old_st.armed)return;
       if(0x9000u!=old_st.arm_cockpit||3u!=old_st.arm_generation||caller!=restore_destructor_caller){old_st.clear_arm(cancel_destructor);old_st.reset_attempt();return;}
       std::uint32_t monitor=0,task_id=0;
       const unsigned why=legacy::transfer_proof(old_st,src,r,0x1000,monitor,task_id);
       if(why){old_st.refuse(why);old_st.clear_arm(cancel_destructor);old_st.reset_attempt();return;}
       old_st.transfer(monitor,src.task,task_id,7,1,path_jump);}();
     }else{
      d.cell(0x3200+15,e.warp);
      const unsigned top=e.shape==0?dock_stack(restore_reset_prefix,3):e.shape==1?dock_stack(restore_dock_reset_prefix,4):dock_stack(wrong,4);
      SeamRegs s=d.regs();s.ebx=top;SeamDecode dec;if(!seam_decode(s,r,cr,0x1000,dec)){ok=false;break;}
      auto writable=[&](std::uintptr_t,unsigned){return e.io!=1;};
      if(now_st.pending)consume_step(now_st,s,dec,e.epoch,r,cr,0x1000,writable,[&](std::uintptr_t,std::uint32_t v){++now_writes;return v==restore_rear_mode&&e.io!=2;},[](unsigned){});
      if(old_st.pending){ // 47d5e3d9 restore_on_store pending branch, verbatim order
       unsigned why=legacy::seam_consume_proof(old_st,s,dec,e.epoch,r,cr,0x1000);
       if(!why&&!writable(s.ebx+1,4))why=refuse_writable;
       if(why){old_st.refuse(why);old_st.clear_pending(cancel_proof);}
       else{old_st.take_pending();++old_writes;if(e.io!=2)++old_st.consumed;else{++old_st.writes_failed;old_st.refuse(refuse_write);}}
      }
     }
     if(!same_state(now_st,old_st)||now_writes!=old_writes){ok=false;break;}
    }
    ++sequences;if(!ok)++mismatches;transfers+=now_st.transfers;consumes+=now_st.consumed;
   }
   std::printf("option-off equivalence: alphabet=%u sequences=%llu mismatches=%llu transfers=%llu consumed=%llu\n",n,
    static_cast<unsigned long long>(sequences),static_cast<unsigned long long>(mismatches),static_cast<unsigned long long>(transfers),static_cast<unsigned long long>(consumes));
   check(mismatches==0&&sequences==std::uint64_t(n)*n*n*n&&transfers>0&&consumes>0,"option off: destroy/consume sequencing equals the pre-change sequencing");
  }
 }
 std::printf("chase restore host: %u checks PASS\n",checks);
}
