// Execute each actual generated callback stub against a RET continuation.
// The separate site probe verifies displaced spans against the game executable.
#include "../../src/proxy/chase_transition.cpp"
#include "../../src/proxy/chase_lead.cpp"
#include <cstdio>
// log() must never run under the chase SRW lock (present() holds capture's
// mutex while report() takes this lock); count seam lines and violations.
static unsigned log_under_lock=0,seam_lines=0;
namespace x3m { void log(const char* format,...) {
 if(TryAcquireSRWLockExclusive(&x3m::chase_transition::lock))ReleaseSRWLockExclusive(&x3m::chase_transition::lock);else ++log_under_lock;
 if(!std::strncmp(format,"chase_view_restore_seam ",24))++seam_lines;
} }
namespace x3m::telemetry { bool enabled(){return true;} }
namespace x3m::object_trace { bool executable_verified(){return true;} }
namespace x3m::chase_camera { bool installed(){return true;} bool wanted(){return true;} }
namespace transition=x3m::chase_transition;
extern "C" {
struct Snapshot { std::uint32_t regs[9];unsigned char xmm[128],x87[108];std::uint32_t mxcsr; };
Snapshot* fixture_output=nullptr;
std::uint32_t fixture_entry_esp=0,fixture_exit_esp=0;
alignas(16) unsigned char fixture_input_x87[108];
alignas(16) unsigned char fixture_xmm_seed[128];
std::uint16_t fixture_cw=0x077f;
std::uint32_t fixture_mxcsr=0x3f80;
std::uint32_t fixture_flags=0x647,fixture_normal_cpu=0;
std::uint32_t fixture_esi=0x12345678,fixture_edi=0x98765432;
void fixture_call(std::uint32_t site,std::uint32_t args,std::uint32_t locals,std::uint32_t eax,std::uint32_t ecx,std::uint32_t edx);
// Controllable interpreter frame: every GPR and twelve caller stack words are
// supplied, the site is called (return address at [ESP]), and the resulting
// GPRs/flags are captured in PUSHAD order (edi..eax, eflags) as in the stubs.
struct FrameCall { std::uint32_t eax,ecx,edx,ebx,ebp,esi,edi; std::uint32_t stack[12]; std::uint32_t out[9]; std::uint32_t flags; };
FrameCall* fixture_frame_current=nullptr;std::uint32_t fixture_frame_target=0;
void fixture_call_frame(void* target,FrameCall* frame);
}
asm(".text\n.globl _fixture_call_frame\n_fixture_call_frame:\n"
"pushl %ebp\n movl %esp,%ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
"movl 12(%ebp),%edx\n movl %edx,_fixture_frame_current\n movl 8(%ebp),%eax\n movl %eax,_fixture_frame_target\n"
"movl $11,%ecx\n1:\n pushl 28(%edx,%ecx,4)\n decl %ecx\n jns 1b\n"
"pushl 112(%edx)\n popfl\n movl 0(%edx),%eax\n movl 4(%edx),%ecx\n movl 12(%edx),%ebx\n movl 20(%edx),%esi\n movl 24(%edx),%edi\n movl 16(%edx),%ebp\n movl 8(%edx),%edx\n"
"call *_fixture_frame_target\n"
"pushfl\n pushal\n movl %esp,%esi\n movl _fixture_frame_current,%edi\n addl $76,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
"addl $84,%esp\n popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n ret\n");
// Four-byte caller stack; no 16-byte alignment promise. Save the fixture's
// state, seed hostile live state, run the real span, snapshot it, restore caller.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
"movl %esp,_fixture_entry_esp\n"
"pushfl\n pushal\n subl $256,%esp\n"
"fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
"movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
"movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
"movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
"fninit\n cmpl $0,_fixture_normal_cpu\n jne 1f\n fld1\n fldpi\n1:\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
"movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
"movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
"movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
"movl _fixture_esi,%esi\n movl _fixture_edi,%edi\n pushl _fixture_flags\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n call *240(%esp)\n"
"pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
"movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
"movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
"fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n addl $36,%esp\n"
"frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
"movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
"movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
"addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n");

