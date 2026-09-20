#include "../../src/proxy/media_engine_adapter.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
using namespace x3m;
using namespace x3m::media_engine;
struct IsolatedIngress final:RecordIngress {
    void retiring(EngineKey) noexcept override {}
    void retiring_address(std::uint32_t) noexcept override {}
    void clearing() noexcept override {}
    void foreign_refusal() noexcept override {}
    bool healthy() const noexcept override {return true;}
};
static IsolatedIngress isolated_ingress;

static unsigned checks=0,failures=0;
static thread_local ExecutionPoint executing{1,0x9000};
static ExecutionPoint execution() noexcept {return executing;}
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL line=%u %s\n",__LINE__,#x);}}while(0)
struct Mem:Memory {
    std::array<unsigned char,262144> bytes{};unsigned allocations=0,frees=0,reads=0;bool alloc_ok=true,writes=true;
    bool read(std::uint32_t a,void* p,unsigned n) noexcept override {++reads;if(a+n>bytes.size())return false;std::memcpy(p,bytes.data()+a,n);return true;}
    bool write(std::uint32_t a,const void* p,unsigned n) noexcept override {if(!writes||a+n>bytes.size())return false;std::memcpy(bytes.data()+a,p,n);return true;}
    std::uint32_t allocate_shell() noexcept override {return alloc_ok?0x2000+0x100*allocations++:0;}
    void release_unpublished_shell(std::uint32_t) noexcept override {++frees;}
    template<class T>void put(unsigned a,const T& x){CHECK(write(a,&x,sizeof x));}
    template<class T>T get(unsigned a){T x{};CHECK(read(a,&x,sizeof x));return x;}
};
struct Service:Services {
    bool is_ready=true,reserve_ok=true,binding_ok=true,drain=true;unsigned reserves=0,cancels=0,pumps=0;
    media::SessionHandle latest{};BindingObservation binding{};
    Consumer* consumer=nullptr;bool invalidate=false;unsigned invalidate_record=0;
    PumpResult result=PumpResult::pending;double rate=1;unsigned positions=0;
    bool ready() const noexcept override {return is_ready;}
    bool reserve(media::SessionHandle h,media::SourceKey) noexcept override {latest=h;++reserves;return reserve_ok;}
    bool observe_record(media::SessionHandle,const BindingObservation& b) noexcept override {binding=b;return binding_ok;}
    void cancel(media::SessionHandle) noexcept override {++cancels;}
    void publish(Adapter& a,media::SessionHandle) noexcept override {if(drain){media::Command c;while(a.pop_command(c)){}}}
    PumpResult pump(const PumpRequest& r,CurrentCheck check,void* owner) noexcept override {
        ++pumps;CHECK(check(owner,r));
        if(invalidate){Frame f{};f.saved_esp=0x8ff8;f.esi=invalidate_record;consumer->dispatch(SiteId::retire_record,f);CHECK(!check(owner,r));}
        return result;
    }
    bool position(media::SessionHandle,std::uint32_t& p) noexcept override {p=1234;++positions;return true;}
    static bool set(void* p,std::int32_t,double r) noexcept {static_cast<Service*>(p)->rate=r;return true;}
    media_playback::ClockTransaction clock(media::SessionHandle) noexcept override {return {this,&set};}
};
Routes routes(){Routes r;for(unsigned i=0;i<site_count;++i){r.forward[i]=0x100000+i*4096;r.unobserved[i]=0x300000+i*4096;}
    for(unsigned i=0;i<return_count;++i)r.after[i]=0x200000+i*256;
    r.return_plain=10;r.manager_drop4=11;r.manager_drop8=12;r.manager_next8=13;r.speech_drop4=14;r.destroy_free=15;r.manager_error8=16;return r;}
