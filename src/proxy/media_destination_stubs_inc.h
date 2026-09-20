// Included by the one destination translation unit; the fixture uses this exact encoder.
namespace {
struct Emitter {
    unsigned char* bytes;unsigned capacity,used=0;std::uint32_t base;bool good=true;unsigned field=0;
    void byte(unsigned x) noexcept {if(used<capacity)bytes[used++]=static_cast<unsigned char>(x);else good=false;}
    void word(std::uint32_t x) noexcept {for(unsigned i=0;i<4;++i)byte(x>>(i*8));}
    void rel(unsigned opcode,std::uint32_t target) noexcept {byte(opcode);word(target-(base+used+4));}
    void xmm(bool load) noexcept {for(unsigned i=0;i<8;++i){byte(0x0f);byte(load?0x10:0x11);byte(0x44|(i<<3));byte(0x24);byte(i*16);}}
    unsigned envelope(unsigned id,std::uint32_t helper) noexcept {
        byte(0x8d);byte(0x64);byte(0x24);byte(0xfc);byte(0x9c);byte(0x60);
        byte(0x81);byte(0xec);word(128);xmm(false);byte(0xfc);
        byte(0x68);word(id);byte(0x8d);byte(0x44);byte(0x24);byte(4);byte(0x50);
        byte(0xc7);byte(0x84);byte(0x24);word(172);const auto at=used;word(0);
        rel(0xe8,helper);byte(0x83);byte(0xc4);byte(8);xmm(true);
        byte(0x81);byte(0xc4);word(128);byte(0x61);byte(0x9d);byte(0xc3);return at;
    }
    void patch(unsigned at,std::uint32_t value) noexcept {if(at+4<=capacity)std::memcpy(bytes+at,&value,4);else good=false;}
    void raw(const unsigned char* data,unsigned n) noexcept {for(unsigned i=0;i<n;++i)byte(data[i]);}
};
}
bool encode_stub(unsigned char* out,unsigned capacity,std::uint32_t base,unsigned index,std::uint32_t helper,Routes& routes,unsigned& size) noexcept {
    if(!out||capacity<2048||!base||!helper||index>=site_count||base>UINT32_MAX-capacity)return false;
    Emitter e{out,capacity,0,base,true};const auto& s=sites[index];const auto id=static_cast<SiteId>(index);
    if(id==SiteId::restore_matched_search){
        // Original compare and nonmatch successor execute without an observer.
        e.raw(s.bytes,3);e.byte(0x0f);e.byte(0x85);e.word(s.continuation-(base+e.used+4));
    }
    const auto pre=e.envelope(index,helper);routes.forward[index]=base+e.used;e.patch(pre,routes.forward[index]);
    if(index<lifecycle_count){
        if(id==SiteId::wrapper_cleanup){e.raw(s.bytes,3);e.byte(0x0f);e.byte(0x85);e.word(0x4dcc7a-(base+e.used+4));}
        else {const auto start=e.used;e.raw(s.bytes,s.length);if(s.rel32){std::uint32_t old;std::memcpy(&old,s.bytes+s.rel32,4);e.patch(start+s.rel32,s.address+s.rel32+4+old-(base+start+s.rel32+4));}}
        e.rel(0xe9,s.continuation);
        routes.after[index]=base+e.used;const auto after=e.envelope(2*site_count+index,helper);
        // Only a substituted return reaches this route; Observer supplies the
        // durable original caller. Missing tokens are a permanent fail-stop.
        e.patch(after,base+e.used);e.byte(0x0f);e.byte(0x0b);
    }else{
        if(id==SiteId::restore_matched_search){const unsigned char match[]={0x83,0x48,0x2c,4,0x89,0x50,0x30};e.raw(match,7);}
        else e.raw(s.bytes,id==SiteId::slot_publish_loaded?4:s.length);
        const auto post=e.envelope(site_count+index,helper);e.patch(post,base+e.used);
        if(id==SiteId::slot_publish_loaded)e.rel(0xe8,0x4ee360);
        e.rel(0xe9,id==SiteId::restore_matched_search?0x498be6:s.continuation);
    }
    size=e.used;return e.good;
}