static unsigned checks=0,failures=0;
static void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL %s\n",label);}}
// Fixed, paired measurements of the complete emitted no-op callback path.
// Both paths use the same expensive register/x87 snapshot harness. Differences
// are diagnostic estimates, may be negative under noise, and are not game FPS.
static inline double as_double(std::uint64_t v){return double(std::uint32_t(v>>32))*4294967296.0+double(std::uint32_t(v));}
static bool benchmark_batch(void* target,unsigned loops,std::uint64_t& ticks){
 std::uint32_t args[16]{};Snapshot out{};fixture_output=&out;
 const auto entry=std::uint32_t(reinterpret_cast<std::uintptr_t>(target));
 const auto arguments=std::uint32_t(reinterpret_cast<std::uintptr_t>(args));
 LARGE_INTEGER start{},end{};
 if(!QueryPerformanceCounter(&start)||start.QuadPart<=0)return false;
 for(unsigned i=0;i<loops;++i)fixture_call(entry,arguments,arguments,0,0,0);
 if(!QueryPerformanceCounter(&end)||end.QuadPart<start.QuadPart)return false;
 ticks=std::uint64_t(end.QuadPart-start.QuadPart);return true;
}
static void benchmark(void* continuation,void* const* stubs,void* seam_idle){
 constexpr unsigned loops=4096,trials=3;
 LARGE_INTEGER f{};
 const bool clock_ok=QueryPerformanceFrequency(&f)&&f.QuadPart>0;
 check(clock_ok,"benchmark QPC frequency");if(!clock_ok)return;
 // All callbacks disabled: the benchmark isolates wrapper/guard cost. Live
 // handler and native-function timings are separately recorded during gameplay.
 transition::enabled.store(false);x3m::chase_lead::enabled.store(false);
 x3m::chase_lead::hud_enabled.store(false);x3m::chase_lead::native_timing_enabled.store(false);
 transition::restore_enabled.store(false);transition::restore_filter[0].store(0); // kind 18: the seam stub's idle prefilter path
 fixture_normal_cpu=1;fixture_cw=0x037f;fixture_mxcsr=0x1f80;fixture_flags=0x247;
 const double scale=1e6/as_double(std::uint64_t(f.QuadPart))/double(loops);
 for(unsigned kind=0;kind<19;++kind){
  void* const target=kind<18?stubs[kind]:seam_idle;
  std::uint64_t ignored=0;bool valid=benchmark_batch(continuation,16,ignored)&&benchmark_batch(target,16,ignored);
  std::uint64_t base[trials]{},hook[trials]{};
  for(unsigned trial=0;trial<trials&&valid;++trial){
   // Alternate order so a single warm-up/order bias is not always charged to
   // the callback; warm-up calls themselves are excluded from the measurements.
   if(trial&1)valid=benchmark_batch(target,loops,hook[trial])&&benchmark_batch(continuation,loops,base[trial]);
   else valid=benchmark_batch(continuation,loops,base[trial])&&benchmark_batch(target,loops,hook[trial]);
  }
  check(valid,"bounded paired benchmark QPC samples");if(!valid)continue;
  double sum_base=0,sum_hook=0,best_delta=0;
  for(unsigned trial=0;trial<trials;++trial){
   const double b=as_double(base[trial])*scale,h=as_double(hook[trial])*scale;
   sum_base+=b;sum_hook+=h;if(!trial||h-b<best_delta)best_delta=h-b;
  }
  std::printf("CHASE STUB BENCH kind=%u trials=%u loops=%u baseline_mean_us=%.6f full_stub_mean_us=%.6f delta_mean_us=%.6f delta_best_trial_us=%.6f scope=%s cpu=normal harness=same_fixture_call native_work=excluded game_fps=unmeasured\n",
      kind,trials,loops,sum_base/double(trials),sum_hook/double(trials),(sum_hook-sum_base)/double(trials),best_delta,kind<18?"full_stub_disabled_callback":"seam_idle_prefilter_skip");
 }
}

