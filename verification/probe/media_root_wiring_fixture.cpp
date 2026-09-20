#include "../../src/proxy/media_root.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <thread>
#include <condition_variable>
#include <functional>
namespace r=x3m::media_root;namespace e=x3m::media_engine;namespace d=x3m::media_destination;
namespace g=x3m::media_presentation_gate;namespace m=x3m::media;namespace s=x3m::media_services;
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u %s\n",__LINE__,#x);}}while(0)
static thread_local e::ExecutionPoint point{7,0x8800};
static e::ExecutionPoint probe() noexcept{return point;}
struct PatchPlatform:g::Platform {
    std::map<unsigned,std::vector<unsigned char>> memory;
    std::map<unsigned,unsigned> protection;
    unsigned reserved=0,swaps=0,fail_swap=0;bool rollback_fault=false,fail_flush=false,window=true;
    std::vector<unsigned> order;
    PatchPlatform(){
        const auto add=[&](unsigned a,const unsigned char* p,unsigned n){const auto base=a&~7u;
            auto& v=memory[base];v.resize(32,0xcc);std::memcpy(v.data()+a-base,p,n);protection[base]=0x20;};
        for(const auto& site:e::sites)add(site.address,site.bytes,site.length);
        for(const auto& site:d::sites)add(site.address,site.bytes,site.length);
        const auto site=g::game_site();add(site.entry,site.entry_bytes,8);add(site.defer,site.defer_bytes,11);
    }
    auto region(unsigned a){auto it=memory.upper_bound(a);if(it==memory.begin())return memory.end();--it;
        return a-it->first<it->second.size()?it:memory.end();}
    bool qualified() override{return true;}bool install_window() override{return window;}
    unsigned reserve() override{const unsigned a=0x10000000+4096*reserved++;memory[a].resize(4096);protection[a]=4;return a;}
    unsigned counter_address(const g::Admission&) override{return 0x20000000;}
    bool read(unsigned a,void* p,unsigned n) override{auto it=region(a);if(it==memory.end()||n>it->second.size()-(a-it->first))return false;std::memcpy(p,it->second.data()+a-it->first,n);return true;}
    bool write_reserved(unsigned a,const void* p,unsigned n) override{auto it=region(a);if(it==memory.end()||n>it->second.size())return false;std::memcpy(it->second.data(),p,n);return true;}
    bool executable(unsigned a,unsigned) override{auto it=region(a);return it!=memory.end()&&(protection[it->first]&0x20);}
    bool protect(unsigned a,unsigned,unsigned desired,unsigned& old) override{auto it=region(a);if(it==memory.end())return false;old=protection[it->first];protection[it->first]=desired;return true;}
    bool compare8(unsigned a,const unsigned char* old,const unsigned char* next) override{
        order.push_back(a);if(++swaps==fail_swap)return false;
        if(rollback_fault&&fail_swap&&swaps>fail_swap)return false;
        auto it=region(a);if(it==memory.end()||std::memcmp(it->second.data()+a-it->first,old,8))return false;
        std::memcpy(it->second.data()+a-it->first,next,8);return true;
    }
    bool flush(unsigned a,unsigned) override{return !(fail_flush&&a<0x10000000);}
    bool original(){unsigned char bytes[16];
        for(const auto& site:e::sites)if(!read(site.address,bytes,site.length)||std::memcmp(bytes,site.bytes,site.length))return false;
        for(const auto& site:d::sites)if(!read(site.address,bytes,site.length)||std::memcmp(bytes,site.bytes,site.length))return false;
        const auto site=g::game_site();return read(site.entry,bytes,8)&&!std::memcmp(bytes,site.entry_bytes,8);
    }
};
struct Memory:e::Memory,d::Memory {
    std::array<unsigned char,0x40000> bytes{};unsigned allocations=0,frees=0,reads=0,writes=0;
    r::Ingress* ingress=nullptr;bool allocation_unlocked=true;
    std::mutex sync;std::condition_variable cv;bool block=false,entered=false,release=false;
    bool read(unsigned a,void* p,unsigned n) noexcept override{
        if(n>bytes.size()||a>bytes.size()-n)return false;
        {std::unique_lock<std::mutex> lock(sync);if(block&&a==0x1000){entered=true;cv.notify_all();cv.wait(lock,[&]{return release;});}}
        ++reads;std::memcpy(p,bytes.data()+a,n);return true;
    }
    bool write(unsigned a,const void* p,unsigned n) noexcept override{if(n>bytes.size()||a>bytes.size()-n)return false;++writes;std::memcpy(bytes.data()+a,p,n);return true;}
    unsigned allocate_shell() noexcept override{return 0x2000+0x100*allocations++;}
    void release_unpublished_shell(unsigned) noexcept override{++frees;}
    void put(unsigned a,unsigned v){CHECK(write(a,&v,4));}
};
struct Identity:d::IdentitySource {bool snapshot(unsigned,unsigned,x3m::ownership::SurfaceLeaseIdentity& id) noexcept override{id={1,1,1};return true;}};
struct Startup:x3m::media_startup::Platform {
    bool read_entry(std::uintptr_t,std::uint32_t (&w)[6]) noexcept override{w[0]=0x4d8494;w[1]=0x20;w[5]=0x402ee1;return true;}
    bool qualified() noexcept override{return true;}std::uint64_t now() noexcept override{return 1;}
    bool launch(x3m::media_startup::Controller&) noexcept override{return true;}
};
struct Hooks:r::Hooks {
    r::Composition composition=r::Composition::disabled_pristine;bool pin=true,bound=false,conflict=false;
    unsigned binds=0,composes=0;
    bool retain() noexcept override{return pin;}
    r::Bindings bind(r::Root& root) noexcept override{++binds;return {root.consumer.bind_owner({7,0x8000,0xa000},&probe),true,true};}
    r::Composition cue() noexcept override{return composition;}
    bool compose(bool enable) noexcept override{++composes;if(enable&&conflict)return false;bound=enable;return true;}
    bool composed() const noexcept override{return bound;}
    unsigned consumer_dispatch() noexcept override{return 0x70000000;}
    unsigned destination_dispatch() noexcept override{return 0x70000100;}
};
struct Workers:s::Workers {
    m::WorkerState status[2]{m::WorkerState::starting,m::WorkerState::starting};unsigned starts=0,polls=0,submits=0; m::Publication desired_values[2]{};
    std::shared_ptr<m::lav_detail::FrameStorage> storage[2]{std::make_shared<m::lav_detail::FrameStorage>(nullptr),std::make_shared<m::lav_detail::FrameStorage>(nullptr)};
    bool prepared[2]{};unsigned sequence[2]{};
    bool start(unsigned,m::WorkerConfig c) override{++starts;return c.package_owner&&c.sources.size()==1;}
    bool submit(unsigned i,const m::Command& command) noexcept override{++submits;if(command.kind!=m::CommandKind::construct)prepared[i]=true;return true;}
    void desired(unsigned i,const m::Publication& p) noexcept override{desired_values[i]=p;}
    m::FrameLease acquire(unsigned i) noexcept override{for(unsigned slot=0;slot<3;++slot)if(storage[i]->slots[slot].state==m::lav_detail::ready_slot)return storage[i]->acquire(storage[i],slot);return {};}
    bool event(unsigned i,m::WorkerEvent& event) noexcept override{++polls;if(!prepared[i])return false;prepared[i]=false;event={m::identity(desired_values[i]),1,m::worker_prepared,1,0,true,true,true};return true;}
    void frame(unsigned i){const auto id=m::identity(desired_values[i]);CHECK(desired_values[i].live&&desired_values[i].playing);
        for(unsigned slot=0;slot<3;++slot)if(storage[i]->reserve(slot,id,1,++sequence[i])){
            auto& value=storage[i]->slots[slot];value.view.start=0;value.view.end=1000000;value.view.width=2;value.view.height=2;value.view.pitch=8;
            std::memset(value.pixels.data(),0x5a,16);storage[i]->publish(slot);return;}
        CHECK(false);
    }
    m::WorkerPoll state(unsigned i) const noexcept override{return {status[i],0,0,0};}
    bool quiescent(unsigned i,const m::Publication& p) noexcept override{return !p.live&&!desired_values[i].live&&m::identity(p)==m::identity(desired_values[i]);}
    void shutdown(unsigned) noexcept override{}
};
struct Backend:d::CopyBackend {
    r::Root* root=nullptr;std::function<void(unsigned)> fault;unsigned calls=0,unlocks=0,releases=0,selected=0;bool held=false,mapped=false;
    unsigned char pixels[2][16]{};int fail_stage=0;
    void stage(unsigned n) noexcept {CHECK(root->admission.depth()==1);CHECK(root->report().state==r::ReportState::busy);if(fault)fault(n);}
    std::int32_t acquire(const d::State::Snapshot& snapshot) noexcept override{++calls;held=true;selected=snapshot.slot%2;stage(1);return 0;}
    std::int32_t describe(d::Descriptor& descriptor) noexcept override{descriptor={2,2,21,2,0,0};stage(2);return fail_stage==2?-1:0;}
    std::int32_t lock(d::Mapping& mapping) noexcept override{mapped=true;mapping={pixels[selected],8};stage(3);return 0;}
    std::int32_t unlock() noexcept override{CHECK(held&&mapped);mapped=false;++unlocks;stage(4);return 0;}
    void release() noexcept override{CHECK(held&&!mapped);held=false;++releases;stage(5);}
};
struct Fixture {
    PatchPlatform platform;Hooks hooks;Memory memory;Identity identity;Startup startup;
    x3m::media_startup::Controller controller{startup};std::uint64_t tick=1000;Backend backend;
    static std::uint64_t now(void* p) noexcept{return *static_cast<std::uint64_t*>(p);}
    r::Root root{platform,hooks,identity,memory,memory,controller,{7,0x8000,0xa000},&probe,{1000,&tick,now},&backend};
    Fixture(){backend.root=&root;}
    Workers* workers=nullptr;
    void prepare(){auto package=std::make_shared<m::PackageConfig>();package->provider_manifest=L"fixture.manifest";package->sources.push_back({2,8,L"fixture-source"});
        auto w=std::make_unique<Workers>();workers=w.get();CHECK(root.services.prepare(package,std::move(w)));}
    e::Frame frame(){e::Frame f{};f.saved_esp=0x9000-8;return f;}
    void destinations(){
        {std::lock_guard<std::mutex> lock(root.destination.domain());CHECK(root.destination.state_locked().table(0x20000,2,0,false));}
        for(unsigned slot=0;slot<2;++slot){d::State::Publication publication;
            {std::lock_guard<std::mutex> lock(root.destination.domain());publication=root.destination.state_locked().publish_slot(0x20000,slot*16,0x30000+slot*16,0x50000+slot*16);}
            root.destination.qualify(publication);
        }
    }
    void bind(unsigned record,unsigned slot){d::Frame f{};f.saved_esp=0x9000-8;f.eax=record;f.ecx=slot;
        root.observer.dispatch(unsigned(d::SiteId::record_bind_matched),f,7);
        unsigned flags=0;CHECK(memory.read(record+0x2c,&flags,4));memory.put(record+0x2c,flags|4);memory.put(record+0x30,slot);
        root.observer.dispatch(d::site_count+unsigned(d::SiteId::record_bind_matched),f,7);
    }
    void play(unsigned record){auto f=frame();f.esi=record;f.edi=0;f.ebx=7000;
        const x3m::media_playback::PlayValues32 values{1,55,2,0,0,0,0,7,0,0};CHECK(memory.write(0x9018,&values,sizeof values));
        root.consumer.dispatch(e::SiteId::explicit_play,f);CHECK(f.target==0x498d7a);
    }
    e::Frame pump(unsigned record){auto f=frame();f.ecx=record;root.consumer.dispatch(e::SiteId::pump,f);return f;}
    m::SessionHandle construct(unsigned record){auto f=frame();f.esi=record;memory.put(0x9004,2);memory.put(0x9008,8);memory.put(record+0x2c,4);memory.put(record+0x30,0);
        root.consumer.dispatch(e::SiteId::construct,f);CHECK(f.eax&&f.target==root.consumers.routes.return_plain);memory.put(record+0x10,2);memory.put(record+0x24,f.eax);memory.put(record+0x2c,8);
        m::SessionHandle h{};x3m::media_playback::EngineKey key{};CHECK(root.consumer.binding_owner(record,h,key));return h;}
};
void installation(){
    for(unsigned fail: {1u,2u,20u,21u,44u}){Fixture f;f.platform.fail_swap=fail;CHECK(!f.root.install());CHECK(!f.root.consumer.enabled()&&!f.root.admission.enabled());CHECK(f.platform.original());CHECK(!f.root.debt());CHECK(!f.hooks.bound);}
    {Fixture f;CHECK(f.root.install());CHECK(f.platform.reserved==44&&f.platform.swaps==44);CHECK(f.platform.order[0]==g::game_site().entry);
     CHECK(f.platform.order[1]==(d::sites[0].address&~7u));CHECK(f.platform.order[20]==(e::sites[0].address&~7u));
     CHECK(!f.root.readiness().complete()&&!f.root.consumer.enabled());CHECK(!f.root.install());CHECK(f.root.rollback());CHECK(f.platform.original());
     CHECK(f.platform.order[44]==(e::sites[e::site_count-1].address&~7u));CHECK(f.platform.order.back()==g::game_site().entry);}
    {Fixture f;f.platform.fail_swap=22;f.platform.rollback_fault=true;CHECK(!f.root.install());CHECK(f.root.debt());
     CHECK(!f.root.consumer.enabled()&&!f.root.admission.enabled());f.platform.rollback_fault=false;CHECK(f.root.rollback());CHECK(!f.root.debt()&&f.platform.original());}
    {Fixture f;f.platform.fail_flush=true;CHECK(!f.root.install());CHECK(f.root.debt());f.platform.fail_flush=false;CHECK(f.root.rollback());CHECK(!f.root.debt());}
    for(auto composition:{r::Composition::disabled_pristine,r::Composition::installed_qualified,r::Composition::unavailable}){
        Fixture f;f.hooks.composition=composition;CHECK(f.root.install()==(composition!=r::Composition::unavailable));
        if(composition==r::Composition::unavailable)CHECK(f.platform.swaps==0&&f.platform.original());}
    {Fixture f;f.hooks.conflict=true;CHECK(!f.root.install());CHECK(f.platform.original());}
    // One failed preflight in the last group prevents ALL earlier publication.
    {Fixture f;f.platform.memory[e::sites[e::site_count-1].address&~7u][0]^=1;CHECK(!f.root.install());CHECK(f.platform.swaps==0);}
}
void readiness(){
    Fixture f;CHECK(f.root.install());f.root.device_published(0x1234,true);CHECK(!f.root.consumer.enabled());f.prepare();
    f.root.present(0x1234);CHECK(!f.root.consumer.enabled());f.workers->status[0]=m::WorkerState::ready;f.root.present(0x1234);CHECK(!f.root.consumer.enabled());
    f.workers->status[1]=m::WorkerState::ready;f.root.present(0x1234);CHECK(f.root.consumer.enabled()&&f.root.admission.enabled());
    auto a=f.construct(0x1000),b=f.construct(0x1100);CHECK(a&&b&&!(a==b));CHECK(f.root.services.diagnostics().assigned==2);
    {std::lock_guard<std::mutex> lock(f.root.destination.domain());auto& state=f.root.destination.state_locked();CHECK(state.table(0x20000,2,0,false));
     const auto p=state.publish_slot(0x20000,0,0x30000,0x50000);CHECK(p.surface==0x50000);}
    const auto publication=[&]{std::lock_guard<std::mutex> lock(f.root.destination.domain());return f.root.destination.state_locked().candidate(0x30000).publication;};
    const auto original=publication();f.root.present(0x1234);f.root.present(0x1234);CHECK(publication()==original);
    f.root.device_published(0x1234,true);CHECK(publication()!=original);const auto fresh=publication();f.root.present(0x1234);CHECK(publication()==fresh);
    const auto polls=f.workers->polls;f.root.present(0x9999);point={8,0x8800};f.root.present(0x1234);point={7,0x8800};CHECK(f.workers->polls==polls);
    const auto whole=f.root.readiness();CHECK(whole.complete());
    const auto report=f.root.report();CHECK(report.state==r::ReportState::available&&report.owner==7&&report.device==0x1234&&report.installed&&!report.veto&&report.services.assigned==2);
    point={9,0x8800};CHECK(f.root.report().state==r::ReportState::wrong_owner);point={7,0x8800};f.hooks.bound=false;CHECK(!f.root.readiness().cue_composed);f.root.present(0x1234);CHECK(!f.root.consumer.enabled());
}
void ingress(){
    Fixture f;CHECK(f.root.install());f.root.device_published(0x1234,true);f.prepare();for(auto& status:f.workers->status)status=m::WorkerState::ready;f.root.present(0x1234);
    auto session=f.construct(0x1000);m::SessionHandle observed{};x3m::media_playback::EngineKey key{};CHECK(f.root.consumer.binding_owner(0x1000,observed,key));
    {std::lock_guard<std::mutex> lock(f.root.destination.domain());CHECK(f.root.destination.state_locked().watch({session,key},0,true));}
    auto frame=f.frame();frame.esi=0x1000;f.root.consumer.dispatch(e::SiteId::retire_record,frame);
    {std::lock_guard<std::mutex> lock(f.root.destination.domain());bool watched=false;for(auto& w:f.root.destination.state_locked().watches)watched|=w.used;CHECK(!watched);}
    CHECK(f.root.ingress.healthy());
    // A known foreign owned/list operation latches the domain; never-seen
    // foreign routing keeps its original unobserved path and does not veto.
    point={9,0x8800};frame=f.frame();frame.ecx=0x9999;f.root.consumer.dispatch(e::SiteId::pump,frame);CHECK(f.root.ingress.healthy());
    frame=f.frame();frame.esi=0x1000;f.root.consumer.dispatch(e::SiteId::retire_record,frame);CHECK(!f.root.ingress.healthy());point={7,0x8800};
    unsigned value=99;const auto reads=f.memory.reads,writes=f.memory.writes;
    CHECK(!f.root.ingress.read(0x1000,&value,4));CHECK(!f.root.ingress.write(0x1000,&value,4));CHECK(f.memory.reads==reads&&f.memory.writes==writes);
    CHECK(f.root.ingress.write(0x9000,&value,4)&&f.root.ingress.read(0x9000,&value,4));CHECK(!f.root.ingress.write(0x9fff,&value,4));
    CHECK(!f.root.consumer.binding_owner(0x1000,observed,key));f.root.present(0x1234);CHECK(!f.root.admission.enabled()&&!f.root.consumer.enabled());CHECK(f.root.report().veto);
    // Covered foreign return guards also veto, without reading owner's guards.
    Fixture ret;CHECK(ret.root.install());ret.root.device_published(0x1234,true);ret.prepare();for(auto& status:ret.workers->status)status=m::WorkerState::ready;ret.root.present(0x1234);
    point={9,0x8800};frame=ret.frame();ret.root.consumer.dispatch(e::SiteId::loop_return,frame);CHECK(!ret.root.ingress.healthy());CHECK(frame.target==ret.root.consumers.routes.manager_drop4);point={7,0x8800};
    // Missing/failed ingress can never satisfy Consumer admission.
    x3m::media_playback::Adapter adapter;e::Consumer missing(adapter,ret.memory,ret.root.services,ret.root.consumers.routes);
    CHECK(missing.bind_owner({7,0x8000,0xa000},probe));CHECK(!missing.enable({true,true,true,true,true,true,true,true}));CHECK(missing.bind_ingress(ret.root.ingress));CHECK(!missing.enable({true,true,true,true,true,true,true,true}));
}
void capacity_and_reset(){
    Fixture f;CHECK(f.root.install());f.root.device_published(0x1234,true);f.prepare();for(auto& status:f.workers->status)status=m::WorkerState::ready;f.root.present(0x1234);
    const auto live=f.construct(0x1000);
    for(unsigned i=0;i<255;++i){const unsigned address=0x10000+i*0x100;f.construct(address);
        auto frame=f.frame();frame.eax=0x2000+(i+1)*0x100;f.root.consumer.dispatch(e::SiteId::destroy_shell,frame);f.root.present(0x1234);}
    auto frame=f.frame();frame.esi=0x30000;f.root.consumer.dispatch(e::SiteId::construct,frame);CHECK(!frame.eax&&f.root.consumer.admission_closed());
    CHECK(f.root.admission.enabled());f.root.present(0x1234);CHECK(f.root.admission.enabled());CHECK(!f.root.consumer.enabled());
    m::Snapshot snapshot{};CHECK(f.root.adapter.snapshot(live,snapshot));CHECK(snapshot.publication.live);
    {g::CopyScope copy(f.root.admission,7,[](void*) noexcept {},nullptr);CHECK(bool(copy));}
    f.destinations();f.bind(0x1000,0);f.play(0x1000);f.workers->frame(live.slot);const auto surviving=f.pump(0x1000);
    CHECK(surviving.target==f.root.consumers.routes.return_plain&&surviving.eax==1);
    CHECK(f.root.services.diagnostics().presented[live.slot]==1&&f.backend.pixels[0][0]==0x5a);
    Fixture reset;CHECK(reset.root.install());reset.root.device_published(0x1234,true);reset.prepare();for(auto& status:reset.workers->status)status=m::WorkerState::ready;reset.root.present(0x1234);
    CHECK(reset.root.destination.begin_engine_reset(7));reset.root.destination.native_reset(7,0x1234,true,0);
    reset.root.destination.native_reset(7,0x1234,false,-1);reset.root.destination.end_engine_reset(7,false);
    for(unsigned i=0;i<3;++i){reset.root.present(0x1234);CHECK(!reset.root.consumer.enabled());g::CopyScope copy(reset.root.admission,7,[](void*) noexcept {},nullptr);CHECK(!copy);}
    Fixture blocked;CHECK(blocked.root.install());blocked.root.device_published(0x1234,true);blocked.prepare();for(auto& status:blocked.workers->status)status=m::WorkerState::ready;blocked.root.present(0x1234);blocked.construct(0x1000);
    blocked.root.publication_blocked();CHECK(!blocked.root.admission.enabled());blocked.root.present(0x9999);
    CHECK(blocked.root.services.diagnostics().draining==1);CHECK(!blocked.root.services.ready());
}
void external_stages(){
    for(unsigned mutation=0;mutation<3;++mutation)for(unsigned at:{2u,3u,4u,5u}){
        Fixture f;CHECK(f.root.install());f.root.device_published(0x1234,true);f.prepare();for(auto& status:f.workers->status)status=m::WorkerState::ready;f.root.present(0x1234);
        const auto session=f.construct(0x1000);f.destinations();f.bind(0x1000,0);f.play(0x1000);f.workers->frame(session.slot);
        unsigned fired=0;
        f.backend.fault=[&](unsigned stage){if(stage!=at||fired)return;++fired;
            if(mutation==0){auto frame=f.frame();frame.esi=0x1000;f.root.consumer.dispatch(e::SiteId::retire_record,frame);}
            else if(mutation==1){std::thread foreign([&]{point={9,0x8800};auto frame=f.frame();frame.esi=0x1000;f.root.consumer.dispatch(e::SiteId::retire_record,frame);});foreign.join();}
            else f.bind(0x1000,1);
            // Reentrant Present during active CopyScope cannot perform owner maintenance.
            const auto polls=f.workers->polls;f.root.present(0x1234);CHECK(f.workers->polls==polls);
        };
        const auto result=f.pump(0x1000);CHECK(fired==1);CHECK(f.root.services.diagnostics().presented[session.slot]==0);
        CHECK(!f.backend.held&&!f.backend.mapped&&f.root.admission.depth()==0&&f.backend.releases==1);
        if(mutation<2){CHECK(result.target==f.root.consumers.routes.manager_drop4);
            CHECK(f.workers->storage[session.slot]->all_free());
            std::lock_guard<std::mutex> lock(f.root.destination.domain());bool watched=false;for(auto& w:f.root.destination.state_locked().watches)watched|=w.used;CHECK(!watched);
        }else{CHECK(result.target==f.root.consumers.routes.return_plain&&result.eax==1);
            f.backend.fault={};const auto retry=f.pump(0x1000);CHECK(retry.target==f.root.consumers.routes.return_plain&&retry.eax==1);
            CHECK(f.root.services.diagnostics().presented[session.slot]==1&&f.backend.pixels[1][0]==0x5a);
        }
    }
    // A local descriptor failure does not cancel an unrelated owned operation.
    Fixture f;CHECK(f.root.install());f.root.device_published(0x1234,true);f.prepare();for(auto& status:f.workers->status)status=m::WorkerState::ready;f.root.present(0x1234);
    auto first=f.construct(0x1000),second=f.construct(0x1100);f.destinations();f.bind(0x1000,0);f.bind(0x1100,1);f.play(0x1000);f.play(0x1100);
    f.workers->frame(first.slot);f.workers->frame(second.slot);f.backend.fail_stage=2;f.pump(0x1000);CHECK(f.root.services.diagnostics().presented[first.slot]==0);
    f.backend.fail_stage=0;const auto result=f.pump(0x1100);CHECK(result.eax==1&&result.target==f.root.consumers.routes.return_plain);
    CHECK(f.root.services.diagnostics().presented[second.slot]==1&&f.backend.pixels[1][0]==0x5a);
    std::printf("external_stages=4 mutations=retire,foreign_veto,rebind cases=12 capacity_existing_copy=1 independent_copy=1\n");
}
void capture_nesting(){
    r::CaptureExclusion capture;CHECK(capture.clear());capture.enter();CHECK(!capture.clear());
    capture.enter();CHECK(!capture.clear());capture.leave();CHECK(!capture.clear());capture.leave();CHECK(capture.clear());
}
void race(){
    Fixture f;CHECK(f.root.install());f.memory.block=true;f.memory.put(0x1000,42);
    unsigned value=0;bool copied=false;std::atomic<bool> attempted{false},finished{false};
    std::thread read([&]{point={7,0x8800};copied=f.root.ingress.read(0x1000,&value,4);});
    {std::unique_lock<std::mutex> lock(f.memory.sync);f.memory.cv.wait(lock,[&]{return f.memory.entered;});}
    std::thread foreign([&]{point={8,0x8800};attempted.store(true);f.root.ingress.foreign_refusal();finished.store(true);});
    while(!attempted.load())std::this_thread::yield();CHECK(!finished.load());
    {std::lock_guard<std::mutex> lock(f.memory.sync);f.memory.release=true;f.memory.cv.notify_all();}
    read.join();foreign.join();CHECK(copied&&value==42&&finished.load());CHECK(!f.root.ingress.healthy());CHECK(!f.root.ingress.read(0x1000,&value,4));
}
int main(){installation();readiness();ingress();race();capture_nesting();capacity_and_reset();external_stages();
    Fixture f;CHECK(f.root.install());f.root.device_published(0x1234,true);f.prepare();for(auto& status:f.workers->status)status=m::WorkerState::ready;f.root.present(0x1234);
    std::recursive_mutex mutex;r::CaptureExclusion exclusion;
    const auto baseline_begin=std::chrono::steady_clock::now();for(unsigned i=0;i<100000;++i){std::lock_guard<std::recursive_mutex> lock(mutex);}
    const auto baseline=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-baseline_begin).count();
    const auto guarded_begin=std::chrono::steady_clock::now();for(unsigned i=0;i<100000;++i){std::lock_guard<std::recursive_mutex> lock(mutex);exclusion.enter();exclusion.leave();}
    const auto guarded=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-guarded_begin).count();
    std::printf("capture_exclusion baseline_ns=%lld guarded_ns=%lld (host scope only)\n",static_cast<long long>(baseline/100000),static_cast<long long>(guarded/100000));
    const auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<100000;++i)f.root.present(0x1234);
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
    std::printf("root_wiring checks=%u failures=%u sites=44 present_ns=%lld (host fixture, not game FPS)\n",checks,failures,static_cast<long long>(ns/100000));return failures?1:0;
}
