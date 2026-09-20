#pragma once
#include <atomic>
#include <cstdint>

namespace x3m::media_startup {
// Actual export-entry span: return, SDK, saved EDI/ESI/EBX, ancestor return.
// No borrowed stack pointer is retained past enter(). The ABI wrapper records
// original ESP before any C++ prologue and safe-reads exactly these 24 bytes.
struct Entry {
    std::uint32_t words[6]{};
    bool owns_entry=false,matched=false;
};
static_assert(sizeof(Entry)<=32,"assembly entry storage bound");
struct BootstrapContext {
    void* pinned_proxy=nullptr;
    std::uint64_t requested_qpc=0;
};
// Runs ONLY on the retained-module bootstrap thread. It may read package
// configuration and start the existing canonical workers, never duplicate them.
// true means preparation accepted, NOT that workers are ready. Keep immutable
// package pins/context alive in the canonical runtime through all worker use.
using Bootstrap=bool(*)(void*,const BootstrapContext&) noexcept;
// Synchronous, CPU-only installation of inert observers in the qualified
// ordinary return context. No engine/COM calls, package I/O or worker waits.
// Owns rollback/debt on failure; success never enables playback by itself.
// Before publishing any hook, independently retain its module and use a
// process-lifetime context. Those owners must survive later invalidation or
// launch failure: the asynchronous bootstrap reference may never be acquired.
using InstallHooks=bool(*)(void*) noexcept;
enum class Status : std::uint32_t {
    idle,closed,reentered,failed_factory,unconfigured,identity_refused,
    launch_failed,scheduled,preparing,prepared,ready,callback_failed,install_failed
};
struct Snapshot {
    Status status=Status::idle;
    bool closed=false,request_claimed=false,entry_active=false;
    std::uint64_t requested_qpc=0,bootstrap_begin_qpc=0,bootstrap_end_qpc=0,
        ready_qpc=0,first_device_attempt_qpc=0;
};
class Controller;
struct Platform {
    virtual ~Platform()=default;
    virtual bool read_entry(std::uintptr_t address,std::uint32_t (&words)[6]) noexcept=0;
    virtual bool qualified() noexcept=0;
    virtual std::uint64_t now() noexcept=0;
    // Acquire module reference BEFORE entrypoint can execute. Exactly one
    // asynchronous bootstrap; no worker wait, DD/COM/HWND or package I/O here.
    // false guarantees no entrypoint started and any module reference released.
    virtual bool launch(Controller&) noexcept=0;
};
class Controller {
public:
    explicit Controller(Platform& platform):platform_(platform){}
    Controller(const Controller&)=delete;
    Controller& operator=(const Controller&)=delete;
    // One registration before the ordinary factory finishes (e.g. its existing
    // backend initialization). Default is unconfigured/off; no env/hidden start.
    // Callback code/context must remain live; production callback lives in proxy.
    bool configure(Bootstrap,void* context,InstallHooks=nullptr) noexcept;
    void enter(Entry&,std::uintptr_t actual_entry_esp) noexcept;
    void leave(Entry&,std::uintptr_t factory_result) noexcept;
    void device_creation_attempt() noexcept;
    void bootstrap(void* pinned_proxy) noexcept;
    bool service_ready() noexcept; // canonical service signals actual readiness
    Snapshot snapshot() const noexcept;
private:
    enum : std::uint32_t { open,closed,claimed,blocked };
    void block(Status) noexcept;
    Platform& platform_;
    std::atomic<std::uint32_t> gate_{open},configuration_{0};
    std::atomic<bool> active_{false},device_seen_{false};
    std::atomic<Status> status_{Status::idle};
    Bootstrap callback_=nullptr;InstallHooks install_=nullptr;void* context_=nullptr;
    std::atomic<std::uint64_t> requested_{0},begin_{0},end_{0},ready_{0},device_{0};
};
// Production singleton; not available in portable host-only builds.
Controller& process() noexcept;
bool configure(Bootstrap,void* context,InstallHooks=nullptr) noexcept;
bool service_ready() noexcept;
Snapshot snapshot() noexcept;
}
extern "C" void x3m_media_startup_device_attempt() noexcept;
