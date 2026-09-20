#include "../../src/proxy/media_destination.h"
#include "lav_worker.h"
#include "media_presentation_gate_contract_fixture.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>
namespace d=x3m::media_destination;
namespace m=x3m::media;
namespace p=x3m::media_playback;
namespace g=x3m::media_presentation_gate;
unsigned checks=0,failures=0;
void check(bool pass,const char* label){++checks;if(!pass){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
d::BindingOwner owner{{0,1},{0x8000,1}};
struct Source:d::IdentitySource {
    std::uint64_t generation=1,serial=1;unsigned calls=0;bool available=true;
    bool snapshot(std::uint32_t device,std::uint32_t surface,x3m::ownership::SurfaceLeaseIdentity& out) noexcept override {
        ++calls;if(!available||device!=0x100||!surface)return false;out={1,generation,serial+surface};return true;
    }
};
struct Fixture {
    Source source;g::Admission gate;gate_fixture::Mock platform;g::Transaction transaction;
    d::Destination destination{gate,source,7};
    Fixture(){check(gate.qualify_owner(7),"gate owner");check(g::stage(platform,transaction,platform.site,gate)&&g::install(platform,transaction,gate)&&gate.enable(7),"gate installed");destination.set_device_key(0x100,7);
        {std::lock_guard<std::mutex> lock(destination.domain());check(destination.state_locked().table(0x20000,4,0,false),"initial table");}
        publish(0,0x30000,0x400);publish(1,0x30100,0x401);destination.observe_record(owner,0,true);
    }
    void publish(unsigned slot,unsigned key,unsigned surface){d::State::Publication token;{std::lock_guard<std::mutex> lock(destination.domain());token=destination.state_locked().publish_slot(0x20000,slot*16,key,surface);}destination.qualify(token);}
};
struct Frame {
    std::shared_ptr<m::lav_detail::FrameStorage> storage=std::make_shared<m::lav_detail::FrameStorage>(nullptr);
    m::FrameLease lease;
    Frame(){auto& s=storage->slots[0];storage->reserve(0,{owner.session,3,4},1,9);s.view.width=4;s.view.height=3;s.view.pitch=16;s.view.end=1;
        for(unsigned i=0;i<48;++i)s.pixels[i]=static_cast<unsigned char>(i*17);storage->publish(0);lease=storage->acquire(storage,0);}
};
struct Live {d::Current value{true,p::Continuation::live};static d::Current read(void* c,const d::CopyRequest&) noexcept{return static_cast<Live*>(c)->value;}};
struct Backend:d::CopyBackend {
    Fixture& fixture;unsigned acquire_count=0,desc_count=0,locks=0,unlocks=0,releases=0,throw_at=0;
    std::int32_t acquire_hr=0,desc_hr=0,lock_hr=0,unlock_hr=0;bool held=false,mapped=false;
    d::Descriptor descriptor{4,3,21,2,0,0};d::Mapping mapping{};std::vector<unsigned char> bytes=std::vector<unsigned char>(96,0xab);
    std::function<void(unsigned)> spy;
    explicit Backend(Fixture& f):fixture(f){mapping={bytes.data(),24};}
    void call(unsigned n) noexcept {check(fixture.gate.depth()==1,"external stage inside copy depth");if(spy)spy(n);}
    std::int32_t acquire(const d::State::Snapshot&) noexcept override {++acquire_count;if(!acquire_hr)held=true;call(1);return acquire_hr;}
    std::int32_t describe(d::Descriptor& out) noexcept override {check(held,"desc retained");++desc_count;out=descriptor;call(2);return desc_hr;}
    std::int32_t lock(d::Mapping& out) noexcept override {check(held,"lock retained");++locks;if(lock_hr>=0)mapped=true;out=mapping;call(3);return lock_hr;}
    std::int32_t unlock() noexcept override {check(mapped&&held,"unlock mapped once");mapped=false;++unlocks;call(4);return unlock_hr;}
    void release() noexcept override {check(held&&!mapped,"release after unlock");held=false;++releases;call(5);}
    void checkpoint(unsigned n) override {if(n==throw_at)throw n;}
};
d::CopyRequest request(){return {owner,3,4,{1,p::Traversal::manager}};}
void state_tests(){
    d::State s;s.set_device(0x100);check(s.table(0x20000,32767,32767,false)&&s.tracked==32768&&s.capacity==65767&&s.bound==65534,"max budget exact bounds");
    std::uint32_t b=0,c=0;check(!d::State::bounds(-1,0,b,c)&&!d::State::bounds(0,-1,b,c),"signed negative refuses");
    for(unsigned i=0;i<32768;++i){auto pub=s.publish_slot(0x20000,i*16,0x30000+(i%113)*16,0x400+(i%113));s.qualify(pub,{1,1,0x400+(i%113)});}
    check(s.watch(owner,32767,true),"late watch maximum selectable");d::State::Snapshot before;check(s.snapshot(owner,before),"late provenance no raw read");
    auto outer=s.begin();auto inner=s.begin();auto pub=s.publish_slot(0x20000,32767*16,0x60000,0x888);s.qualify(pub,{1,1,0x888});d::State::Snapshot out;
    check(!s.snapshot(owner,out),"nested mutation blocks");check(s.end(inner)&&!s.snapshot(owner,out),"inner publication cannot erase outer invalidation");check(s.end(outer)&&s.snapshot(owner,out),"outer normal completion permits exact publication");check(!s.current(before),"binding publication change invalidates capture");
    s.watch(owner,0x10000,true);check(s.snapshot(owner,out),"selector truncates low WORD");s.watch(owner,0x8000,true);check(!s.snapshot(owner,out),"selector sign extends");s.watch(owner,0,true);
    auto old=s.candidate(0x30000);s.retire_wrapper(0x30000);s.qualify(old,{1,1,0x400});check(!s.snapshot(owner,out),"final retirement tombstones aliases");
    auto fresh=s.publish_slot(0x20000,0,0x30000,0x400);s.qualify(fresh,{1,1,0x400});check(fresh.lifetime!=old.lifetime&&s.snapshot(owner,out),"same address new lifetime");
    s.watch(owner,113,true);check(!s.snapshot(owner,out),"old shared alias cannot revive through address reuse");
    s.watch(owner,0,true);s.invalidate_wrapper(0x30000,true);check(!s.candidate(0x30000).publication,"cleanup cannot recovery refresh stale key");
    // Churn an open-addressed index far past its capacity: backward deletion
    // removes tombstones and bounded slot links release exactly their own node.
    for(unsigned i=0;i<100000;++i){auto q=s.publish_slot(0x20000,0,0x100000+i*16,0x500);s.qualify(q,{1,1,0x500});}
    check(s.snapshot(owner,out)&&!s.disabled,"100000 lifetimes without saturation");
    auto same=out;s.invalidate_table();check(!s.current(same),"table invalid immediately");check(s.table(0x20000,4,0,false)&&!s.snapshot(owner,out),"same table address recreated unknown");
    d::State depth;d::State::Token tokens[64];for(auto& t:tokens)t=depth.begin();check(!depth.begin().serial&&depth.disabled,"mutation overflow fail closed");
    d::State duplicate;auto t=duplicate.begin();check(duplicate.end(t)&&!duplicate.end(t)&&duplicate.disabled,"duplicate completion fail closed");
}
void copies(){
    {Fixture f;Frame frame;Live live;Backend b(f);auto r=f.destination.try_copy(request(),frame.lease,{&live,Live::read},b);check(r.kind==d::CopyKind::written&&r.frame_sequence==9,"actual immutable lease writes");
     for(unsigned y=0;y<3;++y){check(!std::memcmp(b.bytes.data()+y*24,frame.storage->slots[0].pixels.data()+y*16,16),"intended row bytes");for(unsigned x=16;x<24;++x)check(b.bytes[y*24+x]==0xab,"padding untouched");}
     check(b.locks==1&&b.unlocks==1&&b.releases==1&&!f.gate.depth(),"copy cleanup balanced");check(frame.storage->slots[0].state==m::lav_detail::reading_slot,"canonical slot held until caller release");}
    for(unsigned stage=1;stage<=5;++stage)for(unsigned action=0;action<5;++action){Fixture f;Frame frame;Live live;Backend b(f);b.spy=[&](unsigned n){if(n!=stage)return;
       if(action==0)f.destination.observe_record(owner,1,true);
       if(action==1)live.value.operation_live=false;
       if(action==2)live.value.continuation=p::Continuation::abort_manager;
       if(action==3){std::lock_guard<std::mutex> lock(f.destination.domain());f.destination.state_locked().invalidate_table();}
       if(action==4){Backend nested(f);auto result=f.destination.try_copy(request(),frame.lease,{&live,Live::read},nested);check(result.kind==d::CopyKind::unavailable&&nested.acquire_count==0,"nested copy refuses before retain");}
    };const auto r=f.destination.try_copy(request(),frame.lease,{&live,Live::read},b);
       check(r.kind==(action==0||action==3?d::CopyKind::unavailable:action==1?d::CopyKind::superseded:action==2?d::CopyKind::abort_traversal:d::CopyKind::written),"stage reentry selects pointer-free result");
       check(b.unlocks==b.locks&&b.releases==1&&!b.held&&!b.mapped&&!f.gate.depth(),"reentry cleanup exactly once");}
    for(unsigned checkpoint=1;checkpoint<=4;++checkpoint){Fixture f;Frame frame;Live live;Backend b(f);b.throw_at=checkpoint;auto r=f.destination.try_copy(request(),frame.lease,{&live,Live::read},b);check(r.kind==d::CopyKind::copy_failed&&r.reason==d::Reason::internal,"caught internal completed-stage throw");check(b.releases==1&&b.unlocks==b.locks&&!f.gate.depth(),"throw cleanup before depth clear");}
    for(unsigned bad=0;bad<8;++bad){Fixture f;Frame frame;Live live;Backend b(f);
        if(bad==0)b.acquire_hr=-1;if(bad==1)b.desc_hr=-2;if(bad==2)b.lock_hr=-3;if(bad==3)b.unlock_hr=-4;
        if(bad==4)b.descriptor.format=23;if(bad==5)b.mapping.pitch=-1;if(bad==6)b.mapping.pitch=8;if(bad==7)b.descriptor.width=2;
        const auto r=f.destination.try_copy(request(),frame.lease,{&live,Live::read},b);check(r.kind!=d::CopyKind::written,"invalid backend facts never acknowledge");check(b.unlocks==(b.locks&&b.lock_hr>=0?1u:0u)&&b.releases==(bad?1u:0u)&&!f.gate.depth(),"failure retain lock accounting");}
    {Fixture f;Frame frame;Live live;Backend b(f);auto r=request();++r.epoch;check(f.destination.try_copy(r,frame.lease,{&live,Live::read},b).reason==d::Reason::frame&&b.acquire_count==0,"wrong epoch rejected before retain");}
    {Fixture f;Frame frame;Live live;Backend b(f);check(f.destination.begin_engine_reset(7),"whole reset enters before cleanup");f.destination.native_reset(7,0x100,true,1);f.source.generation=2;f.destination.native_reset(7,0x100,false,-1);f.destination.end_engine_reset(7,false);
     check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},b).kind==d::CopyKind::unavailable,"failed Reset blocks");
     check(f.destination.begin_engine_reset(7),"later Reset begins");f.destination.native_reset(7,0x100,true,1);f.source.generation=3;f.destination.native_reset(7,0x100,false,0);
     check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},b).kind==d::CopyKind::unavailable,"native success alone still blocks");f.destination.end_engine_reset(7,true);
     check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},b).kind==d::CopyKind::written,"same canonical surface recovery new device generation");}
    {Fixture f;Frame frame;Live live;Backend b(f);f.destination.begin_engine_reset(7);f.destination.native_reset(7,0x100,true,1);f.source.generation=2;f.source.serial=99;f.destination.native_reset(7,0x100,false,0);f.destination.end_engine_reset(7,true);
     check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},b).kind==d::CopyKind::unavailable,"canonical address reuse cannot recovery refresh");}
}
struct Memory:d::Memory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x610000);unsigned wrapper_reads=0;
    bool read(std::uint32_t a,void* p,unsigned n) noexcept override {if(a==0x30030)++wrapper_reads;if(a>bytes.size()-n)return false;std::memcpy(p,bytes.data()+a,n);return true;}
    bool write(std::uint32_t a,const void* p,unsigned n) noexcept override {if(a>bytes.size()-n)return false;std::memcpy(bytes.data()+a,p,n);return true;}
    void word(unsigned a,unsigned v){write(a,&v,4);}
};
d::Frame envelope(unsigned esp){d::Frame f{};f.saved_esp=esp-8;return f;}
void observers(){
    Fixture f;Memory memory;d::Routes routes;for(unsigned i=0;i<d::lifecycle_count;++i)routes.after[i]=0x100000+i*32;
    d::Observer observer(f.destination,memory,routes,{});memory.word(0x1000,0xabc);memory.word(0x30030,0x400);
    auto enter=envelope(0x1000);enter.eax=0;observer.dispatch(unsigned(d::SiteId::slot_materialize),enter,7);
    auto store=envelope(0x900);store.eax=0x30000;store.ecx=0x20000;store.edx=0;
    observer.dispatch(unsigned(d::SiteId::slot_publish_new),store,7);memory.word(0x20008,0x30000);observer.dispatch(d::site_count+unsigned(d::SiteId::slot_publish_new),store,7);
    d::State::Snapshot snapshot;{std::lock_guard<std::mutex> lock(f.destination.domain());check(!f.destination.state_locked().snapshot(owner,snapshot),"actual Observer keeps materialize transaction active");}
    auto after=envelope(0x1004);after.eax=1;observer.dispatch(2*d::site_count+unsigned(d::SiteId::slot_materialize),after,7);check(after.target==0xabc,"durable caller restored");
    check(memory.wrapper_reads==1,"only qualified publication reads wrapper");
    {std::lock_guard<std::mutex> lock(f.destination.domain());check(f.destination.state_locked().snapshot(owner,snapshot),"actual Observer publication eligible at outer return");}
    f.destination.observe_record(owner,0,true);Frame frame;Live live;Backend backend(f);f.destination.try_copy(request(),frame.lease,{&live,Live::read},backend);check(memory.wrapper_reads==1,"late binding and all D3D stages do not read wrapper");
    // Foreign retirement waits for the live producer interval; it then disables
    // acquisition without accessing the owner-only adapter or return stack.
    observer.dispatch(unsigned(d::SiteId::slot_publish_new),store,7);std::atomic<bool> started{false},done{false};
    std::thread foreign([&]{started=true;auto mutation=envelope(0x2000);observer.dispatch(unsigned(d::SiteId::wrapper_cleanup),mutation,8);done=true;});
    while(!started.load())std::this_thread::yield();check(!done.load(),"foreign mutation waits before free ingress");
    observer.dispatch(d::site_count+unsigned(d::SiteId::slot_publish_new),store,7);foreign.join();check(done&&!f.gate.enabled(),"foreign mutation disables owner admission");
}
bool find_binding(void*,std::uint32_t address,m::SessionHandle& session,p::EngineKey& record) noexcept {
    if(address!=owner.record.address)return false;session=owner.session;record=owner.record;return true;
}
void additional_observers(){
    {d::State state;state.set_device(0x100);state.table(0x20000,4,0,false);
     for(unsigned i=0;i<3;++i){auto p=state.publish_slot(0x20000,i*16,0x30000,0x400);state.qualify(p,{1,1,3});}
     const auto retired_lifetime=state.lifetime(0x30000);state.retire_wrapper(0x30000);state.set_device(0x100);
     auto fresh=state.publish_slot(0x20000,48,0x30000,0x400);state.qualify(fresh,{2,1,4});
     check(fresh.lifetime!=retired_lifetime,"new wrapper generation while retired aliases remain");
     check(state.table(0x40000,4,1000,true)&&state.candidate(0x30000).lifetime==fresh.lifetime,"device publication preserves retired tombstone through growth reindex");
     state.watch(owner,1,true);d::State::Snapshot snapshot;check(!state.snapshot(owner,snapshot),"old shared alias cannot qualify after device publication and growth");
     for(unsigned i=0;i<3;++i)state.clear_slot(i);
     check(state.candidate(0x30000).lifetime==fresh.lifetime,"last old alias cannot erase new same-key wrapper index");
     state.watch(owner,3,true);check(state.snapshot(owner,snapshot),"new same-address wrapper remains eligible");state.retire_wrapper(0x30000);
     check(!state.snapshot(owner,snapshot),"new generation remains indexed and final-retirable");}

    {Fixture f;Frame frame;Live live;Backend b(f);b.spy=[&](unsigned stage){if(stage==5)f.destination.set_device_key(0x100,7);};
     check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},b).kind==d::CopyKind::unavailable,"same-address device publication during final Release cannot acknowledge old allocation");
     Backend retry(f);check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},retry).kind==d::CopyKind::unavailable&&retry.acquire_count==0,"same-address device replacement requires fresh producer provenance");
     f.publish(0,0x30000,0x400);Backend fresh(f);check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},fresh).kind==d::CopyKind::written,"new device accepts fresh producer publication");}

    for(unsigned kind=0;kind<2;++kind)for(unsigned unknown=0;unknown<2;++unknown){Fixture f;Memory memory;d::Routes routes;for(unsigned i=0;i<d::lifecycle_count;++i)routes.after[i]=0x100000+i*32;d::Observer observer(f.destination,memory,routes,{});
      const auto key=unknown?0x50000u:0x30000u;const auto site=kind?d::SiteId::wrapper_release:d::SiteId::wrapper_cleanup;
      memory.word(0x1000,0xaaa);memory.word(0x1004,0x20008);memory.word(0x20008,key);
      auto before=envelope(0x1000);before.esi=key;observer.dispatch(unsigned(site),before,7);
      f.publish(kind?1:0,key,0x400);if(kind)f.destination.observe_record(owner,1,true);auto after=envelope(0x1004);after.eax=kind?0:1;observer.dispatch(2*d::site_count+unsigned(site),after,7);
      std::lock_guard<std::mutex> lock(f.destination.domain());d::State::Snapshot snapshot;check(after.target==0xaaa&&!f.destination.state_locked().snapshot(owner,snapshot)&&!f.gate.enabled(),"unknown or sole-alias inner publication cannot resurrect after cleanup/final release");}

    {Fixture f;Memory memory;d::Routes routes;for(unsigned i=0;i<d::lifecycle_count;++i)routes.after[i]=0x100000+i*32;
     d::Observer observer(f.destination,memory,routes,{});memory.word(0x1000,0xaaa);memory.word(0x800,0xbbb);
     auto outer=envelope(0x1000);observer.dispatch(unsigned(d::SiteId::slot_materialize),outer,7);
     auto inner=envelope(0x800);inner.esi=0x30000;observer.dispatch(unsigned(d::SiteId::wrapper_cleanup),inner,7);
     auto returning=envelope(0x1004);observer.dispatch(2*d::site_count+unsigned(d::SiteId::slot_materialize),returning,7);
     check(returning.target==0xaaa&&!f.gate.enabled(),"escaped inner preserves surviving outer original return and vetoes copy");
     memory.word(0x2000,0xccc);auto unrelated=envelope(0x2000);observer.dispatch(unsigned(d::SiteId::slot_materialize),unrelated,7);unsigned actual=0;memory.read(0x2000,&actual,4);check(actual==0xccc,"unrelated later call forwards its own original continuation");}
    {Fixture f;Memory memory;d::Routes routes;d::Observer observer(f.destination,memory,routes,{nullptr,find_binding});
     f.destination.begin_engine_reset(7);f.destination.native_reset(7,0x100,true,1);f.source.available=false;f.publish(2,0x30200,0x402);f.source.available=true;f.source.generation=2;
     f.destination.native_reset(7,0x100,false,0);f.destination.end_engine_reset(7,true);
     for(unsigned slot=1;slot<=2;++slot){auto frame=envelope(0x1000);frame.eax=owner.record.address;frame.ecx=slot;
       observer.dispatch(unsigned(d::SiteId::record_bind_matched),frame,7);observer.dispatch(d::site_count+unsigned(d::SiteId::record_bind_matched),frame,7);
       std::lock_guard<std::mutex> lock(f.destination.domain());d::State::Snapshot snapshot;check(f.destination.state_locked().snapshot(owner,snapshot)&&snapshot.identity.device_generation==2,"actual matched bind finishes unwatched recovery or pending first publication");}
     check(memory.wrapper_reads==0,"late actual binding uses only owned provenance keys");}
    {Fixture f;Frame frame;Live live;Backend failed(f);failed.unlock_hr=-99;auto result=f.destination.try_copy(request(),frame.lease,{&live,Live::read},failed);
     check(result.kind==d::CopyKind::copy_failed&&result.status==-99,"Unlock failure exact HRESULT");Backend retry(f);check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},retry).kind==d::CopyKind::unavailable&&retry.acquire_count==0,"failed Unlock publication loses qualification");
     f.publish(0,0x30000,0x400);Backend fresh(f);check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},fresh).kind==d::CopyKind::written,"fresh producer publication requalifies after Unlock failure");}
    {Fixture f;Frame frame;Live live;Backend failed(f);failed.unlock_hr=-99;failed.spy=[&](unsigned stage){if(stage==4)f.publish(0,0x30000,0x400);};f.destination.try_copy(request(),frame.lease,{&live,Live::read},failed);
     Backend retry(f);check(f.destination.try_copy(request(),frame.lease,{&live,Live::read},retry).kind==d::CopyKind::written,"old Unlock failure cannot invalidate reentrant fresh publication");}
    {d::State state;state.set_device(0x100);check(state.table(0x20000,4,999,false),"pre growth capacity");state.watch(owner,3,true);auto publication=state.publish_slot(0x20000,48,0x30000,0x400);state.qualify(publication,{1,1,3});const auto incarnation=state.incarnation;
     check(state.table(0x40000,4,1000,true)&&state.capacity==2004&&state.incarnation==incarnation,"successful relocated preserved-prefix growth");d::State::Snapshot snapshot;check(state.snapshot(owner,snapshot),"growth keeps observed prefix eligible");check(state.table(0x40000,4,1000,true)&&state.snapshot(owner,snapshot),"failed engine grow rollback retains exact surviving counts/prefix");
     check(!state.table(0x50000,4,999,true)&&!state.snapshot(owner,snapshot),"ambiguous shrinking grow refuses");}
}
struct PatchPlatform:d::Platform {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x800000);unsigned next=0x700000,compares=0,fail_compare=0,flushes=0,protections=0,fail_flush=0,fail_protect=0;bool fail_read_after_swap=false,just_swapped=false;
    PatchPlatform(){for(const auto& s:d::sites)std::memcpy(bytes.data()+s.address,s.bytes,s.length);}
    bool qualified() override{return true;}bool install_window() override{return true;}
    std::uint32_t reserve() override {auto n=next;next+=4096;return n;}
    std::uint32_t counter_address(const g::Admission&) override{return 0x600000;}
    bool read(std::uint32_t a,void* p,unsigned n) override {if(fail_read_after_swap&&just_swapped){fail_read_after_swap=false;return false;}if(a>bytes.size()-n)return false;std::memcpy(p,bytes.data()+a,n);return true;}
    bool write_reserved(std::uint32_t a,const void* p,unsigned n) override{if(a>bytes.size()-n)return false;std::memcpy(bytes.data()+a,p,n);return true;}
    bool executable(std::uint32_t,unsigned) override{return true;}
    bool protect(std::uint32_t,unsigned,std::uint32_t,std::uint32_t& previous) override{previous=g::read_execute;return ++protections!=fail_protect;}
    bool compare8(std::uint32_t a,const unsigned char* old,const unsigned char* value) override{if(++compares==fail_compare||std::memcmp(bytes.data()+a,old,8))return false;std::memcpy(bytes.data()+a,value,8);just_swapped=true;return true;}
    bool flush(std::uint32_t,unsigned) override{return ++flushes!=fail_flush;}
    bool original(){for(const auto& s:d::sites)if(std::memcmp(bytes.data()+s.address,s.bytes,s.length))return false;return true;}
};
void patches(){
    for(unsigned fail=1;fail<=d::site_count;++fail){PatchPlatform p;d::Group group;check(d::stage(p,group,0x600100),"19 site group stages");p.fail_compare=fail;check(!d::install(p,group)&&p.original(),"each publication failure rolls back entire group");}
    {PatchPlatform p;d::Group group;check(d::stage(p,group,0x600100)&&d::install(p,group),"complete observer group installs");check(!d::restore(p,group,false,true),"nonquiescent refuses removal");check(d::restore(p,group,true,true)&&p.original(),"whole group restore exact bytes");}
    for(unsigned rollback_step=3;rollback_step<=4;++rollback_step){PatchPlatform p;d::Group group;check(d::stage(p,group,0x600100),"debt fixture staged");p.fail_flush=p.flushes+1;p.fail_protect=p.protections+rollback_step;
      check(!d::install(p,group)&&group.ever_published,"flush plus rollback API failure refuses group success");const auto& first=group.patches[0];check(first.may_redirect||first.flush_debt||first.protection_debt,"failed rollback retains redirect or visibility/protection debt");p.fail_flush=p.fail_protect=0;
      check(d::restore(p,group,true,true)&&p.original()&&!first.may_redirect&&!first.flush_debt&&!first.protection_debt,"exact retry settles all group debt");}
    {PatchPlatform p;d::Group group;check(d::stage(p,group,0x600100),"readback witness staged");p.fail_read_after_swap=true;check(!d::install(p,group)&&p.original(),"failed post-CAS readback rolls back before success");}
    {PatchPlatform p;d::Group group;check(d::stage(p,group,0x600100)&&d::install(p,group),"foreign rollback witness installed");const auto at=group.patches[0].word_address;p.bytes[at]^=0x80;unsigned char foreign[8];std::memcpy(foreign,p.bytes.data()+at,8);
      check(!d::restore(p,group,true,true)&&!std::memcmp(foreign,p.bytes.data()+at,8)&&group.patches[0].may_redirect,"foreign qword retained untouched with redirect ownership");}
    for(unsigned i=0;i<d::site_count;++i){PatchPlatform p;d::Group group;p.bytes[d::sites[i].address]^=1;check(!d::stage(p,group,0x600100)&&p.compares==0,"preexisting patch refuses before publication");}
}
int main(){const auto start=std::chrono::steady_clock::now();state_tests();copies();observers();additional_observers();patches();const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();std::printf("media_destination_host checks=%u failures=%u churn=100000 reentry=25 throw=4 sites=19 elapsed_ms=%lld\n",checks,failures,static_cast<long long>(elapsed));return failures?1:0;}
