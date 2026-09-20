#include "../../src/proxy/media_startup.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace startup=x3m::media_startup;
static unsigned checks=0,failures=0;
static void check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
struct Mock final:startup::Platform {
    std::uint32_t words[6]={0x4d8494,0x20,1,2,3,0x402ee1};
    unsigned reads=0,identities=0,launches=0;bool readable=true,identity=true,launch_ok=true;
    bool reenter_identity=false,close_identity=false;std::uint64_t ticks=100;
    startup::Controller* controller=nullptr;
    bool read_entry(std::uintptr_t address,std::uint32_t (&out)[6]) noexcept override {
        ++reads;check(address==0x1000,"actual entry address supplied to safe-read");
        if(!readable)return false;std::memcpy(out,words,sizeof words);return true;
    }
    bool qualified() noexcept override {
        ++identities;
        if(reenter_identity){startup::Entry nested;controller->enter(nested,0x1000);controller->leave(nested,1);}
        if(close_identity)controller->device_creation_attempt();
        return identity;
    }
    std::uint64_t now() noexcept override {return ++ticks;}
    bool launch(startup::Controller& value) noexcept override {++launches;controller=&value;return launch_ok;}
};
struct Callback {unsigned calls=0;bool success=true,ready_inside=false;startup::Controller* controller=nullptr;};
static bool callback(void* context,const startup::BootstrapContext& info) noexcept {
    auto& c=*static_cast<Callback*>(context);++c.calls;
    check(info.pinned_proxy==reinterpret_cast<void*>(0x2000)&&info.requested_qpc>0,"bootstrap receives retained module and request timestamp");
    if(c.ready_inside)check(c.controller->service_ready(),"actual service readiness can occur inside bootstrap");
    return c.success;
}
static void call(startup::Controller& c,std::uintptr_t result=1){startup::Entry e;c.enter(e,0x1000);c.leave(e,result);}
int main(){
    {Mock p;startup::Controller c(p);Callback cb;cb.controller=&c;
     check(c.configure(callback,&cb)&&!c.configure(callback,&cb),"single explicit configuration");
     startup::Entry e;c.enter(e,0x1000);check(e.matched&&e.owns_entry&&p.launches==0,"entry capture never starts service");
     c.leave(e,7);check(p.launches==1&&cb.calls==0,"ordinary success schedules only, no inline callback");
     c.leave(e,7);call(c);check(p.launches==1,"duplicate leave and later factory do not reschedule");
     auto s=c.snapshot();check(s.status==startup::Status::scheduled&&s.request_claimed&&!s.entry_active,"one-shot request published");
     check(!c.service_ready(),"cannot assert readiness before bootstrap");
     c.device_creation_attempt();c.bootstrap(reinterpret_cast<void*>(0x2000));
     s=c.snapshot();check(cb.calls==1&&s.status==startup::Status::prepared,"early scheduled bootstrap may complete after first device");
     check(s.requested_qpc<s.first_device_attempt_qpc&&s.first_device_attempt_qpc<s.bootstrap_begin_qpc&&s.bootstrap_begin_qpc<s.bootstrap_end_qpc,"request/device/bootstrap timestamps retain actual order");
     check(c.service_ready()&&!c.service_ready(),"canonical readiness reported once");
     check(c.snapshot().ready_qpc>s.bootstrap_end_qpc,"ready time separate from schedule/preparation");
     c.bootstrap(reinterpret_cast<void*>(0x2000));check(cb.calls==1,"bootstrap cannot repeat");}
    for(unsigned index: {0u,1u,5u}){Mock p;p.words[index]^=1;startup::Controller c(p);Callback cb;c.configure(callback,&cb);call(c);
        check(!p.launches&&!p.identities&&!c.snapshot().entry_active,"unknown caller/SDK/ancestor forwarded without scheduling");
        p.words[index]^=1;call(c);check(p.launches==1,"earlier unrelated caller does not consume ordinary startup");}
    {Mock p;p.words[2]=p.words[3]=p.words[4]=0xffffffffu;startup::Controller c(p);Callback cb;c.configure(callback,&cb);call(c);
     check(p.launches==1,"saved GPR values are not guard keys");}
    for(auto address: {std::uintptr_t(0),std::uintptr_t(0x1001),std::uintptr_t(0xfffffff0)}){
        Mock p;startup::Controller c(p);startup::Entry e;c.enter(e,address);c.leave(e,1);check(!p.reads&&!p.launches,"null/unaligned/wrapping span refused before read");}
    {Mock p;p.readable=false;startup::Controller c(p);Callback cb;c.configure(callback,&cb);call(c);check(!p.launches&&!p.identities,"inaccessible or short 24-byte read refuses");}
    {Mock p;p.identity=false;startup::Controller c(p);Callback cb;c.configure(callback,&cb);call(c);p.identity=true;call(c);
     check(p.identities==1&&!p.launches&&c.snapshot().status==startup::Status::identity_refused,"failed identity never re-arms");}
    {Mock p;startup::Controller c(p);Callback cb;c.configure(callback,&cb);c.device_creation_attempt();call(c);
     const auto stamp=c.snapshot().first_device_attempt_qpc;c.device_creation_attempt();
     check(!p.launches&&!p.reads&&stamp==c.snapshot().first_device_attempt_qpc,"first CreateDevice attempt permanently closes and timestamps once");}
    {Mock p;startup::Controller c(p);Callback cb;c.configure(callback,&cb);startup::Entry e;c.enter(e,0x1000);c.device_creation_attempt();c.leave(e,1);
     check(!p.launches&&c.snapshot().status==startup::Status::closed,"device creation during factory refuses startup");}
    {Mock p;startup::Controller c(p);Callback cb;c.configure(callback,&cb);call(c,0);call(c,1);
     check(!p.launches&&c.snapshot().status==startup::Status::failed_factory,"failed primary factory cannot retry into flight");}
    {Mock p;startup::Controller c(p);call(c);Callback cb;
     check(!c.configure(callback,&cb)&&!p.launches&&c.snapshot().status==startup::Status::unconfigured,"unconfigured ordinary call remains off and cannot enable late");}
    {Mock p;startup::Controller c(p);Callback cb;startup::Entry e;c.enter(e,0x1000);
     check(c.configure(callback,&cb),"existing loader initialization may register before ordinary return");c.leave(e,1);check(p.launches==1,"configuration during factory permits one startup");}
    {Mock p;startup::Controller c(p);Callback cb;c.configure(callback,&cb);startup::Entry outer,nested;c.enter(outer,0x1000);c.enter(nested,0x1000);c.leave(nested,1);c.leave(outer,1);call(c);
     check(!p.launches&&!c.snapshot().entry_active&&c.snapshot().status==startup::Status::reentered,"reentrant and concurrent factory contexts invalidate outer startup");}
    {Mock p;startup::Controller c(p);Callback cb;c.configure(callback,&cb);startup::Entry abandoned;c.enter(abandoned,0x1000);call(c);
     check(!p.launches&&c.snapshot().entry_active&&c.snapshot().status==startup::Status::reentered,"nonlocal lost context fails closed; no fabricated unwind cleanup");}
    for(unsigned kind=0;kind<2;++kind){Mock p;startup::Controller c(p);p.controller=&c;Callback cb;c.configure(callback,&cb);p.reenter_identity=kind==0;p.close_identity=kind==1;call(c);
     check(!p.launches,"reentry/device close while validating identity refuses atomic claim");}
    {Mock p;p.launch_ok=false;startup::Controller c(p);Callback cb;c.configure(callback,&cb);call(c);call(c);
     check(p.launches==1&&!cb.calls&&c.snapshot().status==startup::Status::launch_failed,"failed thread/module scheduling consumes one-shot without retry");}
    {Mock p;startup::Controller c(p);Callback cb;cb.success=false;c.configure(callback,&cb);call(c);c.bootstrap(reinterpret_cast<void*>(0x2000));
     check(cb.calls==1&&c.snapshot().status==startup::Status::callback_failed&&!c.service_ready(),"callback failure never signals readiness");}
    {Mock p;startup::Controller c(p);Callback cb;cb.controller=&c;cb.ready_inside=true;c.configure(callback,&cb);call(c);c.bootstrap(reinterpret_cast<void*>(0x2000));
     check(c.snapshot().status==startup::Status::ready,"bootstrap completion cannot erase prior real readiness");}
    std::printf("MEDIA STARTUP HOST checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
