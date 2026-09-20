#include "../../src/proxy/media_services.h"
#include "media_presentation_gate_contract_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
namespace s=x3m::media_services;
namespace m=x3m::media;
namespace p=x3m::media_playback;
namespace e=x3m::media_engine;
namespace d=x3m::media_destination;
namespace g=x3m::media_presentation_gate;
namespace startup=x3m::media_startup;
static unsigned checks=0,failures=0;static bool count_allocations=false;static unsigned allocations=0;
void* operator new(std::size_t n){if(count_allocations)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static void check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
struct StartupPlatform:startup::Platform {
    bool read_entry(std::uintptr_t,std::uint32_t (&words)[6]) noexcept override {words[0]=0x4d8494;words[1]=0x20;words[5]=0x402ee1;return true;}
    bool qualified() noexcept override{return true;}
    std::uint64_t tick=1;std::uint64_t now() noexcept override{return ++tick;}
    bool launch(startup::Controller&) noexcept override{return true;}
};
struct Source:d::IdentitySource {
    bool snapshot(std::uint32_t device,std::uint32_t surface,x3m::ownership::SurfaceLeaseIdentity& out) noexcept override {
        if(device!=0x100||!surface)return false;out={1,1,surface};return true;
    }
};
struct MockWorkers:s::Workers {
    struct Worker {
        m::WorkerState state=m::WorkerState::starting;
        std::shared_ptr<m::lav_detail::FrameStorage> storage=std::make_shared<m::lav_detail::FrameStorage>(nullptr);
        m::Publication desired{};m::WorkerConfig config{};
        m::WorkerEvent events[16]{};unsigned read=0,write=0;
        m::Command last{};unsigned starts=0,submits=0,polls=0,shutdowns=0;
        bool blocked=false,cancel_ack=false,start_ok=true;std::uint64_t sequence=0;
        std::function<void()> invalid_ack_fault;
    } workers[2];
    bool start(unsigned i,m::WorkerConfig c) override {auto& w=workers[i];++w.starts;w.config=std::move(c);return w.start_ok;}
    bool submit(unsigned i,const m::Command& c) noexcept override {
        auto& w=workers[i];if(w.blocked||!w.desired.live||!(m::identity(c)==m::identity(w.desired)))return false;
        w.last=c;++w.submits;if(w.invalid_ack_fault)w.invalid_ack_fault();return true;
    }
    void desired(unsigned i,const m::Publication& v) noexcept override {workers[i].desired=v;workers[i].cancel_ack=false;}
    m::FrameLease acquire(unsigned i) noexcept override {
        auto& w=workers[i];
        for(unsigned attempt=0;attempt<3;++attempt){
            unsigned next=3;std::uint64_t sequence=UINT64_MAX;
            for(unsigned j=0;j<3;++j){const auto& slot=w.storage->slots[j];if(slot.state==m::lav_detail::ready_slot&&slot.view.sequence<sequence){next=j;sequence=slot.view.sequence;}}
            if(next==3)return {};
            auto lease=w.storage->acquire(w.storage,next);
            if(!w.desired.live||!w.desired.playing||!(lease.view().identity==m::identity(w.desired))){lease.release(m::LeaseRelease::revoked);continue;}
            return lease;
        }
        return {};
    }
    bool event(unsigned i,m::WorkerEvent& out) noexcept override {auto& w=workers[i];++w.polls;if(w.read==w.write)return false;out=w.events[w.read++%16];return true;}
    m::WorkerPoll state(unsigned i) const noexcept override {return {workers[i].state,0,0,0};}
    bool quiescent(unsigned i,const m::Publication& canceled) noexcept override {
        const auto& w=workers[i];return w.cancel_ack&&!w.desired.live&&!canceled.live&&w.desired.playing==canceled.playing&&
            m::identity(canceled)==m::identity(w.desired)&&w.storage->all_free()&&w.read==w.write;
    }
    void shutdown(unsigned i) noexcept override {++workers[i].shutdowns;}
    void event(unsigned i,unsigned flags,m::FrameIdentity identity,unsigned revision=1,unsigned graph=1){
        auto& w=workers[i];check(w.write-w.read<16,"bounded synthetic event capacity");auto& event=w.events[w.write++%16];
        event={identity,graph,flags,revision,0,true,true,true};
    }
    void frame(unsigned i,std::int64_t begin,std::int64_t end,m::FrameIdentity id={}){
        auto& w=workers[i];if(!id.session)id=m::identity(w.desired);
        for(unsigned j=0;j<3;++j)if(w.storage->reserve(j,id,1,++w.sequence)){
            auto& slot=w.storage->slots[j];slot.view.start=begin;slot.view.end=end;slot.view.width=2;slot.view.height=2;slot.view.pitch=8;
            std::memset(slot.pixels.data(),unsigned(w.sequence),16);w.storage->publish(j);return;
        }
        check(false,"frame publication requires a real FREE slot");
    }
};
struct Backend:d::CopyBackend {
    g::Admission& gate;unsigned calls=0,locks=0,unlocks=0,releases=0;bool held=false,mapped=false;int fail=0;
    unsigned char pixels[16]{};std::function<void(unsigned)> spy;
    explicit Backend(g::Admission& g):gate(g){}
    void invoke(unsigned stage) noexcept {check(gate.depth()==1,"destination cleanup remains inside actual copy admission");if(spy)spy(stage);}
    std::int32_t acquire(const d::State::Snapshot&) noexcept override {++calls;held=true;invoke(1);return 0;}
    std::int32_t describe(d::Descriptor& out) noexcept override {out={2,2,21,2,0,0};invoke(2);return fail==2?-1:0;}
    std::int32_t lock(d::Mapping& out) noexcept override {++locks;if(fail==3)return -1;mapped=true;out={pixels,8};invoke(3);return 0;}
    std::int32_t unlock() noexcept override {check(mapped&&held,"unlock has physical mapping/retained destination");mapped=false;++unlocks;invoke(4);return fail==4?-1:0;}
    void release() noexcept override {check(held&&!mapped,"final destination release after unlock");held=false;++releases;invoke(5);}
};
struct Fixture {
    p::Adapter adapter{2,8};Source source;g::Admission gate;gate_fixture::Mock patch;g::Transaction transaction;
    d::Destination destination{gate,source,7};StartupPlatform startup_platform;startup::Controller controller{startup_platform};
    std::uint64_t tick=1000;Backend backend{gate};s::MediaServices services;
    MockWorkers* workers=nullptr;std::shared_ptr<m::PackageConfig> package=std::make_shared<m::PackageConfig>();
    e::Readiness readiness{true,true,true,true,true,true,true,true};
    static std::uint64_t qpc(void* p) noexcept{return static_cast<Fixture*>(p)->tick;}
    static bool preparation(void*,const startup::BootstrapContext&) noexcept{return true;}
    Fixture():services(adapter,destination,controller,{1000,this,&qpc},&backend){
        check(gate.qualify_owner(7)&&g::stage(patch,transaction,patch.site,gate)&&g::install(patch,transaction,gate)&&gate.enable(7),"actual production gate staged for synthetic destination");
        destination.set_device_key(0x100,7);
        {std::lock_guard<std::mutex> lock(destination.domain());check(destination.state_locked().table(0x20000,4,0,false),"observed synthetic table created");}
        for(unsigned i=0;i<2;++i){d::State::Publication token;{std::lock_guard<std::mutex> lock(destination.domain());token=destination.state_locked().publish_slot(0x20000,i*16,0x30000+i*16,0x400+i);}destination.qualify(token);}
        package->provider_manifest=L"fixture.manifest";package->sources.push_back({2,8,L"fixture-source"});
        auto transport=std::make_unique<MockWorkers>();workers=transport.get();
        check(services.prepare(package,std::move(transport)),"bootstrap prepares fixed two worker handles");
        check(workers->workers[0].config.package_owner==package&&workers->workers[1].config.package_owner==package,"both workers retain typed package pin owner");
        check(!workers->workers[0].config.observer&&!workers->workers[1].config.observer,"production config has no reentrant diagnostic observers");
        check(controller.configure(preparation,nullptr),"startup callback configured");startup::Entry entry;controller.enter(entry,0x1000);controller.leave(entry,1);controller.bootstrap(this);
    }
    void initialize(){for(auto& worker:workers->workers)worker.state=m::WorkerState::ready;services.maintenance_on_owner(readiness);}
    m::SessionHandle admit(unsigned record=0){auto id=adapter.try_admit({0x8000+record*0x100,1},2);check(bool(id.session),"actual Adapter admits independently keyed source2 record");
        check(adapter.associate_record(id.session,{0x9000+record*0x100,1}),"actual record association");check(adapter.publish_record_id(id.session,{0x9000+record*0x100,1},2),"actual source ID publication");check(services.reserve(id.session,2),"prepared assignment reserved");
        check(services.observe_record(id.session,{{0x9000+record*0x100,1},record,4,8,false}),"unpublished constructor watch accepted");
        check(services.observe_record(id.session,{{0x9000+record*0x100,1},record,4,12,true}),"published bound watch observed");return id.session;}
    void play(m::SessionHandle h,bool loop=false,int end=-1){p::Request request{{2,0,end,loop},{}};auto reserved=adapter.prepare_play(h,request);check(bool(reserved),"canonical play reservation");check(adapter.commit_play(std::move(reserved)).accepted,"canonical play commit");services.publish(adapter,h);}
    static bool current(void* p,const e::PumpRequest& r) noexcept {return static_cast<Fixture*>(p)->adapter.accepts_publication(r.session,r.operation,r.epoch);}
    e::PumpRequest request(m::SessionHandle h){m::Snapshot snapshot;check(adapter.snapshot(h,snapshot),"current runtime snapshot");return {h,{0x9000+h.slot*0x100,1},snapshot.publication.operation,snapshot.publication.epoch,adapter.begin_traversal(p::Traversal::manager)};}
    e::PumpResult pump(m::SessionHandle h){return services.pump(request(h),current,this);}
};
void readiness_and_offers(){
    Fixture f;f.controller.device_creation_attempt();f.services.maintenance_on_owner(f.readiness);
    check(!f.services.ready()&&f.controller.snapshot().status==startup::Status::prepared,"handles and prepared callback do not fabricate DD readiness");
    f.workers->workers[0].state=m::WorkerState::ready;f.services.maintenance_on_owner(f.readiness);check(!f.services.ready(),"both actual DD service states required initially");
    f.initialize();check(f.services.ready()&&f.controller.snapshot().status==startup::Status::ready,"delayed worker readiness adopted after first device with no patch work");
    auto a=f.admit(0),b=f.admit(1);check(f.services.diagnostics().assigned==2,"two real identities can select the same eligible source2");
    f.workers->workers[0].blocked=true;f.play(a);f.play(b);
    check(!f.workers->workers[0].submits&&f.workers->workers[1].submits==1,"busy first worker does not block independent second offer");
    m::CommandOffer retained;check(f.adapter.peek_command(a,retained)&&f.adapter.offer_current(retained),"rejected offer remains canonical pending");
    f.workers->workers[0].blocked=false;f.services.maintenance_on_owner(f.readiness);
    check(f.workers->workers[0].submits==1&&f.adapter.occupied_commands()==0,"accepted offer acknowledged once");
    const auto submitted=f.workers->workers[0].submits;f.services.maintenance_on_owner(f.readiness);check(f.workers->workers[0].submits==submitted,"acknowledged command never retried");
    f.workers->event(0,m::worker_failed,{{a.slot,a.generation+1},1,1});f.services.maintenance_on_owner(f.readiness);
    check(f.pump(a)==e::PumpResult::pending,"stale terminal ACK cannot fail current operation");
}
struct ZeroSequenceAcknowledgment:m::WorkerObserver {
    unsigned uploaded=0;std::int64_t sequence=-1;
    void observe(const m::Observation& event) noexcept override {
        if(event.kind==m::ObservationKind::lease&&!std::strcmp(event.label,"selected_uploaded")){++uploaded;sequence=event.c;}
    }
    void metadata(const m::Metadata&) noexcept override {}
};
void zero_sequence_acknowledgment(){
    Fixture f;f.initialize();const auto session=f.admit();f.play(session);auto& worker=f.workers->workers[0];
    auto acknowledgment=std::make_shared<ZeroSequenceAcknowledgment>();worker.storage->observer=acknowledgment;
    const auto identity=m::identity(worker.desired);
    check(worker.storage->reserve(0,identity,1,0),"real first-frame zero sequence reserves canonical FREE slot");
    auto& slot=worker.storage->slots[0];slot.view.start=0;slot.view.end=100000;slot.view.width=2;slot.view.height=2;slot.view.pitch=8;
    std::memset(slot.pixels.data(),0x5a,16);worker.storage->publish(0);
    f.workers->event(0,m::worker_prepared,identity);
    check(f.services.diagnostics().binding[0]==0,"no successful-write binding acknowledgment before first pump");
    check(f.pump(session)==e::PumpResult::pending,"first zero-sequence frame is scheduled normally");
    const auto result=f.services.diagnostics();
    check(result.presented[0]==0&&result.binding[0]!=0,"zero frame acknowledgment uses successful binding, not nonzero sequence sentinel");
    unsigned char expected[16];std::memset(expected,0x5a,sizeof expected);
    check(!std::memcmp(f.backend.pixels,expected,sizeof expected),"Services physically writes first zero-sequence pixels");
    check(acknowledgment->uploaded==1&&acknowledgment->sequence==0,"canonical FrameLease release acknowledges selected_uploaded sequence zero exactly once");
    check(worker.storage->all_free()&&result.leases==0&&!f.backend.held&&!f.backend.mapped&&!f.gate.depth(),"acknowledged zero-sequence lease and destination fully released");
    const auto calls=f.backend.calls;check(f.pump(session)==e::PumpResult::pending&&f.backend.calls==calls&&acknowledgment->uploaded==1,"no duplicate copy or acknowledgment without another frame");
}
void frames_and_end(){
    Fixture f;f.initialize();auto h=f.admit();f.play(h);auto& w=f.workers->workers[0];
    check(f.services.diagnostics().clock_generation[0]!=w.desired.epoch,"runtime epoch deliberately differs from mapped canonical generation");
    f.workers->frame(0,0,100000);f.workers->frame(0,100000,200000);f.workers->frame(0,200000,300000);
    f.workers->event(0,m::worker_source_eof|m::worker_graph_retired,m::identity(w.desired),2);
    f.workers->event(0,m::worker_prepared,m::identity(w.desired),0);
    check(f.pump(h)==e::PumpResult::pending&&f.services.diagnostics().presented[0]!=0,"terminal-before-READY ingest preserves actual final frames");
    check(f.services.diagnostics().leases==2,"selected lease is among exactly three physical slots");
    f.tick+=10;check(f.pump(h)==e::PumpResult::pending,"second scheduled source interval");f.tick+=10;check(f.pump(h)==e::PumpResult::pending,"third scheduled source interval");
    f.tick+=10;check(f.pump(h)==e::PumpResult::complete&&w.storage->all_free(),"EOF completes only after canonical final frame end");
    auto request=f.request(h);auto terminal=f.adapter.consume_event(h,request.operation,request.epoch,m::Event::presentation_complete);
    check(terminal.accepted,"consumer retains terminal transition authority");
    f.services.publish(f.adapter,h);check(f.workers->workers[0].shutdowns==0,"ordinary stop preserves warm worker service");
    std::uint32_t position=0;check(f.services.position(h,position)&&position==30,"stopped position retains canonical last committed endpoint");
    check(f.adapter.seek(h,0,m::SeekIntent::preserve).accepted,"stopped seek retains no playing intent");f.services.publish(f.adapter,h);
    check(f.services.position(h,position)&&position==0,"stopped seek explicitly replaces position with requested source start");
}
void retry_and_bounds(){
    Fixture f;f.initialize();auto h=f.admit();f.play(h,false,100);f.workers->frame(0,0,2000000);f.workers->frame(0,2000000,3000000);
    f.services.observe_record(h,{{0x9000,1},0,4,8,true});
    check(f.pump(h)==e::PumpResult::pending&&f.services.diagnostics().leases==2&&!f.backend.calls,"missing binding keeps one selected plus canonical FIFO");
    const auto generation=f.services.diagnostics().clock_generation[0];m::Snapshot logical;f.adapter.snapshot(h,logical);
    check(f.adapter.set_end(h,logical.publication.epoch,250)&&f.adapter.run(h).accepted,"same-epoch Run extends actual end bound");f.services.publish(f.adapter,h);
    check(f.services.diagnostics().clock_generation[0]==generation&&f.services.diagnostics().leases==2,"bound-only setter preserves generation and held queue");
    f.services.observe_record(h,{{0x9000,1},0,4,12,true});check(f.pump(h)==e::PumpResult::pending&&f.services.diagnostics().presented[0],"eligible missing-destination selection retries without resubmit");
    f.tick+=200;f.backend.fail=4;check(f.pump(h)==e::PumpResult::pending,"Unlock failure is availability, not stream death");
    check(!f.backend.held&&!f.backend.mapped&&!f.gate.depth(),"failed copy releases destination before gate depth drops");
    f.backend.fail=0;f.tick+=51;check(f.pump(h)==e::PumpResult::complete,"strict positive bound reached independently of failed copy");
}
void reentry(){
    for(unsigned stage=1;stage<=5;++stage)for(unsigned action=0;action<4;++action){
        Fixture f;f.initialize();auto h=f.admit();f.play(h);f.workers->frame(0,0,1000000);
        const auto before=f.services.diagnostics().revision[0];bool fired=false;
        f.backend.spy=[&](unsigned n){if(n!=stage||fired)return;fired=true;
            if(action==0){check(f.pump(h)==e::PumpResult::pending,"same-slot nested pump declines queue mutation");check(f.adapter.set_rate(h,200000,f.services.clock(h))==p::rate_ok,"nested actual canonical rate transaction");}
            if(action==1){auto seek=f.adapter.seek(h,1,m::SeekIntent::preserve);check(seek.accepted,"nested seek publishes epoch");f.services.publish(f.adapter,h);}
            if(action==2){f.adapter.observe_record_retirement({0x9000,1});f.services.cancel(h);f.services.maintenance_on_owner(f.readiness);}
            if(action==3){f.adapter.stop(h);f.services.publish(f.adapter,h);}
        };
        auto result=f.pump(h);auto diagnostics=f.services.diagnostics();
        check(fired&&!diagnostics.presented[0],"no presentation acknowledgement after cleanup reentry");
        check(!f.backend.held&&!f.backend.mapped&&!f.gate.depth(),"reentrant destination final cleanup completed");
        if(action==0)check(diagnostics.rate_numerator[0]==std::uint64_t(200000)*2748779&&diagnostics.revision[0]>before,"outer copy cannot overwrite nested rate or revision");
        if(action==1)check(diagnostics.clock_generation[0]==2,"outer copy cannot overwrite nested seek generation");
        if(action==2)check(result==e::PumpResult::invalidated&&diagnostics.draining==1,"retirement selects pointer-free abort and cannot reuse active copy slot");
        if(action==3)check(diagnostics.leases==0,"nested stop revokes other frames and stack lease closes after copy");
    }
}
void retirement_and_reuse(){
    Fixture f;f.initialize();auto a=f.admit(0),b=f.admit(1);f.play(a);f.play(b);
    f.workers->frame(0,0,1000000);f.workers->frame(0,1000000,2000000);f.workers->frame(0,2000000,3000000);
    f.services.observe_record(a,{{0x9000,1},0,4,8,true});f.pump(a);
    f.adapter.retire(a);f.services.cancel(a);f.services.maintenance_on_owner(f.readiness);
    check(f.services.diagnostics().draining==1&&f.workers->workers[0].storage->all_free(),"retirement revokes full physical ring but still awaits worker acknowledgement");
    f.workers->workers[0].cancel_ack=true;f.services.maintenance_on_owner(f.readiness);check(f.services.diagnostics().assigned==1,"only canonical quiescent ack permits assignment release");
    auto replacement=f.adapter.try_admit({0xa000,2},2);check(bool(replacement.session)&&f.services.reserve(replacement.session,2),"new full handle reuses warm worker without DD restart");
    check(f.workers->workers[0].starts==1&&f.workers->workers[1].starts==1,"two workers created exactly once through repeated record ownership");
    check(f.services.diagnostics().rate_numerator[0]==media_owned::Clock::unit_rate,"new record lifetime resets exact default 1x");
    f.workers->event(0,m::worker_unsafe_retained,{{77,88},9,10});f.services.maintenance_on_owner(f.readiness);
    check(f.services.ready()&&f.services.diagnostics().quarantined==1,"unsafe worker quarantined without erasing second service readiness");
    f.workers->frame(1,0,1000000);check(f.pump(b)==e::PumpResult::pending&&f.services.diagnostics().presented[1],"healthy second record continues after first worker quarantine");
}

void loop_preplay_and_rate(){
    Fixture f;f.initialize();auto h=f.admit();
    check(f.adapter.seek(h,12,m::SeekIntent::preserve).accepted,"preplay seek has no invented operation");f.services.publish(f.adapter,h);
    std::uint32_t position=99;check(f.services.position(h,position)&&position==12&&f.services.diagnostics().clock_generation[0]==0,"preplay position is stored source start without clock begin");
    auto transaction=f.services.clock(h);const auto unchanged=f.services.diagnostics();double decoded=0;p::decode_rate(200000,decoded);
    check(!transaction.apply(transaction.clock,200000,decoded+1)&&!transaction.apply(transaction.clock,0,0),"rate thunk rejects wrong exact decode and nonpositive inputs");
    check(f.services.diagnostics().revision[0]==unchanged.revision[0],"failed rate transaction leaves revision untouched");
    check(f.adapter.set_rate(h,200000,f.services.clock(h))==p::rate_ok,"rate selection before play is canonical");
    f.play(h,true);check(f.services.diagnostics().rate_numerator[0]==std::uint64_t(200000)*2748779,"record rate survives new playback operation");
    auto old=f.request(h);f.workers->frame(0,0,100000);f.workers->event(0,m::worker_source_eof|m::worker_graph_retired,{h,old.operation,old.epoch},1);
    check(f.pump(h)==e::PumpResult::pending,"loop first frame displayed");f.tick+=6;
    check(f.pump(h)==e::PumpResult::complete,"Services reports canonical loop endpoint without restarting manager");
    const auto generation=f.services.diagnostics().clock_generation[0];const auto submitted=f.workers->workers[0].submits;
    bool pressure=false;auto loop=f.adapter.loop_seek(h,0,-1,&pressure);check(loop.accepted&&!pressure,"manager alone commits exactly one loop seek");f.services.publish(f.adapter,h);
    check(f.services.diagnostics().clock_generation[0]==generation+1&&f.workers->workers[0].submits==submitted+1,"loop publishes one fresh epoch command and one canonical rearm");
    const auto fresh=f.request(h);check(fresh.operation==old.operation&&fresh.epoch!=old.epoch,"loop keeps operation and advances runtime epoch");
    f.workers->event(0,m::worker_source_eof|m::worker_graph_retired,{h,old.operation,old.epoch},2);
    f.workers->event(0,m::worker_prepared,{h,fresh.operation,fresh.epoch},0,2);
    f.workers->frame(0,0,100000);
    check(f.pump(h)==e::PumpResult::pending&&f.services.diagnostics().clock_generation[0]==generation+1,"old terminal and new graph-ready event cannot restart loop twice");
    const auto revision=f.services.diagnostics().revision[0];f.tick-=100;
    check(f.adapter.set_rate(h,100000,f.services.clock(h))==p::rate_failed&&f.services.diagnostics().revision[0]==revision,"regressing QPC rate transaction is failure-atomic");
}
void cancel_without_graph_and_bad_ack(){
    {Fixture f;f.initialize();auto h=f.admit();f.play(h);
     f.workers->frame(0,0,100000);f.workers->frame(0,100000,200000);f.workers->frame(0,200000,300000);
     f.adapter.stop(h);f.services.publish(f.adapter,h);
     check(f.workers->workers[0].storage->all_free()&&f.services.diagnostics().assigned==1,"ordinary stop drains all revoked READY slots without requiring another play/pump");}

    {Fixture f;f.initialize();auto h=f.admit();f.adapter.retire(h);f.services.cancel(h);f.services.maintenance_on_owner(f.readiness);
     check(f.services.diagnostics().draining==1&&!f.workers->workers[0].submits,"cancel before any graph waits for actual worker-side cancellation acknowledgement");
     f.workers->workers[0].cancel_ack=true;f.services.maintenance_on_owner(f.readiness);
     check(f.services.diagnostics().assigned==0&&!f.workers->workers[0].shutdowns,"no-graph cancellation can release assignment without shutting DD down");}
    {Fixture f;f.initialize();auto h=f.admit();f.workers->workers[0].invalid_ack_fault=[&]{f.adapter.stop(h);};
     f.play(h);check(f.services.diagnostics().quarantined==1,"injected submit/ack contract violation quarantines accepted transport");
     const auto accepted=f.workers->workers[0].submits;f.services.maintenance_on_owner(f.readiness);
     check(f.workers->workers[0].submits==accepted,"accepted command with failed canonical ack is never retried");}
}


void preparation_failures(){
    Fixture f;
    auto duplicate=std::make_unique<MockWorkers>();check(!f.services.prepare(f.package,std::move(duplicate)),"preparation handoff is process-one-shot");
    s::MediaServices failed(f.adapter,f.destination,f.controller,{1000,&f,&Fixture::qpc},&f.backend);
    auto transport=std::make_unique<MockWorkers>();auto* inspect=transport.get();inspect->workers[1].start_ok=false;
    check(!failed.prepare(f.package,std::move(transport)),"second service start failure cannot claim prepared bundle");
    failed.maintenance_on_owner(f.readiness);
    check(!failed.ready()&&inspect->workers[0].shutdowns==1&&inspect->workers[1].shutdowns==1,"partial preparation requests asynchronous cleanup without waits");
    s::MediaServices ineligible(f.adapter,f.destination,f.controller,{1000,&f,&Fixture::qpc},&f.backend);
    auto package=std::make_shared<m::PackageConfig>();package->provider_manifest=L"fixture.manifest";package->sources.push_back({2,4,L"source"});
    check(!ineligible.prepare(package,std::make_unique<MockWorkers>()),"source2 eligibility does not broaden effective flags8");
    f.initialize();auto h=f.admit();f.services.close_admission_and_cancel_on_owner();
    check(!f.services.ready()&&f.services.diagnostics().draining==1,"explicit close stops new admission while retirement progresses");
    auto transaction=f.services.clock(h);check(!transaction.apply,"retired assignment cannot expose a rate mutation thunk");
}

void no_allocations(){
    Fixture f;f.initialize();auto h=f.admit();f.play(h);allocations=0;count_allocations=true;
    for(unsigned i=0;i<1000;++i){f.services.maintenance_on_owner(f.readiness);f.pump(h);}
    count_allocations=false;check(allocations==0,"1000 connected empty service passes allocate no memory");
}
int main(){readiness_and_offers();zero_sequence_acknowledgment();frames_and_end();retry_and_bounds();reentry();retirement_and_reuse();loop_preplay_and_rate();cancel_without_graph_and_bad_ack();preparation_failures();no_allocations();
    std::printf("MEDIA SERVICES checks=%u failures=%u allocations=%u scope=synthetic_transport_actual_adapter_clock_leases_destination\n",checks,failures,allocations);return failures?1:0;}