Readiness ready(){return {true,true,true,true,true,true,true,true};}
Frame frame(unsigned esp=0x9000){Frame f{};f.saved_esp=esp-8;return f;}
struct Setup {
    Adapter state;Mem memory;Service service;Routes r=routes();Consumer consumer;
    Setup(unsigned commands=8):state(2,commands),consumer(state,memory,service,r){service.consumer=&consumer;CHECK(consumer.bind_owner({1,0x100,0x10000},&execution));CHECK(consumer.bind_ingress(isolated_ingress));}
    media::SessionHandle construct(unsigned record=0x1000){
        auto f=frame();f.esi=record;memory.put(0x9004,2u);memory.put(0x9008,8u);
        memory.put(record+0x2c,4u);memory.put(record+0x30,0x3du);
        consumer.dispatch(SiteId::construct,f);CHECK(f.target==r.return_plain);CHECK(f.eax!=0);
        memory.put(record+0x10,2u);memory.put(record+0x24,f.eax);memory.put(record+0x2c,8u);
        return service.latest;
    }
    void play(unsigned record=0x1000,bool loop=false){
        auto f=frame();f.esi=record;f.edi=2000;f.ebx=7000;
        media_playback::PlayValues32 values{1,55,2,0,2,0,0,7,0,unsigned(loop)};
        memory.put(0x9018,values);consumer.dispatch(SiteId::explicit_play,f);CHECK(f.target==0x498d7a);CHECK(f.ebp==0);
        memory.put(record+0x1c,2000u);memory.put(record+0x20,7000u);memory.put(record+0x2c,8u|2u|unsigned(loop));
    }
};
void semantics(){
    Setup s;auto f=frame();f.esi=0x1000;s.memory.put(0x9004,2u);s.memory.put(0x9008,8u);
    s.consumer.dispatch(SiteId::construct,f);CHECK(f.target==s.r.forward[0]);CHECK(s.memory.allocations==0);
    CHECK(!s.consumer.enable({}));CHECK(s.consumer.enable(ready()));
    CHECK(s.consumer.eligible(2,8));CHECK(!s.consumer.eligible(3,8));CHECK(!s.consumer.eligible(2,0));
    auto h=s.construct();CHECK(s.consumer.shells()==1);CHECK(s.service.binding.initial_flags==4);
    CHECK(s.service.binding.slot==0x3d&&!s.service.binding.published&&s.service.binding.published_flags==8);
    auto shell=s.memory.get<media_playback::Shell32>(0x2000);CHECK(shell.flags==8&&shell.end_ms==-1&&shell.pump_state==1);
    s.play(0x1000,true);media::Snapshot before{};CHECK(s.state.snapshot(h,before));CHECK(before.operation_active&&before.request.loop);
    s.play(0x1000,true);media::Snapshot after{};CHECK(s.state.snapshot(h,after));CHECK(after.publication.operation!=before.publication.operation);
    // Speech consumes its existing six-argument frame and preserves prior loop.
    f=frame();f.esi=0x1000;f.ebp=3000;f.ebx=2;f.edi=99;
    s.memory.put(0x9010,0u);s.memory.put(0x9018,3u);s.memory.put(0x901c,66u);
    const std::uint32_t meta[3]={101,202,303};CHECK(s.memory.write(0x9024,meta,sizeof meta));
    s.consumer.dispatch(SiteId::speech_play,f);CHECK(f.target==0x498f7f&&f.edi==0&&f.ebx==0&&f.ebp==3000);
    CHECK(s.state.snapshot(h,after));CHECK(after.request.loop&&after.request.end_ms==-1&&after.request.start_ms==3000);
    CHECK(s.memory.get<unsigned>(0x1034)==101&&s.memory.get<unsigned>(0x1038)==202&&s.memory.get<unsigned>(0x103c)==303);
    CHECK(s.memory.get<std::int32_t>(0x2094)==-1);
    // Rate returns HRESULT to the original conversion tail, never Boolean.
    f=frame();f.eax=0x1000;s.memory.put(0x9004,100000u);s.consumer.dispatch(SiteId::rate,f);
    CHECK(f.target==0x498697&&f.eax==0&&s.service.rate<1&&s.service.rate>0.9999);
    const auto old_rate=s.service.rate;f.eax=0x1000;s.memory.put(0x9004,0u);s.consumer.dispatch(SiteId::rate,f);
    CHECK(std::int32_t(f.eax)<0&&s.service.rate==old_rate);
    f=frame();f.eax=0x1000;f.esi=0x8000;s.consumer.dispatch(SiteId::position,f);CHECK(f.eax==1&&s.memory.get<unsigned>(0x8000)==1234);
    // Copy/final-release reentry invalidates traversal before any raw read.
    f=frame();f.ecx=0x1000;s.service.invalidate=true;s.service.invalidate_record=0x1700;
    s.consumer.dispatch(SiteId::pump,f);CHECK(f.target==s.r.manager_drop4);
    s.service.invalidate=false;f=frame();f.esi=0x1000;s.consumer.dispatch(SiteId::stop_all,f);
    CHECK(f.target==0x498322);CHECK(s.state.snapshot(h,after)&&!after.operation_active&&!after.publication.playing);
    f=frame();f.esi=0x1000;s.consumer.dispatch(SiteId::retire_record,f);CHECK(s.consumer.shells()==1);
    f=frame();f.eax=0x1000;s.consumer.dispatch(SiteId::run,f);CHECK(f.target==s.r.return_plain&&!f.eax);
    f.eax=0x2000;s.consumer.dispatch(SiteId::destroy_shell,f);CHECK(f.target==s.r.destroy_free&&s.consumer.shells()==0);
    f=frame();s.consumer.dispatch(SiteId::clear,f);CHECK(s.consumer.enabled());
    s.consumer.dispatch(SiteId::shutdown,f);CHECK(!s.consumer.enabled());
    // Admission partial failures never publish or fall back to legacy.
    for(unsigned which=0;which<3;++which){Setup failure;CHECK(failure.consumer.enable(ready()));
        failure.service.reserve_ok=which!=0;failure.service.binding_ok=which!=1;failure.memory.writes=which!=2;
        // Set stack while writes still available.
        failure.memory.writes=true;failure.memory.put(0x9004,2u);failure.memory.put(0x9008,8u);failure.memory.writes=which!=2;
        f=frame();f.esi=0x1000;failure.consumer.dispatch(SiteId::construct,f);
        CHECK(!f.eax&&f.target==failure.r.return_plain&&failure.consumer.shells()==0&&failure.memory.frees==1);
    }
}
void pressure(){
    Setup s(1);CHECK(s.consumer.enable(ready()));const auto a=s.construct(0x1000),b=s.construct(0x1100);
    s.play(0x1000,true);s.play(0x1100,false);s.service.drain=false;
    // Fill the sole canonical cell without changing A's current operation.
    CHECK(s.state.seek(b,500,media::SeekIntent::preserve).accepted);
    media::Snapshot old{};CHECK(s.state.snapshot(a,old));
    auto f=frame(0x9000);f.eax=0x1000;f.edi=0x1000;f.ebp=0x1100;f.ebx=0;
    s.memory.put(0x9000,0x49840fu);s.memory.put(0x9004,2000u);
    s.consumer.dispatch(SiteId::loop_seek,f);CHECK(!f.eax&&f.target==s.r.manager_next8&&f.ebp==0x1100&&f.ebx==0);
    media::Snapshot still{};CHECK(s.state.snapshot(a,still));CHECK(still.publication.epoch==old.publication.epoch&&still.operation_active);
    // The selected continuation visits B; it can pump despite A pressure.
    f=frame();f.ecx=0x1100;s.consumer.dispatch(SiteId::pump,f);CHECK(f.eax==1&&s.service.pumps==1);
    media::Command command;CHECK(s.state.pop_command(command));
    f=frame();f.eax=0x1000;f.edi=0x1000;f.ebp=0x1100;s.consumer.dispatch(SiteId::loop_seek,f);
    CHECK(f.eax==1&&f.target==s.r.return_plain);CHECK(s.state.snapshot(a,still));
    CHECK(still.publication.operation==old.publication.operation&&still.publication.epoch==old.publication.epoch+1);
    CHECK(still.request.end_ms==7000&&still.request.loop&&still.publication.playing);
    CHECK(s.state.pop_command(command)&&command.request.end_ms==7000&&command.epoch==still.publication.epoch);
    // Inactive live operations are permanent failure, never pressure retries.
    s.state.stop(a);f=frame();f.eax=0x1000;s.consumer.dispatch(SiteId::loop_seek,f);
    CHECK(f.target==s.r.manager_error8&&!f.eax);s.service.drain=true;s.play(0x1000,true);s.service.drain=false;
    // Pending explicit/speech reject does not mutate accepted request or fields.
    CHECK(s.state.seek(b,600,media::SeekIntent::preserve).accepted);
    const auto preserved=s.memory.get<media_playback::Record32>(0x1000);
    f=frame();f.esi=0x1000;s.consumer.dispatch(SiteId::explicit_play,f);CHECK(f.target==0x498ce8);
    f=frame();f.esi=0x1000;s.consumer.dispatch(SiteId::speech_play,f);CHECK(f.target==0x498f08);
    const auto retained=s.memory.get<media_playback::Record32>(0x1000);CHECK(!std::memcmp(&preserved,&retained,sizeof retained));
}
void returns(){
    for(unsigned i=0;i<return_count;++i)for(unsigned stale=0;stale<2;++stale){
        Setup s;auto f=frame();f.eax=0x6000;f.esi=0x6000;f.edi=0x6000;f.ecx=0x6000;
        s.memory.put(0x9000,0xabcdef01u);
        s.consumer.dispatch(guarded_sites[i],f);
        CHECK(f.target==s.r.forward[unsigned(guarded_sites[i])]);
        if(stale){auto retiring=frame();retiring.esi=0x7777;s.consumer.dispatch(SiteId::retire_record,retiring);}
        const bool raised=i<2||i==6||i==8||i==9;
        auto post=frame(0x9000+(raised?4:0));post.eax=0x80004005;post.eflags=0x246;
        s.consumer.dispatch(static_cast<SiteId>(unsigned(SiteId::pump_return)+i),post);
        const auto expected=stale?(i==10?0x498dd8u:i==9?0x498fd2u:i==1?s.r.manager_drop4:
            (i==0||(i>=5&&i<=8))?0x4984beu:0x498362u):
            (i<2||i==8)?0xabcdef01u:sites[unsigned(guarded_sites[i])].continuation;
        CHECK(post.target==expected&&post.eax==0x80004005&&post.eflags==0x246);
    }
}
void reuse_and_callbacks(){
    Setup s;CHECK(s.consumer.enable(ready()));const auto old=s.construct();s.play();
    auto callback=frame();callback.esi=0x1000;s.consumer.dispatch(SiteId::explicit_callback,callback);
    // Distinct same-key request changes operation while old callback is active;
    // its return must not clear/install callback fields for the newer request.
    s.play();auto after=frame();s.consumer.dispatch(SiteId::explicit_return,after);CHECK(after.target==0x498dd8);
    auto retirement=frame();retirement.esi=0x1000;s.consumer.dispatch(SiteId::retire_record,retirement);
    auto destroy=frame();destroy.eax=0x2000;s.consumer.dispatch(SiteId::destroy_shell,destroy);CHECK(destroy.target==s.r.destroy_free);
    const auto replacement=s.construct();CHECK(!(replacement==old));s.play();
    media::Snapshot snapshot{};CHECK(!s.state.snapshot(old,snapshot));CHECK(s.state.snapshot(replacement,snapshot)&&snapshot.operation_active);
    // The successful new shell is associated by constructor ESI/source args,
    // despite address reuse and before the original record ID store.
    CHECK(s.memory.get<unsigned>(0x1024)==0x2100&&s.consumer.shells()==1);
    // Runtime completion settles nonloop operations before native status1 tail.
    s.service.result=PumpResult::complete;auto pump=frame();pump.ecx=0x1000;s.consumer.dispatch(SiteId::pump,pump);
    CHECK(pump.eax==2&&s.state.snapshot(replacement,snapshot)&&!snapshot.operation_active);
    // Speech memory contract failure takes speech's own register-pop epilogue.
    s.play();s.memory.writes=false;auto speech=frame();speech.esi=0x1000;
    s.consumer.dispatch(SiteId::speech_play,speech);CHECK(speech.target==0x498fd2);
}
void abandoned_scopes(){
    Setup s;
    // Same/higher stack retries after an original exception escaped to its
    // existing caller. No return guard storage/lease is retained by the callee.
    for(unsigned n=0;n<100;++n){auto f=frame();f.eax=0x6100;
        s.consumer.dispatch(SiteId::pause_call,f);CHECK(f.target==s.r.forward[unsigned(SiteId::pause_call)]);
        // Simulate the exception leaving this call; no after hook executes.
    }
    auto after=frame();s.consumer.dispatch(SiteId::pause_return,after);CHECK(after.target==0x4982f2);
    // An inner original exception is caught by its outer callback. The deeper
    // abandoned guard must not hide the surviving outer continuation.
    auto outer=frame(0x9000);s.consumer.dispatch(SiteId::callback_call,outer);
    auto inner=frame(0x8000);s.consumer.dispatch(SiteId::pause_call,inner);
    after=frame(0x9000);s.consumer.dispatch(SiteId::callback_return,after);CHECK(after.target==0x498353);
    // True nested scopes still pop in order and each gets its own continuation.
    outer=frame(0x9000);s.consumer.dispatch(SiteId::callback_call,outer);
    inner=frame(0x8000);s.consumer.dispatch(SiteId::pause_call,inner);
    after=frame(0x8000);s.consumer.dispatch(SiteId::pause_return,after);CHECK(after.target==0x4982f2);
    after=frame(0x9000);s.consumer.dispatch(SiteId::callback_return,after);CHECK(after.target==0x498353);
}
void owner_domain(){
    Setup s;CHECK(s.consumer.enable(ready()));const auto session=s.construct();s.play();
    media::SessionHandle got{};EngineKey key{};
    CHECK(s.consumer.binding_owner(0x1000,got,key)&&got==session&&key.address==0x1000);
    const auto reads=s.memory.reads,publishes=s.service.reserves,pumps=s.service.pumps;
    // Actual alternate thread sees only atomic identity data and immutable routes.
    std::atomic<bool> okay{false};
    std::thread foreign([&]{executing={2,0x9000};auto f=frame();f.eax=0x1000;
        s.consumer.dispatch(SiteId::run,f);
        media::SessionHandle h=session;EngineKey k=key;
        const bool refused=!f.eax&&f.target==s.r.return_plain&&
            !s.consumer.binding_owner(0x1000,h,k)&&h==session&&k==key;
        f.eax=0x1700;s.consumer.dispatch(SiteId::run,f);
        okay.store(refused&&f.target==s.r.unobserved[unsigned(SiteId::run)],std::memory_order_release);
    });foreign.join();CHECK(okay.load(std::memory_order_acquire));
    CHECK(s.memory.reads==reads&&s.service.reserves==publishes&&s.service.pumps==pumps);
    CHECK(s.consumer.admission_closed()&&!s.consumer.enabled()&&!s.consumer.enable(ready()));
    // Existing owner cleanup still operates after a foreign violation closes admission.
    auto f=frame();f.esi=0x1000;s.consumer.dispatch(SiteId::retire_record,f);
    f.eax=0x2000;s.consumer.dispatch(SiteId::destroy_shell,f);CHECK(f.target==s.r.destroy_free);
    // Tombstones remain after retirement/free; no historical owned pointer can
    // become an unsafe foreign COM route through address reuse.
    executing={2,0x9000};f.eax=0x2000;s.consumer.dispatch(SiteId::destroy_shell,f);
    CHECK(f.target==s.r.return_plain&&!f.eax);
    f=frame();f.esi=0x1700;s.consumer.dispatch(SiteId::retire_record,f);CHECK(f.target==s.r.return_plain);
    executing={1,0x9000};
    Setup alternate;CHECK(alternate.consumer.enable(ready()));alternate.construct();
    // Same TID with a separate stack allocation is not an owner domain.
    executing={1,0x19000};f=frame(0x19000);f.eax=0x1000;
    const auto before=alternate.memory.reads;alternate.consumer.dispatch(SiteId::seek,f);
    CHECK(f.target==alternate.r.return_plain&&!f.eax&&alternate.memory.reads==before);
    executing={1,0x9000};
    // Even with a valid execution point, a forged saved frame/argument extent
    // crossing either domain boundary is rejected before any stack reads.
    Setup boundary;CHECK(boundary.consumer.enable(ready()));boundary.construct();
    f=frame(0xfff0);f.eax=0x1000;const auto prior=boundary.memory.reads;
    boundary.consumer.dispatch(SiteId::seek,f);CHECK(f.target==boundary.r.return_plain&&boundary.memory.reads==prior);
    f=frame(0x150);f.eax=0x1000;boundary.consumer.dispatch(SiteId::run,f);CHECK(f.target==boundary.r.return_plain);
    // Once claimed/closed, eligible construction fails locally, including on
    // the owner. It cannot return to synchronous original construction.
    f=frame();f.esi=0x1100;boundary.consumer.dispatch(SiteId::construct,f);
    CHECK(f.target==boundary.r.return_plain&&!f.eax&&boundary.memory.allocations==1);
}
void classifier_capacity(){
    // Publication synchronization models handing an admitted pointer to another
    // thread. Both keys must already be visible before the handoff release.
    OwnedKeys keys;std::atomic<unsigned> published{0};std::atomic<bool> correct{true};
    std::thread reader([&]{for(unsigned n=1;n<=OwnedKeys::capacity;++n){
        while(published.load(std::memory_order_acquire)<n)std::this_thread::yield();
        if(!keys.record(0x1000+n*4)||!keys.shell(0x10000+n*4))correct.store(false);
    }});
    for(unsigned n=1;n<=OwnedKeys::capacity;++n){CHECK(keys.room(0x1000+n*4));
        CHECK(keys.publish(0x1000+n*4,0x10000+n*4));published.store(n,std::memory_order_release);}
    reader.join();CHECK(correct.load());CHECK(!keys.room(0x1004));
    CHECK(!keys.publish(0x7000,0x8000));CHECK(keys.record(0x1004)&&keys.shell(0x10004));
    // Consumer exhaustion must happen before allocator/counter/service mutation.
    Setup s;CHECK(s.consumer.enable(ready()));
    for(unsigned n=0;n<OwnedKeys::capacity;++n){s.construct();auto f=frame();f.esi=0x1000;
        s.consumer.dispatch(SiteId::retire_record,f);f.eax=0x2000+n*0x100;s.consumer.dispatch(SiteId::destroy_shell,f);}
    const auto allocations=s.memory.allocations,frees=s.memory.frees,reserves=s.service.reserves;
    auto f=frame();f.esi=0x1000;s.consumer.dispatch(SiteId::construct,f);
    CHECK(!f.eax&&f.target==s.r.return_plain&&s.consumer.admission_closed());
    CHECK(s.memory.allocations==allocations&&s.memory.frees==frees&&s.service.reserves==reserves);
    // Other IDs still preserve the owner's original unowned route.
    s.memory.put(0x9004,3u);f=frame();f.esi=0x1100;s.consumer.dispatch(SiteId::construct,f);CHECK(f.target==s.r.forward[0]);
}
struct PatchPlatform:Platform {
    std::map<unsigned,std::vector<unsigned char>> regions;
    std::map<unsigned,unsigned> protections;
    unsigned reserved=0,calls=0,fail_at=0;bool fail_flush=false;
    PatchPlatform(){for(const auto& s:sites){const auto word=s.address&~7u;
        auto& v=regions[word];v.resize(32,0xcc);std::memcpy(v.data()+(s.address-word),s.bytes,s.length);protections[word]=0x20;}}
    bool pass(){return ++calls!=fail_at;}
    bool qualified() override{return pass();} bool install_window() override{return pass();}
    unsigned reserve() override {if(!pass())return 0;const unsigned a=0x10000000+4096*reserved++;regions[a].resize(4096);protections[a]=4;return a;}
    unsigned counter_address(const media_presentation_gate::Admission&) override{return 0;}
    auto region(unsigned a){auto it=regions.upper_bound(a);if(it==regions.begin())return regions.end();--it;return a-it->first<it->second.size()?it:regions.end();}
    bool read(unsigned a,void* p,unsigned n) override {if(!pass())return false;auto it=region(a);if(it==regions.end()||a-it->first+n>it->second.size())return false;std::memcpy(p,it->second.data()+a-it->first,n);return true;}
    bool write_reserved(unsigned a,const void* p,unsigned n) override {if(!pass())return false;auto it=region(a);if(it==regions.end())return false;std::memcpy(it->second.data(),p,n);return true;}
    bool executable(unsigned a,unsigned) override {if(!pass())return false;auto it=region(a);return it!=regions.end()&&(protections[it->first]&0x20);}
    bool protect(unsigned a,unsigned,unsigned desired,unsigned& old) override {if(!pass())return false;auto it=region(a);if(it==regions.end())return false;old=protections[it->first];protections[it->first]=desired;return true;}
    bool compare8(unsigned a,const unsigned char* before,const unsigned char* after) override {if(!pass())return false;auto it=region(a);if(it==regions.end()||std::memcmp(it->second.data(),before,8))return false;std::memcpy(it->second.data(),after,8);return true;}
    bool flush(unsigned,unsigned) override {return pass()&&!fail_flush;}
    bool original(){for(const auto& s:sites){auto it=region(s.address);if(std::memcmp(it->second.data()+s.address-it->first,s.bytes,s.length)||protections[it->first]!=0x20)return false;}return true;}
};
void patches(){
    PatchPlatform baseline;Group group;CHECK(stage(baseline,group,0x70000000));const auto stage_calls=baseline.calls;
    baseline.calls=0;CHECK(install(baseline,group));const auto install_calls=baseline.calls;
    CHECK(group.installed&&group.ever_published);CHECK(!restore(baseline,group,true,false));
    CHECK(restore(baseline,group,true,true)&&baseline.original());
    for(unsigned i=1;i<=stage_calls;++i){PatchPlatform p;Group g;p.fail_at=i;CHECK(!stage(p,g,0x70000000));CHECK(!g.installed&&!g.ever_published&&p.original());}
    for(unsigned i=1;i<=install_calls;++i){PatchPlatform p;Group g;CHECK(stage(p,g,0x70000000));p.calls=0;p.fail_at=i;
        CHECK(!install(p,g));p.fail_at=0;CHECK(restore(p,g,true,true)&&p.original());}
    PatchPlatform debt;Group d;CHECK(stage(debt,d,0x70000000));debt.fail_flush=true;CHECK(!install(debt,d));
    CHECK(!std::strcmp(d.status,"install_rollback_debt"));CHECK(d.patches[0].flush_debt);
    debt.fail_flush=false;CHECK(restore(debt,d,true,true)&&debt.original());
    // Every forward tail retains complete displaced bytes; rel32 is re-based.
    Routes r;for(unsigned i=0;i<site_count;++i){unsigned char bytes[2048]{};unsigned size=0;const unsigned base=0x10000000+i*4096;
        CHECK(encode_stub(bytes,sizeof bytes,base,i,0x70000000,r,size));CHECK(size<=sizeof bytes);
        const auto offset=r.forward[i]-base;const auto& s=sites[i];
        if(s.original_call){CHECK(bytes[offset]==0xe9);unsigned rel;std::memcpy(&rel,bytes+offset+1,4);CHECK(base+offset+5+rel==s.original_call);}
        else for(unsigned j=0;j<s.length;++j)if(!s.rel32||j<s.rel32||j>=s.rel32+4)CHECK(bytes[offset+j]==s.bytes[j]);
        if(s.rel32&&!s.original_call){unsigned before,after;std::memcpy(&before,s.bytes+s.rel32,4);std::memcpy(&after,bytes+offset+s.rel32,4);
            CHECK(s.address+s.rel32+4+before==base+offset+s.rel32+4+after);}
    }
    std::printf("stage_failure_points=%u install_failure_points=%u sites=%u returns=%u\n",stage_calls,install_calls,site_count,return_count);
}
int main(){semantics();pressure();returns();reuse_and_callbacks();abandoned_scopes();owner_domain();classifier_capacity();patches();std::printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;}
