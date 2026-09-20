#pragma once
#include "../../src/proxy/media_presentation_gate.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace gate_fixture {
namespace gate=x3m::media_presentation_gate;
inline unsigned checks=0,failures=0;
inline void check(bool okay,const char* label){++checks;if(!okay){++failures;std::printf("FAIL %s\n",label);}}
struct Mock final:gate::Platform {
    gate::Site site=gate::game_site();
    unsigned char entry[8]{},defer[11]{},code[gate::gate_size]{};
    std::uint32_t site_protection=gate::read_execute,code_protection=4;
    bool identity=true,window=true,allocated=false,code_executable=true,race_compare=false;
    unsigned swaps=0;
    std::vector<std::string> events;
    std::vector<unsigned> fail;
    Mock(){std::memcpy(entry,site.entry_bytes,8);std::memcpy(defer,site.defer_bytes,11);}
    bool hit(const char* name){events.emplace_back(name);return std::find(fail.begin(),fail.end(),events.size())!=fail.end();}
    void reset_events(){events.clear();fail.clear();}
    bool qualified() override{return !hit("identity")&&identity;}
    bool install_window() override{return !hit("window")&&window;}
    std::uint32_t reserve() override{if(hit("reserve"))return 0;allocated=true;return 0x200000;}
    std::uint32_t counter_address(const gate::Admission&) override{return 0x300000;}
    unsigned char* memory(std::uint32_t a,unsigned n){
        if(a==site.entry&&n<=8)return entry;
        if(a==site.defer&&n<=11)return defer;
        if(a==0x200000&&n<=gate::gate_size)return code;
        return nullptr;
    }
    bool read(std::uint32_t a,void* out,unsigned n) override{
        if(hit(a==0x200000?"read_code":"read_site"))return false;
        auto* p=memory(a,n);if(!p)return false;std::memcpy(out,p,n);return true;
    }
    bool write_reserved(std::uint32_t a,const void* in,unsigned n) override{
        if(hit("write_code")||!allocated||a!=0x200000||n>sizeof code)return false;
        std::memcpy(code,in,n);return true;
    }
    bool executable(std::uint32_t a,unsigned) override{return !hit("executable")&&(a!=0x200000||code_executable);}
    bool protect(std::uint32_t a,unsigned,std::uint32_t protection,std::uint32_t& previous) override{
        if(hit(a==0x200000?"seal":protection==gate::read_write_execute?"writable":"reprotect"))return false;
        auto& cell=a==0x200000?code_protection:site_protection;
        previous=cell;cell=protection;return true;
    }
    bool compare8(std::uint32_t a,const unsigned char* expected,const unsigned char* desired) override{
        if(race_compare){entry[1]^=0x55;race_compare=false;}
        if(hit("compare")||a!=site.entry||site_protection!=gate::read_write_execute||std::memcmp(entry,expected,8))return false;
        std::memcpy(entry,desired,8);++swaps;return true;
    }
    bool flush(std::uint32_t a,unsigned) override{return !hit(a==0x200000?"flush_code":"flush_site");}
};
inline bool original(const Mock& p){return !std::memcmp(p.entry,p.site.entry_bytes,8);}
inline void settled(Mock& p,gate::Transaction& t,gate::Admission& a){
    p.reset_events();check(gate::restore(p,t,a,true),"restore retries settle all debt");
    check(original(p)&&p.site_protection==gate::read_execute,"settled bytes and original page protection");
    check(!t.may_redirect&&!t.flush_debt&&!t.protection_debt&&!t.installed,"settled ownership and visibility debt");
}
inline unsigned event_index(const std::vector<std::string>& events,const char* name,unsigned occurrence=0){
    for(unsigned i=0;i<events.size();++i)if(events[i]==name){if(!occurrence--)return i+1;}
    return 0;
}
inline void transactions(){
    unsigned stage_steps=0,install_steps=0,rollback_steps=0,flush_failure=0;
    {Mock p;gate::Transaction t;gate::Admission a;check(gate::stage(p,t,p.site,a),"stage success");stage_steps=p.events.size();
     check(original(p)&&!a.enabled()&&p.swaps==0,"staging never installs or admits");
     check(!gate::stage(p,t,p.site,a),"duplicate stage refused");
     gate::Admission other_state;check(!gate::install(p,t,other_state)&&original(p),"wrong counter cannot install or enable");
     p.reset_events();
     check(gate::install(p,t,a),"install success admission disabled");install_steps=p.events.size();flush_failure=event_index(p.events,"flush_site");
     check(t.ever_published&&t.may_redirect&&!a.enabled(),"published lifetime retained, admission off");
     check(!std::memcmp(p.entry+5,p.site.entry_bytes+5,3),"atomic patch preserves neighbors");
     check(!gate::install(p,t,a),"duplicate install refused");
     gate::Admission other;check(!gate::restore(p,t,other,true)&&t.may_redirect,"wrong counter cannot detach original active gate");
     settled(p,t,a);
     check(t.ever_published&&p.allocated,"retired code/counter lifetime remains required");}
    for(unsigned fail=1;fail<=stage_steps;++fail){
        Mock p;gate::Transaction t;gate::Admission a;p.fail={fail};
        check(!gate::stage(p,t,p.site,a),"every staged API failure refuses publication");
        check(!t.ready&&!t.ever_published&&original(p)&&p.swaps==0,"stage failure cannot touch site");
    }
    for(unsigned fail=1;fail<=install_steps;++fail){
        Mock p;gate::Transaction t;gate::Admission a;check(gate::stage(p,t,p.site,a),"failure case staged");p.reset_events();p.fail={fail};
        check(!gate::install(p,t,a),"every install API failure refuses success");
        check(!a.enabled()&&(!t.ever_published||p.allocated),"failure retains code and keeps admission off");
        settled(p,t,a);
    }
    {Mock p;gate::Transaction t;gate::Admission a;gate::stage(p,t,p.site,a);p.reset_events();p.fail={flush_failure};
     check(!gate::install(p,t,a),"flush failure rolls back");rollback_steps=p.events.size();}
    // Exercise each rollback operation failing after installation's cache flush
    // fails. The actual bytes/protection and retained debts are inspected, then
    // the same transaction is retried with failures removed.
    for(unsigned second=flush_failure+1;second<=rollback_steps;++second){
        Mock p;gate::Transaction t;gate::Admission a;gate::stage(p,t,p.site,a);p.reset_events();p.fail={flush_failure,second};
        check(!gate::install(p,t,a),"two failures never claim installation success");
        const bool patched=!std::memcmp(p.entry,t.patched,8);
        check(!patched||t.may_redirect,"remaining executable redirect stays owned");
        check(p.site_protection==gate::read_execute||t.protection_debt,"page restoration failure retained");
        check(t.ever_published&&p.allocated&&!a.enabled(),"rollback retains process-lifetime code/counter");settled(p,t,a);
    }
    // Independently cover restoration of an already successful installation.
    unsigned restore_steps=0;
    {Mock p;gate::Transaction t;gate::Admission a;gate::stage(p,t,p.site,a);gate::install(p,t,a);p.reset_events();
     check(gate::restore(p,t,a,true),"ordinary restore");restore_steps=p.events.size();}
    for(unsigned fail=1;fail<=restore_steps;++fail){
        Mock p;gate::Transaction t;gate::Admission a;gate::stage(p,t,p.site,a);gate::install(p,t,a);p.reset_events();p.fail={fail};
        check(!gate::restore(p,t,a,true),"restore operation failure returns failure");settled(p,t,a);
    }
    {Mock p;gate::Transaction t;gate::Admission a;gate::stage(p,t,p.site,a);p.race_compare=true;
     check(!gate::install(p,t,a)&&p.swaps==0&&p.entry[1]==(p.site.entry_bytes[1]^0x55),"racing foreign replacement survives atomic expected-byte comparison");
     check(!t.may_redirect&&!t.ever_published&&p.site_protection==gate::read_execute,"failed CAS never claims redirect ownership");}
    {Mock p;gate::Transaction t;gate::Admission a;p.identity=false;check(!gate::stage(p,t,p.site,a)&&original(p),"changed EXE refused");}
    {Mock p;gate::Transaction t;gate::Admission a;p.window=false;check(!gate::stage(p,t,p.site,a)&&original(p),"late stage refused");}
    for(unsigned i=0;i<8;++i){Mock p;gate::Transaction t;gate::Admission a;p.entry[i]^=1;check(!gate::stage(p,t,p.site,a)&&p.swaps==0,"changed entry or continuation refused");}
    for(unsigned i=0;i<11;++i){Mock p;gate::Transaction t;gate::Admission a;p.defer[i]^=1;check(!gate::stage(p,t,p.site,a)&&p.swaps==0,"changed defer target refused");}
    {Mock p;gate::Transaction t;gate::Admission a;gate::stage(p,t,p.site,a);p.window=false;
     check(!gate::install(p,t,a)&&original(p),"window rechecked at install");}
    {Mock p;gate::Transaction t;gate::Admission a;gate::stage(p,t,p.site,a);gate::install(p,t,a);
     check(!gate::restore(p,t,a,false)&&t.may_redirect,"nonquiescent restore refuses");
     p.entry[1]^=1;unsigned char foreign[8];std::memcpy(foreign,p.entry,8);
     check(!gate::restore(p,t,a,true)&&!std::memcmp(foreign,p.entry,8)&&t.may_redirect,"foreign patch never overwritten or released");}
}
struct CleanupContext {gate::Admission* a;unsigned count=0;bool active=false,nested_refused=false;};
inline void cleanup(void* context) noexcept {
    auto& c=*static_cast<CleanupContext*>(context);++c.count;c.active=c.a->depth()==1;
    gate::CopyScope nested(*c.a,7,cleanup,context);c.nested_refused=!nested;
}
inline void admission(){
    gate::Admission a;CleanupContext c{&a};
    check(!a.enabled()&&!a.depth(),"admission defaults off");
    check(a.qualify_owner(7)&&!a.qualify_owner(7)&&!a.qualify_owner(8),"single owner qualification");
    {gate::CopyScope copy(a,7,cleanup,&c);check(!copy,"disabled rejects copies");}
    check(!a.enable(7),"owner cannot enable without successfully installed gate");
    Mock platform;gate::Transaction transaction;
    check(gate::stage(platform,transaction,platform.site,a)&&gate::install(platform,transaction,a),"scope fixture checked gate installed");
    check(a.enable(7),"qualified owner explicitly enables");
    {gate::CopyScope foreign(a,8,cleanup,&c);check(!foreign,"foreign copy rejected");}
    {gate::CopyScope copy(a,7,cleanup,&c);check(bool(copy)&&a.depth()==1,"copy admitted before acquire");
     gate::CopyScope nested(a,7,cleanup,&c);check(!nested&&a.depth()==1,"nested copy rejected without depth increment");
     check(!a.begin_reset(7,gate::Admission::Reset::engine)&&!a.begin_reset(7,gate::Admission::Reset::native),"Reset refused throughout copy");
     copy.close();copy.close();}
    check(c.count==1&&c.active&&c.nested_refused&&!a.depth(),"final cleanup active, exactly once, then depth drops");
    check(a.begin_reset(7,gate::Admission::Reset::engine),"whole Reset preparation admitted");
    {gate::CopyScope copy(a,7,cleanup,&c);check(!copy,"engine precleanup excludes upload");}
    check(a.begin_reset(7,gate::Admission::Reset::native),"native Reset within engine Reset");
    check(a.end_reset(7,gate::Admission::Reset::native,false),"native failure recorded");
    check(a.end_reset(7,gate::Admission::Reset::engine,true),"engine success cannot erase native failure");
    {gate::CopyScope copy(a,7,cleanup,&c);check(!copy,"failed Reset excludes uploads");}
    check(a.observe_recovery(7),"actual recovery explicitly observed");
    {gate::CopyScope copy(a,7,cleanup,&c);check(bool(copy),"ordinary admission resumes after recovery");}
    check(!a.depth()&&!a.end_reset(7,gate::Admission::Reset::native,true)&&!a.enabled(),"unmatched reset poisons admission without latching depth");
    check(gate::restore(platform,transaction,a,true)&&!a.enable(7),"restored gate cannot readmit copies");
}
inline void encoding(){
    unsigned char bytes[gate::gate_size]{};auto s=gate::game_site();
    check(gate::encode(bytes,sizeof bytes,0x200000,0x300000,s),"valid encoding");
    check(bytes[0]==0x9c&&bytes[1]==0x83&&bytes[2]==0x3d&&bytes[7]==0&&bytes[8]==0x75&&10+bytes[9]==21,"saved flags and short JNE reaches restore-flags arm");
    std::uint32_t d=0;std::memcpy(&d,bytes+17,4);check(0x200000u+21+d==s.forward,"forward rel32 targets exact continuation");
    std::memcpy(&d,bytes+23,4);check(0x200000u+27+d==s.defer,"defer rel32 targets existing lost return");
    check(bytes[10]==0x9d&&bytes[11]==0xa1&&bytes[21]==0x9d&&bytes[22]==0xe9,"both arms pop flags, only forward loads renderer");
    unsigned char saved[sizeof bytes];std::memcpy(saved,bytes,sizeof bytes);
    check(!gate::encode(bytes,sizeof bytes-1,0x200000,0x300000,s)&&!std::memcmp(saved,bytes,sizeof bytes),"short capacity unchanged");
    check(!gate::encode(bytes,sizeof bytes,0x200000,0x300001,s),"unaligned counter refused");
    s.entry++;check(!gate::encode(bytes,sizeof bytes,0x200000,0x300000,s),"unaligned site refused");
}
inline void run(){encoding();admission();transactions();}
}
