// Execute each actual generated callback stub against a RET continuation.
// The separate site probe verifies displaced spans against the game executable.
#include "../../src/proxy/chase_transition.cpp"
#include "../../src/proxy/chase_lead.cpp"
#include <cstdio>
namespace x3m { void log(const char*,...) {} }
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
void fixture_call(std::uint32_t site,std::uint32_t args,std::uint32_t locals,std::uint32_t eax,std::uint32_t ecx,std::uint32_t edx);
}
// Four-byte caller stack; no 16-byte alignment promise. Save the fixture's
// state, seed hostile live state, run the real span, snapshot it, restore caller.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
"movl %esp,_fixture_entry_esp\n"
"pushfl\n pushal\n subl $256,%esp\n"
"fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
"movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
"movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
"movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
"fninit\n fld1\n fldpi\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
"movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
"movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
"movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
"movl $0x12345678,%esi\n movl $0x98765432,%edi\n pushl $0x647\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n call *240(%esp)\n"
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
int main(){
 for(unsigned i=0;i<sizeof fixture_xmm_seed;++i)fixture_xmm_seed[i]=static_cast<unsigned char>(i*37+9);
 x3m::engine_patch::Emitter tail(8);void* continuation=tail.here();tail.byte(0xc3);if(!tail.finish())return 2;
 transition::enabled.store(true);transition::diagnostic.store(true);
 x3m::chase_lead::enabled.store(false);
 for(unsigned kind=0;kind<12;++kind){
  void** next=nullptr;void* stub=kind<9?transition::emit(kind,&next):x3m::chase_lead::emit(kind-9,&next);
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
 std::printf("CHASE TRANSITION LEAD CPU stubs=12 checks=%u failures=%u\n",checks,failures);
 return failures?1:0;
}
