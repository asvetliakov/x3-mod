#include "engine_patch.h"
#include <cstring>

static_assert(sizeof(void*)==4,"x86 code patching only");
namespace x3m::engine_patch {
namespace {
constexpr unsigned arena_size=8192;
unsigned char* arena=nullptr;
unsigned arena_cursor=0;
SRWLOCK arena_lock=SRWLOCK_INIT;
bool ensure_arena() {
    if(arena)return true;
    arena=static_cast<unsigned char*>(VirtualAlloc(nullptr,arena_size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READ));
    return arena!=nullptr;
}
// The arena is PAGE_EXECUTE_READ except while a block is being written.
bool arena_writable(bool writable) {
    DWORD ignored=0;
    return VirtualProtect(arena,arena_size,writable?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_READ,&ignored)!=FALSE;
}
}
bool read_code(uintptr_t address,unsigned char* out,unsigned count) {
    SIZE_T copied=0;
    return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,count,&copied)&&copied==count;
}
bool verify_bytes(uintptr_t address,const unsigned char* expected,unsigned length) {
    unsigned char actual[max_prologue]{};
    return length>=5&&length<=max_prologue&&read_code(address,actual,length)&&!std::memcmp(actual,expected,length);
}
unsigned arena_used(){return arena_cursor;}
unsigned arena_capacity(){return arena_size;}