void Observer::enter(unsigned index,Frame& f,std::uint32_t thread) noexcept {
    std::lock_guard<std::mutex> lock(destination_.domain());auto& state=destination_.state_locked();
    if(!destination_.owner(thread)){state.disabled=true;return;}
    // A later same/higher owner stack proves these old callbacks escaped.
    // Keep their mutation tokens vetoed forever; only discard obsolete caller
    // addresses. New unrelated calls forward normally without substitution.
    if(depth_&&returns_[depth_-1].stack<=f.input_esp()+4){
        while(depth_&&returns_[depth_-1].stack<=f.input_esp()+4)--depth_;
        state.disabled=true;destination_.disable();return;
    }
    if(depth_==State::max_depth){state.disabled=true;destination_.disable();return;}
    Return entry{};entry.site=index;entry.stack=f.input_esp()+4;entry.table=state.incarnation;
    if(!read(f.input_esp(),entry.continuation)){state.disabled=true;destination_.disable();return;}
    const auto id=static_cast<SiteId>(index);
    if(id==SiteId::wrapper_release){if(!read(f.input_esp()+4,entry.argument)||!read(entry.argument,entry.key)){state.disabled=true;destination_.disable();return;}entry.lifetime=state.lifetime(entry.key);state.clear_pointer_slot(entry.argument);state.invalidate_wrapper(entry.key,false);}
    if(id==SiteId::wrapper_cleanup||id==SiteId::wrapper_surface_replace){entry.key=f.esi;entry.lifetime=state.lifetime(entry.key);state.invalidate_wrapper(entry.key,true);}
    if(id==SiteId::wrapper_surface_replace&&!read(f.input_esp()+4,entry.argument)){state.disabled=true;destination_.disable();return;}
    if(id==SiteId::slot_name_replace){const auto low=std::uint16_t(f.eax);entry.argument=low;state.clear_slot(low<32768?low:std::int32_t(low)-65536);}
    if(id==SiteId::table_initial_loader||id==SiteId::table_allocate||id==SiteId::table_destroy)state.invalidate_table();
    entry.token=state.begin();
    if(!entry.token.serial||!memory_.write(f.input_esp(),&routes_.after[index],4)){state.disabled=true;destination_.disable();return;}
    returns_[depth_++]=entry;
}
void Observer::leave(unsigned index,Frame& f,std::uint32_t thread) noexcept {
    State::Publication publish{};bool reset=false;bool refresh=false;
    {
        std::lock_guard<std::mutex> lock(destination_.domain());auto& state=destination_.state_locked();
        if(!destination_.same_thread(thread)){state.disabled=true;destination_.disable();return;}
        // Original recovery may abandon an inner observer. A returning outer
        // frame proves which deeper return addresses are obsolete. Its own
        // original caller remains durable; abandoned mutations NEVER revive.
        while(depth_&&returns_[depth_-1].stack<f.input_esp()){
            --depth_;state.disabled=true;destination_.disable();
        }
        if(!depth_||returns_[depth_-1].site!=index||returns_[depth_-1].stack!=f.input_esp()){state.disabled=true;destination_.disable();return;}
        const auto entry=returns_[--depth_];f.target=entry.continuation;
        if(!destination_.owner(thread)){state.disabled=true;return;}
        const auto id=static_cast<SiteId>(index);
        if(id==SiteId::table_allocate||id==SiteId::table_initial_loader||id==SiteId::table_grow){
            std::uint32_t key=0;std::int16_t base=0,dynamic=0;
            if(!read(0x6069ac,key)||!memory_.read(0x6069b0,&base,2)||!memory_.read(0x6069b4,&dynamic,2))state.invalidate_table();
            else if(id==SiteId::table_allocate)state.table(key,base,dynamic,false);
            else if(id==SiteId::table_initial_loader){if(key!=state.table_key||!state.table_live)state.table(key,base,dynamic,false);}
            else if(state.table_live&&state.incarnation==entry.table)state.table(key,base,dynamic,true);
            else state.invalidate_table();
        }
        if(id==SiteId::table_destroy)state.invalidate_table();
        if(id==SiteId::slot_name_replace||id==SiteId::wrapper_release){
            if(state.incarnation!=entry.table){state.disabled=true;destination_.disable();}
            else if(id==SiteId::slot_name_replace)state.clear_slot(entry.argument<32768?entry.argument:std::int32_t(entry.argument)-65536);
            else state.clear_pointer_slot(entry.argument);
        }
        if(id==SiteId::wrapper_cleanup||id==SiteId::wrapper_release||id==SiteId::wrapper_surface_replace){
            const auto current_lifetime=state.lifetime(entry.key);
            // Inner publication can introduce an initially untracked key, or
            // unlink/recreate its sole provenance alias while this physical
            // wrapper is still in original cleanup. A new metadata lifetime
            // therefore does not prove the outer terminal writes missed it.
            // No alias remains when current_lifetime is zero; otherwise this
            // unresolved physical-lifetime change permanently vetoes copying.
            if(current_lifetime&&current_lifetime!=entry.lifetime){state.disabled=true;destination_.disable();}
        }
        if(id==SiteId::wrapper_cleanup&&entry.lifetime&&state.lifetime(entry.key)==entry.lifetime)state.invalidate_wrapper(entry.key,true);
        if(id==SiteId::wrapper_release&&entry.key&&entry.lifetime&&state.lifetime(entry.key)==entry.lifetime&&f.eax==0)state.retire_wrapper(entry.key);
        if(id==SiteId::wrapper_surface_replace&&entry.lifetime&&state.lifetime(entry.key)==entry.lifetime&&f.eax==1)publish=state.publish_surface(entry.key,entry.argument);
        reset=id==SiteId::whole_reset;
        state.end(entry.token);refresh=!state.depth;
    }
    destination_.qualify(publish);
    if(reset)destination_.end_engine_reset(thread,f.eax==1);
    else if(refresh)destination_.recover_watches();
}
void Observer::publication(unsigned index,Frame& f,bool after,std::uint32_t thread) noexcept {
    const auto id=static_cast<SiteId>(index);
    if(!after){
        destination_.domain().lock();
        if(!destination_.owner(thread)){destination_.state_locked().disabled=true;return;}
        if(id==SiteId::reset_root_surface_publications){first_wrapper_=f.ecx;first_surface_=f.edx;}
        return;
    }
    auto& state=destination_.state_locked();State::Publication a{},b{};
    if(destination_.owner(thread)&&!state.disabled){
        if(id==SiteId::slot_publish_new||id==SiteId::slot_publish_loaded){
            std::uint32_t surface=0;
            // Factory-owned live wrapper is read ONLY while the store/read domain
            // is held, before the displaced foreign call on the loaded route.
            if(!f.eax||read(f.eax+0x30,surface))a=state.publish_slot(f.ecx,id==SiteId::slot_publish_new?f.edx:f.ebx,f.eax,surface);
            else state.disabled=true;
        }else if(id==SiteId::reset_root_surface_publications){a=state.publish_surface(first_wrapper_,first_surface_);b=state.publish_surface(f.eax,f.ecx);}
        else if(binding_.find){
            BindingOwner owner{};
            if(binding_.find(binding_.consumer,f.eax,owner.session,owner.record)){
                const auto slot=id==SiteId::record_bind_matched?f.ecx:f.edx;
                state.watch(owner,slot,id!=SiteId::record_unbind_matched);
            }
        }
    }
    destination_.domain().unlock();destination_.qualify(a);destination_.qualify(b);
    if(id==SiteId::record_bind_matched||id==SiteId::record_inline_bind||id==SiteId::restore_matched_search)destination_.recover_watches();
}
void Observer::dispatch(unsigned event,Frame& f,std::uint32_t thread) noexcept {
    if(event>=2*site_count){const auto index=event-2*site_count;if(index<lifecycle_count)leave(index,f,thread);return;}
    if(event>=site_count){publication(event-site_count,f,true,thread);return;}
    if(event>=site_count)return;
    // Emitted replay is already the default. Lifecycle observers only substitute
    // original return words; value publications bracket CPU-only replay.
    if(event<lifecycle_count){
        if(event==unsigned(SiteId::whole_reset))destination_.begin_engine_reset(thread);
        enter(event,f,thread);
    }else publication(event,f,false,thread);
}
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
        t.replacement[offset]=0xe9;
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
