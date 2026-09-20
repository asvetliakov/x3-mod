#include "media_presentation_gate.h"
#include <cstring>
namespace x3m::media_presentation_gate {
static_assert(sizeof(std::atomic<std::uint32_t>)==4,"gate counter representation");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,"gate requires lock-free word");
namespace {
void word(unsigned char* out,std::uint32_t value){std::memcpy(out,&value,4);}
bool valid(const Site& s){
    return s.entry&&!(s.entry&7)&&s.entry<=0xfffffff7u&&s.renderer_word&&
        s.forward==s.entry+5&&s.defer&&s.defer<=0xfffffff4u&&s.unavailable_word&&
        s.entry_bytes[0]==0xa1&&s.entry_bytes[5]==0x8b&&s.entry_bytes[6]==0x48&&s.entry_bytes[7]==0x18&&
        !std::memcmp(s.entry_bytes+1,&s.renderer_word,4)&&
        s.defer_bytes[0]==0xc7&&s.defer_bytes[1]==0x05&&
        !std::memcmp(s.defer_bytes+2,&s.unavailable_word,4)&&
        s.defer_bytes[6]==0&&s.defer_bytes[7]==0&&s.defer_bytes[8]==0&&s.defer_bytes[9]==0&&s.defer_bytes[10]==0xc3;
}
bool matches(Platform& p,std::uint32_t address,const void* expected,unsigned size){
    unsigned char got[32]{};
    return size<=sizeof got&&p.read(address,got,size)&&!std::memcmp(got,expected,size);
}
bool anchors(Platform& p,const Site& s){
    return p.executable(s.entry,8)&&p.executable(s.defer,11)&&
        matches(p,s.entry,s.entry_bytes,8)&&matches(p,s.defer,s.defer_bytes,11);
}
}
Site game_site(){return {0x4dac30,0x608b3c,0x4dac35,0x4dac6b,0x608ae0,
    {0xa1,0x3c,0x8b,0x60,0,0x8b,0x48,0x18},
    {0xc7,0x05,0xe0,0x8a,0x60,0,0,0,0,0,0xc3}};}
