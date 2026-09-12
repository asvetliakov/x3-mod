#include "object_trace.h"
#include <wincrypt.h>
#include <excpt.h>
#include <array>
#include <cstring>
#include <cstddef>
#include <atomic>

static_assert(sizeof(void*)==4,"Verified x86 callsite only");
namespace {
using Target=int(__cdecl*)(uintptr_t,uintptr_t,uintptr_t,uint32_t,uintptr_t,uintptr_t);
struct Scope {
    void* previous_seh;
    void* handler;
    Scope* parent;
    uintptr_t args[6];
    uint32_t depth;
};
static_assert(sizeof(Scope)==40);
DWORD tls_slot=TLS_OUT_OF_INDEXES;
uintptr_t engine_slot=0,world_slot=0,basis_slot=0,view_slot=0,projection_slot=0;
unsigned char* patched_site=nullptr;
std::array<unsigned char,5> before_bytes{},our_bytes{};
bool installed=false; // ownership record, even when observation is disabled
std::atomic<bool> observation{false};
DWORD original_protection=0;
std::atomic<const char*> state{"disabled"};
uint64_t session=0;
unsigned fixture_fail=0;
#ifdef X3M_OBJECT_TRACE_FIXTURE
bool fail_next_tls=false;
#endif
bool set_top(void* value){
#ifdef X3M_OBJECT_TRACE_FIXTURE
    if(fail_next_tls){fail_next_tls=false;return false;}
#endif
    return TlsSetValue(tls_slot,value)!=FALSE;
}

bool read_memory(uintptr_t address,void* out,size_t size) {
    SIZE_T copied=0;
    return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&copied)&&copied==size;
}
Scope* top(){return tls_slot==TLS_OUT_OF_INDEXES?nullptr:static_cast<Scope*>(TlsGetValue(tls_slot));}
void pop(Scope* scope){if(top()==scope&&!set_top(scope->parent)){observation.store(false);state.store("tls_restore_failed");}}
}
extern "C" {
Target x3m_object_original=nullptr;
__attribute__((force_align_arg_pointer)) void __cdecl x3m_object_enter(Scope* scope,const uintptr_t* args) {
    const DWORD error=GetLastError();
    if(!observation.load()){scope->parent=nullptr;scope->depth=0;SetLastError(error);return;}
    scope->parent=top();
    scope->depth=scope->parent?scope->parent->depth+1:1;
    std::memcpy(scope->args,args,sizeof scope->args);
    if(!set_top(scope)){observation.store(false);state.store("tls_enter_failed");}
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_object_leave(Scope* scope) {
    const DWORD error=GetLastError();pop(scope);SetLastError(error);
}
__attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl x3m_object_unwind(EXCEPTION_RECORD* record,void* frame,CONTEXT*,void*) {
    if(record->ExceptionFlags&(EXCEPTION_UNWINDING|EXCEPTION_EXIT_UNWIND))
        x3m_object_leave(static_cast<Scope*>(frame));
    return ExceptionContinueSearch;
}
// A real x86 SEH registration covers MSVC game exceptions as well as normal
// returns. GCC C++ destructors alone do not cover foreign SEH unwinding. Scope
// occupies EBP-48..-9; result occupies EBP-4. No backend prologue is relocated.
__attribute__((naked)) int __cdecl x3m_object_dispatch(uintptr_t,uintptr_t,uintptr_t,uint32_t,uintptr_t,uintptr_t) {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $48,%esp\n\t"
        "leal 8(%ebp),%edx\n\tleal -48(%ebp),%eax\n\tpushl %edx\n\tpushl %eax\n\t"
        "call _x3m_object_enter\n\taddl $8,%esp\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,-48(%ebp)\n\t"
        "movl $_x3m_object_unwind,-44(%ebp)\n\tleal -48(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "pushl 28(%ebp)\n\tpushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\t"
        "call *_x3m_object_original\n\taddl $24,%esp\n\tmovl %eax,-4(%ebp)\n\t"
        "leal -48(%ebp),%eax\n\tpushl %eax\n\tcall _x3m_object_leave\n\taddl $4,%esp\n\t"
        "movl -48(%ebp),%eax\n\tmovl %eax,%fs:0\n\tmovl -4(%ebp),%eax\n\tleave\n\tret\n\t");
}
}
namespace x3m::object_trace {
namespace {
bool patch(void* site,void* target) {
    if(installed||!site||!target){state="invalid_patch_request";return false;}
    std::array<unsigned char,5> code{};
    if(!read_memory(reinterpret_cast<uintptr_t>(site),code.data(),code.size())||code[0]!=0xe8){state="callsite_mismatch";return false;}
    uint32_t displacement=0;std::memcpy(&displacement,code.data()+1,4);
    if(reinterpret_cast<uintptr_t>(site)+5+displacement!=reinterpret_cast<uintptr_t>(target)){state="target_mismatch";return false;}
    if(tls_slot==TLS_OUT_OF_INDEXES)tls_slot=TlsAlloc();
    if(tls_slot==TLS_OUT_OF_INDEXES){state="tls_unavailable";return false;}
    auto replacement=code;
    const uint32_t redirected=reinterpret_cast<uintptr_t>(&x3m_object_dispatch)-(reinterpret_cast<uintptr_t>(site)+5);
    std::memcpy(replacement.data()+1,&redirected,4);
    DWORD protection=0;
    if(fixture_fail==1||!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)){state="protect_failed";return false;}
    // Publish ownership before mutation. Failed rollback must remain recoverable,
    // even though diagnostics are disabled and initialize() returns false.
    patched_site=static_cast<unsigned char*>(site);before_bytes=code;our_bytes=replacement;
    original_protection=protection;installed=true;observation.store(false);
    x3m_object_original=reinterpret_cast<Target>(target);
    std::memcpy(site,replacement.data(),5);
    const bool flushed=fixture_fail!=2&&fixture_fail<4&&FlushInstructionCache(GetCurrentProcess(),site,5);
    DWORD unused=0;
    const bool protected_again=fixture_fail!=3&&VirtualProtect(site,5,protection,&unused);
    if(!flushed||!protected_again) {
        DWORD writable=0;
        if(fixture_fail==4||!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&writable)){state="rollback_protect_failed";return false;}
        std::memcpy(site,code.data(),5);
        const bool rollback_flush=fixture_fail!=5&&FlushInstructionCache(GetCurrentProcess(),site,5)!=FALSE;
        const bool rollback_protect=fixture_fail!=6&&VirtualProtect(site,5,protection,&unused)!=FALSE;
        state=rollback_flush&&rollback_protect?"patch_rolled_back":"rollback_failed";
        if(rollback_flush&&rollback_protect){installed=false;patched_site=nullptr;}
        return false;
    }
    ++session;observation.store(true);state="active";return true;
}

