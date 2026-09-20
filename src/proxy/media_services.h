#pragma once
#include "media_engine_adapter.h"
#include "media_destination.h"
#include "media_startup.h"
#include "lav_worker.h"
#include "owned_clock/clock.h"
#include "../media/package_config.h"
#include <array>
#include <atomic>
#include <memory>

namespace x3m::media_services {
// One main owner after the bootstrap handoff. The production implementation is
// a direct forwarding pair of LavWorkers; tests inject transport events only.
// No observer with engine access is allowed on any of these calls or leases.
struct Workers {
    virtual ~Workers()=default;
    virtual bool start(unsigned,media::WorkerConfig)=0; // bootstrap only
    virtual bool submit(unsigned,const media::Command&) noexcept=0;
    virtual void desired(unsigned,const media::Publication&) noexcept=0;
    virtual media::FrameLease acquire(unsigned) noexcept=0;
    virtual bool event(unsigned,media::WorkerEvent&) noexcept=0;
    virtual media::WorkerPoll state(unsigned) const noexcept=0;
    // Worker-side acknowledgement of consumed cancellation, drained command/
    // event transport, no graph and all physical slots safe. Never a timeout.
    virtual bool quiescent(unsigned,const media::Publication&) noexcept=0;
    virtual void shutdown(unsigned) noexcept=0;
};
struct Counter {
    std::uint64_t frequency=0;
    void* context=nullptr;
    std::uint64_t (*read)(void*) noexcept=nullptr;
};
struct Diagnostics {
    bool preparation_published=false,initialized=false,admission=false;
    unsigned assigned=0,draining=0,quarantined=0,leases=0,failed_mask=0;
    std::uint64_t presented[2]{},binding[2]{},clock_generation[2]{},revision[2]{},rate_numerator[2]{};
};
class MediaServices final:public media_engine::Services {
public:
    // All referenced objects and this object are process-lifetime. Counter and
    // test CopyBackend are scalar/non-reentrant except the explicit copy seam.
    MediaServices(media_playback::Adapter&,media_destination::Destination&,
                  media_startup::Controller&,Counter,
                  media_destination::CopyBackend* test_copy=nullptr) noexcept;
    // Bootstrap-only ownership transfer. Exactly two workers; immutable package
    // owner is copied into BOTH WorkerConfigs before either can start.
    bool prepare(std::shared_ptr<const media::PackageConfig>,std::unique_ptr<Workers>) noexcept;
#ifdef _WIN32
    bool prepare_on_bootstrap(const media_startup::BootstrapContext&) noexcept;
    static bool bootstrap(void*,const media_startup::BootstrapContext&) noexcept;
#endif
    // Root invokes on its already qualified regular owner boundary, even with
    // no records. No patch installation, consumer enable, wait or engine callback.
    void maintenance_on_owner(media_engine::Readiness) noexcept;
    void close_admission_and_cancel_on_owner() noexcept;
    bool ready() const noexcept override;
    bool reserve(media::SessionHandle,media::SourceKey) noexcept override;
    bool observe_record(media::SessionHandle,const media_engine::BindingObservation&) noexcept override;
    void cancel(media::SessionHandle) noexcept override;
    void publish(media_playback::Adapter&,media::SessionHandle) noexcept override;
    media_engine::PumpResult pump(const media_engine::PumpRequest&,CurrentCheck,void*) noexcept override;
    bool position(media::SessionHandle,std::uint32_t&) noexcept override;
    media_playback::ClockTransaction clock(media::SessionHandle) noexcept override;
    Diagnostics diagnostics() const noexcept;
private:
    struct Preparation {std::shared_ptr<const media::PackageConfig> package;std::unique_ptr<Workers> workers;};
    struct Slot {
        explicit Slot(std::uint64_t frequency):clock(frequency){}
        media::SessionHandle session{};media::SourceKey source=0;
        std::uint64_t serial=0,revision=0;
        media::Snapshot logical{};bool have_logical=false,draining=false,quarantine=false,position_from_bounds=true;
        bool initialized=false,failed=false,eof_seen=false,eof_admitted=false;
        unsigned pump_depth=0,in_copy=0;
        media_owned::Clock clock;
        media::FrameIdentity mapped{};media::Publication cancelled{};
        media::FrameLease frames[3];unsigned fifo[3]{},queued=0;int pending=-1;
        unsigned terminal_flags=0,terminal_revision=0,graph=0;
        std::uint64_t presented=0,binding=0;
        MediaServices* owner=nullptr;media::SessionHandle rate_session{};std::uint64_t rate_serial=0;
    };
    struct CopyTicket {
        MediaServices* owner;Slot* slot;media_engine::PumpRequest request;
        CurrentCheck consumer;void* context;std::uint64_t serial,revision;
    };
    static media_destination::Current current(void*,const media_destination::CopyRequest&) noexcept;
    static bool rate(void*,std::int32_t,double) noexcept;
    Slot* find(media::SessionHandle) noexcept;
    unsigned index(const Slot& s) const noexcept {return &s==&slots_[0]?0:1;}
    std::uint64_t now() const noexcept {return counter_.read?counter_.read(counter_.context):0;}
    bool revise(Slot&) noexcept;
    bool synchronize(Slot&,const media::Snapshot&,std::uint64_t) noexcept;
    void revoke(Slot&,media::LeaseRelease=media::LeaseRelease::revoked) noexcept;
    void offer_one(Slot&) noexcept;
    void poll_events(Slot&) noexcept;
    void ingest(Slot&) noexcept;
    void retire_progress(Slot&) noexcept;
    unsigned held(const Slot&) const noexcept;
    bool eligible(const Slot&,const media::FrameLease&) const noexcept;
    media_engine::PumpResult schedule(Slot&,std::uint64_t) noexcept;
    media_playback::Adapter& adapter_;media_destination::Destination& destination_;
    media_startup::Controller& startup_;Counter counter_;
    media_destination::CopyBackend* test_copy_;
    Slot slots_[2];
    std::atomic<unsigned> preparation_state_{0}; // 0 empty,1 preparing,2 published,3 failed
    std::unique_ptr<Preparation> preparation_;Preparation* adopted_=nullptr;
    bool initialized_=false,closed_=false,readiness_signaled_=false;
    media_engine::Readiness readiness_{};
};
}
