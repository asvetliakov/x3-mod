#include "media_services.h"
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif
namespace x3m::media_services {
namespace {
bool same(media::FrameIdentity a,media::FrameIdentity b) noexcept {return a==b;}
media::FrameIdentity tuple(const media::Snapshot& s) noexcept {return media::identity(s.publication);}
}
MediaServices::MediaServices(media_playback::Adapter& a,media_destination::Destination& d,
    media_startup::Controller& s,Counter c,media_destination::CopyBackend* copy) noexcept
    :adapter_(a),destination_(d),startup_(s),counter_(c),test_copy_(copy),slots_{Slot(c.frequency),Slot(c.frequency)} {
    for(auto& slot:slots_)slot.owner=this;
}
bool MediaServices::prepare(std::shared_ptr<const media::PackageConfig> package,std::unique_ptr<Workers> workers) noexcept {
    unsigned empty=0;
    if(!preparation_state_.compare_exchange_strong(empty,1))return false;
    try {
        if(!package||!workers||!counter_.frequency||!counter_.read){preparation_state_.store(3,std::memory_order_release);return false;}
        media::WorkerConfig configs[2];
        for(unsigned i=0;i<2;++i){
            configs[i].instance=i;configs[i].provider_manifest=package->provider_manifest;configs[i].package_owner=package;
            for(const auto& source:package->sources)if(source.id==2&&source.effective_flags==8)
                configs[i].sources.push_back({source.id,source.path});
            if(configs[i].sources.size()!=1||configs[i].sources[0].path.empty()||configs[i].provider_manifest.empty()){
                preparation_state_.store(3,std::memory_order_release);return false;
            }
        }
        auto bundle=std::make_unique<Preparation>();bundle->package=std::move(package);bundle->workers=std::move(workers);
        const bool first=bundle->workers->start(0,std::move(configs[0]));
        const bool second=first&&bundle->workers->start(1,std::move(configs[1]));
        if(!second){bundle->workers->shutdown(0);bundle->workers->shutdown(1);}
        preparation_=std::move(bundle);
        preparation_state_.store(second?2:3,std::memory_order_release);return second;
    }catch(...){preparation_state_.store(3,std::memory_order_release);return false;}
}
MediaServices::Slot* MediaServices::find(media::SessionHandle h) noexcept {
    if(!h)return nullptr;
    for(auto& s:slots_)if(s.session==h)return &s;
    return nullptr;
}
unsigned MediaServices::held(const Slot& s) const noexcept {
    unsigned n=s.in_copy;for(const auto& frame:s.frames)n+=bool(frame);return n;
}
bool MediaServices::revise(Slot& s) noexcept {
    if(s.revision==UINT64_MAX){s.quarantine=true;s.failed=true;return false;}
    ++s.revision;return true;
}
void MediaServices::revoke(Slot& s,media::LeaseRelease reason) noexcept {
    for(auto& frame:s.frames)frame.release(reason);
    s.queued=0;s.pending=-1;
}
bool MediaServices::ready() const noexcept {
    return adopted_&&preparation_state_.load(std::memory_order_acquire)==2&&initialized_&&!closed_&&readiness_.complete();
}
bool MediaServices::reserve(media::SessionHandle h,media::SourceKey source) noexcept {
    if(!ready()||!h||source!=2||find(h))return false;
    media::Snapshot initial;if(!adapter_.snapshot(h,initial)||initial.request.source!=source)return false;
    for(auto& s:slots_){
        if(s.session||s.draining||s.quarantine||!s.initialized||s.serial==UINT64_MAX||s.pump_depth)continue;
        const auto state=adopted_->workers->state(index(s)).state;
        if(state!=media::WorkerState::ready&&state!=media::WorkerState::failed)continue;
        // Safe prior assignment retirement permits reuse after graph failure;
        // source failure is not permanent service shutdown.
        ++s.serial;s.revision=1;s.session=h;s.source=source;s.logical=initial;s.have_logical=true;
        s.clock=media_owned::Clock(counter_.frequency);s.position_from_bounds=true;s.mapped={};s.failed=false;s.eof_seen=false;s.eof_admitted=false;
        s.terminal_flags=0;s.terminal_revision=0;s.graph=0;s.presented=0;s.binding=0;s.cancelled={};
        return true;
    }
    return false;
}
bool MediaServices::observe_record(media::SessionHandle h,const media_engine::BindingObservation& observation) noexcept {
    auto* s=find(h);if(!s||s->draining)return false;
    const bool bound=observation.published&&(observation.published_flags&4)!=0;
    return destination_.observe_record({h,observation.record},observation.slot,bound);
}
void MediaServices::cancel(media::SessionHandle h) noexcept {
    auto* s=find(h);if(!s||s->draining||!adopted_)return;
    s->draining=true;revise(*s);revoke(*s);destination_.cancel(h);
    media::Publication desired=s->logical.publication,current{};
    if(adapter_.publication(h.slot,current)&&current.session==h)desired=current;
    desired.session=h;desired.live=false;desired.playing=false;s->cancelled=desired;
    adopted_->workers->desired(index(*s),desired);
}
bool MediaServices::synchronize(Slot& s,const media::Snapshot& next,std::uint64_t tick) noexcept {
    if(!(next.publication.session==s.session)||!next.publication.live||s.draining)return false;
    const bool changed=!s.have_logical||!same(tuple(next),tuple(s.logical));
    const bool active=next.publication.playing&&next.operation_active;
    bool okay=true;
    if(active&&(changed||!same(s.mapped,tuple(next)))){
        revoke(s);revise(s);
        if(s.clock.operation()!=next.publication.operation||!s.clock.intent())
            okay=s.clock.begin(next.publication.operation,std::int64_t(next.request.start_ms)*10000,next.request.end_ms,next.request.loop,tick);
        else okay=s.clock.seek(std::int64_t(next.request.start_ms)*10000,next.request.end_ms,tick);
        if(okay){s.position_from_bounds=false;s.mapped=tuple(next);s.failed=false;s.eof_seen=false;s.eof_admitted=false;s.terminal_flags=0;s.terminal_revision=0;s.graph=0;}
    }else if(!active){
        if(changed||s.clock.intent()){revise(s);revoke(s);}
        if(s.clock.intent())okay=s.clock.stop(tick);
        media::CommandOffer stopped_offer;
        if(!s.clock.operation()||(adapter_.peek_command(s.session,stopped_offer)&&
           stopped_offer.command().kind==media::CommandKind::seek&&!stopped_offer.command().playing))s.position_from_bounds=true;
        s.mapped={};s.eof_seen=false;s.eof_admitted=false;
    }else if(next.request.end_ms!=s.logical.request.end_ms){
        // Canonical dependency: bound-only update preserves queue/time/generation.
        okay=s.clock.set_end(next.request.end_ms,tick);
        if(okay)revise(s);
    }
    if(!okay){s.failed=true;return false;}
    s.logical=next;s.have_logical=true;return true;
}
void MediaServices::offer_one(Slot& s) noexcept {
    if(s.draining||s.quarantine||!adopted_)return;
    media::CommandOffer offer;
    if(!adapter_.peek_command(s.session,offer)||!adapter_.offer_current(offer))return;
    if(!adopted_->workers->submit(index(s),offer.command()))return;
    // No external call or diagnostic callback may reenter between submit and ack.
    if(!adapter_.acknowledge_command(offer)){
        revise(s);revoke(s);s.failed=true;
        media::Publication desired{};
        if(adapter_.publication(s.session.slot,desired))adopted_->workers->desired(index(s),desired);
        // Accepted transport is never retried after an impossible/invalid ack.
        s.quarantine=true;
    }
}
void MediaServices::publish(media_playback::Adapter& adapter,media::SessionHandle h) noexcept {
    auto* s=find(h);if(&adapter!=&adapter_||!s||s->draining||!adopted_)return;
    media::Snapshot snapshot;if(!adapter_.snapshot(h,snapshot)){cancel(h);return;}
    if(!synchronize(*s,snapshot,now()))return;
    adopted_->workers->desired(index(*s),snapshot.publication);
    if(!snapshot.publication.playing){
        // An ordinary stop keeps the service assignment, but READY frames from
        // the revoked epoch still need main-side consumption even without pump.
        for(unsigned i=0;i<3;++i){auto frame=adopted_->workers->acquire(index(*s));if(!frame)break;frame.release(media::LeaseRelease::revoked);}
    }
    offer_one(*s);
}
void MediaServices::poll_events(Slot& s) noexcept {
    if(!adopted_)return;
    const auto worker=adopted_->workers->state(index(s));
    if(worker.state==media::WorkerState::ready)s.initialized=true;
    if(worker.state==media::WorkerState::unsafe_retained){s.quarantine=true;s.failed=true;}
    if(worker.state==media::WorkerState::retired){s.quarantine=true;s.failed=true;}
    // At most one cumulative terminal + one ordinary publication. Extra bounded
    // turns cover a producer update racing consumption without an unbounded loop.
    for(unsigned count=0;count<4;++count){
        media::WorkerEvent event;if(!adopted_->workers->event(index(s),event))break;
        if(event.flags&media::worker_unsafe_retained){s.quarantine=true;s.failed=true;}
        if(!s.session||s.draining||!same(event.identity,tuple(s.logical)))continue;
        if(event.graph&&s.graph&&event.graph<s.graph)continue;
        if(event.revision&&event.revision<s.terminal_revision)continue;
        if(event.revision)s.terminal_revision=event.revision;
        s.terminal_flags|=event.flags;if(event.graph)s.graph=event.graph;
        if(event.flags&(media::worker_failed|media::worker_unsafe_retained))s.failed=true;
        if(event.flags&media::worker_source_eof)s.eof_seen=true;
        if((event.flags&media::worker_prepared)&&!s.failed&&!s.eof_seen)
            adapter_.consume_event(s.session,event.identity.operation,event.identity.epoch,media::Event::ready);
    }
}
void MediaServices::ingest(Slot& s) noexcept {
    if(!adopted_||s.draining||s.failed||!s.clock.intent()||!same(s.mapped,tuple(s.logical)))return;
    bool drained=false;
    for(unsigned count=0;count<3;++count){
        unsigned free=3;for(unsigned i=0;i<3;++i)if(!s.frames[i]){free=i;break;}
        if(free==3||held(s)==3)break;
        auto lease=adopted_->workers->acquire(index(s));if(!lease){drained=true;break;}
        if(!same(lease.view().identity,s.mapped)){lease.release(media::LeaseRelease::revoked);continue;}
        const auto& view=lease.view();
        const auto admitted=s.clock.submit({s.clock.generation(),std::uint64_t(free+1),view.start,view.end});
        if(admitted!=media_owned::Admission::accepted){
            lease.release();
            if(admitted==media_owned::Admission::malformed||admitted==media_owned::Admission::full)s.failed=true;
            continue;
        }
        s.frames[free]=std::move(lease);s.fifo[s.queued++]=free;
    }
    // EOF may be polled before READY slots. With all three real slots held,
    // there can be no fourth final frame waiting in the canonical transport.
    if(s.eof_seen&&!s.eof_admitted&&(drained||held(s)==3))
        s.eof_admitted=s.clock.provider_eof(s.clock.generation());
}
void MediaServices::retire_progress(Slot& s) noexcept {
    if(!s.draining||!adopted_)return;
    // try_acquire also discards revoked READY transport slots under live=false.
    for(unsigned i=0;i<3;++i){auto frame=adopted_->workers->acquire(index(s));if(!frame)break;frame.release(media::LeaseRelease::revoked);}
    if(s.quarantine||s.pump_depth||held(s)||!adopted_->workers->quiescent(index(s),s.cancelled))return;
    s.session={};s.source=0;s.draining=false;s.have_logical=false;s.mapped={};s.failed=false;
}
void MediaServices::maintenance_on_owner(media_engine::Readiness readiness) noexcept {
    readiness_=readiness;
    const auto state=preparation_state_.load(std::memory_order_acquire);
    if(!adopted_&&(state==2||state==3)&&preparation_)adopted_=preparation_.get();
    if(!adopted_)return;
    for(auto& s:slots_){
        poll_events(s);retire_progress(s);
        if(s.session&&!s.draining&&!s.pump_depth)publish(adapter_,s.session);
    }
    if(state==2&&slots_[0].initialized&&slots_[1].initialized)initialized_=true;
    if(initialized_&&!readiness_signaled_){readiness_signaled_=startup_.service_ready();}
}
void MediaServices::close_admission_and_cancel_on_owner() noexcept {
    closed_=true;for(auto& s:slots_)if(s.session)cancel(s.session);
}
bool MediaServices::position(media::SessionHandle h,std::uint32_t& output) noexcept {
    auto* s=find(h);if(!s||s->draining||!s->have_logical)return false;
    if(s->position_from_bounds){output=std::uint32_t(s->logical.request.start_ms);return true;}
    const auto value=s->clock.milliseconds();if(!value.in_range)return false;
    output=std::uint32_t(value.truncated);return true;
}
media_playback::ClockTransaction MediaServices::clock(media::SessionHandle h) noexcept {
    auto* s=find(h);if(!s||s->draining||s->quarantine)return {};
    s->rate_session=h;s->rate_serial=s->serial;return {s,&rate};
}
bool MediaServices::rate(void* context,std::int32_t input,double exact) noexcept {
    auto& s=*static_cast<Slot*>(context);double decoded=0;
    if(!s.owner||!s.session||s.draining||s.quarantine||!(s.session==s.rate_session)||s.serial!=s.rate_serial||
       s.revision==UINT64_MAX||!media_playback::decode_rate(input,decoded)||decoded!=exact)return false;
    media::Snapshot live;if(!s.owner->adapter_.snapshot(s.session,live))return false;
    if(!s.clock.set_rate(input,s.owner->now()))return false;
    ++s.revision;return true;
}
bool MediaServices::eligible(const Slot& s,const media::FrameLease& frame) const noexcept {
    return frame&&same(frame.view().identity,s.mapped)&&s.clock.intent()&&s.clock.end()==media_owned::End::none&&
        s.clock.compare(frame.view().start)>=0&&s.clock.compare(frame.view().end)<0;
}
media_engine::PumpResult MediaServices::schedule(Slot& s,std::uint64_t tick) noexcept {
    using Result=media_engine::PumpResult;
    if(s.failed)return Result::failed;
    ingest(s);if(s.failed)return Result::failed;
    const auto update=s.clock.update(tick);
    if(!update.accepted||update.consumed>s.queued){s.failed=true;return Result::failed;}
    int selected=-1;
    for(unsigned i=0;i<update.consumed;++i){
        const auto slot=s.fifo[i];
        if(update.selected&&update.frame.token==slot+1)selected=int(slot);
        else s.frames[slot].release(media::LeaseRelease::clock_consumed);
    }
    for(unsigned i=update.consumed;i<s.queued;++i)s.fifo[i-update.consumed]=s.fifo[i];
    s.queued-=update.consumed;
    if(selected>=0){if(s.pending>=0&&s.pending!=selected)s.frames[s.pending].release(media::LeaseRelease::clock_consumed);s.pending=selected;}
    if(s.pending>=0&&!eligible(s,s.frames[s.pending])){s.frames[s.pending].release();s.pending=-1;}
    if(update.end==media_owned::End::positive||update.end==media_owned::End::eof){revoke(s);return Result::complete;}
    if(update.end==media_owned::End::malformed||update.end==media_owned::End::position_range){revoke(s);s.failed=true;return Result::failed;}
    return Result::pending;
}
media_destination::Current MediaServices::current(void* context,const media_destination::CopyRequest&) noexcept {
    const auto& ticket=*static_cast<CopyTicket*>(context);const auto& s=*ticket.slot;
    const auto continuation=ticket.owner->adapter_.classify_continuation(ticket.request.traversal);
    const bool live=s.session==ticket.request.session&&s.serial==ticket.serial&&s.revision==ticket.revision&&!s.draining&&
        ticket.owner->adapter_.accepts_publication(ticket.request.session,ticket.request.operation,ticket.request.epoch)&&
        ticket.consumer&&ticket.consumer(ticket.context,ticket.request);
    return {live,continuation};
}
media_engine::PumpResult MediaServices::pump(const media_engine::PumpRequest& request,CurrentCheck check,void* context) noexcept {
    using Result=media_engine::PumpResult;
    const auto traversal=adapter_.classify_continuation(request.traversal);
    if(traversal!=media_playback::Continuation::live||!check||!check(context,request))return Result::invalidated;
    auto* s=find(request.session);if(!s||s->draining)return Result::invalidated;
    if(s->pump_depth)return Result::pending;
    struct PumpScope {Slot& slot;explicit PumpScope(Slot& s):slot(s){++slot.pump_depth;}~PumpScope(){--slot.pump_depth;}} scope(*s);
    publish(adapter_,s->session);poll_events(*s);
    if(!adapter_.accepts_publication(request.session,request.operation,request.epoch))return Result::invalidated;
    const auto scheduled=schedule(*s,now());if(scheduled!=Result::pending)return scheduled;
    if(s->pending<0)return Result::pending;
    const unsigned selected=unsigned(s->pending);s->pending=-1;
    media::FrameLease local=std::move(s->frames[selected]);s->in_copy=1;
    CopyTicket ticket{this,s,request,check,context,s->serial,s->revision};
    media_destination::CopyRequest copy{{request.session,request.record},request.operation,request.epoch,request.traversal};
    const media_destination::CurrentCheck guard{&ticket,&current};
    media_destination::CopyResult result;
    if(test_copy_)result=destination_.try_copy(copy,local,guard,*test_copy_);
#ifdef _WIN32
    else result=destination_.try_copy(copy,local,guard);
#endif
    const auto after=current(&ticket,copy);s->in_copy=0;
    if(after.continuation!=media_playback::Continuation::live||result.kind==media_destination::CopyKind::abort_traversal){local.release(media::LeaseRelease::revoked);return Result::invalidated;}
    if(!after.operation_live){local.release(media::LeaseRelease::revoked);return Result::pending;}
    if(result.kind==media_destination::CopyKind::written){
        s->presented=result.frame_sequence;s->binding=result.binding_epoch;
        local.release(media::LeaseRelease::selected_uploaded);
    }else if(result.kind!=media_destination::CopyKind::superseded&&eligible(*s,local)){
        s->frames[selected]=std::move(local);s->pending=int(selected);
    }else local.release(media::LeaseRelease::revoked);
    // Clock scheduling already committed before D3D. Never overwrite nested
    // rate/seek/stop with an old Clock copy, and never infer end from copy result.
    return Result::pending;
}
Diagnostics MediaServices::diagnostics() const noexcept {
    Diagnostics out{};out.preparation_published=preparation_state_.load(std::memory_order_acquire)==2;
    out.initialized=initialized_;out.admission=ready();
    for(unsigned i=0;i<2;++i){const auto& s=slots_[i];out.assigned+=bool(s.session);out.draining+=s.draining;out.quarantined+=s.quarantine;
        if(s.failed)out.failed_mask|=1u<<i;
        out.leases+=held(s);out.presented[i]=s.presented;out.binding[i]=s.binding;out.clock_generation[i]=s.clock.generation();out.revision[i]=s.revision;out.rate_numerator[i]=s.clock.rate_numerator();}
    return out;
}
#ifdef _WIN32
namespace {
class NativeWorkers final:public Workers {
    media::LavWorker workers_[2];
public:
    bool start(unsigned i,media::WorkerConfig c) override {return i<2&&workers_[i].start_service(std::move(c));}
    bool submit(unsigned i,const media::Command& c) noexcept override {return workers_[i].try_submit(c);}
    void desired(unsigned i,const media::Publication& p) noexcept override {workers_[i].publish_desired(p);}
    media::FrameLease acquire(unsigned i) noexcept override {return workers_[i].try_acquire_frame();}
    bool event(unsigned i,media::WorkerEvent& e) noexcept override {return workers_[i].poll_event(e);}
    media::WorkerPoll state(unsigned i) const noexcept override {return workers_[i].poll_service_state();}
    bool quiescent(unsigned i,const media::Publication& canceled) noexcept override {
        // Pending canonical worker dependency; requires exact canceled tuple.
        return workers_[i].poll_assignment_quiescent(canceled);
    }
    void shutdown(unsigned i) noexcept override {workers_[i].request_shutdown();}
};
}
bool MediaServices::prepare_on_bootstrap(const media_startup::BootstrapContext& context) noexcept {
    std::shared_ptr<const media::PackageConfig> package;media::PackageError error;
    if(media::load_package_config(static_cast<HMODULE>(context.pinned_proxy),package,error)!=media::PackageStatus::ready)return false;
    try{return prepare(std::move(package),std::make_unique<NativeWorkers>());}catch(...){return false;}
}
bool MediaServices::bootstrap(void* context,const media_startup::BootstrapContext& startup) noexcept {
    return context&&static_cast<MediaServices*>(context)->prepare_on_bootstrap(startup);
}
#endif
}
