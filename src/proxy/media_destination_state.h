#pragma once
#include "media_playback.h"
#include "../ownership/surface_lease_core.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>

namespace x3m::media_destination {
using media_playback::EngineKey;
struct BindingOwner { media::SessionHandle session{}; EngineKey record{}; };
inline bool operator==(const BindingOwner& a,const BindingOwner& b) noexcept {return a.session==b.session&&a.record==b.record;}
// All methods run under the destination domain. This is the production POD
// index, also compiled by the host fixture. It never dereferences a value key.
class State {
public:
    static constexpr std::uint32_t none=UINT32_MAX,max_slots=32768,max_depth=64;
    struct Slot {std::uint64_t serial=0;std::uint32_t wrapper=none,next=none,previous=none,state=0;};
    struct Wrapper {
        std::uint64_t lifetime=0,publication=0;
        ownership::SurfaceLeaseIdentity identity{};
        std::uint32_t key=0,device=0,surface=0,head=none,refs=0,state=0;
    };
    struct Index {std::uint32_t key=0,wrapper=none;};
    static_assert(sizeof(Slot)==24&&sizeof(Wrapper)==64&&sizeof(Index)==8);
    struct Token {std::uint64_t serial=0;};
    struct Publication {std::uint64_t table=0,lifetime=0,publication=0;std::uint32_t wrapper=none,device=0,surface=0;ownership::SurfaceLeaseIdentity previous{};};
    struct Watch {BindingOwner owner{};std::uint64_t epoch=0;std::int32_t slot=-1;bool used=false,bound=false;};
    struct Snapshot {BindingOwner owner{};std::uint64_t binding=0,table=0,slot_serial=0,lifetime=0,publication=0;std::uint32_t slot=none,wrapper=none,device=0,surface=0;ownership::SurfaceLeaseIdentity identity{};};
    bool disabled=false,table_live=false,recovery_ready=true;
    std::uint64_t table_serial=0,incarnation=0;
    std::uint32_t table_key=0,device_key=0,capacity=0,bound=0,tracked=0,depth=0;
    Watch watches[media::Runtime::max_sessions]{};
    static bool bounds(std::int16_t base,std::int16_t dynamic,std::uint32_t& count,std::uint32_t& cap) noexcept {
        if(base<0||dynamic<0)return false;
        count=std::uint32_t(base)+std::uint32_t(dynamic);
        cap=std::uint32_t(base)+1000*(std::uint32_t(dynamic)/1000+1);
        return cap<=65767&&count<=cap;
    }
    std::uint64_t next() noexcept {if(serial_==UINT64_MAX){disabled=true;return 0;}return ++serial_;}
    Token begin() noexcept {
        if(disabled||depth==max_depth){disabled=true;return {};}
        const auto value=next();if(!value)return {};tokens_[depth++]=value;return {value};
    }
    bool end(Token token) noexcept {
        if(!token.serial||!depth||tokens_[depth-1]!=token.serial){disabled=true;return false;}
        --depth;return !disabled;
    }
    void set_device(std::uint32_t key) noexcept {
        // Called for an actual successful CreateDevice publication, not a
        // per-frame key refresh. Address equality is not allocation identity.
        device_key=key;recovery_ready=true;
        // Cold device replacement; old values cannot qualify against a new device.
        for(std::uint32_t i=0;i<tracked;++i)if(wrappers_[i].refs&&!(wrappers_[i].state&retired)){
            wrappers_[i].state=0;wrappers_[i].publication=next();
        }
    }
    bool table(std::uint32_t key,std::int16_t base,std::int16_t dynamic,bool preserve) noexcept {
        std::uint32_t count=0,cap=0;
        if(!key||!bounds(base,dynamic,count,cap)||key>UINT32_MAX-cap*16){invalidate_table();return false;}
        const auto n=std::min(cap,max_slots);
        if(preserve&&(!table_live||count<bound||cap<capacity)){invalidate_table();return false;}
        if(!preserve||n!=tracked){
            auto slots=std::unique_ptr<Slot[]>(new(std::nothrow) Slot[n]);
            auto wrappers=std::unique_ptr<Wrapper[]>(new(std::nothrow) Wrapper[n]);
            auto index=std::unique_ptr<Index[]>(new(std::nothrow) Index[2*n]);
            if(!slots||!wrappers||!index){invalidate_table();return false;}
            if(preserve){std::copy_n(slots_.get(),tracked,slots.get());std::copy_n(wrappers_.get(),tracked,wrappers.get());}
            const auto old=preserve?tracked:0;
            slots_=std::move(slots);wrappers_=std::move(wrappers);index_=std::move(index);tracked=n;
            free_=none;
            for(std::uint32_t i=n;i-->0;)if(i>=old||!wrappers_[i].refs){wrappers_[i]={};wrappers_[i].head=free_;free_=i;}
            for(std::uint32_t i=0;i<n;++i)if(wrappers_[i].refs&&!(wrappers_[i].state&retired))insert(wrappers_[i].key,i);
        }
        table_key=key;capacity=cap;bound=count;table_live=true;table_serial=next();if(!preserve)incarnation=next();return !disabled;
    }
    void invalidate_table() noexcept {table_live=false;table_serial=next();}
    bool watch(BindingOwner owner,std::uint32_t slot,bool bound_value) noexcept {
        if(!owner.session||!owner.record.address||!owner.record.generation)return false;
        Watch* w=nullptr;for(auto& item:watches)if(item.used&&item.owner==owner){w=&item;break;}
        if(!w)for(auto& item:watches)if(!item.used){w=&item;break;}
        if(!w)return false;
        // Engine explicitly truncates to a signed WORD.
        const auto low=std::uint16_t(slot);const auto selected=low<32768?std::int32_t(low):std::int32_t(low)-65536;
        *w={owner,next(),selected,true,bound_value};return !disabled;
    }
    void cancel(media::SessionHandle session) noexcept {for(auto& w:watches)if(w.used&&w.owner.session==session){w={};next();}}
    void clear_watches() noexcept {for(auto& w:watches)w={};next();}
    void clear_slot(std::int32_t i) noexcept {if(i>=0&&std::uint32_t(i)<tracked){unlink(std::uint32_t(i));slots_[i].serial=next();}}
    Publication publish_slot(std::uint32_t table,std::uint32_t offset,std::uint32_t key,std::uint32_t surface) noexcept {
        if(disabled||!table_live||table!=table_key||(offset&15)||offset/16>=bound||offset/16>=tracked)return {};
        const auto i=offset/16;clear_slot(std::int32_t(i));if(!key)return {};
        auto wi=find(key);
        if(wi==none){if(free_==none){disabled=true;return {};}
            wi=free_;free_=wrappers_[wi].head;wrappers_[wi]={};auto& w=wrappers_[wi];w.key=key;w.lifetime=next();insert(key,wi);
        }
        auto& w=wrappers_[wi];auto& s=slots_[i];s.wrapper=wi;s.next=w.head;s.previous=none;s.state=1;
        if(w.head!=none)slots_[w.head].previous=i;
        w.head=i;++w.refs;
        // Every producer observation qualifies an actual surface value, including aliases.
        return publish_surface_index(wi,surface);
    }
    Publication publish_surface(std::uint32_t key,std::uint32_t surface) noexcept {
        const auto i=find(key);return i==none?Publication{}:publish_surface_index(i,surface);
    }
    Publication candidate(std::uint32_t key) const noexcept {
        const auto i=find(key);if(i==none)return {};const auto& w=wrappers_[i];if(w.state!=known&&w.state!=pending)return {};
        return {table_serial,w.lifetime,w.publication,i,w.device,w.surface,w.identity};
    }
    bool same(const Publication& p) const noexcept {
        return p.wrapper<tracked&&table_live&&p.table==table_serial&&wrappers_[p.wrapper].lifetime==p.lifetime&&
            wrappers_[p.wrapper].publication==p.publication&&!(wrappers_[p.wrapper].state&retired);
    }
    void refuse(const Snapshot& p) noexcept {
        if(p.wrapper<tracked&&p.table==table_serial&&wrappers_[p.wrapper].lifetime==p.lifetime&&wrappers_[p.wrapper].publication==p.publication)wrappers_[p.wrapper].state=0;
    }
    void refuse(const Publication& p) noexcept {if(same(p))wrappers_[p.wrapper].state=0;}
    void qualify(const Publication& p,const ownership::SurfaceLeaseIdentity& identity,bool recovery=false) noexcept {
        if(!same(p)||!identity.valid())return;
        if(recovery&&p.previous.valid()&&(identity.device_serial!=p.previous.device_serial||identity.surface_serial!=p.previous.surface_serial)){refuse(p);return;}
        auto& w=wrappers_[p.wrapper];w.identity=identity;w.state=known;
    }
    std::uint64_t invalidate_wrapper(std::uint32_t key,bool clear_identity) noexcept {
        const auto i=find(key);if(i==none)return 0;auto& w=wrappers_[i];w.publication=next();
        if(clear_identity){w.state=0;w.identity={};}return w.publication;
    }
    std::uint64_t lifetime(std::uint32_t key) const noexcept {const auto i=find(key);return i==none?0:wrappers_[i].lifetime;}
    void retire_wrapper(std::uint32_t key) noexcept {
        const auto i=find(key);if(i==none)return;erase(key);auto& w=wrappers_[i];w.state=retired;w.publication=next();w.identity={};
    }
    // Exact caller slot may clear while surviving aliases remain qualified.
    void clear_pointer_slot(std::uint32_t address) noexcept {
        if(address>=table_key+8){const auto delta=address-table_key-8;if(!(delta&15)&&delta/16<tracked)clear_slot(std::int32_t(delta/16));}
    }
    bool snapshot(BindingOwner owner,Snapshot& out) const noexcept {
        if(disabled||depth||!table_live||!device_key||!recovery_ready)return false;
        for(const auto& watch:watches)if(watch.used&&watch.bound&&watch.owner==owner&&watch.slot>=0&&std::uint32_t(watch.slot)<bound&&std::uint32_t(watch.slot)<tracked){
            const auto& s=slots_[watch.slot];if(s.wrapper==none)return false;const auto& w=wrappers_[s.wrapper];
            if(w.state!=known||w.device!=device_key||!w.identity.valid())return false;
            out={owner,watch.epoch,table_serial,s.serial,w.lifetime,w.publication,std::uint32_t(watch.slot),s.wrapper,w.device,w.surface,w.identity};return true;
        }
        return false;
    }
    bool current(const Snapshot& expected) const noexcept {
        Snapshot s;return snapshot(expected.owner,s)&&s.binding==expected.binding&&s.table==expected.table&&s.slot_serial==expected.slot_serial&&
            s.lifetime==expected.lifetime&&s.publication==expected.publication&&ownership::detail::same_surface_identity(s.identity,expected.identity);
    }
    Publication watched_candidate(unsigned i) const noexcept {
        if(i>=media::Runtime::max_sessions)return {};
        const auto& w=watches[i];
        if(!w.used||!w.bound||w.slot<0||std::uint32_t(w.slot)>=tracked)return {};
        const auto wi=slots_[w.slot].wrapper;if(wi==none||wrappers_[wi].state==retired)return {};
        return candidate(wrappers_[wi].key);
    }
private:
    static constexpr unsigned known=1,pending=2,retired=4;
    std::unique_ptr<Slot[]> slots_;std::unique_ptr<Wrapper[]> wrappers_;std::unique_ptr<Index[]> index_;
    std::uint64_t serial_=0,tokens_[max_depth]{};std::uint32_t free_=none;
    std::uint32_t hash(std::uint32_t key) const noexcept {return (key*2654435761u)%(2*tracked);}
    std::uint32_t find(std::uint32_t key) const noexcept {
        if(!key||!tracked)return none;
        auto at=hash(key);
        for(std::uint32_t n=0;n<2*tracked;++n){const auto& e=index_[at];if(!e.key)return none;if(e.key==key)return e.wrapper;at=(at+1)%(2*tracked);}return none;
    }
    void insert(std::uint32_t key,std::uint32_t wrapper) noexcept {auto at=hash(key);while(index_[at].key)at=(at+1)%(2*tracked);index_[at]={key,wrapper};}
    void erase(std::uint32_t key) noexcept {
        auto hole=hash(key);while(index_[hole].key&&index_[hole].key!=key)hole=(hole+1)%(2*tracked);
        if(!index_[hole].key)return;
        auto at=(hole+1)%(2*tracked);
        while(index_[at].key){const auto home=hash(index_[at].key),n=2*tracked;
            if((at+n-home)%n>=(at+n-hole)%n){index_[hole]=index_[at];hole=at;}at=(at+1)%(2*tracked);
        }index_[hole]={};
    }
    void unlink(std::uint32_t i) noexcept {
        auto& s=slots_[i];if(s.wrapper==none)return;const auto wi=s.wrapper;auto& w=wrappers_[wi];
        if(s.previous==none)w.head=s.next;else slots_[s.previous].next=s.next;
        if(s.next!=none)slots_[s.next].previous=s.previous;
        if(!--w.refs){if(!(w.state&retired))erase(w.key);w={};w.head=free_;free_=wi;}s={};
    }
    Publication publish_surface_index(std::uint32_t i,std::uint32_t surface) noexcept {
        auto& w=wrappers_[i];w.publication=next();w.device=device_key;w.surface=surface;w.identity={};w.state=surface?pending:0;
        return {table_serial,w.lifetime,w.publication,i,w.device,w.surface,{}};
    }
};
}