bool fingerprint(HMODULE module) {
    wchar_t path[32768];const DWORD length=GetModuleFileNameW(module,path,32768);
    if(!length||length>=32768)return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    bool ok=GetFileSizeEx(file,&size)&&size.QuadPart==2153984&&
        CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&
        CryptCreateHash(provider,CALG_SHA_256,0,0,&hash);
    unsigned char buffer[16384];DWORD count=0;
    while(ok){if(!ReadFile(file,buffer,sizeof buffer,&count,nullptr)){ok=false;break;}if(!count)break;ok=CryptHashData(hash,buffer,count,0)!=FALSE;}
    unsigned char digest[32]{};DWORD digest_size=sizeof digest;
    static constexpr unsigned char expected[]={0xfd,0xbf,0x34,0x18,0xd8,0xf0,0xa8,0x97,0xb5,0x8a,0x0b,0xbb,0x44,0x9b,0x23,0xf5,0x98,0x13,0x5b,0xa6,0xaa,0x9e,0xa4,0xec,0xa6,0x6d,0xf3,0x3a,0xdd,0x34,0xf8,0xab};
    ok=ok&&CryptGetHashParam(hash,HP_HASHVAL,digest,&digest_size,0)&&digest_size==32&&!std::memcmp(digest,expected,32);
    if(hash)CryptDestroyHash(hash);
    if(provider)CryptReleaseContext(provider,0);
    CloseHandle(file);return ok;
}
// The exact-executable identity: preferred base, PE headers of the expected
// image extent and the SHA-256 of the file on disk. Evaluated once per process
// (the hash reads 2 MB); shared with every module that reads engine globals.
bool verified_image() {
    static int cached=-1;
    if(cached>=0)return cached==1;
    HMODULE module=GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    const uintptr_t base=reinterpret_cast<uintptr_t>(module);
    const bool valid=base==0x400000&&read_memory(base,&dos,sizeof dos)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<0x1000&&
        read_memory(base+dos.e_lfanew,&nt,sizeof nt)&&nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC&&nt.OptionalHeader.SizeOfImage>0x208b40&&fingerprint(module);
    cached=valid?1:0;
    return valid;
}
}
bool executable_verified() {
    const DWORD error=GetLastError();
    const bool valid=verified_image();
    SetLastError(error);
    return valid;
}
bool initialize() {
    const DWORD error=GetLastError();
    if(installed){const bool enabled=observation.load();SetLastError(error);return enabled;}
    wchar_t setting[4]{};
    if(GetEnvironmentVariableW(L"X3M_OBJECT_TRACE",setting,4)!=1||setting[0]!=L'1'){state="disabled";SetLastError(error);return false;}
    const uintptr_t base=0x400000;
    bool valid=verified_image();
    static constexpr unsigned char expected[]={0xe8,0x23,0xaf,0xff,0xff};
    unsigned char call[5]{};
    valid=valid&&read_memory(base+0xc5228,call,5)&&!std::memcmp(call,expected,5);
    if(!valid){state="executable_mismatch";SetLastError(error);return false;}
    engine_slot=base+0x208518;world_slot=base+0x208a44;basis_slot=base+0x208a48;view_slot=base+0x208a40;projection_slot=base+0x208a38;
    const bool result=patch(reinterpret_cast<void*>(base+0xc5228),reinterpret_cast<void*>(base+0xc0150));SetLastError(error);return result;
}
bool active(){return observation.load();}
bool recovery_required(){return installed&&!observation.load();}
const char* status(){return state.load();}
bool current(Snapshot* out) {
    if(!out)return false;
    const DWORD error=GetLastError();*out={};
    if(!observation.load()){SetLastError(error);return false;}
    Scope* scope=top();
    if(!scope){SetLastError(error);return false;}
    out->session=session;out->scope_depth=scope->depth;out->mesh=scope->args[0];out->node=scope->args[1];out->camera=scope->args[2];
    // Snapshot finite fixed-size regions only. No pointer walks or game strings.
    uint32_t node[0x150/4]{};
    if(read_memory(out->node,node,sizeof node)){
        out->valid|=Node;out->node_handle=node[0x28/4];out->model=node[0x140/4];out->lod=node[0x14c/4];out->flags12c=node[0x12c/4];out->flags130=node[0x130/4];
        std::memcpy(out->position,node+0xb0/4,sizeof out->position);out->scale[0]=node[0x70/4];
        std::memcpy(out->scale+1,node+0x80/4,12);
        for(unsigned i=0;i<3;++i)std::memcpy(out->basis+i*3,node+0xc0/4+i*4,12);
    }
    if(read_memory(out->camera+0x28,&out->camera_handle,4))out->valid|=Camera;
    if(read_memory(engine_slot,&out->engine,4)&&read_memory(out->engine+0xc,&out->registry,4)&&out->registry)out->valid|=Registry;
    auto matrix=[&](uintptr_t slot,uint32_t* result,Valid bit){uintptr_t address=0;if(read_memory(slot,&address,4)&&read_memory(address,result,64))out->valid|=bit;};
    matrix(world_slot,out->world,World);matrix(basis_slot,out->world_basis,WorldBasis);matrix(view_slot,out->view,View);matrix(projection_slot,out->projection,Projection);
    SetLastError(error);return true;
}
bool shutdown() {
    const DWORD error=GetLastError();
    if(!installed){SetLastError(error);return true;}
    unsigned char code[5]{};DWORD protection=0,unused=0;
    if((observation.load()&&top())||!read_memory(reinterpret_cast<uintptr_t>(patched_site),code,5)||
       (std::memcmp(code,our_bytes.data(),5)&&std::memcmp(code,before_bytes.data(),5))){
        state="shutdown_not_owned_or_active";SetLastError(error);return false;}
    observation.store(false);
    if(!set_top(nullptr)){state="shutdown_tls_failed";SetLastError(error);return false;}
    if(fixture_fail==1||!VirtualProtect(patched_site,5,PAGE_EXECUTE_READWRITE,&protection)){state="shutdown_protect_failed";SetLastError(error);return false;}
    std::memcpy(patched_site,before_bytes.data(),5);
    const bool flush=fixture_fail!=2&&FlushInstructionCache(GetCurrentProcess(),patched_site,5)!=FALSE;
    const bool protect=fixture_fail!=3&&VirtualProtect(patched_site,5,original_protection,&unused)!=FALSE;
    state=flush&&protect?"restored":"restore_failed";
    if(flush&&protect){installed=false;patched_site=nullptr;}
    SetLastError(error);return flush&&protect;
}

#ifdef X3M_OBJECT_TRACE_FIXTURE
bool fixture_install(void* site,void* target,const FixtureAddresses& addresses,unsigned fail_stage) {
    engine_slot=addresses.engine_slot;world_slot=addresses.world_slot;basis_slot=addresses.basis_slot;view_slot=addresses.view_slot;projection_slot=addresses.projection_slot;
    fixture_fail=fail_stage;const bool result=patch(site,target);fixture_fail=0;return result;
}
bool fixture_shutdown(unsigned fail_stage){fixture_fail=fail_stage;const bool result=shutdown();fixture_fail=0;return result;}
void fixture_fail_next_tls_set(){fail_next_tls=true;}
#endif
}
