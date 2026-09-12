// Original synthetic rel32-call/SEH fixture; no game code or game launch.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <excpt.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include "../../src/proxy/object_trace.h"
#include "../../src/proxy/engine_memory.h"
using Fn=int(__cdecl*)(uintptr_t,uintptr_t,uintptr_t,uint32_t,uintptr_t,uintptr_t);
namespace ot=x3m::object_trace;
namespace em=x3m::engine_memory;
unsigned checks=0,failures=0,calls=0;
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
// Read-path cases (modes 7..12): FNV-1a over every snapshot field so the
// ReadProcessMemory and direct paths can be compared record for record.
uint64_t record_hash=0;unsigned read_failures=0;
void fold(const void* bytes,size_t size){const auto* p=static_cast<const unsigned char*>(bytes);for(size_t i=0;i<size;++i){record_hash^=p[i];record_hash*=1099511628211ull;}}
void fold_snapshot(const ot::Snapshot& s){
#define X3M_FOLD(field) fold(&s.field,sizeof s.field)
    X3M_FOLD(valid);X3M_FOLD(scope_depth);X3M_FOLD(mesh);X3M_FOLD(node);X3M_FOLD(camera);X3M_FOLD(registry);X3M_FOLD(engine);
    X3M_FOLD(node_handle);X3M_FOLD(camera_handle);X3M_FOLD(model);X3M_FOLD(lod);X3M_FOLD(flags12c);X3M_FOLD(flags130);
    X3M_FOLD(position);X3M_FOLD(basis);X3M_FOLD(scale);X3M_FOLD(world);X3M_FOLD(world_basis);X3M_FOLD(view);X3M_FOLD(projection);
#undef X3M_FOLD
}
void read_path(uint32_t mode,uintptr_t n){
    ot::Snapshot snap{};
    switch(mode){
    case 7:if(ot::current(&snap,false))fold_snapshot(snap);else ++read_failures;break; // route path: node, camera, registry
    case 8:if(ot::current(&snap,true))fold_snapshot(snap);else ++read_failures;break;  // capture path: plus the four matrices
    case 9:break;                                                                      // dispatch baseline: no snapshot
    case 10:check(ot::current(&snap,false)&&(snap.valid&ot::Node)&&snap.node_handle==77&&snap.node==n,"committed fixture page node readable");break;
    case 11:check(ot::current(&snap,false)&&!(snap.valid&ot::Node)&&(snap.valid&ot::Camera)&&snap.node==n,"decommitted page fails safely, camera still valid");break;
    case 12:check(ot::current(&snap,false)&&!(snap.valid&ot::Node)&&(snap.valid&ot::Camera),"span crossing into an uncommitted page rejected");break;
    }
}
Fn invoke=nullptr;
bool expect_scope=true;
std::array<uint32_t,0x150/4> node{},camera{};
std::array<uint32_t,16> world{},basis{},view{},projection{};
std::array<uint32_t,4> engine{};
uintptr_t enginePtr=0,worldPtr=0,basisPtr=0,viewPtr=0,projectionPtr=0;
extern "C" int __cdecl original(uintptr_t mesh,uintptr_t n,uintptr_t c,uint32_t mode,uintptr_t l0,uintptr_t l1){
    ++calls;
    if(mode>=7){read_path(mode,n);SetLastError(0x246);return 0x31415926;}
    check(GetLastError()==0x145,"entry LastError preserved");
    check(mesh==0x11223344&&l0==0x55667788&&l1==0x99aabbcc,"six args forwarded");
    ot::Snapshot snap{};const bool got_scope=ot::current(&snap);check(got_scope==expect_scope,"scope availability in backend");
    if(!expect_scope){SetLastError(0x246);return 0x31415926;}check(GetLastError()==0x145,"snapshot LastError preserved");
    check(snap.mesh==mesh&&snap.node==n&&snap.camera==c,"scope args exact");
    if(mode!=4){check(snap.valid==127,"all bounded snapshots valid");check(snap.node_handle==42&&snap.camera_handle==7,"handle offsets");check(snap.model==123&&snap.lod==2,"model LOD offsets");check(snap.world[12]==world[12]&&snap.position[1]==22,"raw matrix/node bits");}
    else check(!(snap.valid&(ot::Node|ot::Camera)),"unreadable pointers excluded");
    if(mode==1){check(snap.scope_depth==1,"outer scope depth");const int nested=invoke(mesh,n,c,2,l0,l1);check(nested==0x31415926,"nested EAX result");ot::Snapshot after{};check(ot::current(&after)&&after.scope_depth==1,"nested scope restored");}
    if(mode==2)check(snap.scope_depth==2,"nested scope depth");
    if(mode==3)RaiseException(0xe3450001,0,0,nullptr);
    if(mode==6)ot::fixture_fail_next_tls_set();
    SetLastError(0x246);return 0x31415926;
}
std::jmp_buf outer_jump;
extern "C" void object_fixture_recovery();
extern "C" __attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl object_fixture_handler(EXCEPTION_RECORD* record,void* frame,CONTEXT*,void*){
    if(!(record->ExceptionFlags&(EXCEPTION_UNWINDING|EXCEPTION_EXIT_UNWIND))&&record->ExceptionCode==0xe3450001)
    {
        RtlUnwind(frame,nullptr,nullptr,nullptr);
        // i386 RtlUnwind returns to its caller after running termination handlers;
        // the catcher performs the final nonlocal transfer and restores its chain.
        void* previous=*static_cast<void**>(frame);
        __asm__ __volatile__("movl %0,%%fs:0"::"r"(previous));
        std::longjmp(outer_jump,1);
    }
    return ExceptionContinueSearch;
}
extern "C" Fn object_fixture_invoke;
Fn object_fixture_invoke=nullptr;
// Outer original SEH frame catches beyond the intercepted call, forcing the
// module's foreign-exception unwind cleanup rather than C++ stack destruction.
extern "C" __attribute__((naked)) int __cdecl catch_outer(uintptr_t,uintptr_t,uintptr_t,uint32_t,uintptr_t,uintptr_t){
    __asm__ __volatile__(
      "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
      "movl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\tmovl $_object_fixture_handler,-4(%ebp)\n\t"
      "leal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
      "pushl 28(%ebp)\n\tpushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\t"
      "call *_object_fixture_invoke\n\taddl $24,%esp\n\t"
      ".globl _object_fixture_recovery\n_object_fixture_recovery:\n\t"
      "movl -8(%ebp),%edx\n\tmovl %edx,%fs:0\n\tleave\n\tret\n\t");
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);
    node[0x28/4]=42;node[0x140/4]=123;node[0x14c/4]=2;node[0xb4/4]=22;camera[0x28/4]=7;
    world[12]=0x3f800000;engine[3]=0x13579;enginePtr=reinterpret_cast<uintptr_t>(engine.data());worldPtr=reinterpret_cast<uintptr_t>(world.data());basisPtr=reinterpret_cast<uintptr_t>(basis.data());viewPtr=reinterpret_cast<uintptr_t>(view.data());projectionPtr=reinterpret_cast<uintptr_t>(projection.data());
    ot::FixtureAddresses addresses{reinterpret_cast<uintptr_t>(&enginePtr),reinterpret_cast<uintptr_t>(&worldPtr),reinterpret_cast<uintptr_t>(&basisPtr),reinterpret_cast<uintptr_t>(&viewPtr),reinterpret_cast<uintptr_t>(&projectionPtr)};
    auto memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!memory)return 2;
    // Original x86 cdecl stub: six pushes then a normal direct relative call.
    const unsigned char prefix[]={0x55,0x89,0xe5,0xff,0x75,0x1c,0xff,0x75,0x18,0xff,0x75,0x14,0xff,0x75,0x10,0xff,0x75,0x0c,0xff,0x75,0x08};
    std::memcpy(memory,prefix,sizeof prefix);auto site=memory+sizeof prefix;site[0]=0xe8;const uint32_t delta=reinterpret_cast<uintptr_t>(&original)-(reinterpret_cast<uintptr_t>(site)+5);std::memcpy(site+1,&delta,4);
    const unsigned char suffix[]={0x83,0xc4,0x18,0xc9,0xc3};std::memcpy(site+5,suffix,sizeof suffix);DWORD old=0;VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old);FlushInstructionCache(GetCurrentProcess(),memory,4096);
    invoke=reinterpret_cast<Fn>(memory);object_fixture_invoke=invoke;unsigned char originalBytes[5];std::memcpy(originalBytes,site,5);
    SetEnvironmentVariableW(L"X3M_OBJECT_TRACE",nullptr);check(!ot::initialize()&&!ot::active(),"default off");
    SetEnvironmentVariableW(L"X3M_OBJECT_TRACE",L"1");check(!ot::initialize()&&!ot::active(),"non-game executable rejected");
    check(!ot::fixture_install(site,reinterpret_cast<void*>(&catch_outer),addresses),"wrong expected target rejected");
    for(unsigned stage=1;stage<=3;++stage){check(!ot::fixture_install(site,reinterpret_cast<void*>(&original),addresses,stage),"injected install failure");check(!std::memcmp(site,originalBytes,5),"install failure bytes unchanged/restored");MEMORY_BASIC_INFORMATION info{};VirtualQuery(memory,&info,sizeof info);check(info.Protect==PAGE_EXECUTE_READ,"install failure protection restored");}
    for(unsigned stage=4;stage<=6;++stage){
        check(!ot::fixture_install(site,reinterpret_cast<void*>(&original),addresses,stage),"second-stage rollback failure reported");
        check(!ot::active()&&ot::recovery_required(),"failed rollback retained disabled ownership record");
        if(stage==4){expect_scope=false;SetLastError(0x145);check(invoke(0x11223344,1,1,0,0x55667788,0x99aabbcc)==0x31415926,"still-patched disabled dispatcher forwards safely");expect_scope=true;}
        check(ot::shutdown()&&!ot::recovery_required(),"second-stage rollback retry succeeds");
        check(!std::memcmp(site,originalBytes,5),"retry restores original bytes");MEMORY_BASIC_INFORMATION info{};VirtualQuery(memory,&info,sizeof info);check(info.Protect==PAGE_EXECUTE_READ,"retry restores original protection");
    }
    check(ot::fixture_install(site,reinterpret_cast<void*>(&original),addresses),"verified original callsite installed");
    const uintptr_t n=reinterpret_cast<uintptr_t>(node.data()),c=reinterpret_cast<uintptr_t>(camera.data());
    for(uint32_t mode:{0u,1u,4u}){SetLastError(0x145);const int result=invoke(0x11223344,mode==4?1:n,mode==4?1:c,mode,0x55667788,0x99aabbcc);check(result==0x31415926,"result EAX preserved");check(GetLastError()==0x246,"backend LastError preserved");ot::Snapshot snap{};check(!ot::current(&snap),"scope cleared on normal return");}
    SetLastError(0x145);const int caught=setjmp(outer_jump);if(!caught)catch_outer(0x11223344,n,c,3,0x55667788,0x99aabbcc);check(caught==1,"SEH propagated to outer handler");ot::Snapshot after{};check(!ot::current(&after),"scope cleared after foreign SEH unwind");
    SetLastError(0x145);check(invoke(0x11223344,n,c,0,0x55667788,0x99aabbcc)==0x31415926,"healthy call after foreign exception");
    check(ot::shutdown(),"quiescent restore");check(!std::memcmp(site,originalBytes,5),"restore exact bytes");check(!ot::active(),"inactive after restore");
    for(unsigned stage=1;stage<=3;++stage){
        check(ot::fixture_install(site,reinterpret_cast<void*>(&original),addresses),"reinstall for shutdown failure");
        check(!ot::fixture_shutdown(stage)&&ot::recovery_required()&&!ot::active(),"failed shutdown retains disabled recovery record");
        check(ot::shutdown()&&!ot::recovery_required(),"shutdown retry succeeds");
        check(!std::memcmp(site,originalBytes,5),"shutdown retry exact bytes");MEMORY_BASIC_INFORMATION info{};VirtualQuery(memory,&info,sizeof info);check(info.Protect==PAGE_EXECUTE_READ,"shutdown retry original protection");
    }
    for(unsigned mode:{5u,6u}){
        check(ot::fixture_install(site,reinterpret_cast<void*>(&original),addresses),"reinstall for TLS failure");
        if(mode==5){ot::fixture_fail_next_tls_set();expect_scope=false;}
        SetLastError(0x145);check(invoke(0x11223344,n,c,mode,0x55667788,0x99aabbcc)==0x31415926,"TLS failure preserves backend result");expect_scope=true;
        ot::Snapshot noStale{};check(!ot::active()&&!ot::current(&noStale),"TLS failure disables stale observation");
        check(GetLastError()==0x246,"TLS failure preserves backend LastError");check(ot::shutdown(),"quiescent TLS failure recovery");
    }
    // Read path (engine_memory): identical records and per-call cost of the
    // ReadProcessMemory path against validated direct reads, then decommit safety.
    check(ot::fixture_install(site,reinterpret_cast<void*>(&original),addresses),"reinstall for read-path cases");
    {
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        const unsigned iterations=20000;
        uint64_t hashes[2][2]{};double micros[2][3]{};
        for(unsigned m=0;m<2;++m){
            SetEnvironmentVariableW(L"X3M_ENGINE_READS",m?L"direct":L"rpm");em::configure();
            check(em::mode()==(m?em::Mode::Direct:em::Mode::ReadProcessMemory),"read mode selected");
            double queries_per_call[3]{},syscalls_per_call[3]{};
            for(unsigned path=0;path<3;++path){ // 0 dispatch baseline, 1 route (node, camera, registry), 2 capture (plus matrices)
                const uint32_t mode=path==0?9u:path==1?7u:8u;
                record_hash=1469598103934665603ull;read_failures=0;
                const auto before=em::stats();
                LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
                for(unsigned i=0;i<iterations;++i){if((i&255)==0)em::next_frame();invoke(0x11223344,n,c,mode,0x55667788,0x99aabbcc);}
                QueryPerformanceCounter(&end);
                const auto after=em::stats();
                micros[m][path]=double(end.QuadPart-begin.QuadPart)*1e6/double(frequency.QuadPart)/iterations;
                queries_per_call[path]=double(after.queries-before.queries)/iterations;syscalls_per_call[path]=double(after.syscalls-before.syscalls)/iterations;
                if(path)hashes[m][path-1]=record_hash;
                check(!read_failures,"read-path snapshots available");
            }
            std::printf("TIMING mode=%s baseline_us=%.3f route_us=%.3f capture_us=%.3f route_read_us=%.3f capture_read_us=%.3f route_queries_per_call=%.4f route_syscalls_per_call=%.2f capture_syscalls_per_call=%.2f\n",
                m?"direct":"rpm",micros[m][0],micros[m][1],micros[m][2],micros[m][1]-micros[m][0],micros[m][2]-micros[m][0],queries_per_call[1],syscalls_per_call[1],syscalls_per_call[2]);
        }
        const bool equal=hashes[0][0]==hashes[1][0]&&hashes[0][1]==hashes[1][1];
        check(equal,"identical snapshot records in both read modes");
        std::printf("IDENTITY route_rpm=%016llx route_direct=%016llx capture_rpm=%016llx capture_direct=%016llx equal=%u\n",
            static_cast<unsigned long long>(hashes[0][0]),static_cast<unsigned long long>(hashes[1][0]),static_cast<unsigned long long>(hashes[0][1]),static_cast<unsigned long long>(hashes[1][1]),equal);
        // A fake node on its own page: readable while committed, refused (never
        // faulting) once the page is decommitted between frames, readable again
        // after a recommit, and refused when the 0x150-byte span runs into a
        // reserved-only page. Both read modes must agree.
        for(unsigned m=0;m<2;++m){
            SetEnvironmentVariableW(L"X3M_ENGINE_READS",m?L"direct":L"rpm");em::configure();
            auto* reserved=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS));
            check(reserved&&VirtualAlloc(reserved,4096,MEM_COMMIT,PAGE_READWRITE),"fixture node page committed");
            auto* fake=reinterpret_cast<uint32_t*>(reserved);fake[0x28/4]=77;
            const uintptr_t f=reinterpret_cast<uintptr_t>(fake);
            em::next_frame();invoke(0x11223344,f,c,10,0x55667788,0x99aabbcc);
            em::next_frame();check(VirtualFree(reserved,0,MEM_DECOMMIT)!=0,"page decommitted between frames");
            invoke(0x11223344,f,c,11,0x55667788,0x99aabbcc);
            check(VirtualAlloc(reserved,4096,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"page recommitted");fake[0x28/4]=77;
            em::next_frame();invoke(0x11223344,f,c,10,0x55667788,0x99aabbcc);
            em::next_frame();invoke(0x11223344,f+4096-0x100,c,12,0x55667788,0x99aabbcc);
            check(VirtualFree(reserved,0,MEM_RELEASE)!=0,"fixture node page released");
        }
        SetEnvironmentVariableW(L"X3M_ENGINE_READS",nullptr);em::configure();
    }
    check(ot::shutdown(),"read-path shutdown");
    VirtualFree(memory,0,MEM_RELEASE);std::printf("RESULT %s checks=%u failures=%u backend_calls=%u\n",failures?"FAIL":"PASS",checks,failures,calls);return failures?1:0;
}