bool encode(unsigned char* out,unsigned capacity,std::uint32_t base,std::uint32_t depth,const Site& s){
    if(!out||capacity<gate_size||!base||base>0xffffffffu-gate_size||!depth||(depth&3)||!valid(s))return false;
    unsigned char b[gate_size]={0x9c,0x83,0x3d,0,0,0,0,0,0x75,0x0b,
        0x9d,0xa1,0,0,0,0,0xe9,0,0,0,0,0x9d,0xe9,0,0,0,0};
    word(b+3,depth);word(b+12,s.renderer_word);
    word(b+17,s.forward-(base+21));word(b+23,s.defer-(base+27));
    std::memcpy(out,b,sizeof b);return true;
}
bool Admission::qualify_owner(std::uint32_t thread){
    std::uint32_t empty=0;return thread&&owner_.compare_exchange_strong(empty,thread);
}
bool Admission::enable(std::uint32_t thread){
    if(!owner(thread)||!gate_ready_.load()||depth()||engine_reset_||native_reset_||failed_reset_)return false;
    enabled_.store(true);return true;
}
bool Admission::begin_copy(std::uint32_t thread){
    if(!owner(thread)||!enabled()||engine_reset_||native_reset_||failed_reset_)return false;
    std::uint32_t empty=0;
    // A bit, not an incrementing count: nested copies never wrap/underflow.
    return depth_.compare_exchange_strong(empty,1);
}
void Admission::finish_copy(){depth_.store(0);}
bool Admission::begin_reset(std::uint32_t thread,Reset kind){
    if(!owner(thread)){disable();return false;}
    if(depth())return false;
    bool& flag=kind==Reset::engine?engine_reset_:native_reset_;
    if(flag||(kind==Reset::engine&&native_reset_)){disable();return false;}
    flag=true;return true;
}
bool Admission::end_reset(std::uint32_t thread,Reset kind,bool succeeded){
    if(!owner(thread)){disable();return false;}
    bool& flag=kind==Reset::engine?engine_reset_:native_reset_;
    if(!flag||(kind==Reset::engine&&native_reset_)){disable();return false;}
    flag=false;if(!succeeded)failed_reset_=true;return true;
}
bool Admission::observe_recovery(std::uint32_t thread){
    if(!owner(thread)||depth()||engine_reset_||native_reset_)return false;
    failed_reset_=false;return true;
}
CopyScope::CopyScope(Admission& state,std::uint32_t thread,Cleanup cleanup,void* context){
    if(cleanup&&state.begin_copy(thread)){state_=&state;cleanup_=cleanup;context_=context;}
}
void CopyScope::close(){
    if(!state_||closing_)return;
    closing_=true;cleanup_(context_);state_->finish_copy();state_=nullptr;
}
bool stage(Platform& p,Transaction& t,const Site& s,const Admission& a){
    if(t.code||t.ready||t.may_redirect||t.protection_debt){t.status="already_staged";return false;}
    if(a.enabled()||a.depth()||!p.qualified()||!p.install_window()||!valid(s)||!anchors(p,s)){
        t.status="preflight_refused";return false;
    }
    const auto counter=p.counter_address(a);
    if(!counter||(counter&3)){t.status="counter_address";return false;}
    t.site=s;t.admission=&a;t.code=p.reserve();
    if(!t.code){t.status="reserve_failed";return false;}
    unsigned char bytes[gate_size]{};
    if(!encode(bytes,sizeof bytes,t.code,static_cast<std::uint32_t>(counter),s)||
       !p.write_reserved(t.code,bytes,sizeof bytes)||!matches(p,t.code,bytes,sizeof bytes)){
        t.status="emit_write_failed";return false;
    }
    // Never publish unless BOTH cache coherency and RX sealing succeeded.
    const bool flushed=p.flush(t.code,gate_size);t.emission_flush_debt=!flushed;std::uint32_t previous=0;
    const bool sealed=p.protect(t.code,gate_size,read_execute,previous);t.emission_protection_debt=!sealed;
    if(!flushed||!sealed||!p.executable(t.code,gate_size)){t.status="emit_seal_failed";return false;}
    std::memcpy(t.original,s.entry_bytes,8);std::memcpy(t.patched,t.original,8);
    t.patched[0]=0xe9;word(t.patched+1,t.code-(s.entry+5));
    t.ready=true;t.status="staged";return true;
}
bool restore(Platform& p,Transaction& t,Admission& a,bool quiescent){
    if(t.admission&&t.admission!=&a){t.status="counter_mismatch";return false;}
    a.gate_ready_.store(false);a.disable();
    if(!quiescent||a.depth()){t.status="restore_busy";return false;}
    if(!t.may_redirect&&!t.flush_debt&&!t.protection_debt){t.installed=false;t.status="restored";return true;}
    unsigned char current[8]{};
    if(!p.read(t.site.entry,current,8)){t.status="restore_unreadable";return false;}
    const bool ours=!std::memcmp(current,t.patched,8);
    if(!ours&&std::memcmp(current,t.original,8)){t.status="restore_foreign";return false;}
    if(ours){
        std::uint32_t previous=0;
        if(!p.protect(t.site.entry,8,read_write_execute,previous)){t.status="restore_writable_failed";return false;}
        t.protection_debt=true;
        if(!p.compare8(t.site.entry,t.patched,t.original)){t.status="restore_compare_failed";return false;}
        t.flush_debt=true;
    }
    // A failed readback retains conservative redirect ownership, even after a
    // successful CAS. Retry checks original or exact owned patch, never foreign.
    const bool readback=matches(p,t.site.entry,t.original,8);
    if(readback)t.may_redirect=false;
    if(t.flush_debt&&p.flush(t.site.entry,8))t.flush_debt=false;
    if(t.protection_debt){std::uint32_t previous=0;
        if(p.protect(t.site.entry,8,t.protection,previous))t.protection_debt=false;}
    const bool okay=readback&&!t.may_redirect&&!t.flush_debt&&!t.protection_debt;
    if(okay)t.installed=false;
    t.status=okay?"restored":"restore_debt";return okay;
}
bool install(Platform& p,Transaction& t,Admission& a){
    if(t.admission!=&a||!t.ready||t.ever_published||t.may_redirect||t.protection_debt||a.enabled()||a.depth()||
       !p.qualified()||!p.install_window()||!anchors(p,t.site)){
        t.status="install_refused";return false;
    }
    if(!p.protect(t.site.entry,8,read_write_execute,t.protection)){t.status="site_writable_failed";return false;}
    t.protection_debt=true;
    // Conservatively retain redirect/counter before publication. CAS cannot
    // replace a foreign patch, including changes to the three neighbor bytes.
    t.may_redirect=true;
    const bool swapped=p.compare8(t.site.entry,t.original,t.patched);
    if(!swapped)t.may_redirect=false;
    else {t.ever_published=true;t.flush_debt=true;}
    const bool readback=swapped&&matches(p,t.site.entry,t.patched,8);
    if(t.flush_debt&&p.flush(t.site.entry,8))t.flush_debt=false;
    std::uint32_t previous=0;
    if(p.protect(t.site.entry,8,t.protection,previous))t.protection_debt=false;
    if(swapped&&readback&&!t.flush_debt&&!t.protection_debt){t.installed=true;a.gate_ready_.store(true);t.status="installed_admission_off";return true;}
    if(!swapped&&!t.protection_debt){t.status="site_changed";return false;}
    // The startup window is still a caller-owned execution exclusion. No
    // callbacks/engine work occur within this serialized transaction.
    const bool rolled_back=restore(p,t,a,true);
    t.status=rolled_back?"install_rolled_back":"install_rollback_debt";return false;
}
}