// ---------------- X3M_CHASE_VIEW_RESTORE scenarios ----------------
namespace restore_fixture {
using namespace x3m::chase_transition;
using namespace x3m::chase_transition::detail;
namespace engine_patch=x3m::engine_patch;
constexpr std::uint32_t player_id=0xfffefa78u,controller_id=0xffff6eafu,monitor_id=0xffff6eaeu,native_id=0x8dbu,task_id=0x1234u,task2_id=0x1235u;
alignas(16) unsigned char world[0x4000];
unsigned char* code=nullptr;
std::uint32_t W(unsigned off){return std::uint32_t(reinterpret_cast<std::uintptr_t>(world+off));}
template<class T>void put(std::uint32_t at,T v){std::memcpy(reinterpret_cast<void*>(at),&v,sizeof v);}
template<class T>T get(std::uint32_t at){T v;std::memcpy(&v,reinterpret_cast<void*>(at),sizeof v);return v;}
void cell(std::uint32_t at,std::uint32_t value,unsigned char tag=1){*reinterpret_cast<unsigned char*>(at)=tag;put(at+1,value);}
// Offsets inside world.
enum : unsigned { o_roots=0,o_vm=0x100,o_classes=0x1600,o_global_cells=0x1700,o_warp_cells=0x1780,o_monitor=0x1800,o_monitor_cells=0x1810,
    o_task=0x1900,o_stack=0x1a00,o_stack_base=0x1a00+128*5,o_ship=0x1d00,o_native_registry=0x1e00,o_native_table=0x1e20,o_native_bucket=0x1e40,o_native_row=0x1e60,
    o_cockpit_registry=0x1f00,o_cockpit_table=0x1f20,o_cockpit_bucket=0x1f40,o_cockpit_row=0x1f60,o_cockpit=0x2000,o_destructor_frame=0x2300,o_seam_frame=0x2400,
    o_task2=0x2500,o_seam_frame2=0x2540,o_ctx_a=0x2600,o_ctx_b=0x2610,o_ctx_c=0x2620,o_other_monitor=0x2700,o_other_monitor_cells=0x2710,o_stack2=0x2800,o_stack2_base=0x2800+128*5 };
std::uint32_t code_at(std::uint32_t pc){return std::uint32_t(reinterpret_cast<std::uintptr_t>(code))+pc;}
std::uint32_t descriptor(unsigned i){return W(o_classes)+i*0x38;}
void build_world(){
 std::memset(world,0,sizeof world);
 put(W(o_roots),W(o_vm));put(W(o_roots+4),W(o_native_registry));put(W(o_roots+8),W(o_cockpit_registry));
 put(W(o_vm+8),code_at(0));put(W(o_vm+0x1c),3u);put(W(o_vm+0x20),W(o_classes));put(W(o_vm+0x48),0x42d340u); // group1 dispatch
 const std::uint32_t ids[3]={0,restore_warp_class,restore_monitor_class};const unsigned cells[3]={o_global_cells,o_warp_cells,0};const unsigned counts[3]={12,8,41};
 for(unsigned i=0;i<3;++i){put(descriptor(i),ids[i]);put(descriptor(i)+8,descriptor(i));put(descriptor(i)+0xc,cells[i]?W(cells[i]):0u);put(descriptor(i)+0x1c,counts[i]);}
 cell(W(o_global_cells)+5*9,player_id);cell(W(o_global_cells)+5*8,controller_id);
 cell(W(o_warp_cells)+5*3,1);cell(W(o_warp_cells)+5*6,0); // mid-warp world: transfer requires warp1/killed0
 put(W(o_monitor),monitor_id);put(W(o_monitor+8),descriptor(2));put(W(o_monitor+0xc),W(o_monitor_cells));
 cell(W(o_monitor_cells),restore_rear_mode);cell(W(o_monitor_cells)+5,0);cell(W(o_monitor_cells)+55,20);cell(W(o_monitor_cells)+80,0);cell(W(o_monitor_cells)+85,player_id); // run65 layout: cell11 priority, cell16 number, cell17 ref
 put(W(o_other_monitor),0xffff0001u);put(W(o_other_monitor+8),descriptor(2));put(W(o_other_monitor+0xc),W(o_other_monitor_cells));
 cell(W(o_other_monitor_cells),1);cell(W(o_other_monitor_cells)+5,0);cell(W(o_other_monitor_cells)+55,60);cell(W(o_other_monitor_cells)+80,1);cell(W(o_other_monitor_cells)+85,0xffff0002u);
 put(W(o_task+8),task_id);put(W(o_task+0x10),128u);put(W(o_task+0x14),W(o_stack_base));put(W(o_task+0x3c),W(o_monitor));
 put(W(o_task2+8),task2_id);put(W(o_task2+0x10),128u);put(W(o_task2+0x14),W(o_stack2_base));put(W(o_task2+0x3c),W(o_monitor));
 put(W(o_ship+8),native_id);put(W(o_ship+0x94),player_id);
 put(W(o_native_registry+0x14),W(o_native_table));put(W(o_native_table),W(o_native_bucket));put(W(o_native_table+4),4u);
 put(W(o_native_bucket)+4*(native_id&3),W(o_native_row));put(W(o_native_row+4),native_id);put(W(o_native_row+8),W(o_ship));
 put(W(o_cockpit_registry),W(o_cockpit_table));put(W(o_cockpit_registry+0x10),7u);put(W(o_cockpit_table),W(o_cockpit_bucket));put(W(o_cockpit_table+4),4u);
 put(W(o_cockpit_bucket)+4*(7&3),W(o_cockpit_row));put(W(o_cockpit_row+4),7u);put(W(o_cockpit_row+8),W(o_cockpit));
 put(W(o_cockpit+0xc),W(o_ship));put(W(o_cockpit+0x10),W(o_ship));put(W(o_cockpit+0x150),restore_rear_mode);put(W(o_cockpit+0x1c0),0u);
 put(W(o_destructor_frame+4),0x4a3909u);put(W(o_destructor_frame+0xc),W(o_task));put(W(o_destructor_frame+0x10),1u);
 put(W(o_seam_frame+8),W(o_task));put(W(o_seam_frame2+8),W(o_task2));
 put(W(o_ctx_a),1u);put(W(o_ctx_b),2u);put(W(o_ctx_c),3u);
 // CODE: recognised optimized stores at the documented PCs, native call before the destructor PC.
 const unsigned char mode_store[4]={0x94,0,0,0x24},player_store[4]={0x93,9,0,0x24},controller_store[4]={0x93,8,0,0x24},killed_store[4]={0x94,6,0,0x24},unknown_global[4]={0x93,9,0,0x24};
 std::memcpy(code+restore_mode_pc,mode_store,4);std::memcpy(code+restore_player_pc,player_store,4);std::memcpy(code+restore_controller_pc,controller_store,4);
 std::memcpy(code+restore_killed_pcs[0],killed_store,4);std::memcpy(code+restore_killed_pcs[1],killed_store,4);std::memcpy(code+0x1000,unknown_global,4);
 const unsigned char native_call[5]={0x82,1,0,1,0};std::memcpy(code+0xf008a-5,native_call,5);put(W(o_task+0x1c),0xf008au);
 put(W(o_task+0x18),std::int32_t(-10));
}
// Warp destructor stack (published index) and, later in the same task, the seam stack (live EBX).
void build_warp_stack(){
 const std::uint32_t warp_ctx[5]={W(o_ctx_a),W(o_ctx_b),W(o_ctx_b),W(o_ctx_c),W(o_ctx_c)};
 for(unsigned i=0;i<5;++i){cell(W(o_stack_base)-50+i*10,warp_ctx[i],10);cell(W(o_stack_base)-45+i*10,restore_warp_prefix[i],3);}
}
std::uint32_t seam_source(std::uint32_t base){return base-7*5;}
void build_seam_stack(std::uint32_t base,std::uint32_t requested=1,unsigned char tag=1,const std::uint32_t* prefix=restore_reset_prefix){
 const std::uint32_t ctx[3]={W(o_ctx_a),W(o_ctx_b),W(o_ctx_c)};
 cell(seam_source(base),requested,tag);
 for(unsigned i=0;i<3;++i){cell(base-30+i*10,ctx[i],10);cell(base-25+i*10,prefix[i],3);}
}
unsigned checks=0,failures=0;
void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL restore %s\n",label);}}
void* stubs[restore_site_count]{};void* continuation=nullptr;
void reset_state(){ restore={};restore_filter_publish();restore_epoch.store(1);state={};restore_enabled.store(true); }
std::uint32_t thread(){return GetCurrentThreadId();}
void lifetime(){state.construct(W(o_cockpit),thread());state.complete(W(o_cockpit),thread());}
void update(){ std::uint32_t frame[4]{};frame[1]=W(o_cockpit);std::uint32_t regs[9]{};regs[3]=std::uint32_t(reinterpret_cast<std::uintptr_t>(frame))-4;handle(3,regs); }
void destroy(std::uint32_t caller=restore_destructor_caller,std::uint32_t cockpit=0){
 std::uint32_t frame[4]{};frame[0]=caller;frame[1]=cockpit?cockpit:W(o_cockpit);
 std::uint32_t regs[9]{};regs[3]=std::uint32_t(reinterpret_cast<std::uintptr_t>(frame))-4;regs[2]=W(o_destructor_frame);handle(2,regs);
}
bool arm(){reset_state();lifetime();update();return restore.armed;}
bool arm_pending(){ if(!arm())return false;build_warp_stack();destroy();build_seam_stack(W(o_stack_base));return restore.pending; }
FrameCall seam_frame(std::uint32_t pc,std::uint32_t ebx,std::uint32_t esi,std::uint32_t eax,std::uint32_t ebp=0,std::uint32_t context=0){
 FrameCall f{};f.eax=eax;f.ebx=ebx;f.esi=esi;f.edi=code_at(pc)+1;f.ebp=ebp?ebp:W(o_seam_frame);f.ecx=0x11;f.edx=0x22;f.flags=0x202;
 f.stack[5]=W(o_vm);f.stack[7]=context?context:eax;return f;
}
void run(unsigned kind,FrameCall& f){fixture_call_frame(stubs[kind],&f);}
std::uint32_t payload(){return get<std::uint32_t>(seam_source(W(o_stack_base))+1);}
void scenarios(){
 build_seam_stack(W(o_stack_base));build_seam_stack(W(o_stack2_base));
 // Exactly-once source-payload write through the actual seam stub.
 check(arm_pending(),"arm then warp destructor transfers to pending");
 check(restore_filter[0].load()==2&&restore_filter[1].load()==code_at(restore_mode_pc)+1,"pending publishes mode 2 and mode operand address");
 const auto calls=restore.seam_calls;
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);
  check(payload()==restore_rear_mode&&restore.consumed==1&&!restore.pending&&restore.seam_calls==calls+1,"consume writes 258 into the live source payload once");
  check(f.out[4]==seam_source(W(o_stack_base))&&f.out[0]==code_at(restore_mode_pc)+1&&f.out[7]==W(o_monitor)&&(f.out[8]&0x8d5)==(0x202&0x8d5),"seam stub returns the interpreter registers and flags untouched");
  check(restore_filter[0].load()==0,"consumed ticket returns the prefilter to idle");}
 build_seam_stack(W(o_stack_base));
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);
  check(payload()==1&&restore.consumed==1&&restore.seam_calls==calls+1,"idle prefilter skips the handler and never writes again");}
 // Same-valued selection cancels the arm; a direct caller's prefix refuses the pending consume.
 check(arm(),"re-arm");restore_filter[0].store(1);
 build_seam_stack(W(o_stack_base),restore_rear_mode);
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);
  check(!restore.armed&&restore.cancels[cancel_selection]==1&&restore.attempt_mode==0,"same-valued SelectMode(258) while armed cancels the arm");}
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);
  check(!restore.armed&&restore.cancels[cancel_selection]==1,"idle after selection ignores stores");}
 check(arm_pending(),"arm/pending for direct-caller prefix");
 const std::uint32_t direct_prefix[3]={0xedc91,0x16000,0};build_seam_stack(W(o_stack_base),1,1,direct_prefix);
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);
  check(payload()==1&&!restore.pending&&restore.last_refusal==refuse_stack&&restore.cancels[cancel_proof]==1,"direct SelectMode caller prefix refuses and clears pending without writing");}
 build_seam_stack(W(o_stack_base));
 // Foreign monitor selections are ignored while pending; malformed context cancels.
 check(arm_pending(),"arm/pending for foreign monitor");
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_other_monitor));run(0,f);
  check(restore.pending&&payload()==1,"another monitor's SelectMode leaves pending untouched");}
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor),0,W(o_other_monitor));run(0,f);
  check(!restore.pending&&payload()==1&&restore.last_refusal==refuse_context,"current-object/context mismatch cancels pending without writing");}
 // A secondary monitor whose variable11 is the player, visited first while pending, never touches the ticket.
 check(arm_pending(),"arm/pending for secondary player monitor");cell(W(o_other_monitor_cells)+85,player_id);
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_other_monitor));run(0,f);
  check(restore.pending&&payload()==1&&restore.cancels[cancel_proof]==0,"secondary monitor viewing the player leaves pending untouched");}
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);
  check(!restore.pending&&payload()==restore_rear_mode&&restore.consumed==1,"main monitor then consumes once");}
 cell(W(o_other_monitor_cells)+85,0xffff0002u);build_seam_stack(W(o_stack_base));
 // Side monitors never set cell17 (ef8c4 PUSH 0 / STOREM 17): their SelectMode(0) is foreign, never a cancel.
 check(arm(),"arm for side monitor SelectMode(0)");cell(W(o_other_monitor_cells)+85,0,0);build_seam_stack(W(o_stack_base),0);
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_other_monitor));run(0,f);check(restore.armed&&restore.cancels[cancel_selection]==0,"side monitor with unset cell17 leaves the arm untouched");}
 build_warp_stack();destroy();check(restore.pending,"armed ticket still transfers");build_seam_stack(W(o_stack_base),0);
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_other_monitor));run(0,f);check(restore.pending&&restore.cancels[cancel_proof]==0,"side monitor SelectMode(0) while pending leaves pending untouched");}
 build_seam_stack(W(o_stack_base));
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);check(!restore.pending&&payload()==restore_rear_mode&&restore.consumed==1,"main monitor consumes once after side monitors");}
 cell(W(o_other_monitor_cells)+85,0xffff0002u);build_seam_stack(W(o_stack_base));
 // An exception through the EH adapter while armed must not prevent a later re-arm and gate.
 check(arm(),"arm before eh bump");{FrameCall f{};f.flags=0x202;run(6,f);}
 update();check(restore.armed&&restore.arms==2&&restore.cancels[cancel_epoch]==1,"admitted rear update re-arms after an eh epoch bump");
 build_warp_stack();destroy();build_seam_stack(W(o_stack_base));check(restore.pending,"re-armed ticket transfers at the gate");
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);check(payload()==restore_rear_mode&&restore.consumed==1,"re-armed ticket consumes once");}
 build_seam_stack(W(o_stack_base));
 // Deserialization kind7 through the existing mode-load observer clears and advances the epoch.
 check(arm_pending(),"arm/pending for deserialization");{const auto epoch=restore_epoch.load();std::uint32_t regs[9]{};regs[2]=W(o_cockpit);regs[5]=1;handle(7,regs);
  check(!restore.pending&&!restore.armed&&restore_epoch.load()==epoch+1&&restore.cancels[cancel_epoch]==1&&restore_filter[0].load()==0,"kind7 deserialization cancels pending and advances the epoch");}
 check(arm(),"arm for deserialization");{std::uint32_t regs[9]{};regs[2]=W(o_cockpit);regs[5]=1;handle(7,regs);check(!restore.armed&&restore.attempt_mode==0,"kind7 deserialization clears the arm and allows re-arm");}
 update();check(restore.armed,"rear update after deserialization re-arms");
 // A->B->A identity stores cancel pending and armed tickets.
 check(arm_pending(),"arm/pending for player store");
 cell(seam_source(W(o_stack_base)),player_id);
 {FrameCall f=seam_frame(restore_player_pc,seam_source(W(o_stack_base)),45,W(o_classes));run(0,f);
  check(!restore.pending&&!restore.armed&&restore.cancels[cancel_identity_store]==1,"global player store to the same value cancels pending");}
 check(arm(),"arm for controller store");
 {FrameCall f=seam_frame(restore_controller_pc,seam_source(W(o_stack_base)),40,W(o_classes));run(0,f);
  check(!restore.armed&&restore.cancels[cancel_identity_store]==1,"controller setup store cancels the arm");}
 check(arm_pending(),"arm/pending for unknown global slot store");
 {FrameCall f=seam_frame(0x1000,seam_source(W(o_stack_base)),45,W(o_classes));run(0,f);
  check(!restore.pending&&restore.cancels[cancel_global_slot]==1,"unknown global slot9 store cancels pending");}
 check(arm(),"arm for unknown global slot while armed");
 {FrameCall f=seam_frame(0x1000,seam_source(W(o_stack_base)),45,W(o_classes));run(0,f);
  check(restore.armed&&restore.seam_calls==0,"armed prefilter skips unknown global stores entirely");}
 // Killed stores: nonzero cancels, well-formed zero keeps.
 check(arm(),"arm for killed store");cell(seam_source(W(o_stack_base)),0);
 {FrameCall f=seam_frame(restore_killed_pcs[0],seam_source(W(o_stack_base)),30,descriptor(1));run(0,f);check(restore.armed&&restore.seam_calls==1,"zero killed store keeps the arm");}
 cell(seam_source(W(o_stack_base)),1);
 {FrameCall f=seam_frame(restore_killed_pcs[1],seam_source(W(o_stack_base)),30,descriptor(1));run(0,f);check(!restore.armed&&restore.cancels[cancel_killed_store]==1,"nonzero killed store cancels the arm");}
 build_seam_stack(W(o_stack_base));
 // Yield/re-entry: another task at the seam refuses and clears; the original task then cannot write.
 check(arm_pending(),"arm/pending for yield");
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack2_base)),0,W(o_monitor),W(o_seam_frame2));run(0,f);
  check(!restore.pending&&restore.last_refusal==refuse_task&&get<std::uint32_t>(seam_source(W(o_stack2_base))+1)==1,"re-entered task at the seam refuses and clears pending");}
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);check(payload()==1&&restore.consumed==0,"original task after the yield does not write");}
 check(arm_pending(),"arm/pending for thread mismatch");restore.pending_thread^=1;
 {FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);check(!restore.pending&&restore.last_refusal==refuse_thread&&payload()==1,"other thread refuses");}
 // Task terminations.
 for(unsigned kind=1;kind<=2;++kind){
  check(arm_pending(),"arm/pending for termination");
  FrameCall other{};other.stack[0]=W(o_vm);other.stack[1]=W(o_task2);other.flags=0x202;run(kind,other);
  check(restore.pending,"unrelated task termination keeps pending");
  FrameCall f{};f.stack[0]=W(o_vm);f.stack[1]=W(o_task);f.flags=0x202;f.ebx=0x77;run(kind,f);
  check(!restore.pending&&restore.cancels[kind==1?cancel_task_complete:cancel_task_abort]==1,kind==1?"task completion clears pending":"task abort clears pending");
  check(f.out[4]==0x77,"termination stub leaves EBX for the displaced prologue");
 }
 // VM construct/clear/load advance the epoch and clear everything.
 for(unsigned kind=3;kind<=5;++kind){
  check(arm_pending(),"arm/pending for vm epoch");const auto epoch=restore_epoch.load();
  FrameCall f{};f.stack[0]=W(o_vm);f.esi=W(o_vm);f.flags=0x202;run(kind,f);
  check(!restore.pending&&!restore.armed&&restore_epoch.load()==epoch+1&&restore.cancels[cancel_epoch]==1&&restore_filter[0].load()==0,"vm boundary bumps epoch and clears");
 }
 // EH adapter: lock-free epoch bump only; the next seam store observes it.
 check(arm_pending(),"arm/pending for eh");{const auto epoch=restore_epoch.load();const auto bumps=restore_eh_bumps.load();
  FrameCall f{};f.flags=0x202;run(6,f);
  check(restore.pending&&restore_epoch.load()==epoch+1&&restore_eh_bumps.load()==bumps+1,"eh adapter bumps the epoch without touching locked state");
  FrameCall g=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,g);
  check(!restore.pending&&restore.cancels[cancel_epoch]==1&&payload()==1,"stale epoch cancels at the seam without writing");}
 // Bad tags and stack bounds refuse.
 struct Bad{const char* label;unsigned refusal;void(*mutate)();void(*undo)();};
 const Bad bad[]={
  {"source tag 2",refuse_source,[]{cell(seam_source(W(o_stack_base)),1,2);},[]{cell(seam_source(W(o_stack_base)),1);}},
  {"source payload 2",refuse_source,[]{cell(seam_source(W(o_stack_base)),2);},[]{cell(seam_source(W(o_stack_base)),1);}},
  {"destination mode tag 0",refuse_mode_cell,[]{cell(W(o_monitor_cells),restore_rear_mode,0);},[]{cell(W(o_monitor_cells),restore_rear_mode);}},
  {"persistent mode 1",refuse_mode_cell,[]{cell(W(o_monitor_cells),1);},[]{cell(W(o_monitor_cells),restore_rear_mode);}},
  {"native handle nonzero",refuse_handle_cell,[]{cell(W(o_monitor_cells)+5,0x55);},[]{cell(W(o_monitor_cells)+5,0);}},
  {"cell17 ref not player",refuse_ref_cell,[]{cell(W(o_monitor_cells)+85,0xffff0002u);},[]{cell(W(o_monitor_cells)+85,player_id);}},
  {"cell17 tag 2",refuse_ref_tag,[]{cell(W(o_monitor_cells)+85,player_id,2);},[]{cell(W(o_monitor_cells)+85,player_id);}},
  {"cell17 heap tag differs from global9",refuse_ref_tag,[]{cell(W(o_monitor_cells)+85,player_id,8);},[]{cell(W(o_monitor_cells)+85,player_id);}},
  {"cell17 tag 0 (cleared by Show)",refuse_ref_tag,[]{cell(W(o_monitor_cells)+85,0,0);},[]{cell(W(o_monitor_cells)+85,player_id);}},
  {"cell16 monitor number 1",refuse_monitor_number,[]{cell(W(o_monitor_cells)+80,1);},[]{cell(W(o_monitor_cells)+80,0);}},
  {"cell11 priority is not consulted",refuse_ref_cell,[]{cell(W(o_monitor_cells)+55,player_id);cell(W(o_monitor_cells)+85,0xffff0002u);},[]{cell(W(o_monitor_cells)+55,20);cell(W(o_monitor_cells)+85,player_id);}},
  {"warp 0",refuse_warp,[]{cell(W(o_warp_cells)+15,0);},[]{cell(W(o_warp_cells)+15,1);}},
  {"killed 1",refuse_warp,[]{cell(W(o_warp_cells)+30,1);},[]{cell(W(o_warp_cells)+30,0);}},
  {"task id reused",refuse_task_id,[]{put(W(o_task+8),task_id+1);},[]{put(W(o_task+8),task_id);}},
  {"monitor class not 25e",refuse_monitor_class,[]{put(W(o_monitor+8),descriptor(1));},[]{put(W(o_monitor+8),descriptor(2));}},
 };
 for(const auto& b:bad){
  check(arm_pending(),"arm/pending for bad proof");b.mutate();
  FrameCall f=seam_frame(restore_mode_pc,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);
  const bool ok=!restore.pending&&payload()!=restore_rear_mode&&restore.last_refusal==b.refusal&&restore.consumed==0;
  if(!ok)std::printf("  bad proof %s refusal=%u pending=%u payload=%lu\n",b.label,restore.last_refusal,unsigned(restore.pending),static_cast<unsigned long>(payload()));
  check(ok,"bad tag/identity refuses without writing");b.undo();
 }
 check(arm_pending(),"arm/pending for stack overflow");
 {const std::uint32_t overflow=W(o_stack_base)-65*5;cell(overflow,1);FrameCall f=seam_frame(restore_mode_pc,overflow,0,W(o_monitor));run(0,f);
  check(!restore.pending&&restore.last_refusal==refuse_stack&&get<std::uint32_t>(overflow+1)==1,"stack beyond 64 cells refuses");}
 check(arm_pending(),"arm/pending for misaligned stack");
 {const std::uint32_t skew=seam_source(W(o_stack_base))-1;cell(skew,1);FrameCall f=seam_frame(restore_mode_pc,skew,0,W(o_monitor));run(0,f);
  check(!restore.pending&&restore.last_refusal==refuse_stack&&get<std::uint32_t>(skew+1)==1,"five-byte misaligned EBX refuses");build_seam_stack(W(o_stack_base));}
 // Destructor boundaries and update expiry.
 check(arm(),"arm for arbitrary destructor");destroy(0x41ccfb);check(!restore.armed&&restore.cancels[cancel_destructor]==1,"native deleting destructor clears the arm");
 check(arm_pending(),"arm/pending for second destruction");destroy();check(!restore.pending&&restore.cancels[cancel_second_destruction]==1,"second destruction clears pending");
 check(arm(),"arm for wrong warp prefix");build_warp_stack();put(W(o_stack_base)-45+10+1,0xedba1u);destroy();check(!restore.pending&&!restore.armed&&restore.last_refusal==refuse_prefix,"wrong destructor prefix refuses transfer");
 check(arm_pending(),"arm/pending for expiry");lifetime();for(unsigned i=0;i<=restore_pending_update_expiry;++i)update();check(!restore.pending&&restore.cancels[cancel_expiry]==1,"bounded update expiry cancels pending");
 // Arm refusals: mode 1, connect nonzero, native/script mismatch, unarmed update re-attempt only on mode change.
 reset_state();lifetime();put(W(o_cockpit+0x150),1u);update();check(!restore.armed&&restore.arms==0,"internal view never arms");
 put(W(o_cockpit+0x150),restore_rear_mode);update();check(restore.armed&&restore.arms==1,"return to rear view arms once");update();check(restore.arms==1,"armed updates perform no further identity walks");
 reset_state();lifetime();put(W(o_ship+0x94),controller_id);update();check(!restore.armed&&restore.arm_refusals==1,"native +94 != global9 refuses arming");put(W(o_ship+0x94),player_id);
 update();check(!restore.armed&&restore.arm_refusals==1,"refused arm is not retried every update");
 reset_state();lifetime();put(W(o_cockpit+0x1c0),2u);update();check(!restore.armed,"connect mode nonzero refuses arming");put(W(o_cockpit+0x1c0),0u);
 check(arm(),"arm for mode-left");put(W(o_cockpit+0x150),1u);update();check(!restore.armed&&restore.cancels[cancel_mode_left]==1,"leaving mode 258 disarms");put(W(o_cockpit+0x150),restore_rear_mode);
 // Armed prefilter: unrelated operand addresses never reach the handler.
 check(arm(),"arm for prefilter");{FrameCall f=seam_frame(0x2000,seam_source(W(o_stack_base)),0,W(o_monitor));run(0,f);check(restore.seam_calls==0&&restore.armed,"armed prefilter skips unrelated member stores");}
 reset_state();
}
// Real install/rollback on fixture-owned copies of the seven spans.
struct Synthetic { unsigned char* fn[restore_site_count]{};unsigned char* memory=nullptr; };
bool build_synthetic(Synthetic& s){
 s.memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!s.memory)return false;
 // Tails discard what the displaced prologues pushed so their register effects stay observable.
 const unsigned char tails[restore_site_count][4]={{0xc3},{0x83,0xc4,0x04,0xc3},{0x83,0xc4,0x04,0xc3},{0x83,0xc4,0x04,0xc3},{0x83,0xc4,0x0c,0xc3},{0x83,0xc4,0x08,0xc3},{0xc3}};
 const unsigned tail_len[restore_site_count]={1,4,4,4,4,4,1};
 for(unsigned i=0;i<restore_site_count;++i){s.fn[i]=s.memory+0x40*i;std::memcpy(s.fn[i],restore_specs[i].expected,restore_specs[i].length);std::memcpy(s.fn[i]+restore_specs[i].length,tails[i],tail_len[i]);}
 return FlushInstructionCache(GetCurrentProcess(),s.memory,0x1000)!=FALSE;
}
FrameCall native_frame(unsigned kind,std::uint32_t* scratch){
 FrameCall f{};f.flags=0x246;f.eax=std::uint32_t(reinterpret_cast<std::uintptr_t>(scratch));f.esi=f.eax+0x40;f.ebx=0x31;f.ebp=0x32;f.ecx=0x33;f.edx=0x34;f.edi=code_at(0x2000)+1;
 for(unsigned i=0;i<12;++i)f.stack[i]=0x100+i;
 if(kind==0){scratch[3]=0x10;} // [eax+0xc]=0x10 -> esi=scratch+0x50 (readable), CMP byte [esi],8
 if(kind==3)scratch[17]=0xdeadu; // mov [esi+4],ebx(0) clears it
 return f;
}
void install_rollback(){
 Synthetic s;check(build_synthetic(s),"synthetic span functions");if(!s.memory)return;
 engine_patch::SiteSpec local[restore_site_count];
 for(unsigned i=0;i<restore_site_count;++i){local[i]=restore_specs[i];local[i].address=reinterpret_cast<std::uintptr_t>(s.fn[i]);}
 alignas(16) std::uint32_t scratch[32]{};
 FrameCall baseline[restore_site_count];
 for(unsigned i=0;i<restore_site_count;++i){baseline[i]=native_frame(i,scratch);fixture_call_frame(s.fn[i],&baseline[i]);}
 check(baseline[1].out[4]==0x101&&baseline[2].out[4]==0x101&&baseline[3].out[4]==0&&scratch[17]==0&&baseline[4].out[2]==0x100&&baseline[6].out[7]==0x56e5f4&&(baseline[0].out[8]&1),"synthetic spans execute their displaced semantics");
 // Partial install: a corrupted fourth span rolls the first three back.
 s.fn[3][1]^=1;
 check(!restore_sites_install(local),"corrupted span refuses the install");
 bool clean=true;for(unsigned i=0;i<restore_site_count;++i)clean=clean&&!restore_sites[i].patched_in&&!std::memcmp(s.fn[i],i==3?restore_specs[i].expected:local[i].expected,i==3?1:local[i].length);
 check(clean&&!std::strcmp(restore_sites[3].status,"bytes_mismatch"),"partial install rolls earlier sites back to original bytes");
 s.fn[3][1]^=1;for(auto& site:restore_sites)site={};
 restore={};restore_enabled.store(true);restore_filter_publish();
 check(restore_sites_install(local),"all seven synthetic sites install");
 bool patched=true;for(unsigned i=0;i<restore_site_count;++i)patched=patched&&restore_sites[i].patched_in&&s.fn[i][0]==0xe9&&!std::strcmp(restore_sites[i].status,"active");
 check(patched,"seven sites active with jmp at entry");
 restore_filter[0].store(2);restore_filter[1].store(code_at(0x2000)+1); // force the seam's full path once
 for(unsigned i=0;i<restore_site_count;++i){
  FrameCall f=native_frame(i,scratch);SetLastError(0x2468ace0);fixture_call_frame(s.fn[i],&f);
  const bool same_regs=!std::memcmp(f.out,baseline[i].out,8*4)&&(f.out[8]&0x8d5)==(baseline[i].out[8]&0x8d5);
  check(same_regs&&GetLastError()==0x2468ace0,"patched span preserves registers, flags, LastError and displaced semantics");
 }
 check(restore.cancels[cancel_seam_unreadable]==0&&restore.seam_calls==1,"store seam handler ran once through the patched span and found nothing armed");
 check(restore_sites_restore(),"restore returns success");
 bool restored=true;for(unsigned i=0;i<restore_site_count;++i)restored=restored&&!restore_sites[i].patched_in&&!std::memcmp(s.fn[i],local[i].expected,local[i].length)&&!std::strcmp(restore_sites[i].status,"restored");
 check(restored,"rollback restores every original byte");
 for(unsigned i=0;i<restore_site_count;++i){FrameCall f=native_frame(i,scratch);fixture_call_frame(s.fn[i],&f);check(!std::memcmp(f.out,baseline[i].out,8*4),"restored span behaves as baseline");}
 for(auto& site:restore_sites)site={};
 restore={};restore_filter_publish();
}
void late_window(){
 Synthetic s;if(!build_synthetic(s))return;
 engine_patch::SiteSpec local[restore_site_count];
 for(unsigned i=0;i<restore_site_count;++i){local[i]=restore_specs[i];local[i].address=reinterpret_cast<std::uintptr_t>(s.fn[i]);}
 engine_patch::close_install_window("fixture first present");
 check(!restore_sites_install(local)&&!std::strcmp(restore_sites[0].status,"late_claim")&&s.fn[0][0]==restore_specs[0].expected[0],"late window refuses without touching bytes");
}
}
int main(int argc,char** argv){
 const bool cpu_only=argc==2&&!std::strcmp(argv[1],"--cpu-only");
 if(argc>1&&!cpu_only){std::fprintf(stderr,"usage: chase_transition_cpu_fixture.exe [--cpu-only]\n");return 2;}
 for(unsigned i=0;i<sizeof fixture_xmm_seed;++i)fixture_xmm_seed[i]=static_cast<unsigned char>(i*37+9);
 x3m::engine_patch::Emitter tail(8);void* continuation=tail.here();tail.byte(0xc3);if(!tail.finish())return 2;
 // Option-off path first: production initialize() with X3M_CHASE_VIEW_RESTORE unset claims none of the seven sites.
 SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE",nullptr);
 transition::initialize();
 {bool none=!transition::restore_installed();for(const auto& site:transition::restore_sites)none=none&&!site.claimed&&!site.patched_in;
  check(none&&!transition::restore_wanted()&&transition::restore_filter[0].load()==0,"option off: no restore site claimed, prefilter idle");}
 SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE",L"1");check(transition::restore_wanted(),"option parses X3M_CHASE_VIEW_RESTORE=1");
 transition::enabled.store(true);transition::diagnostic.store(true);
 LARGE_INTEGER timing_frequency{};
 transition::timing=QueryPerformanceFrequency(&timing_frequency)&&timing_frequency.QuadPart>0;
 transition::frequency=transition::timing?std::uint64_t(timing_frequency.QuadPart):0;
 check(transition::timing,"transition handler timing enabled for CPU preservation checks");
 x3m::chase_lead::enabled.store(false);
 x3m::chase_lead::hud_enabled.store(false);x3m::chase_lead::native_timing_enabled.store(false);
 void* stubs[18]{};
 for(unsigned kind=0;kind<18;++kind){
  void** next=nullptr;void* stub=kind<9?transition::emit(kind,&next):x3m::chase_lead::emit(kind-9,&next);
  stubs[kind]=stub;
  check(stub&&next,"emit succeeds");if(!stub||!next)return 2;
  check(x3m::engine_patch::store_pointer(next,continuation),"continuation stored");
  std::uint32_t arguments[16]{};Snapshot baseline{},hooked{};
  auto invoke=[&](void* target,Snapshot& out){fixture_output=&out;SetLastError(0x13572468);
   fixture_call(std::uint32_t(reinterpret_cast<std::uintptr_t>(target)),std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)),std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)),0,0,0);
   check(GetLastError()==0x13572468,"LastError preserved");
   check(fixture_entry_esp==fixture_exit_esp,"four-byte caller stack balanced");
   check(!std::memcmp(fixture_input_x87,out.x87,108),"incoming live x87 image preserved");
  };
  invoke(continuation,baseline);invoke(stub,hooked);
  for(unsigned r=0;r<9;++r)if(r!=3)check(baseline.regs[r]==hooked.regs[r],"GPR/flags preserved");
  check(!std::memcmp(baseline.xmm,hooked.xmm,128),"XMM0-7 preserved");
  check(baseline.mxcsr==hooked.mxcsr,"MXCSR preserved");
 }
 for(unsigned kind=0;kind<9;++kind)check(transition::handler_timing[kind].calls==1,"each transition handler records one timed CPU-check call");
 // Restore stubs: the same hostile-state preservation harness, full path (pending prefilter mode) and idle skip path.
 restore_fixture::code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x100000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 check(restore_fixture::code!=nullptr,"CODE buffer");if(!restore_fixture::code)return 2;
 restore_fixture::build_world();
 transition::vm_root=restore_fixture::W(0);transition::native_registry_root=restore_fixture::W(4);transition::cockpit_registry_root=restore_fixture::W(8);
 transition::restore_enabled.store(true);
 static unsigned char readable_operand[16]{0x94,0,0,0x24};fixture_edi=std::uint32_t(reinterpret_cast<std::uintptr_t>(readable_operand))+1;
 for(unsigned kind=0;kind<transition::restore_site_count;++kind){
  void** next=nullptr;void* stub=transition::emit_restore(kind,&next);restore_fixture::stubs[kind]=stub;
  check(stub&&next,"restore emit succeeds");if(!stub||!next)return 2;
  check(x3m::engine_patch::store_pointer(next,continuation),"restore continuation stored");
  for(unsigned mode=0;mode<2;++mode){
   std::uint32_t arguments[16]{};Snapshot baseline{},hooked{};
   auto invoke=[&](void* target,Snapshot& out){fixture_output=&out;SetLastError(0x13572468);transition::restore_filter[0].store(mode?2:0);transition::restore_filter[1].store(mode?fixture_edi:0);
    fixture_call(std::uint32_t(reinterpret_cast<std::uintptr_t>(target)),std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)),std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)),0,0,0);
    check(GetLastError()==0x13572468,"restore LastError preserved");
    check(fixture_entry_esp==fixture_exit_esp,"restore four-byte caller stack balanced");
    check(!std::memcmp(fixture_input_x87,out.x87,108),"restore incoming live x87 image preserved");
   };
   const auto calls=transition::restore.seam_calls;
   invoke(continuation,baseline);invoke(stub,hooked);
   for(unsigned r=0;r<9;++r)if(r!=3)check(baseline.regs[r]==hooked.regs[r],"restore GPR/flags preserved");
   check(!std::memcmp(baseline.xmm,hooked.xmm,128),"restore XMM0-7 preserved");
   check(baseline.mxcsr==hooked.mxcsr,"restore MXCSR preserved");
   if(kind==0)check(transition::restore.seam_calls==calls+(mode?1:0),mode?"pending prefilter reaches the handler":"idle prefilter skips the handler");
  }
 }
 fixture_edi=0x98765432;
 restore_fixture::install_rollback();
 restore_fixture::scenarios();
 restore_fixture::late_window();
 check(log_under_lock==0,"no log call under the chase SRW lock");
 check(seam_lines>0,"pending seam calls emit the chase_view_restore_seam diagnostic after the lock is released");
 checks+=restore_fixture::checks;failures+=restore_fixture::failures;
 if(!cpu_only)benchmark(continuation,stubs,restore_fixture::stubs[0]);
 std::printf("CHASE TRANSITION LEAD CPU stubs=18 restore_stubs=%u checks=%u failures=%u\n",transition::restore_site_count,checks,failures);
 return failures?1:0;
}
