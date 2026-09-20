#include "media_engine_adapter.h"
#include <cstring>
#include <utility>
#ifdef _WIN32
#include "cpu_state.h"
#include <windows.h>
#endif
namespace x3m::media_engine {
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,"foreign classification must not lock");
bool OwnedKeys::contains(const std::atomic<std::uint32_t>* keys,std::uint32_t key) noexcept {
    if(!key)return false;
    for(unsigned i=0;i<capacity;++i)if(keys[i].load(std::memory_order_acquire)==key)return true;
    return false;
}
bool OwnedKeys::append(std::atomic<std::uint32_t>* keys,std::uint32_t key) noexcept {
    if(!key)return false;
    if(contains(keys,key))return true;
    for(unsigned i=0;i<capacity;++i)if(!keys[i].load(std::memory_order_relaxed)){
        keys[i].store(key,std::memory_order_release);return true;
    }
    return false;
}
bool OwnedKeys::record(std::uint32_t key) const noexcept {return contains(records_,key);}
bool OwnedKeys::shell(std::uint32_t key) const noexcept {return contains(shells_,key);}
bool OwnedKeys::room(std::uint32_t record_key) const noexcept {
    bool record_room=record(record_key),shell_room=false;
    for(unsigned i=0;i<capacity;++i){
        record_room=record_room||!records_[i].load(std::memory_order_relaxed);
        shell_room=shell_room||!shells_[i].load(std::memory_order_relaxed);
    }
    // Shell address is unknown until allocation. Conservatively reserve one
    // unused slot even if the allocator might return an old known address.
    return record_key&&record_room&&shell_room;
}
bool OwnedKeys::publish(std::uint32_t record_key,std::uint32_t shell_key) noexcept {
    return append(records_,record_key)&&append(shells_,shell_key);
}
bool Consumer::bind_owner(OwnerDomain domain,ExecutionProbe probe) noexcept {
    if(!probe||!domain.thread||domain.thread==UINT32_MAX||domain.low>=domain.high||
       domain.high-domain.low<sizeof(Frame)+64)return false;
    const auto point=probe();
    if(point.thread!=domain.thread||point.stack<domain.low||point.stack>=domain.high)return false;
    std::uint32_t empty=0;
    if(!owner_thread_.compare_exchange_strong(empty,UINT32_MAX,std::memory_order_acq_rel))return false;
    stack_low_=domain.low;stack_high_=domain.high;execution_=probe;
    owner_thread_.store(domain.thread,std::memory_order_release);return true;
}
bool Consumer::on_owner() const noexcept {
    const auto thread=owner_thread_.load(std::memory_order_acquire);
    if(!thread||thread==UINT32_MAX)return false;
    const auto point=execution_();
    return point.thread==thread&&point.stack>=stack_low_&&point.stack<stack_high_;
}
bool Consumer::frame_on_owner(const Frame& frame) const noexcept {
    if(!on_owner())return false;
    #ifdef _WIN32
    const auto address=reinterpret_cast<std::uintptr_t>(&frame);
    if(address<stack_low_||address>=stack_high_||stack_high_-address<sizeof(Frame))return false;
#endif
    const auto input=frame.input_esp();
#ifdef _WIN32
    if(address+sizeof(Frame)!=input)return false;
#endif
    // All selected argument reads fit [input,input+64). Saved frame below input
    // and every normalized return-guard SP must remain in this same allocation.
    return input>=stack_low_&&input-stack_low_>=sizeof(Frame)&&
        input<=stack_high_&&stack_high_-input>=64;
}
bool Consumer::binding_owner(std::uint32_t address,SessionHandle& session,EngineKey& record) noexcept {
    if(!on_owner())return false;
    const auto* entry=by_record(address);
    if(!entry||!entry->live)return false;
    session=entry->session;record=entry->record;return true;
}
void Consumer::unsupported(SiteId id,Frame& f) noexcept {
    const auto index=unsigned(id);
    if(index>=site_count){
        const auto offset=index-unsigned(SiteId::pump_return);
        f.target=offset==10?0x498dd8u:offset==9?0x498fd2u:offset==1?routes_.manager_drop4:
            (offset==0||(offset>=5&&offset<=8))?0x4984beu:0x498362u;
        if(claimed_.load(std::memory_order_acquire))close_admission();
        return;
    }
    f.target=routes_.unobserved[index];
    const bool claimed=claimed_.load(std::memory_order_acquire);
    if(!claimed)return;
    // No mutable registry, service, engine record or stack argument read below.
    // Constructor ESI/EBX/EDI identity is qualified at the selected CALL seam.
    const bool list=id==SiteId::shutdown||id==SiteId::clear||id==SiteId::retire_record||
        id==SiteId::stop_all||id==SiteId::retire_call;
    const auto record=id==SiteId::pump?f.ecx:
        (id==SiteId::explicit_play||id==SiteId::speech_play||id==SiteId::callback_call||
         id==SiteId::speech_callback||id==SiteId::explicit_callback||id==SiteId::pause_call||id==SiteId::audio_call)?f.esi:
        (id==SiteId::position_call||
         id==SiteId::end_callback||id==SiteId::error_callback)?f.edi:f.eax;
    const bool owned=id==SiteId::construct?(f.ebx==2&&f.edi==8):
        id==SiteId::destroy_shell?keys_.shell(f.eax):keys_.record(record);
    if(!list&&!owned)return; // Never-seen unowned identity: exact unobserved replay.
    close_admission();
    switch(id){
    case SiteId::pump:case SiteId::retire_call:f.target=routes_.manager_drop4;return;
    case SiteId::loop_seek:f.target=routes_.manager_drop8;return;
    case SiteId::explicit_play:case SiteId::explicit_callback:f.target=0x498dd8;return;
    case SiteId::speech_play:f.target=0x498fd2;return;
    case SiteId::speech_callback:f.target=routes_.speech_drop4;return;
    case SiteId::end_callback:f.target=routes_.manager_drop4;return;
    case SiteId::position_call:case SiteId::error_callback:f.target=0x4984be;return;
    case SiteId::stop_all:case SiteId::pause_call:case SiteId::audio_call:case SiteId::callback_call:f.target=0x498362;return;
    case SiteId::rate:f.target=0x498697;f.eax=std::uint32_t(media_playback::rate_failed);return;
    default:f.target=routes_.return_plain;f.eax=0;return;
    }
}
bool Consumer::enable(Readiness r) noexcept {
    if(!on_owner()||closed_.load(std::memory_order_acquire)||!r.complete()||!service_.ready()||!routes_.return_plain||!routes_.destroy_free||
       !routes_.manager_drop4||!routes_.manager_drop8||!routes_.manager_next8||
       !routes_.manager_error8||!routes_.speech_drop4)return false;
    for(auto address:routes_.forward)if(!address)return false;
    for(auto address:routes_.after)if(!address)return false;
    for(auto address:routes_.unobserved)if(!address)return false;
    claimed_.store(true,std::memory_order_release);
    enabled_.store(true,std::memory_order_release);return true;
}
bool Consumer::eligible(std::uint32_t source,std::uint32_t flags) const noexcept {
    return enabled()&&source==2&&flags==8&&on_owner();
}
unsigned Consumer::shells() const noexcept {if(!on_owner())return UINT32_MAX;unsigned n=0;for(const auto& e:entries_)n+=e.allocated;return n;}
Consumer::Identity* Consumer::by_record(std::uint32_t key) noexcept {
    for(auto& e:entries_)if(e.allocated&&e.record.address==key)return &e;
    return nullptr;
}
Consumer::Identity* Consumer::by_shell(std::uint32_t key) noexcept {
    for(auto& e:entries_)if(e.allocated&&e.shell.address==key)return &e;
    return nullptr;
}
bool Consumer::read_word(std::uint32_t at,std::uint32_t& value) noexcept {return memory_.read(at,&value,4);}
bool Consumer::publish_id(Identity& e) noexcept {
    std::uint32_t source=0;
    return e.live&&read_word(e.record.address+0x10,source)&&
        state_.publish_record_id(e.session,e.record,source);
}
void Consumer::retire(Identity& e) noexcept {
    if(!e.live)return;
    e.live=false;
    state_.retire(e.session);service_.cancel(e.session);
}
void Consumer::clear() noexcept {
    state_.clear();
    for(auto& e:entries_)if(e.allocated&&e.live){e.live=false;service_.cancel(e.session);}
}
bool Consumer::current(const PumpRequest& p) const noexcept {
    if(!on_owner())return false;
    if(state_.classify_continuation(p.traversal)!=media_playback::Continuation::live)return false;
    for(const auto& e:entries_)if(e.allocated&&e.live&&e.record==p.record&&e.session==p.session)
        return state_.accepts_publication(p.session,p.operation,p.epoch);
    return false;
}
bool Consumer::check(void* owner,const PumpRequest& p) noexcept {return static_cast<Consumer*>(owner)->current(p);}
void Consumer::construct(Frame& f) noexcept {
    std::uint32_t source=0,flags=0;
    if(!read_word(f.input_esp()+4,source)||!read_word(f.input_esp()+8,flags)||source!=2||flags!=8)return;
    if(!claimed_.load(std::memory_order_acquire))return;
    if(!enabled()){f.target=routes_.return_plain;f.eax=0;return;}
    f.target=routes_.return_plain;f.eax=0; // Eligible attempt never falls back to graph construction.
    if(!service_.ready()||generation_==UINT64_MAX||by_record(f.esi))return;
    Identity* e=nullptr;for(auto& candidate:entries_)if(!candidate.allocated){e=&candidate;break;}
    if(!e)return;
    if(!keys_.room(f.esi)){close_admission();return;} // No allocation/accounting on classifier exhaustion.
    const auto shell=memory_.allocate_shell();if(!shell)return;
    if(!keys_.publish(f.esi,shell)){close_admission();memory_.release_unpublished_shell(shell);return;}
    const EngineKey shell_key{shell,generation_++},record_key{f.esi,shell_key.generation};
    const auto admitted=state_.try_admit(shell_key,source);
    bool reserved=false;
    if(admitted.session)reserved=service_.reserve(admitted.session,source);
    media_playback::Shell32 value{};
    std::uint32_t initial_flags=0,slot=0;
    const bool binding=reserved&&read_word(f.esi+0x2c,initial_flags)&&read_word(f.esi+0x30,slot)&&
        service_.observe_record(admitted.session,{record_key,slot,initial_flags,flags,false});
    const bool initialized=binding&&reserved&&state_.associate_record(admitted.session,record_key)&&
        media_playback::initialize_shell(&value,sizeof value,admitted,flags)&&memory_.write(shell,&value,sizeof value);
    if(!initialized){
        if(admitted.session)state_.retire(admitted.session);
        if(reserved)service_.cancel(admitted.session);
        memory_.release_unpublished_shell(shell);return;
    }
    *e={record_key,shell_key,admitted.session,true,true};
    service_.publish(state_,e->session);f.eax=shell;
}
bool Consumer::guard_before(SiteId id,Frame& f) noexcept {
    unsigned offset=0;while(offset<return_count&&guarded_sites[offset]!=id)++offset;
    if(offset==return_count)return false;
    const bool manager=offset<2||(offset>=5&&offset<=8),speech=offset==9,explicit_play=offset==10;
    const bool return_replace=offset<2||offset==8;
    const bool pending_argument=offset==6||speech;
    const auto abort=explicit_play?0x498dd8u:speech?0x498fd2u:manager?(offset==1?routes_.manager_drop8:
        return_replace?routes_.manager_drop4:0x4984beu):0x498362u;
    const auto stack=f.input_esp()+((return_replace||pending_argument)?4u:0u);
    // Values only: an escaped original exception can abandon a nested call.
    // On the qualified single downward-growing stack, a same/higher new frame
    // proves those deeper scopes no longer exist. No exception is swallowed.
    while(return_depth_&&returns_[return_depth_-1].stack<=stack)--return_depth_;
    if(return_depth_==32||!routes_.after[offset]){
        // The two partial callback spans enter with status already pushed.
        // No callee ran: explicitly remove that DWORD before the stale exit.
        f.target=pending_argument?(speech?routes_.speech_drop4:routes_.manager_drop4):abort;return false;
    }
    auto& guard=returns_[return_depth_];guard={};
    guard.stack=stack;
    guard.after=static_cast<SiteId>(unsigned(SiteId::pump_return)+offset);
    guard.ticket=state_.begin_traversal(manager?media_playback::Traversal::manager:media_playback::Traversal::stop_all);
    const auto record=(offset==4||speech||explicit_play)?f.esi:f.edi;
    if(auto* owned=by_record(record)){
        media::Snapshot snapshot{};
        if(owned->live&&state_.snapshot(owned->session,snapshot)){
            guard.record=owned->record;guard.session=owned->session;
            guard.operation=snapshot.publication.operation;guard.epoch=snapshot.publication.epoch;guard.check_operation=true;
        }
    }
    if(return_replace){
        if(!read_word(f.input_esp(),guard.continuation)||
           !memory_.write(f.input_esp(),&routes_.after[offset],4)){f.target=abort;return false;}
    }else guard.continuation=sites[unsigned(id)].continuation;
    ++return_depth_;return true;
}
void Consumer::guard_after(SiteId id,Frame& f) noexcept {
    const auto offset=unsigned(id)-unsigned(SiteId::pump_return);
    const bool manager=offset<2||(offset>=5&&offset<=8),speech=offset==9,explicit_play=offset==10;
    f.target=explicit_play?0x498dd8u:speech?0x498fd2u:manager?(offset==1?routes_.manager_drop4:0x4984beu):0x498362u;
    // Inner exceptions caught by the original outer callback leave abandoned
    // deeper tokens; drop those, then preserve/match the actual outer scope.
    while(return_depth_&&returns_[return_depth_-1].stack<f.input_esp())--return_depth_;
    if(!return_depth_)return;
    const auto guard=returns_[return_depth_-1];
    if(guard.after!=id||guard.stack!=f.input_esp())return;
    --return_depth_;
    if(state_.classify_continuation(guard.ticket)!=media_playback::Continuation::live)return;
    if(guard.check_operation){
        const auto* owned=by_record(guard.record.address);media::Snapshot snapshot{};
        if(!owned||!owned->live||!(owned->record==guard.record)||!(owned->session==guard.session)||
           !state_.snapshot(owned->session,snapshot)||snapshot.publication.operation!=guard.operation||
           snapshot.publication.epoch!=guard.epoch)return;
    }
    f.target=guard.continuation;
}
void Consumer::dispatch(SiteId id,Frame& f) noexcept {
    if(!frame_on_owner(f)){unsupported(id,f);return;}
    const auto index=unsigned(id);
    if(index>unsigned(SiteId::count)){guard_after(id,f);return;}
    if(index>=site_count)return;
    f.target=routes_.forward[index];
    if(id==SiteId::construct){construct(f);return;}
    if(id==SiteId::pause_call||id==SiteId::audio_call||id==SiteId::callback_call||
       id==SiteId::position_call||id==SiteId::end_callback||id==SiteId::error_callback||
       id==SiteId::retire_call||id==SiteId::speech_callback||id==SiteId::explicit_callback){guard_before(id,f);return;}
    if(id==SiteId::shutdown||id==SiteId::clear){if(id==SiteId::shutdown)disable();clear();return;}
    if(id==SiteId::retire_record){
        // Invalidate on EVERY record, including an unowned cached-next node,
        // before original callbacks/COM. No record memory is needed here.
        auto* e=by_record(f.esi);
        if(e){state_.observe_record_retirement(e->record);if(e->live){e->live=false;service_.cancel(e->session);}}
        else state_.invalidate_traversal();
        return;
    }
    if(id==SiteId::destroy_shell){
        auto* e=by_shell(f.eax);if(!e)return;
        retire(*e);e->allocated=false;
        // The exact original free/accounting tail executes with its two saved
        // registers. There is never owned legacy COM cleanup.
        f.target=routes_.destroy_free;return;
    }
    const auto record=(id==SiteId::pump)?f.ecx:
        ((id==SiteId::explicit_play||id==SiteId::speech_play||id==SiteId::stop_all)?f.esi:f.eax);
    auto* e=by_record(record);
    if(!e){if(id==SiteId::pump||id==SiteId::loop_seek)guard_before(id,f);return;}
    const auto session=e->session;
    if(id==SiteId::explicit_play||id==SiteId::speech_play){
        const bool speech=id==SiteId::speech_play;
        f.target=speech?0x498f08:0x498ce8;
        media_playback::PlayValues32 values{};
        if(!e->live||!publish_id(*e))return;
        media_playback::Request request{};
        std::uint32_t speech_values[3]{},speech_end=0;
        if(speech){
            std::uint32_t callback[2]{},flags=0;
            if(!memory_.read(f.input_esp()+0x18,callback,sizeof callback)||
               !memory_.read(f.input_esp()+0x24,speech_values,sizeof speech_values)||
               !read_word(f.input_esp()+0x10,speech_end)||!read_word(record+0x2c,flags))return;
            std::int32_t start,end,index_value;
            std::memcpy(&start,&f.ebp,4);std::memcpy(&end,&speech_end,4);std::memcpy(&index_value,callback,4);
            request={{2,start,end>0?end:-1,(flags&1)!=0},{callback[1],index_value}};
        }else{
            if(!memory_.read(f.input_esp()+0x18,&values,sizeof values))return;
            request=media_playback::decode_play(values);
            // Original commit ORs loop; a new nonloop request cannot clear an
            // existing engine loop bit. Mirror that same accepted intent.
            std::uint32_t flags=0;if(!read_word(record+0x2c,flags))return;
            request.playback.loop=request.playback.loop||(flags&1);
        }
        auto prepared=state_.prepare_play(session,request);
        if(!prepared)return;
        // No callback, COM, worker wait, or other reentry between prepare and
        // commit. Engine writes use a caller-qualified live shell allocation.
        auto result=state_.commit_play(std::move(prepared));if(!result.accepted)return;
        const std::int32_t bounds[2]={request.playback.start_ms,request.playback.end_ms};
        if(!memory_.write(e->shell.address+0x90,bounds,sizeof bounds)){
            retire(*e);f.target=speech?0x498fd2:0x498d17;return; // broken live-memory contract; no callback raw access
        }
        service_.publish(state_,session);
        if(speech){
            // All three original pre-seek metadata writes occur only after the
            // bounded transaction accepts, preserving the old request on reject.
            if(!memory_.write(record+0x34,speech_values,sizeof speech_values)){
                retire(*e);f.target=0x498fd2;return;
            }
            f.edi=speech_end;f.ebx=0;f.target=0x498f7f;
        }else{f.ebp=0;f.target=0x498d7a;}
        return; // original old-status1 / incoming callback commit
    }
    if(id==SiteId::rate){
        std::uint32_t input=0;f.eax=std::uint32_t(media_playback::rate_failed);f.target=0x498697;
        if(e->live&&read_word(f.input_esp()+4,input)){
            std::int32_t signed_input;std::memcpy(&signed_input,&input,4);
            f.eax=std::uint32_t(state_.set_rate(session,signed_input,service_.clock(session)));
        }
        return;
    }
    if(id==SiteId::stop_all){
        f.target=0x498362;if(!e->live)return;
        state_.stop(session);service_.publish(state_,session);
        f.target=0x498322;return;
    }
    if(id==SiteId::pump){
        const auto ticket=state_.begin_traversal(media_playback::Traversal::manager);
        f.target=routes_.return_plain;f.eax=0;
        if(!e->live){f.target=routes_.manager_drop4;return;}
        media::Snapshot snapshot{};
        if(!publish_id(*e)||!state_.snapshot(session,snapshot))return;
        service_.publish(state_,session);
        const PumpRequest request{session,e->record,snapshot.publication.operation,snapshot.publication.epoch,ticket};
        const auto result=service_.pump(request,&check,this);
        if(result==PumpResult::invalidated||!current(request)){f.target=routes_.manager_drop4;return;}
        // Original AX: zero error/retire, one pending/success, two completion.
        // Service errors retire locally before original status1 path. Completion
        // stays active until the engine chooses loop rearm or terminal stop.
        f.eax=result==PumpResult::complete?2u:result==PumpResult::failed?0u:1u;
        if(result==PumpResult::failed||(result==PumpResult::complete&&!snapshot.request.loop)){
            state_.consume_event(session,request.operation,request.epoch,
                result==PumpResult::failed?media::Event::failed:media::Event::presentation_complete);
            service_.publish(state_,session);
        }
        return;
    }
    f.target=routes_.return_plain;f.eax=0;
    if(!e->live){if(id==SiteId::loop_seek)f.target=routes_.manager_drop8;return;} // no owned COM fallthrough
    if(id==SiteId::position){
        std::uint32_t position=0;
        f.eax=service_.position(session,position)&&memory_.write(f.esi,&position,4);return;
    }
    if(id==SiteId::stop){f.eax=state_.stop(session).accepted;service_.publish(state_,session);return;}
    if(id==SiteId::run){
        std::uint32_t end=0;media::Snapshot snapshot{};
        if(!read_word(e->shell.address+0x94,end)||!state_.snapshot(session,snapshot))return;
        std::int32_t signed_end;std::memcpy(&signed_end,&end,4);
        if(!state_.set_end(session,snapshot.publication.epoch,signed_end))return;
        f.eax=state_.run(session).accepted;service_.publish(state_,session);return;
    }
    if(id==SiteId::seek||id==SiteId::loop_seek){
        std::uint32_t start=0,ret=0;
        const bool loop=id==SiteId::loop_seek;
        if(!read_word(f.input_esp()+4,start)||!read_word(f.input_esp(),ret))return;
        media_playback::SeekCaller caller;media::SeekIntent intent;
        if(!media_playback::decode_seek_caller(ret,caller,intent)){if(loop)f.target=routes_.manager_drop8;return;}
        std::int32_t signed_start;std::memcpy(&signed_start,&start,4);
        media_playback::Transition transition;bool backpressure=false;
        if(loop){
            media::Snapshot live{};if(!state_.snapshot(session,live)||!live.publication.live){f.target=routes_.manager_drop8;return;}
            std::uint32_t end=0;if(!read_word(record+0x20,end)){f.target=routes_.manager_drop8;return;}
            std::int32_t signed_end;std::memcpy(&signed_end,&end,4);
            transition=state_.loop_seek(session,signed_start,signed_end,&backpressure);
        }else transition=state_.seek(session,signed_start,intent);
        f.eax=transition.accepted;
        if(f.eax){
            const std::int32_t bounds[2]={signed_start,-1};
            if(!memory_.write(e->shell.address+0x90,bounds,sizeof bounds)){
                retire(*e);if(loop)f.target=routes_.manager_drop8;return;
            }
        }
        service_.publish(state_,session);
        // The loop caller discards Boolean EAX and never calls Run. A full local
        // command queue must not proceed as if rearm succeeded. Leave old state
        // intact and skip to the validated next node once this pass; stale identity above
        // uses the pointer-free abort instead.
        if(loop&&!f.eax)f.target=backpressure?routes_.manager_next8:routes_.manager_error8;
    }
}
#include "media_engine_stubs_inc.h"
namespace {
bool same(Platform& p,std::uint32_t address,const void* expected,unsigned size) noexcept {
    unsigned char got[2048]{};
    return size<=sizeof got&&p.read(address,got,size)&&!std::memcmp(got,expected,size);
}
bool span(Platform& p,unsigned i) noexcept {
    const auto& s=sites[i];return p.executable(s.address,s.length)&&same(p,s.address,s.bytes,s.length);
}
bool restore_one(Platform& p,Patch& t) noexcept {
    if(!t.may_redirect&&!t.flush_debt&&!t.protection_debt)return true;
    unsigned char current[8]{};
    if(!p.read(t.word_address,current,8))return false;
    const bool ours=!std::memcmp(current,t.replacement,8);
    if(!ours&&std::memcmp(current,t.original,8))return false;
    if(ours){
        std::uint32_t previous=0;
        if(!p.protect(t.word_address,8,media_presentation_gate::read_write_execute,previous))return false;
        t.protection_debt=true;
        if(!p.compare8(t.word_address,t.replacement,t.original))return false;
        t.flush_debt=true;
    }
    const bool readback=same(p,t.word_address,t.original,8);
    if(readback)t.may_redirect=false;
    if(t.flush_debt&&p.flush(t.word_address,8))t.flush_debt=false;
    if(t.protection_debt){std::uint32_t previous=0;
        if(p.protect(t.word_address,8,t.protection,previous))t.protection_debt=false;}
    return readback&&!t.may_redirect&&!t.flush_debt&&!t.protection_debt;
}
}
bool stage(Platform& p,Group& group,std::uint32_t helper) noexcept {
    if(group.ever_published||group.installed||!helper||!p.qualified()||!p.install_window()){
        group.status="stage_refused";return false;
    }
    for(unsigned i=0;i<site_count;++i)if(group.patches[i].code||!span(p,i)){
        group.status="preflight_failed";return false;
    }
    for(unsigned i=0;i<site_count;++i){
        auto& t=group.patches[i];const auto& s=sites[i];
        t.word_address=s.address&~7u;
        if((s.address&7)>3||!p.read(t.word_address,t.original,8)){group.status="word_preflight_failed";return false;}
        t.code=p.reserve();if(!t.code){group.status="reserve_failed";return false;}
        unsigned char bytes[2048]{};
        if(!encode_stub(bytes,sizeof bytes,t.code,i,helper,group.routes,t.code_size)||
           !p.write_reserved(t.code,bytes,t.code_size)||!same(p,t.code,bytes,t.code_size)){
            group.status="emission_write_failed";return false;
        }
        t.emission_flush_debt=!p.flush(t.code,t.code_size);std::uint32_t previous=0;
        t.emission_protection_debt=!p.protect(t.code,t.code_size,media_presentation_gate::read_execute,previous);
        if(t.emission_flush_debt||t.emission_protection_debt||!p.executable(t.code,t.code_size)){
            group.status="emission_seal_failed";return false;
        }
        std::memcpy(t.replacement,t.original,8);const auto offset=s.address&7;
        t.replacement[offset]=s.original_call?0xe8:0xe9;
        const auto relative=t.code-(s.address+5);std::memcpy(t.replacement+offset+1,&relative,4);t.ready=true;
    }
    group.status="staged";return true;
}
bool restore(Platform& p,Group& group,bool quiescent,bool no_owned_shells) noexcept {
    // An installed live group may not be torn down based on admission-off alone.
    if(!quiescent||!no_owned_shells){group.status="restore_busy";return false;}
    bool okay=true;
    for(unsigned n=site_count;n;--n)if(!restore_one(p,group.patches[n-1]))okay=false;
    if(okay)group.installed=false;
    group.status=okay?"restored":"rollback_debt";return okay;
}
bool install(Platform& p,Group& group) noexcept {
    if(group.ever_published||group.installed||!p.qualified()||!p.install_window()){
        group.status="install_refused";return false;
    }
    for(unsigned i=0;i<site_count;++i){const auto& t=group.patches[i];
        if(!t.ready||t.may_redirect||t.flush_debt||t.protection_debt||!span(p,i)||
           !same(p,t.word_address,t.original,8)){
            group.status="install_preflight_failed";return false;
        }
    }
    for(unsigned i=0;i<site_count;++i){auto& t=group.patches[i];
        bool okay=p.protect(t.word_address,8,media_presentation_gate::read_write_execute,t.protection);
        if(okay){
            t.protection_debt=true;t.may_redirect=true;
            const bool swapped=p.compare8(t.word_address,t.original,t.replacement);
            if(!swapped)t.may_redirect=false;
            else{group.ever_published=true;t.flush_debt=true;}
            const bool readback=swapped&&same(p,t.word_address,t.replacement,8);
            if(t.flush_debt&&p.flush(t.word_address,8))t.flush_debt=false;
            std::uint32_t previous=0;
            if(p.protect(t.word_address,8,t.protection,previous))t.protection_debt=false;
            okay=swapped&&readback&&!t.flush_debt&&!t.protection_debt;
        }
        if(!okay){const bool restored=restore(p,group,true,true);
            group.status=restored?"install_rolled_back":"install_rollback_debt";return false;}
    }
    group.installed=true;group.status="installed_admission_off";return true;
}
#ifdef _WIN32
namespace {std::atomic<Consumer*> dispatcher{nullptr};}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_media_engine_dispatch(Frame* frame,unsigned site) noexcept {
    // Stub saves all GPRs/EFLAGS/XMM0..7. This envelope additionally preserves
    // LastError, full x87 state and MXCSR. Helpers compile SSE2, stack4 incoming.
    PreserveCpuState cpu;
    if(auto* consumer=dispatcher.load(std::memory_order_acquire))
        consumer->dispatch(static_cast<SiteId>(site),*frame);
}
ExecutionPoint native_execution_point() noexcept {
    std::uint32_t stack=0;asm volatile("mov %%esp,%0":"=r"(stack));
    return {GetCurrentThreadId(),stack};
}
bool query_native_owner(OwnerDomain& out) noexcept {
    const auto point=native_execution_point();ULONG_PTR low=0,high=0;
    using Limits=void(WINAPI*)(PULONG_PTR,PULONG_PTR);
    const auto kernel=GetModuleHandleW(L"kernel32.dll");
    const auto address=kernel?GetProcAddress(kernel,"GetCurrentThreadStackLimits"):nullptr;
    Limits limits=nullptr;static_assert(sizeof limits==sizeof address);std::memcpy(&limits,&address,sizeof limits);
    if(limits)limits(&low,&high);
    else{
        MEMORY_BASIC_INFORMATION region{};
        if(VirtualQuery(reinterpret_cast<void*>(point.stack),&region,sizeof region)!=sizeof region||
           region.State!=MEM_COMMIT||region.Type!=MEM_PRIVATE||!region.AllocationBase)return false;
        low=reinterpret_cast<ULONG_PTR>(region.AllocationBase);
        // Public VirtualQuery groups pages by state/protection. Walk only this
        // allocation, including its reserved/guard region, at serialized bind.
        ULONG_PTR cursor=low;
        for(unsigned n=0;n<64;++n){
            if(VirtualQuery(reinterpret_cast<void*>(cursor),&region,sizeof region)!=sizeof region)return false;
            if(reinterpret_cast<ULONG_PTR>(region.AllocationBase)!=low){high=cursor;break;}
            if(!region.RegionSize||cursor>UINT32_MAX-region.RegionSize)return false;
            cursor=reinterpret_cast<ULONG_PTR>(region.BaseAddress)+region.RegionSize;
        }
    }
    if(!low||low>=high||high>UINT32_MAX||point.stack<low||point.stack>=high)return false;
    out={point.thread,std::uint32_t(low),std::uint32_t(high)};return true;
}
bool bind_dispatcher(Consumer* value) noexcept {
    if(!value||dispatcher.load(std::memory_order_acquire))return false;
    OwnerDomain domain{};
    if(!query_native_owner(domain)||!value->bind_owner(domain,&native_execution_point))return false;
    Consumer* empty=nullptr;return dispatcher.compare_exchange_strong(empty,value,std::memory_order_release,std::memory_order_relaxed);
}
bool owned_eligible(std::uint32_t source,std::uint32_t flags) noexcept {
    auto* consumer=dispatcher.load(std::memory_order_acquire);
    return consumer&&consumer->eligible(source,flags);
}
std::uint32_t dispatcher_address() noexcept {
    static_assert(sizeof(void*)==4,"x86 engine ABI only");
    return reinterpret_cast<std::uint32_t>(&x3m_media_engine_dispatch);
}
bool NativeMemory::read(std::uint32_t address,void* out,unsigned bytes) noexcept {
    if(!address||!out||address>UINT32_MAX-bytes)return false;
    std::memcpy(out,reinterpret_cast<const void*>(address),bytes);return true;
}
bool NativeMemory::write(std::uint32_t address,const void* input,unsigned bytes) noexcept {
    if(!address||!input||address>UINT32_MAX-bytes)return false;
    std::memcpy(reinterpret_cast<void*>(address),input,bytes);return true;
}
std::uint32_t NativeMemory::allocate_shell() noexcept {
    const auto allocate=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x5112c4);
    void* p=allocate(sizeof(media_playback::Shell32));if(!p)return 0;
    // Original successful allocation0x4cf4d8..4cf4f0. Cumulative counters are
    // never decremented by matching free; only the two live counters are.
    *reinterpret_cast<std::uint32_t*>(0x6085f4)+=0xb4;
    *reinterpret_cast<std::uint32_t*>(0x6085f8)+=0xb4;
    *reinterpret_cast<std::uint32_t*>(0x6089f8)+=0xb4;
    ++*reinterpret_cast<std::uint32_t*>(0x6089fc);
    return reinterpret_cast<std::uint32_t>(p);
}
void NativeMemory::release_unpublished_shell(std::uint32_t shell) noexcept {
    reinterpret_cast<void(__cdecl*)(void*)>(0x50e1b0)(reinterpret_cast<void*>(shell));
    *reinterpret_cast<std::uint32_t*>(0x6085f4)-=0xb4;
    *reinterpret_cast<std::uint32_t*>(0x6085f8)-=0xb4;
}
#endif
}