Emitter::Emitter(unsigned reserve):reserve_(reserve) {
    AcquireSRWLockExclusive(&arena_lock);
    if(!ensure_arena()||arena_cursor+reserve>arena_size||!arena_writable(true)){ReleaseSRWLockExclusive(&arena_lock);return;}
    start_=cursor_=arena+arena_cursor;
}
Emitter::~Emitter(){ if(start_){finish();} }
void Emitter::byte(unsigned char b){ if(cursor_&&unsigned(cursor_-start_)<reserve_)*cursor_++=b; else cursor_=nullptr; }
void Emitter::bytes(const void* p,unsigned n){ for(unsigned i=0;i<n;++i)byte(static_cast<const unsigned char*>(p)[i]); }
void Emitter::dword(uint32_t v){ byte(v&0xff);byte((v>>8)&0xff);byte((v>>16)&0xff);byte((v>>24)&0xff); }
void Emitter::rel32(const void* target){ dword(uint32_t(reinterpret_cast<uintptr_t>(target)-(reinterpret_cast<uintptr_t>(cursor_)+4))); }
void* Emitter::finish() {
    if(!start_)return nullptr;
    void* result=cursor_?start_:nullptr;
    if(cursor_){
        // Data words the stubs read (chain heads) share the arena; keep every block 4-aligned.
        arena_cursor=unsigned(cursor_-arena);arena_cursor=(arena_cursor+3)&~3u;
        FlushInstructionCache(GetCurrentProcess(),start_,unsigned(cursor_-start_));
    }
    arena_writable(false);
    ReleaseSRWLockExclusive(&arena_lock);
    start_=cursor_=nullptr;
    return result;
}
bool claim(Site& site,const SiteSpec& spec) {
    if(site.claimed){site.status="already_claimed";return false;}
    site.spec=spec;
    if(!spec.address||spec.length<5||spec.length>max_prologue){site.status="invalid_spec";return false;}
    if(!read_code(spec.address,site.original,spec.length)){site.status="unreadable";return false;}
    if(std::memcmp(site.original,spec.expected,spec.length)){site.status="bytes_mismatch";return false;}
    // Tail: displaced prologue, jmp back. Entry word. Dispatcher: jmp [entry].
    {
        Emitter e(spec.length+5+4+6+8);
        if(!e.ok()){site.status="arena_full";return false;}
        site.tail=e.here();e.bytes(site.original,spec.length);e.byte(0xe9);e.rel32(reinterpret_cast<const void*>(spec.address+spec.length));
        while(reinterpret_cast<uintptr_t>(e.here())&3)e.byte(0xcc);
        site.entry=static_cast<void**>(e.here());e.dword(uint32_t(reinterpret_cast<uintptr_t>(site.tail)));
        site.dispatcher=e.here();e.byte(0xff);e.byte(0x25);e.dword(uint32_t(reinterpret_cast<uintptr_t>(site.entry)));
        if(!e.finish()){site.status="emit_failed";return false;}
    }
    site.patched[0]=0xe9;
    const uint32_t displacement=uint32_t(reinterpret_cast<uintptr_t>(site.dispatcher)-(spec.address+5));
    std::memcpy(site.patched+1,&displacement,4);
    auto* code=reinterpret_cast<unsigned char*>(spec.address);
    if(!VirtualProtect(code,spec.length,PAGE_EXECUTE_READWRITE,&site.protection)){site.status="protect_failed";return false;}
    site.claimed=true;
    std::memcpy(code,site.patched,5);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),code,spec.length)!=FALSE;
    DWORD unused=0;
    const bool protected_again=VirtualProtect(code,spec.length,site.protection,&unused)!=FALSE;
    if(!flushed||!protected_again){
        DWORD writable=0;
        if(VirtualProtect(code,spec.length,PAGE_EXECUTE_READWRITE,&writable)){
            std::memcpy(code,site.original,spec.length);FlushInstructionCache(GetCurrentProcess(),code,spec.length);
            VirtualProtect(code,spec.length,site.protection,&unused);
            site.claimed=false;site.status="patch_rolled_back";
        } else site.status="rollback_failed";
        return false;
    }
    site.patched_in=true;site.status="active";
    return true;
}
bool store_pointer(void** slot,void* value) {
    if(!slot||!arena||reinterpret_cast<unsigned char*>(slot)<arena||reinterpret_cast<unsigned char*>(slot)+4>arena+arena_size)return false;
    AcquireSRWLockExclusive(&arena_lock);
    const bool okay=arena_writable(true);
    if(okay){*slot=value;arena_writable(false);}
    ReleaseSRWLockExclusive(&arena_lock);
    return okay;
}
void* push_front(Site& site,void* stub) {
    if(!site.claimed||!site.entry||!stub)return nullptr;
    void* previous=*site.entry;
    return store_pointer(site.entry,stub)?previous:nullptr;
}
bool claim_call(CallSite& site,uintptr_t address,uintptr_t expected_target,void* replacement) {
    if(site.patched_in){site.status="already_claimed";return false;}
    site.address=address;site.expected_target=expected_target;site.replacement=replacement;
    if(!address||!replacement){site.status="invalid_spec";return false;}
    if(!read_code(address,site.original,5)||site.original[0]!=0xe8){site.status="callsite_mismatch";return false;}
    uint32_t displacement=0;std::memcpy(&displacement,site.original+1,4);
    if(address+5+displacement!=expected_target){site.status="target_mismatch";return false;}
    std::memcpy(site.patched,site.original,5);
    const uint32_t redirected=uint32_t(reinterpret_cast<uintptr_t>(replacement)-(address+5));
    std::memcpy(site.patched+1,&redirected,4);
    auto* code=reinterpret_cast<unsigned char*>(address);
    if(!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&site.protection)){site.status="protect_failed";return false;}
    std::memcpy(code,site.patched,5);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),code,5)!=FALSE;
    DWORD unused=0;
    const bool protected_again=VirtualProtect(code,5,site.protection,&unused)!=FALSE;
    if(!flushed||!protected_again){
        DWORD writable=0;
        if(VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&writable)){std::memcpy(code,site.original,5);FlushInstructionCache(GetCurrentProcess(),code,5);VirtualProtect(code,5,site.protection,&unused);site.status="patch_rolled_back";}
        else site.status="rollback_failed";
        return false;
    }
    site.patched_in=true;site.status="active";
    return true;
}
bool restore_call(CallSite& site) {
    if(!site.patched_in)return true;
    unsigned char current[5]{};
    if(!read_code(site.address,current,5)||std::memcmp(current,site.patched,5)){site.status="restore_not_owned";return false;}
    auto* code=reinterpret_cast<unsigned char*>(site.address);DWORD protection=0,unused=0;
    if(!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&protection)){site.status="restore_protect_failed";return false;}
    std::memcpy(code,site.original,5);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),code,5)!=FALSE;
    const bool protected_again=VirtualProtect(code,5,site.protection,&unused)!=FALSE;
    site.patched_in=false;site.status=flushed&&protected_again?"restored":"restore_failed";
    return flushed&&protected_again;
}
bool restore(Site& site) {
    if(!site.patched_in)return true;
    auto* code=reinterpret_cast<unsigned char*>(site.spec.address);
    unsigned char current[max_prologue]{};
    if(!read_code(site.spec.address,current,site.spec.length)||std::memcmp(current,site.patched,5)){site.status="restore_not_owned";return false;}
    DWORD protection=0,unused=0;
    if(!VirtualProtect(code,site.spec.length,PAGE_EXECUTE_READWRITE,&protection)){site.status="restore_protect_failed";return false;}
    std::memcpy(code,site.original,site.spec.length);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),code,site.spec.length)!=FALSE;
    const bool protected_again=VirtualProtect(code,site.spec.length,site.protection,&unused)!=FALSE;
    site.patched_in=false;site.status=flushed&&protected_again?"restored":"restore_failed";
    return flushed&&protected_again;
}
}
