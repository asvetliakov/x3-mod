// Original synthetic registry and x86 ABI/SEH/patch-ownership fixture. No game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <excpt.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csetjmp>
#include <array>
#include "../../src/proxy/object_lifetime.h"
#include "../../src/proxy/engine_memory.h"
namespace lt=x3m::object_lifetime;
namespace em=x3m::engine_memory;
unsigned checks=0,failures=0,calls=0;
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
struct Node {std::uint32_t padding[10]{},handle=0;};
struct Link {Link* next;std::uint32_t key;Node* value;};
struct Map {Link** buckets=nullptr;std::uint32_t capacity=8,counter=0,count=0;};
struct Engine {std::uint32_t padding[3]{};Map* registry=nullptr;};
Map primary{},unrelated{};Engine engine{};Engine* engine_slot=&engine;
Node node{{},42},camera{{},7},replacement{{},42},third{{},9};
bool check_inside=false,raise_inside=false,nest_inside=false,rehash_inside=false;
extern "C" void insert_entry();
extern "C" void remove_entry();
extern "C" void remove_nonempty();
extern "C" void destroy_entry();
extern "C" void __cdecl invoke_custom(void*,Map*,std::uint32_t,std::uintptr_t);
extern "C" {
alignas(16) unsigned char input_fx[512]{},output_fx[512]{};
std::uint32_t input_regs[9]{},output_regs[9]{};
alignas(16) std::uint32_t xmm_seed[4]={0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
std::uint32_t mxcsr_seed=0x3fa0;
std::uint16_t x87_control_seed=0x077f;
std::uint32_t call_stack_before=0,call_stack_after=0;
}
// FNV-1a over every published lifetime field (read-path identity comparison).
void fold(std::uint64_t& hash,const void* bytes,std::size_t size){const auto* p=static_cast<const unsigned char*>(bytes);for(std::size_t i=0;i<size;++i){hash^=p[i];hash*=1099511628211ull;}}
void fold_snapshot(std::uint64_t& hash,const lt::Snapshot& s){
    const std::uint32_t known=s.known,reason=static_cast<std::uint32_t>(s.reason);
    fold(hash,&known,4);fold(hash,&reason,4);fold(hash,&s.observer_epoch,8);fold(hash,&s.load_epoch,8);fold(hash,&s.registry_epoch,8);
    fold(hash,&s.node_serial,8);fold(hash,&s.camera_serial,8);fold(hash,&s.mutation_revision,8);
}
lt::Snapshot snapshot(){lt::Snapshot result{};lt::current(reinterpret_cast<std::uintptr_t>(&primary),reinterpret_cast<std::uintptr_t>(&node),node.handle,reinterpret_cast<std::uintptr_t>(&camera),camera.handle,&result);return result;}
void inside(){
    ++calls;
    if(check_inside){auto s=snapshot();check(!s.known&&s.reason==lt::Reason::MutationInProgress,"mutation is unavailable before backend writes");}
    if(raise_inside)RaiseException(0xe3450001,0,0,nullptr);
    if(nest_inside){nest_inside=false;invoke_custom(reinterpret_cast<void*>(&insert_entry),&primary,camera.handle,reinterpret_cast<std::uintptr_t>(&camera));}
}
void empty(Map& map){
    if(map.buckets){for(unsigned i=0;i<map.capacity;++i){auto* link=map.buckets[i];while(link){auto* next=link->next;std::free(link);link=next;}}std::free(map.buckets);}
    map={};
}
void rehash(Map& map){
    auto** fresh=static_cast<Link**>(std::calloc(map.capacity*2,sizeof(Link*)));
    for(unsigned i=0;i<map.capacity;++i){auto* link=map.buckets[i];while(link){auto* next=link->next;const unsigned index=link->key&(map.capacity*2-1);link->next=fresh[index];fresh[index]=link;link=next;}}
    std::free(map.buckets);map.buckets=fresh;map.capacity*=2;
}
extern "C" int __cdecl insert_backend(Map* map,std::uint32_t key,Node* value){
    inside();if(!key){SetLastError(0x246);return 0;}
    if(!map->buckets)map->buckets=static_cast<Link**>(std::calloc(map->capacity,sizeof(Link*)));
    auto*& head=map->buckets[key&(map->capacity-1)];
    for(auto* p=head;p;p=p->next)if(p->key==key){p->value=value;SetLastError(0x246);return 0;}
    auto* p=static_cast<Link*>(std::malloc(sizeof(Link)));*p={head,key,value};head=p;++map->count;if(rehash_inside){rehash_inside=false;rehash(*map);}SetLastError(0x246);return 1;
}
extern "C" std::uintptr_t __cdecl remove_backend(Map* map,std::uint32_t key){
    inside();auto** p=&map->buckets[key&(map->capacity-1)];while(*p){if((*p)->key==key){auto* found=*p;*p=found->next;auto result=reinterpret_cast<std::uintptr_t>(found->value);std::free(found);--map->count;SetLastError(0x246);return result;}p=&(*p)->next;}SetLastError(0x246);return 0;
}
extern "C" int __cdecl destroy_backend(Map* map){inside();empty(*map);SetLastError(0x246);return 0x31415926;}
extern "C" int __cdecl load_backend(std::uint32_t stream){inside();SetLastError(0x246);return stream?0x31415926:0;}
#define CAPTURE_INPUT "pushfl\n\tpushal\n\tmovl %esp,%esi\n\tmovl $_input_regs,%edi\n\tmovl $9,%ecx\n\tcld\n\trep movsl\n\tfxsave _input_fx\n\tpopal\n\tpopfl\n\t"
#define OUTPUT_STATE "movl $0x789abcde,%ecx\n\tmovl $0xfedcba98,%edx\n\tfld1\n\tmovdqu _xmm_seed,%xmm2\n\tpushl $0x246\n\tpopfl\n\tret\n\t"
extern "C" __attribute__((naked)) void insert_entry(){__asm__ __volatile__(
    ".byte 0x55,0x8b,0x6c,0x24,0x08\n\t" CAPTURE_INPUT
    "pushl %ebx\n\tpushl %esi\n\tpushl %edi\n\tpushl 24(%esp)\n\tpushl %ebp\n\tpushl %edi\n\tcall _insert_backend\n\taddl $12,%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\t" OUTPUT_STATE);}
extern "C" __attribute__((naked)) void remove_entry(){__asm__ __volatile__(
    "movl (%edi),%eax\n\ttestl %eax,%eax\n\tjnz _remove_nonempty\n\txorl %eax,%eax\n\tret\n\t"
    ".globl _remove_nonempty\n_remove_nonempty:\n\t.byte 0x8b,0x4f,0x04,0x83,0xe9,0x01\n\t" CAPTURE_INPUT
    "pushl %ebx\n\tpushl %esi\n\tpushl %edi\n\tpushl %edx\n\tpushl %edi\n\tcall _remove_backend\n\taddl $8,%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\t" OUTPUT_STATE);}
extern "C" __attribute__((naked)) void load_entry(){__asm__ __volatile__(CAPTURE_INPUT
    "pushl 4(%esp)\n\tcall _load_backend\n\taddl $4,%esp\n\t" OUTPUT_STATE);}
extern "C" __attribute__((naked)) void destroy_entry(){__asm__ __volatile__(
    ".byte 0x53,0x8b,0x5c,0x24,0x08\n\t" CAPTURE_INPUT
    "pushl %ebx\n\tcall _destroy_backend\n\taddl $4,%esp\n\tpopl %ebx\n\t" OUTPUT_STATE);}
// Preserve this fixture caller's state independently; expose every target output
// register/flag and the original target's FP/SSE state before C++ touches them.
extern "C" __attribute__((naked)) void __cdecl invoke_custom(void*,Map*,std::uint32_t,std::uintptr_t){__asm__ __volatile__(
    "pushal\n\tsubl $16,%esp\n\tmovl 52(%esp),%eax\n\tmovl %eax,8(%esp)\n\t"
    "movl 60(%esp),%eax\n\tmovl %eax,(%esp)\n\tmovl 64(%esp),%eax\n\tmovl %eax,4(%esp)\n\t"
    "movl 56(%esp),%edi\n\tmovl 60(%esp),%edx\n\tmovl $0x12345678,%esi\n\tmovl $0x23456789,%ebx\n\t"
    "movl $0x3456789a,%ecx\n\tmovl $0x456789ab,%eax\n\tmovl $0x56789abc,%ebp\n\t"
    "fninit\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfld1\n\tfninit\n\tfldcw _x87_control_seed\n\tfldz\n\tfldz\n\tfdivp\n\tfld1\n\tldmxcsr _mxcsr_seed\n\tmovdqu _xmm_seed,%xmm0\n\tmovdqu _xmm_seed,%xmm1\n\t"
    "movdqu _xmm_seed,%xmm2\n\tmovdqu _xmm_seed,%xmm3\n\tmovdqu _xmm_seed,%xmm4\n\tmovdqu _xmm_seed,%xmm5\n\t"
    "movdqu _xmm_seed,%xmm6\n\tmovdqu _xmm_seed,%xmm7\n\tpushl $0x246\n\tpopfl\n\tmovl %esp,12(%esp)\n\tcall *8(%esp)\n\t"
    "pushfl\n\tpushal\n\tmovl 48(%esp),%eax\n\tmovl %eax,_call_stack_before\n\tleal 36(%esp),%eax\n\tmovl %eax,_call_stack_after\n\tmovl %esp,%esi\n\tmovl $_output_regs,%edi\n\tmovl $9,%ecx\n\tcld\n\trep movsl\n\t"
    "fxsave _output_fx\n\tpopal\n\tpopfl\n\taddl $16,%esp\n\tpopal\n\tret\n\t");}
void insert(Map& map,Node& value){SetLastError(0x145);invoke_custom(reinterpret_cast<void*>(&insert_entry),&map,value.handle,reinterpret_cast<std::uintptr_t>(&value));check(GetLastError()==0x246,"insert backend LastError preserved");check(call_stack_before==call_stack_after,"exact caller ESP preserved");}
void remove(Map& map,std::uint32_t key){invoke_custom(reinterpret_cast<void*>(&remove_entry),&map,key,0);}
void destroy(Map& map){invoke_custom(reinterpret_cast<void*>(&destroy_entry),&map,reinterpret_cast<std::uintptr_t>(&map),0);}
std::jmp_buf outer_jump;
extern "C" __attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl fixture_handler(EXCEPTION_RECORD* record,void* frame,CONTEXT*,void*){
    if(!(record->ExceptionFlags&(EXCEPTION_UNWINDING|EXCEPTION_EXIT_UNWIND))&&record->ExceptionCode==0xe3450001){RtlUnwind(frame,nullptr,nullptr,nullptr);void* previous=*static_cast<void**>(frame);__asm__ __volatile__("movl %0,%%fs:0"::"r"(previous));std::longjmp(outer_jump,1);}return ExceptionContinueSearch;
}
extern "C" __attribute__((naked)) void __cdecl catch_outer(void*,Map*,std::uint32_t,std::uintptr_t){__asm__ __volatile__(
    "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\tmovl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\t"
    "movl $_fixture_handler,-4(%ebp)\n\tleal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
    "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall _invoke_custom\n\taddl $16,%esp\n\t"
    "movl -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\tleave\n\tret\n\t");}
void write_bytes(void* address,const void* bytes,unsigned count){DWORD old=0,unused=0;check(VirtualProtect(address,count,PAGE_EXECUTE_READWRITE,&old)!=0,"fixture patch protect");std::memcpy(address,bytes,count);check(FlushInstructionCache(GetCurrentProcess(),address,count)!=0,"fixture patch flush");check(VirtualProtect(address,count,old,&unused)!=0,"fixture patch restore protection");}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);engine.registry=&primary;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!memory)return 2;
    const unsigned char prefix[]={0xff,0x74,0x24,0x04,0xe8};std::memcpy(memory,prefix,sizeof prefix);
    const std::uint32_t delta=reinterpret_cast<std::uintptr_t>(&load_entry)-(reinterpret_cast<std::uintptr_t>(memory)+9);std::memcpy(memory+5,&delta,4);
    const unsigned char suffix[]={0x83,0xc4,0x04,0xc3};std::memcpy(memory+9,suffix,sizeof suffix);DWORD old=0;VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old);FlushInstructionCache(GetCurrentProcess(),memory,4096);
    lt::FixtureSites sites{reinterpret_cast<void*>(&insert_entry),reinterpret_cast<void*>(&remove_nonempty),reinterpret_cast<void*>(&destroy_entry),memory+4,reinterpret_cast<void*>(&load_entry),reinterpret_cast<std::uintptr_t>(&engine_slot)};
    auto load=[&](unsigned stream){invoke_custom(memory,&primary,stream,0);};
    unsigned char originals[4][6]{};void* addresses[]={sites.insert_entry,sites.remove_nonempty,sites.destroy_entry,sites.load_call};unsigned sizes[]={5,6,5,5};for(unsigned i=0;i<4;++i)std::memcpy(originals[i],addresses[i],sizes[i]);
    SetEnvironmentVariableW(L"X3M_OBJECT_LIFETIME",nullptr);check(!lt::initialize()&&!lt::active(),"default off");SetEnvironmentVariableW(L"X3M_OBJECT_LIFETIME",L"1");check(!lt::initialize()&&!lt::active(),"non-game executable rejected");
    for(unsigned kind=0;kind<4;++kind){auto bad=sites;if(kind==0)bad.insert_entry=memory+9;else if(kind==1)bad.remove_nonempty=memory+9;else if(kind==2)bad.destroy_entry=memory+9;else bad.load_target=reinterpret_cast<void*>(&load_backend);check(!lt::fixture_install(bad)&&!lt::active()&&!lt::recovery_required(),"wrong instruction boundary or call target rejects atomically");for(unsigned i=0;i<4;++i)check(!std::memcmp(addresses[i],originals[i],sizes[i]),"rejected layout leaves all sites unchanged");}
    check(!lt::fixture_install(sites,0)&&!lt::fixture_install(sites,16385),"invalid observer capacity rejected");
    // Baseline versus wrapped full register/flag/FX effects on the same original.
    insert(primary,node);std::array<std::uint32_t,9> before_input{},before_output{};std::memcpy(before_input.data(),input_regs,sizeof input_regs);std::memcpy(before_output.data(),output_regs,sizeof output_regs);std::array<unsigned char,512> before_ifx{},before_ofx{};std::memcpy(before_ifx.data(),input_fx,512);std::memcpy(before_ofx.data(),output_fx,512);empty(primary);
    check(lt::fixture_install(sites),"install all four boundaries");check(lt::stats().baseline_complete&&lt::stats().baseline_entries==0,"empty baseline validated");check_inside=true;insert(primary,node);check_inside=false;
    for(unsigned i=0;i<9;++i)if(i!=3){check(input_regs[i]==before_input[i],"input register/flag matches original");check(output_regs[i]==before_output[i],"output register/flag matches original");}
    check(!std::memcmp(before_ifx.data(),input_fx,160)&&!std::memcmp(before_ifx.data()+160,input_fx+160,128),"input x87/SSE/MXCSR matches original");check(!std::memcmp(before_ofx.data(),output_fx,160)&&!std::memcmp(before_ofx.data()+160,output_fx+160,128),"output x87/SSE/MXCSR matches original");
    insert(primary,camera);auto first=snapshot();check(first.known&&first.node_serial&&first.camera_serial&&first.node_serial!=first.camera_serial,"observed births get distinct tokens");check(first.observer_epoch&&first.registry_epoch,"separate epochs nonzero");
    SetLastError(0x567);snapshot();check(GetLastError()==0x567,"snapshot preserves LastError");
    insert(unrelated,third);auto unchanged=snapshot();check(unchanged.known&&unchanged.node_serial==first.node_serial&&unchanged.mutation_revision==first.mutation_revision,"unrelated map operation does not affect lifetimes");
    insert(primary,node);auto overwrite=snapshot();check(output_regs[7]==0&&overwrite.known&&overwrite.node_serial!=first.node_serial,"same-pointer overwrite EAX zero still starts new lifetime");
    remove(primary,node.handle);check(!snapshot().known,"removal retires before free");insert(primary,node);auto reused=snapshot();check(reused.known&&reused.node_serial!=overwrite.node_serial,"same pointer and handle reuse gets new serial");
    remove(primary,camera.handle);check(!snapshot().known&&snapshot().reason==lt::Reason::UnknownCameraBirth,"camera removal retires its token");insert(primary,camera);
    auto pre_absent=snapshot();remove(primary,12345);check(snapshot().known&&snapshot().node_serial==pre_absent.node_serial,"absent-key removal preserves existing lifetimes");
    Node collision{{},50};insert(primary,collision);remove(primary,collision.handle);check(snapshot().known,"collision-chain deletion preserves other key identities");
    rehash_inside=true;insert(primary,third);check(snapshot().known&&primary.capacity==16,"rehash inside central insertion preserves known nodes");
    insert(primary,replacement);check(!snapshot().known,"replacement pointer is not old object");insert(primary,node);
    check_inside=true;nest_inside=true;insert(primary,node);check_inside=false;check(snapshot().known,"nested insert completes both observations");
    auto pre_load=snapshot();check_inside=true;load(1);check_inside=false;auto after_load=snapshot();check(!after_load.known&&after_load.load_epoch!=pre_load.load_epoch,"load invalidates before successful call");insert(primary,node);insert(primary,camera);load(0);check(!snapshot().known&&output_regs[7]==0,"failed load also invalidates");
    insert(primary,node);insert(primary,camera);raise_inside=true;const int caught=setjmp(outer_jump);if(!caught)catch_outer(reinterpret_cast<void*>(&insert_entry),&primary,node.handle,reinterpret_cast<std::uintptr_t>(&node));raise_inside=false;check(caught==1,"foreign SEH propagated to outer frame");check(!snapshot().known&&snapshot().reason!=lt::Reason::MutationInProgress,"SEH clears in-flight and invalidates old tokens");insert(primary,node);insert(primary,camera);check(snapshot().known,"fresh births recover after foreign unwind");
    auto before_destroy=snapshot();check_inside=true;destroy(primary);check_inside=false;check(!snapshot().known,"destroy invalidates before storage release");insert(primary,node);insert(primary,camera);check(snapshot().known&&snapshot().registry_epoch!=before_destroy.registry_epoch,"same map address after destruction has new generation");
    for(volatile unsigned kind=0;kind<4;++kind){
        insert(primary,node);insert(primary,camera);raise_inside=true;
        const int caught_kind=setjmp(outer_jump);
        if(!caught_kind){void* target=kind==0?reinterpret_cast<void*>(&insert_entry):kind==1?reinterpret_cast<void*>(&remove_entry):kind==2?reinterpret_cast<void*>(&destroy_entry):memory;const unsigned arg=kind==2?reinterpret_cast<std::uintptr_t>(&primary):kind==3?1:node.handle;catch_outer(target,&primary,arg,reinterpret_cast<std::uintptr_t>(&node));}
        raise_inside=false;check(caught_kind==1,"all boundaries propagate foreign SEH");check(!snapshot().known&&snapshot().reason!=lt::Reason::MutationInProgress,"all boundaries clear unwind scopes and invalidate tokens");
    }
    check(lt::shutdown(),"normal shutdown");
    // Compare every intercepted boundary independently against its original ABI.
    // ESP differs inside the extra call frame, but caller ESP is checked separately.
    for(unsigned kind=0;kind<4;++kind){
        empty(primary);insert_backend(&primary,node.handle,&node);
        void* target=kind==0?reinterpret_cast<void*>(&insert_entry):kind==1?reinterpret_cast<void*>(&remove_entry):kind==2?reinterpret_cast<void*>(&destroy_entry):memory;
        const unsigned arg=kind==2?reinterpret_cast<std::uintptr_t>(&primary):kind==3?1:node.handle;
        invoke_custom(target,&primary,arg,reinterpret_cast<std::uintptr_t>(&node));
        std::memcpy(before_input.data(),input_regs,sizeof input_regs);std::memcpy(before_output.data(),output_regs,sizeof output_regs);std::memcpy(before_ifx.data(),input_fx,512);std::memcpy(before_ofx.data(),output_fx,512);
        empty(primary);insert_backend(&primary,node.handle,&node);check(lt::fixture_install(sites),"install per-boundary ABI comparison");
        invoke_custom(target,&primary,arg,reinterpret_cast<std::uintptr_t>(&node));
        for(unsigned i=0;i<9;++i)if(i!=3){check(input_regs[i]==before_input[i],"all boundaries input register/flag preserved");check(output_regs[i]==before_output[i],"all boundaries output register/flag preserved");}
        check(!std::memcmp(before_ifx.data(),input_fx,288),"all boundaries input FX state preserved");check(!std::memcmp(before_ofx.data(),output_fx,288),"all boundaries output FX state preserved");
        check(call_stack_before==call_stack_after,"all boundaries exact caller ESP preserved");check(lt::shutdown(),"per-boundary ABI shutdown");
    }
    empty(primary);insert(primary,node);insert(primary,camera);
    // Objects already present at installation need a complete baseline, not a lazy draw adoption.
    check(lt::fixture_install(sites),"reinstall with populated registry");check(lt::stats().baseline_complete&&lt::stats().baseline_entries==2&&snapshot().known,"complete initial map snapshot includes existing camera");check(lt::shutdown(),"baseline shutdown");
    auto bad_baseline=[&](unsigned limit=16384){check(lt::fixture_install(sites,limit),"invalid baseline keeps observer available for future births");check(!lt::stats().baseline_complete&&lt::stats().baseline_entries==0&&!snapshot().known,"invalid baseline publishes no partial tokens");check(lt::shutdown(),"invalid baseline shutdown");};
    auto* node_link=primary.buckets[node.handle&(primary.capacity-1)];auto* camera_link=primary.buckets[camera.handle&(primary.capacity-1)];
    auto* saved_value=camera_link->value;camera_link->value=&node;bad_baseline();camera_link->value=saved_value;
    auto* saved_next=node_link->next;node_link->next=node_link;bad_baseline();node_link->next=saved_next;
    primary.count=1;bad_baseline();primary.count=3;bad_baseline();primary.count=2;
    node_link->value=reinterpret_cast<Node*>(1);bad_baseline();node_link->value=&node;
    node.handle=50;bad_baseline();node.handle=42;
    bad_baseline(1);
    auto** saved_buckets=primary.buckets;primary.buckets=nullptr;bad_baseline();primary.buckets=saved_buckets;
    primary.capacity=3;bad_baseline();primary.capacity=8;
    check(lt::fixture_install(sites,16384,0,0,false),"installation without baseline");check(!snapshot().known,"draw lookup cannot lazily adopt existing entries");insert(primary,node);insert(primary,camera);check(snapshot().known,"observed overwrites establish births after absent baseline");check(lt::shutdown(),"absent baseline shutdown");
    // No renderer exists at early D3D construction. Unrelated generic maps are safe while dormant.
    empty(primary);engine_slot=nullptr;check(lt::fixture_install(sites),"dormant installation");insert(unrelated,third);check(lt::active()&&!snapshot().known,"unrelated map while engine absent remains dormant");engine_slot=&engine;insert(primary,node);insert(primary,camera);check(snapshot().known,"later renderer births become known");engine_slot=nullptr;auto lost=snapshot();check(!lost.known&&!lt::active(),"loss after trusted registry permanently invalidates");engine_slot=&engine;check(!snapshot().known,"registry restoration cannot reuse stale serial");check(lt::shutdown(),"loss shutdown");
    check(lt::fixture_install(sites),"install registry-loss mutation case");check(snapshot().known,"registry loss starts from trusted baseline");engine_slot=nullptr;insert(unrelated,third);check(!lt::active(),"generic map call detects loss after trusted registry");engine_slot=&engine;check(!snapshot().known,"post-loss birth observation remains unavailable");check(lt::shutdown(),"mutation registry-loss shutdown");
    // Exact bound exhaustion disables instead of evicting live identities.
    empty(primary);check(lt::fixture_install(sites,2),"bounded capacity installation");insert(primary,node);insert(primary,camera);check(snapshot().known,"capacity holds two entries");insert(primary,third);check(!lt::active()&&snapshot().reason==lt::Reason::CapacityExhausted,"capacity overflow fails closed without token recycling");check(lt::shutdown(),"capacity shutdown");empty(primary);
    // Each patch failure and each rollback failure at every site remains retryable.
    for(unsigned site=0;site<4;++site)for(unsigned stage=1;stage<=6;++stage){check(!lt::fixture_install(sites,16384,stage,site),"injected install failure");check(!lt::active(),"failed install never publishes observation");if(stage>=4)check(lt::recovery_required(),"failed rollback retains trampoline ownership");check(lt::shutdown()&&!lt::recovery_required(),"rollback retry succeeds");for(unsigned i=0;i<4;++i)check(!std::memcmp(addresses[i],originals[i],sizes[i]),"all original bytes restored after failed install");}
    for(unsigned site=0;site<4;++site)for(unsigned stage=1;stage<=3;++stage){check(lt::fixture_install(sites),"install for shutdown fault");check(!lt::fixture_shutdown(stage,site)&&!lt::active()&&lt::recovery_required(),"shutdown fault retains disabled ownership");check(lt::shutdown()&&!lt::recovery_required(),"shutdown retry succeeds");}
    // Losing even one hook makes equality of a reused pointer+handle untrustworthy.
    insert(primary,node);insert(primary,camera);check(lt::fixture_install(sites),"install ownership case");check(snapshot().known,"ownership baseline known");unsigned char ours[6]{};std::memcpy(ours,sites.insert_entry,5);write_bytes(sites.insert_entry,originals[0],5);insert(primary,node);check(!snapshot().known&&!lt::active(),"bypassed birth cannot retain old token");check(lt::shutdown(),"original-restored ownership shutdown");
    check(lt::fixture_install(sites),"install foreign code case");std::memcpy(ours,sites.insert_entry,5);unsigned char foreign[5];std::memcpy(foreign,ours,5);foreign[4]^=1;write_bytes(sites.insert_entry,foreign,5);check(!snapshot().known&&!lt::active(),"foreign hook ownership disables observation");check(!lt::shutdown()&&lt::recovery_required(),"foreign replacement is not overwritten");check(!std::memcmp(sites.insert_entry,foreign,5),"foreign bytes preserved");write_bytes(sites.insert_entry,ours,5);check(lt::shutdown(),"foreign ownership repair permits retry");
    // Read path (engine_memory): identical lifetime records and per-call cost of
    // the ReadProcessMemory path against validated direct reads, then a bucket
    // array on a page decommitted between frames must fail safely in both modes.
    {
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        const unsigned iterations=20000;
        std::uint64_t hashes[2]{};
        empty(primary);insert(primary,node);insert(primary,camera);
        check(lt::fixture_install(sites),"install read-path case");
        for(unsigned m=0;m<2;++m){
            SetEnvironmentVariableW(L"X3M_ENGINE_READS",m?L"direct":L"rpm");em::configure();
            check(em::mode()==(m?em::Mode::Direct:em::Mode::ReadProcessMemory),"read mode selected");
            std::uint64_t hash=1469598103934665603ull;unsigned unknown=0;
            const auto before=em::stats();
            LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
            for(unsigned i=0;i<iterations;++i){if((i&255)==0)em::next_frame();const auto s=snapshot();if(!s.known)++unknown;fold_snapshot(hash,s);}
            QueryPerformanceCounter(&end);
            const auto after=em::stats();
            hashes[m]=hash;check(!unknown,"read-path snapshots known");
            std::printf("TIMING mode=%s snapshot_us=%.3f reads_per_call=%.2f queries_per_call=%.4f syscalls_per_call=%.2f\n",m?"direct":"rpm",
                double(end.QuadPart-begin.QuadPart)*1e6/double(frequency.QuadPart)/iterations,double(after.reads-before.reads)/iterations,
                double(after.queries-before.queries)/iterations,double(after.syscalls-before.syscalls)/iterations);
        }
        check(hashes[0]==hashes[1],"identical lifetime records in both read modes");
        std::printf("IDENTITY rpm=%016llx direct=%016llx equal=%u\n",static_cast<unsigned long long>(hashes[0]),static_cast<unsigned long long>(hashes[1]),hashes[0]==hashes[1]);
        check(lt::shutdown(),"read-path shutdown");
        for(unsigned m=0;m<2;++m){
            SetEnvironmentVariableW(L"X3M_ENGINE_READS",m?L"direct":L"rpm");em::configure();
            empty(primary);insert(primary,node);insert(primary,camera);
            auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));check(page!=nullptr,"bucket page committed");
            std::memcpy(page,primary.buckets,primary.capacity*sizeof(Link*));std::free(primary.buckets);primary.buckets=reinterpret_cast<Link**>(page);
            check(lt::fixture_install(sites),"install with the bucket array on the fixture page");
            em::next_frame();check(snapshot().known,"bucket array on a committed page is readable");
            Link* links[64]{};unsigned link_count=0;for(unsigned i=0;i<primary.capacity;++i)for(auto* l=primary.buckets[i];l;l=l->next)links[link_count++]=l;
            em::next_frame();check(VirtualFree(page,0,MEM_DECOMMIT)!=0,"bucket page decommitted between frames");
            const auto lost=snapshot();check(!lost.known&&lost.reason==lt::Reason::LookupUnavailable,"decommitted bucket page fails safely and retires the identities");
            check(VirtualAlloc(page,4096,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"bucket page recommitted");
            em::next_frame();check(!snapshot().known,"retired identities do not revive after the recommit");
            check(lt::shutdown(),"decommit case shutdown");
            for(unsigned i=0;i<link_count;++i){std::free(links[i]);}
            primary.buckets=nullptr;primary.count=0;
            check(VirtualFree(page,0,MEM_RELEASE)!=0,"bucket page released");
        }
        SetEnvironmentVariableW(L"X3M_ENGINE_READS",nullptr);em::configure();
        insert(primary,node);insert(primary,camera); // the retirement case below expects both present
    }
    // Retirement journal: bounded ring, written only for a registered consumer.
    {
        using K=lt::JournalKind;
        static Node burst[6];for(unsigned i=0;i<6;++i)burst[i].handle=100+i;
        static lt::JournalEntry out[lt::JournalCapacity];
        auto serial_of=[&](Node& n){lt::Snapshot s{};lt::current(reinterpret_cast<std::uintptr_t>(&primary),reinterpret_cast<std::uintptr_t>(&n),n.handle,reinterpret_cast<std::uintptr_t>(&camera),camera.handle,&s);return s.known?s.node_serial:0;};
        auto raw_insert=[&](Node& n){invoke_custom(reinterpret_cast<void*>(&insert_entry),&primary,n.handle,reinterpret_cast<std::uintptr_t>(&n));};
        unsigned case_failures=failures;auto end_case=[&](const char* name){std::printf("JOURNAL_CASE name=%s result=%s\n",name,failures==case_failures?"PASS":"FAIL");case_failures=failures;};
        empty(primary);check(lt::fixture_install(sites),"install journal case");
        const auto idle=lt::journal_stats();insert(primary,node);insert(primary,camera);insert(primary,node);remove(primary,node.handle);load(1);
        check(lt::journal_stats().head==idle.head&&lt::journal_stats().consumers==0,"no consumer: retirements and epoch bumps write nothing");
        lt::JournalCursor stale{};auto unregistered=lt::journal_drain(stale,out,lt::JournalCapacity);
        check(unregistered.overflow&&!unregistered.available&&!unregistered.count,"drain without a consumer demands revalidation");
        check(!stale.valid(),"refused drain leaves an invalid cursor invalid");end_case("no_consumer");
        auto cursor=lt::journal_register();check(cursor.valid()&&lt::journal_stats().consumers==1,"consumer registered");
        auto quiet=lt::journal_drain(cursor,out,lt::JournalCapacity);check(quiet.available&&!quiet.overflow&&!quiet.count&&!quiet.more,"fresh cursor starts at the head");
        insert(primary,camera);std::uint64_t serials[6]{};for(unsigned i=0;i<6;++i){insert(primary,burst[i]);serials[i]=serial_of(burst[i]);check(serials[i]!=0,"journal node born");}
        check(!lt::journal_drain(cursor,out,lt::JournalCapacity).count,"births of fresh keys append nothing");
        for(unsigned i=0;i<4;++i)remove(primary,burst[i].handle);
        remove(primary,54321); // absent key: nothing retired
        insert(primary,burst[4]); // overwrite retires the old lifetime
        remove_backend(&primary,burst[5].handle);check(!serial_of(burst[5]),"failed membership retires"); // behind the observer's back
        SetLastError(0x321);auto drained=lt::journal_drain(cursor,out,lt::JournalCapacity);check(GetLastError()==0x321,"drain preserves LastError");
        check(drained.available&&!drained.overflow&&!drained.more&&drained.count==7,"retire N below capacity: all drained");
        bool ordered=drained.count==7;for(unsigned i=0;ordered&&i<5;++i)ordered=out[i].kind==K::Retired&&out[i].node_serial==serials[i]&&out[i].load_epoch==drained.load_epoch&&out[i].registry_epoch==drained.registry_epoch;
        check(ordered&&out[5].kind==K::Retired&&out[5].node_serial==serials[5]&&out[6].kind==K::Retired&&out[6].node_serial!=0,"journal order and keys: removals, overwrite, failed membership (node then camera)");
        check(serial_of(burst[4])==0,"failed membership retired the camera too");insert(primary,camera);check(serial_of(burst[4])!=0&&serial_of(burst[4])!=serials[4],"overwritten node has a new serial");
        end_case("retire_in_order");
        // Partial drain through a small buffer keeps order and position.
        insert(primary,burst[0]);insert(primary,burst[1]);insert(primary,burst[2]);const std::uint64_t a=serial_of(burst[0]),b=serial_of(burst[1]),c=serial_of(burst[2]);
        remove(primary,burst[0].handle);remove(primary,burst[1].handle);remove(primary,burst[2].handle);
        auto part=lt::journal_drain(cursor,out,2);check(part.count==2&&part.more&&out[0].node_serial==a&&out[1].node_serial==b,"small buffer drains the oldest first");
        part=lt::journal_drain(cursor,out,2);check(part.count==1&&!part.more&&out[0].node_serial==c,"remainder drained from the cursor");
        end_case("partial_drain");
        // A drain that cannot deliver is invalid, never an endless more=true.
        raw_insert(burst[0]);remove(primary,burst[0].handle);const auto held=cursor;
        auto refused=lt::journal_drain(cursor,nullptr,lt::JournalCapacity);check(refused.invalid&&!refused.count&&!refused.more&&!refused.overflow&&cursor.sequence==held.sequence,"null buffer: invalid, no more, cursor unmoved");
        refused=lt::journal_drain(cursor,out,0);check(refused.invalid&&!refused.count&&!refused.more&&!refused.overflow&&cursor.sequence==held.sequence,"zero capacity: invalid, no more, cursor unmoved");
        refused=lt::journal_drain(cursor,out,lt::JournalCapacity);check(!refused.invalid&&refused.count==1,"entry still delivered to a valid drain");end_case("invalid_drain");
        // Epoch bumps and table clears are flush-all entries.
        const auto pre=lt::journal_drain(cursor,out,lt::JournalCapacity);load(1);auto flushed=lt::journal_drain(cursor,out,lt::JournalCapacity);
        check(flushed.count==1&&out[0].kind==K::FlushAll&&flushed.load_epoch!=pre.load_epoch&&flushed.mutation_revision!=pre.mutation_revision,"load epoch bump is one flush-all entry");
        end_case("flush_load_epoch");
        insert(primary,node);insert(primary,camera);lt::journal_drain(cursor,out,lt::JournalCapacity);destroy(primary);flushed=lt::journal_drain(cursor,out,lt::JournalCapacity);
        check(flushed.count==1&&out[0].kind==K::FlushAll&&flushed.registry_epoch!=pre.registry_epoch,"registry destruction is a flush-all entry");
        end_case("flush_registry_destroy");
        insert(primary,node);insert(primary,camera);lt::journal_drain(cursor,out,lt::JournalCapacity);
        {const auto bound=lt::journal_drain(cursor,out,lt::JournalCapacity);engine.registry=&unrelated;insert(unrelated,third);auto rebound=lt::journal_drain(cursor,out,lt::JournalCapacity);
         check(rebound.count==1&&out[0].kind==K::FlushAll&&rebound.registry_epoch!=bound.registry_epoch&&rebound.available,"registry rebind is one flush-all entry");
         engine.registry=&primary;insert(primary,node);insert(primary,camera);rebound=lt::journal_drain(cursor,out,lt::JournalCapacity);check(rebound.count==1&&out[0].kind==K::FlushAll&&snapshot().known,"rebinding back flushes again and births recover");}
        end_case("flush_registry_rebind");
        // Overflow: more retirements than the ring holds between two drains.
        const unsigned flood=lt::JournalCapacity+88;for(unsigned i=0;i<flood;++i){raw_insert(burst[0]);remove(primary,burst[0].handle);}
        auto lost=lt::journal_drain(cursor,out,lt::JournalCapacity);check(lost.overflow&&lost.available&&!lost.count&&!lost.more,"overflow reported, nothing partial delivered");
        check(snapshot().known,"full revalidation through current() works after overflow");
        raw_insert(burst[0]);const auto survivor=serial_of(burst[0]);remove(primary,burst[0].handle);lost=lt::journal_drain(cursor,out,lt::JournalCapacity);
        check(!lost.overflow&&lost.count==1&&out[0].node_serial==survivor,"cursor recovers after overflow");
        for(unsigned i=0;i<lt::JournalCapacity;++i){raw_insert(burst[0]);remove(primary,burst[0].handle);}
        lost=lt::journal_drain(cursor,out,lt::JournalCapacity);check(!lost.overflow&&lost.count==lt::JournalCapacity&&!lost.more,"exactly one full ring drains without overflow");
        for(unsigned i=0;i<lt::JournalCapacity+1;++i){raw_insert(burst[0]);remove(primary,burst[0].handle);}
        lost=lt::journal_drain(cursor,out,lt::JournalCapacity);check(lost.overflow&&!lost.count&&!lost.more,"capacity+1 between drains is overflow");
        lost=lt::journal_drain(cursor,out,lt::JournalCapacity);check(!lost.overflow&&!lost.count,"cursor at the head after the boundary overflow");end_case("overflow_and_recovery");
        // Costs: hooked insert+remove cycle without and with a consumer, and the empty drain.
        {
            LARGE_INTEGER frequency{},begin{},end{};QueryPerformanceFrequency(&frequency);const unsigned cycles=20000;double cost[2]{};std::uint64_t seen=0,overflows=0;
            for(unsigned pass=0;pass<4;++pass){
                const bool journaled=pass&1;if(!journaled)lt::journal_unregister();else cursor=lt::journal_register();
                QueryPerformanceCounter(&begin);
                for(unsigned i=0;i<cycles;++i){raw_insert(burst[0]);remove(primary,burst[0].handle);if(journaled&&(i&255)==255){auto d=lt::journal_drain(cursor,out,lt::JournalCapacity);seen+=d.count;overflows+=d.overflow;}}
                QueryPerformanceCounter(&end);
                if(journaled){auto d=lt::journal_drain(cursor,out,lt::JournalCapacity);seen+=d.count;overflows+=d.overflow;}
                const double us=double(end.QuadPart-begin.QuadPart)*1e6/double(frequency.QuadPart)/cycles;if(pass<2||us<cost[journaled])cost[journaled]=us;
            }
            check(seen==2ull*cycles&&!overflows,"every timed retirement was journaled once");
            QueryPerformanceCounter(&begin);for(unsigned i=0;i<cycles;++i)lt::journal_drain(cursor,out,lt::JournalCapacity);QueryPerformanceCounter(&end);
            std::printf("JOURNAL capacity=%u cycle_idle_us=%.4f cycle_journal_us=%.4f retirement_delta_us=%.4f empty_drain_us=%.4f drained=%llu\n",unsigned(lt::JournalCapacity),cost[0],cost[1],cost[1]-cost[0],
                double(end.QuadPart-begin.QuadPart)*1e6/double(frequency.QuadPart)/cycles,static_cast<unsigned long long>(seen));
        }
        end_case("cost");
        // A cursor of an ended registration is foreign; shutdown flushes and reports unavailable.
        const auto old_cursor=cursor;lt::journal_unregister();const auto silent=lt::journal_stats();raw_insert(burst[0]);remove(primary,burst[0].handle);check(lt::journal_stats().head==silent.head,"unregistered: writes stop again");
        cursor=lt::journal_register();auto foreign=old_cursor;check(lt::journal_drain(foreign,out,lt::JournalCapacity).overflow&&foreign.sequence==cursor.sequence,"cursor of an ended registration drains as overflow");
        check(lt::shutdown(),"journal case shutdown");auto final=lt::journal_drain(cursor,out,lt::JournalCapacity);
        check(!final.available&&final.count==1&&out[0].kind==K::FlushAll,"shutdown appends flush-all and reports the journal unavailable");
        end_case("reregistration_and_shutdown");
        // Capacity exhaustion clears the table: flush-all, then unavailable.
        empty(primary);check(lt::fixture_install(sites,2),"install bounded journal case");insert(primary,node);insert(primary,camera);lt::journal_drain(cursor,out,lt::JournalCapacity);
        insert(primary,third);final=lt::journal_drain(cursor,out,lt::JournalCapacity);check(!lt::active()&&!final.available&&final.count==1&&out[0].kind==K::FlushAll,"capacity exhaustion is a flush-all entry and an unavailable journal");
        check(lt::shutdown(),"bounded journal shutdown");lt::journal_drain(cursor,out,lt::JournalCapacity);end_case("flush_capacity_exhausted");
        // Saturated consumer count: registration refused, nothing owed, count unchanged.
        lt::fixture_journal_consumers(~0u);auto refused_cursor=lt::journal_register();check(!refused_cursor.valid()&&lt::journal_stats().consumers==~0u,"saturated registration refused with an invalid cursor");
        auto saturated=lt::journal_drain(refused_cursor,out,lt::JournalCapacity);check(saturated.overflow&&!saturated.available&&!saturated.count&&!refused_cursor.valid(),"invalid cursor never becomes valid by draining");
        lt::fixture_journal_consumers(1);end_case("saturated_registration");
        lt::journal_unregister();check(lt::journal_stats().consumers==0,"consumer unregistered");
        empty(primary);insert(primary,node);insert(primary,camera); // the retirement case below expects both present
    }
    // Production retirement keeps a previously published dispatcher callable,
    // while the fixture-only repeated-install seam above promises no such callers.
    check(lt::fixture_install(sites,16384,0,0,true,true),"retained production-style installation");
    std::int32_t thunk_delta=0;std::memcpy(&thunk_delta,static_cast<unsigned char*>(sites.insert_entry)+1,4);auto* saved_thunk=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(sites.insert_entry)+5+thunk_delta);
    check(lt::shutdown()&&!lt::active()&&!lt::recovery_required(),"retained dispatch retires safely");
    invoke_custom(saved_thunk,&primary,node.handle,reinterpret_cast<std::uintptr_t>(&node));check(output_regs[7]==0&&GetLastError()==0x246,"saved foreign-chain thunk still forwards after retirement");check(!snapshot().known,"retired forwarding never publishes a lifetime");check(!lt::fixture_install(sites)&&!lt::initialize(),"retired dispatch forbids reinstallation");
    empty(primary);empty(unrelated);VirtualFree(memory,0,MEM_RELEASE);std::printf("RESULT %s checks=%u failures=%u backend_calls=%u\n",failures?"FAIL":"PASS",checks,failures,calls);return failures?1:0;
}
