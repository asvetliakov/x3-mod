#pragma once
#include "media_services.h"

namespace x3m::media_root {
// Mutated only while capture's existing recursive mutex is held. Queries are
// lock-free and conservatively reject another thread's held capture scope too.
class CaptureExclusion {
public:
    void enter() noexcept {if(!depth_++)held_.store(true,std::memory_order_release);}
    void leave() noexcept {if(!--depth_)held_.store(false,std::memory_order_release);}
    bool clear() const noexcept {return !held_.load(std::memory_order_acquire);}
private:
    unsigned depth_=0;std::atomic<bool> held_{false};
};
// The only shared record-memory domain. The immutable stack bounds are captured
// on the qualified startup thread; a veto never authorizes arbitrary stack I/O.
class Ingress final:public media_engine::RecordIngress,public media_engine::Memory {
public:
    Ingress(media_destination::Destination& d,media_engine::Memory& m,
            media_engine::OwnerDomain owner,media_engine::ExecutionProbe probe) noexcept
        :destination_(d),memory_(m),owner_(owner),probe_(probe){}
    bool healthy() const noexcept override {return !veto_.load(std::memory_order_acquire);}
    void retiring(media_playback::EngineKey) noexcept override;
    void retiring_address(std::uint32_t) noexcept override;
    void clearing() noexcept override;
    void foreign_refusal() noexcept override;
    bool read(std::uint32_t,void*,unsigned) noexcept override;
    bool write(std::uint32_t,const void*,unsigned) noexcept override;
    std::uint32_t allocate_shell() noexcept override;
    void release_unpublished_shell(std::uint32_t) noexcept override;
    bool owner() const noexcept;
private:
    bool stack(std::uint32_t,unsigned) const noexcept;
    media_destination::Destination& destination_;media_engine::Memory& memory_;
    const media_engine::OwnerDomain owner_;const media_engine::ExecutionProbe probe_;
    std::atomic<bool> veto_{false};
};
enum class Composition {unavailable,disabled_pristine,installed_qualified};
enum class ReportState {absent,available,wrong_owner,busy};
struct Report {
    ReportState state=ReportState::absent;
    media_startup::Snapshot startup{};media_services::Diagnostics services{};
    std::uint32_t owner=0,device=0;
    bool installed=false,debt=false,veto=false,consumer_admission=false,copy_admission=false,capacity_closed=false;
};
class Root;
// Startup-only bindings are reported individually. No hook can acquire a Root
// from an arbitrary first backend caller; Root construction requires its owner.
struct Bindings {bool consumer=false,observer=false,reset=false;};
struct Hooks {
    virtual ~Hooks()=default;
    virtual bool retain() noexcept=0;
    virtual Bindings bind(Root&) noexcept=0;
    virtual Composition cue() noexcept=0;
    virtual bool compose(bool enable) noexcept=0;
    virtual bool composed() const noexcept=0;
    virtual std::uint32_t consumer_dispatch() noexcept=0;
    virtual std::uint32_t destination_dispatch() noexcept=0;
};
class Root {
public:
    Root(media_presentation_gate::Platform&,Hooks&,media_destination::IdentitySource&,
         media_engine::Memory&,media_destination::Memory&,media_startup::Controller&,
         media_engine::OwnerDomain,media_engine::ExecutionProbe,media_services::Counter,
         media_destination::CopyBackend* test_copy=nullptr) noexcept;
    bool install() noexcept;
    bool rollback() noexcept; // original startup window only; attempts every group
    void device_published(std::uint32_t application,bool canonical) noexcept;
    // Only atomic stores: safe while an outer capture scope forbids domain work.
    void publication_blocked() noexcept {consumer.disable();destination.disable();closing_.store(true,std::memory_order_release);}
    void present(std::uint32_t application) noexcept;
    media_engine::Readiness readiness() noexcept;
    Report report() const noexcept; // fixed scalar reads; no domain/registry/COM or worker calls
    bool installed() const noexcept {return installed_;}
    bool debt() const noexcept;
    media_playback::Adapter adapter;
    media_presentation_gate::Admission admission;
    media_presentation_gate::Transaction gate;
    media_destination::Group destinations;
    media_engine::Group consumers;
    media_destination::Destination destination;
    Ingress ingress;
    media_services::MediaServices services;
    media_engine::Consumer consumer;
    media_destination::Observer observer;
private:
    static bool binding(void*,std::uint32_t,media::SessionHandle&,media_playback::EngineKey&) noexcept;
    media_presentation_gate::Platform& platform_;Hooks& hooks_;
    const media_engine::OwnerDomain owner_;
    Bindings bindings_{};Composition composition_=Composition::unavailable;
    media_engine::Readiness installation_{};
    void record_installation() noexcept;
    bool pinned_=false,ingress_bound_=false,installed_=false,attempted_=false;
    bool admission_started_=false,maintaining_=false,failed_=false;
    std::uint32_t device_=0;
    std::atomic<bool> closing_{false};
};
#ifdef _WIN32
// Registration stores callbacks only. Context construction happens in qualified
// Controller::leave, before the initial engine table loader and any device.
bool configure() noexcept;
void on_device_created(void* application,bool capture_clear) noexcept;
void on_present_owner(void* application) noexcept;
Report report_on_owner() noexcept;
#endif
}
