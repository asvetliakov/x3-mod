// Host-only execution of the capture compositor lifetime and Reset control flow.
// The Python test writes the selected production function bodies into the
// generated include below. This file supplies scripted COM/component doubles;
// it does not qualify Windows ABI/SEH behavior or GPU state restoration.
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "../../src/proxy/comparison_controls.h"

#define WINAPI
using DWORD = std::uint32_t;
using ULONG = std::uint32_t;
using UINT = std::uint32_t;
using HRESULT = std::int32_t;
using HWND = void*;
constexpr HRESULT S_OK = 0;
constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
constexpr HRESULT D3DERR_INVALIDCALL = static_cast<HRESULT>(0x8876086cu);
constexpr HRESULT D3DERR_NOTFOUND = static_cast<HRESULT>(0x88760866u);
#define SUCCEEDED(value) ((value) >= 0)
#define FAILED(value) ((value) < 0)

struct D3DCAPS9 {};
struct D3DSURFACE_DESC { UINT Width = 1, Height = 1; };
struct D3DPRESENT_PARAMETERS { HWND hDeviceWindow = nullptr; };
struct D3DDISPLAYMODEEX {};
struct IDirect3DDevice9;

struct IDirect3DSurface9 {
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
    virtual HRESULT GetDesc(D3DSURFACE_DESC* value) {
        if (value) *value = {};
        return S_OK;
    }
protected:
    ~IDirect3DSurface9() = default;
};

struct NativeDevice {
    std::atomic<ULONG> refs{1};
    std::atomic<unsigned> addref_calls{0}, release_calls{0}, destroyed{0};
    std::atomic<unsigned> reset_calls{0}, reset_ex_calls{0};
    HRESULT reset_result = S_OK, reset_ex_result = S_OK;
    bool reset_observed_revoked = false;
    bool reset_observed_defaults_dropped = false;
    bool reset_observed_pin_alive = false;
};
struct IDirect3DDevice9 { NativeDevice* native = nullptr; ULONG Release(); };

static ULONG WINAPI native_addref(IDirect3DDevice9* device) {
    ++device->native->addref_calls;
    return ++device->native->refs;
}
static ULONG WINAPI native_release(IDirect3DDevice9* device) {
    ++device->native->release_calls;
    const ULONG value = --device->native->refs;
    if (!value) ++device->native->destroyed;
    return value;
}

static DWORD GetCurrentThreadId() {
    static std::atomic<DWORD> next{1};
    thread_local const DWORD id = next++;
    return id;
}
static void* GetModuleHandleW(const wchar_t*) { return reinterpret_cast<void*>(0x400000u); }

namespace x3m {

struct Device;
struct CompositorInvocation;
ULONG WINAPI release_device(IDirect3DDevice9*);

enum class AliasModel { NativeObject, OwnershipAlias };

struct Surface final : IDirect3DSurface9 {
    IDirect3DDevice9* device;
    AliasModel model;
    ULONG refs = 1;
    unsigned releases = 0;
    bool dead = false;
    Surface(IDirect3DDevice9* owner, AliasModel kind) : device(owner), model(kind) {
        native_addref(device); // the resource's persistent device ownership
    }
    ULONG AddRef() override {
        ++refs;
        if (model == AliasModel::OwnershipAlias) native_addref(device);
        return refs;
    }
    ULONG Release() override {
        ++releases;
        if (!refs) return 0;
        const ULONG value = --refs;
        if (model == AliasModel::OwnershipAlias || !value) release_device(device);
        if (!value) dead = true;
        return value;
    }
};

namespace renderer {
struct BloomBoundary {
    IDirect3DSurface9* main = nullptr;
    IDirect3DSurface9* depth = nullptr;
    D3DSURFACE_DESC main_desc{}, depth_desc{};
    std::uint64_t frame = 0, reset = 0;
    DWORD thread = 0;
    bool admitted = false;
};
struct BloomPrepare {
    IDirect3DSurface9* scene = nullptr;
    BloomBoundary boundary{};
    int agx = 0, decode = 0;
    float sharpen = 0;
};
struct BloomCandidate { IDirect3DSurface9* surface = nullptr; };
struct BloomPrepared {
    BloomCandidate candidate{};
    bool ready = false, state_preserved = true;
    const char* reason = "stub";
    HRESULT operation = S_OK, restore = S_OK;
};
struct BloomCommitted {
    bool committed = true, state_preserved = true, original_preserved = true;
    const char* reason = "stub";
    HRESULT operation = S_OK, restore = S_OK, recovery = S_OK, recovery_restore = S_OK;
};
struct BloomCaps { const char* reason = "stub"; };
struct BloomPrograms {};
static const BloomPrograms& bloom_programs() { static BloomPrograms value; return value; }

struct BloomPass {
    std::vector<Surface*> resources;
    bool releasing_ = false, enabled_ = true;
    unsigned shutdowns = 0, resets = 0, commits = 0;
    unsigned references() const noexcept { return static_cast<unsigned>(resources.size()); }
    bool releasing() const noexcept { return releasing_; }
    void release_all() noexcept {
        const bool prior = releasing_; releasing_ = true;
        for (auto* resource : resources) resource->Release();
        resources.clear(); releasing_ = prior;
    }
    void shutdown() noexcept { ++shutdowns; release_all(); }
    void before_reset() noexcept { ++resets; release_all(); }
    bool enabled() const noexcept { return enabled_; }
    HRESULT attach(IDirect3DDevice9*, void* const*, const D3DCAPS9&, const BloomPrograms&) noexcept { return S_OK; }
    BloomCaps caps() const noexcept { return {}; }
    BloomPrepared prepare(const BloomPrepare&) noexcept { return {}; }
    BloomCommitted commit(const BloomCandidate&, const BloomBoundary&) noexcept { ++commits; return {}; }
    std::uint64_t resource_bytes() const noexcept { return 0; }
};
} // namespace renderer

struct MotionOutput {
    struct ComparisonExposure {bool ready=true,automatic=false,frame_used=true;float ev=0.f;const char* reason="ready";};
    ComparisonExposure comparison_exposure() const noexcept {return {};}
    std::vector<Surface*> resources;
    bool releasing_ = false, taa_busy_ = false, composition_busy_ = false, boundary_available = true;
    unsigned restores = 0, releases = 0, resets = 0, after_resets = 0, stateblocks = 0;
    unsigned scene_end_hooks = 0, scene_end_callbacks = 0;
    bool composition_operation_active() const noexcept { return composition_busy_; }
    bool reference_accounting_busy() const noexcept { return releasing_ || taa_busy_ || composition_busy_; }
    unsigned device_references() const noexcept {
        return reference_accounting_busy() ? 0u : static_cast<unsigned>(resources.size());
    }
    void restore_bindings() noexcept { ++restores; }
    void release_resources() noexcept {
        ++releases;
        const bool prior = releasing_; releasing_ = true;
        for (auto* resource : resources) resource->Release();
        resources.clear(); releasing_ = prior;
    }
    void before_reset() noexcept { ++resets; release_resources(); }
    void after_reset(HRESULT) noexcept { ++after_resets; }
    bool bloom_boundary_available() const noexcept { return boundary_available; }
    void stateblock_applied() noexcept { ++stateblocks; }
    using SceneCallback = void(*)(void*, const struct MotionHdrScene&);
    void scene_end_hook(SceneCallback callback, void*) noexcept {
        ++scene_end_hooks;
        if (callback) ++scene_end_callbacks;
    }
};
struct SceneCapture { unsigned invalidations = 0; void invalidate() { ++invalidations; } };
struct MotionCapture { unsigned invalidations = 0; void invalidate() { ++invalidations; } };
namespace object_capture { struct Cache { unsigned invalidations = 0; void invalidate() noexcept { ++invalidations; } }; }
struct Stats {
    bool had_present = false, last_frame_capture = false;
    unsigned resets = 0;
    HWND focus_window = nullptr, window = nullptr;
};
struct Hooks {
    void* original[134]{};
    template<class Function> Function get(std::size_t slot) const {
        return reinterpret_cast<Function>(original[slot]);
    }
};
struct Device : Hooks {
    D3DCAPS9 caps{};
    std::uint64_t id = 1, frame = 1;
    Stats stats{};
    SceneCapture scene_depth{};
    MotionCapture motion{};
    object_capture::Cache object_evidence{}; // diagnostic association; inert here
    MotionOutput motion_output{};
    renderer::BloomPass bloom{};
    ComparisonControls comparison{};
    struct Notice {
        unsigned hides=0;char first[64]{},second[64]{};
        void hide() noexcept {++hides;}
        void text(const char* a,const char* b) noexcept {
            std::snprintf(first,sizeof first,"%s",a);std::snprintf(second,sizeof second,"%s",b);
        }
    } comparison_notice;
    bool comparison_report_pending=false,bloom_effective_on=false;
    std::uint64_t bloom_effective_frame=UINT64_MAX;
    CompositorInvocation* compositor = nullptr;
    std::uint64_t reset_generation = 0;
    DWORD scene_thread = 0;
    unsigned bloom_busy = 0;
    bool reset_active = false, bloom_attempted = false, composition_scene_owner = false;
    unsigned bloom_failure_reports = 0, bloom_prepared = 0, bloom_committed = 0, remaining = 0;
    bool capture = false;
    static std::atomic<unsigned> destructors;
    ~Device() { ++destructors; }
};
std::atomic<unsigned> Device::destructors{0};

std::recursive_mutex mutex;
std::map<IDirect3DDevice9*, std::shared_ptr<Device>> devices;
bool bloom_requested=true;

namespace compositor_owner {
struct Snapshot { std::uintptr_t renderer = 1, record = 2, device = 0, manager = 3, manager_device = 0; };
enum class Result { Ok, Failed };
static Snapshot current{};
static Result result = Result::Ok;
static Result read(std::uintptr_t, Snapshot& output) noexcept { output = current; return result; }
static bool same(const Snapshot& a, const Snapshot& b) noexcept {
    return a.renderer == b.renderer && a.record == b.record && a.device == b.device
        && a.manager == b.manager && a.manager_device == b.manager_device;
}
} // namespace compositor_owner

struct CompositorInvocation {
    std::shared_ptr<Device> owner;
    IDirect3DDevice9* device = nullptr;
    compositor_owner::Snapshot identity{};
    renderer::BloomPrepare input{};
    renderer::BloomCandidate candidate{};
    bool native_pin = false, ready = false, revoked = false;
};
struct BloomOperation {
    Device& owner;
    explicit BloomOperation(Device& value) noexcept : owner(value) { ++owner.bloom_busy; }
    ~BloomOperation() { --owner.bloom_busy; }
};
template<class T> static void bloom_drop(T*& value) noexcept {
    T* old = value; value = nullptr; if (old) old->Release();
}

struct X3mCompositorFrame { std::uintptr_t caller_pc = 0, caller_stack = 0; };
struct MotionHdrScene {};
namespace scene_hook {
static bool active = true;
static std::uintptr_t pc = 0x4721b6u;
static bool compositor_active() noexcept { return active; }
static std::uintptr_t compositor_caller_pc() noexcept { return pc; }
} // namespace scene_hook
enum class BloomRefusal : unsigned { Caller, Owner, Device, Nested, Thread, Reset, Glow,
    Scene, Pass, Boundary, Post, Count };
static std::uint64_t bloom_calls = 0;
static std::uint64_t bloom_refusals[unsigned(BloomRefusal::Count)]{};
static void bloom_refuse(BloomRefusal reason, const Device* = nullptr) noexcept {
    ++bloom_refusals[unsigned(reason)];
}
static bool glow_enabled = true;
static bool compositor_glow_enabled(std::uintptr_t) noexcept { return glow_enabled; }
static void retain_compositor_scene(void*, const MotionHdrScene&) noexcept {}

static thread_local unsigned hook_guard_depth=0;
struct HookGuard {
    std::unique_lock<std::recursive_mutex> lock{mutex};
    HookGuard(){++hook_guard_depth;}
    ~HookGuard(){--hook_guard_depth;}
};
struct CpuCallBoundary { void before_original() noexcept {} void after_original() noexcept {} };
namespace ownership {
struct AdmissionMonitor {};
static AdmissionMonitor monitor;
static AdmissionMonitor* process_admission_monitor() noexcept { return &monitor; }
struct ApplicationAdmissionAbi {
    explicit ApplicationAdmissionAbi(AdmissionMonitor*) noexcept {}
    void finish() noexcept {}
};
} // namespace ownership
namespace telemetry {
enum class Metric { Reset };
struct State {};
static std::uint64_t now() noexcept { static std::atomic<std::uint64_t> n{0}; return ++n; }
static State& process() noexcept { static State value; return value; }
template<class... Args> static void record(Args&&...) noexcept {}
template<class... Args> static void summary(Args&&...) noexcept {}
} // namespace telemetry
namespace game_phases { static void invalidate_device() noexcept {} }
namespace sampling_profiler {
static unsigned shutdown_under_lock=0;
static void shutdown() noexcept {if(hook_guard_depth)++shutdown_under_lock;}
}
namespace chase_camera { static void note_last_device() noexcept {} }
namespace lod_scale { static void refresh() noexcept {} } // X3M_LOD_SCALE mirror refresh called from the Reset/Present paths (src/proxy/lod_scale.h); no-op on the host
namespace resource_reader { static void report() noexcept {} }
namespace loading_trace { static void crypt_cache_report(const char*) noexcept {} }
namespace voice_dmo_fallback { static void shutdown() noexcept {} } // disarms the fault witness at the last device destroy (capture.cpp, voice DMO fallback hook)
static void log(const char*, ...) noexcept {}
static void final_admission_metric(ownership::AdmissionMonitor*, const char*) noexcept {}
static void presentation_parameters(const char*, std::uint64_t, HWND, const D3DPRESENT_PARAMETERS*) noexcept {}
static void finite_upload_metrics(IDirect3DDevice9*, const Device&, const char*) noexcept {}
static void ownership_depth_info(IDirect3DDevice9*, std::uint64_t, std::uint64_t, const char*) noexcept {}

static bool native_reset_witness(IDirect3DDevice9* device, bool extended);
static HRESULT WINAPI native_reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS*) {
    ++device->native->reset_calls;
    native_reset_witness(device, false);
    return device->native->reset_result;
}
static HRESULT WINAPI native_reset_ex(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*) {
    ++device->native->reset_ex_calls;
    native_reset_witness(device, true);
    return device->native->reset_ex_result;
}

#include "capture_bloom_lifetime_under_test_inc.h"

static bool native_reset_witness(IDirect3DDevice9* device, bool) {
    auto found = devices.find(device);
    if (found == devices.end()) return false;
    const auto& ctx = *found->second;
    const auto* call = ctx.compositor;
    device->native->reset_observed_revoked = call && call->revoked && !call->ready
        && !call->input.boundary.admitted;
    device->native->reset_observed_defaults_dropped = call && !call->candidate.surface
        && !call->input.scene && !call->input.boundary.main && !call->input.boundary.depth
        && ctx.motion_output.resources.empty() && ctx.bloom.resources.empty();
    device->native->reset_observed_pin_alive = call && call->native_pin
        && device->native->refs.load() != 0;
    return true;
}

static unsigned failures = 0, checks = 0, scenarios = 0;
static void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}

struct Environment {
    AliasModel model;
    NativeDevice native{};
    IDirect3DDevice9 device{&native};
    std::shared_ptr<Device> ctx = std::make_shared<Device>();
    std::vector<std::unique_ptr<Surface>> surfaces;
    explicit Environment(AliasModel value, unsigned motion_count=2, unsigned bloom_count=2) : model(value) {
        ctx->scene_thread = GetCurrentThreadId();
        ctx->original[1] = reinterpret_cast<void*>(&native_addref);
        ctx->original[2] = reinterpret_cast<void*>(&native_release);
        ctx->original[16] = reinterpret_cast<void*>(&native_reset);
        ctx->original[132] = reinterpret_cast<void*>(&native_reset_ex);
        devices.emplace(&device, ctx);
        compositor_owner::current = {1, 2, reinterpret_cast<std::uintptr_t>(&device), 3,
                                     reinterpret_cast<std::uintptr_t>(&device)};
        compositor_owner::result = compositor_owner::Result::Ok;
        glow_enabled = true;
        for (unsigned i=0; i<motion_count; ++i) {
            surfaces.push_back(std::make_unique<Surface>(&device, model));
            ctx->motion_output.resources.push_back(surfaces.back().get());
        }
        for (unsigned i=0; i<bloom_count; ++i) {
            surfaces.push_back(std::make_unique<Surface>(&device, model));
            ctx->bloom.resources.push_back(surfaces.back().get());
        }
    }
    ~Environment() {
        if (devices.count(&device)) {
            ctx->compositor = nullptr;
            ctx->bloom.shutdown();
            ctx->motion_output.release_resources();
            release_device(&device);
        }
        devices.erase(&device);
    }
    Surface* motion(unsigned index) { return ctx->motion_output.resources.at(index); }
    Surface* bloom(unsigned index) { return ctx->bloom.resources.at(index); }
};

static void construct_invocation(Environment& env, CompositorInvocation* call, bool aliases=true) {
    new(call) CompositorInvocation{};
    call->owner = env.ctx;
    call->device = &env.device;
    call->identity = compositor_owner::current;
    native_addref(&env.device);
    call->native_pin = true;
    call->ready = true;
    call->input.boundary.admitted = true;
    call->input.boundary.frame = env.ctx->frame;
    call->input.boundary.reset = env.ctx->reset_generation;
    call->input.boundary.thread = env.ctx->scene_thread;
    env.ctx->compositor = call;
    if (aliases) {
        call->input.scene = env.motion(0); call->input.scene->AddRef();
        call->input.boundary.main = env.motion(1); call->input.boundary.main->AddRef();
        call->input.boundary.depth = env.motion(0); call->input.boundary.depth->AddRef();
        call->candidate.surface = env.bloom(0); call->candidate.surface->AddRef();
    }
}

static void nonterminal_get_device(AliasModel model) {
    ++scenarios;
    Environment env(model);
    const ULONG baseline = env.native.refs;
    native_addref(&env.device); // scripted IDirect3DResource9::GetDevice
    const unsigned shutdowns = env.ctx->bloom.shutdowns;
    const ULONG result = release_device(&env.device);
    check(result == baseline, "GetDevice Release returns baseline count");
    check(devices.count(&env.device) == 1, "nonterminal Release keeps map entry");
    check(env.ctx->bloom.shutdowns == shutdowns, "nonterminal Release keeps resources");
}

// Actual production holder, with the same declaration order as Present. The
// independent wiring assertion binds this lifetime witness to that call site.
static void notice_pin_lifetime(AliasModel model,unsigned drop_at) {
    ++scenarios;
    Environment env(model);
    std::weak_ptr<Device> cpu=env.ctx;
    const unsigned bad_shutdowns=sampling_profiler::shutdown_under_lock;
    unsigned present_calls=0;
    {
        NoticePin pin;
        HookGuard outer;
        auto owner=env.ctx;
        native_addref(&env.device);pin.device=&env.device;pin.owner=owner;
        {
            BloomOperation injected(*owner);
            Surface* alias=env.motion(0);alias->AddRef();
            if(drop_at==1)release_device(&env.device); // reentry during notice
            alias->Release();
        }
        check(env.native.destroyed==0&&!owner->bloom.shutdowns,"notice pin survives callback and temporary aliases");
        ++present_calls;
        if(drop_at==2)release_device(&env.device); // reentry from native Present
        check(env.native.destroyed==0&&devices.count(&env.device)==1,"notice pin remains through native Present");
        if(drop_at)env.ctx.reset();
    }
    check(present_calls==1,"notice path submits original Present once");
    check(sampling_profiler::shutdown_under_lock==bad_shutdowns,"final notice-pin retirement runs after outer lock");
    if(drop_at){
        check(env.native.destroyed==1&&devices.count(&env.device)==0,"normal pin Release retires last owned resources");
        check(cpu.expired(),"notice CPU owner outlives native/map retirement");
    }else check(env.native.destroyed==0&&devices.count(&env.device)==1&&!env.ctx->bloom.shutdowns,"nonterminal notice pin keeps application resources");
}

static void notice_readiness_text() {
    ++scenarios;
    Environment env(AliasModel::NativeObject);
    env.ctx->bloom.enabled_=false;env.ctx->bloom_attempted=false;
    comparison_notice_text(*env.ctx);
    check(!std::strcmp(env.ctx->comparison_notice.second,"BLOOM WAITING"),"configured unattempted bloom is waiting");
    env.ctx->bloom_attempted=true;comparison_notice_text(*env.ctx);
    check(!std::strcmp(env.ctx->comparison_notice.second,"BLOOM UNAVAILABLE"),"attempted refused bloom is unavailable");
    env.ctx->bloom.enabled_=true;comparison_notice_text(*env.ctx);
    check(!std::strcmp(env.ctx->comparison_notice.second,"BLOOM ON REQUESTED"),"ready bloom without this-frame commit remains requested");
    env.ctx->bloom_effective_frame=env.ctx->frame;env.ctx->bloom_effective_on=true;comparison_notice_text(*env.ctx);
    check(!std::strcmp(env.ctx->comparison_notice.second,"BLOOM ON"),"this-frame successful commit confirms ON");
    bloom_requested=false;env.ctx->bloom_attempted=false;comparison_notice_text(*env.ctx);
    check(!std::strcmp(env.ctx->comparison_notice.second,"BLOOM UNAVAILABLE"),"unrequested bloom is unavailable not waiting");
    bloom_requested=true;
}

static void final_during_invocation(AliasModel model, bool worker) {
    ++scenarios;
    Device::destructors = 0;
    Environment env(model);
    alignas(CompositorInvocation) unsigned char storage[sizeof(CompositorInvocation)];
    auto* call = reinterpret_cast<CompositorInvocation*>(storage);
    construct_invocation(env, call);
    const unsigned shutdowns = env.ctx->bloom.shutdowns;
    ULONG result = 0;
    if (worker) {
        std::thread thread([&] { result = release_device(&env.device); });
        thread.join();
    } else result = release_device(&env.device); // final application Release from original
    check(result > 0 && env.native.destroyed == 0, "explicit pin defers native destruction");
    check(devices.count(&env.device) == 1 && env.ctx->bloom.shutdowns == shutdowns,
          "invocation Release neither retires CPU owner nor persistent resources");
    std::weak_ptr<Device> cpu = env.ctx;
    env.ctx.reset();
    compositor_cleanup(nullptr, storage, nullptr, 0);
    check(env.native.destroyed == 1 && devices.count(&env.device) == 0,
          "cleanup pin Release performs final native retirement");
    check(cpu.expired() && Device::destructors == 1, "CPU pin outlives map erase and is destroyed last");
}

static void nested_busy_release(AliasModel model) {
    ++scenarios;
    Environment env(model, 1, 2);
    auto child = std::make_unique<Surface>(&env.device, model);
    env.ctx->motion_output.taa_busy_ = true;
    const unsigned adds = env.native.addref_calls;
    child->Release();
    check(env.ctx->bloom.references() == 2, "busy nested release has nonzero bloom references");
    check(env.native.addref_calls == adds, "motion busy suppresses combined final-reference probe");
    check(env.ctx->bloom.shutdowns == 0 && env.native.destroyed == 0,
          "nested busy release preserves persistent resources and device");
    env.ctx->motion_output.taa_busy_ = false;
}

static void reset_case(AliasModel model, bool extended, bool success) {
    ++scenarios;
    Environment env(model);
    alignas(CompositorInvocation) unsigned char storage[sizeof(CompositorInvocation)];
    auto* call = reinterpret_cast<CompositorInvocation*>(storage);
    construct_invocation(env, call);
    env.native.reset_result = success ? S_OK : E_FAIL;
    env.native.reset_ex_result = success ? S_OK : E_FAIL;
    env.ctx->composition_scene_owner = true;
    const unsigned adds = env.native.addref_calls;
    D3DPRESENT_PARAMETERS parameters{};
    D3DDISPLAYMODEEX mode{};
    const HRESULT result = extended ? reset_ex(&env.device, &parameters, &mode)
                                    : reset(&env.device, &parameters);
    check(bool(SUCCEEDED(result)) == success, "Reset/ResetEx forwards exact success or failure");
    check(env.native.reset_observed_revoked && env.native.reset_observed_defaults_dropped,
          "invocation aliases and DEFAULT resources drop before native Reset");
    check(env.native.reset_observed_pin_alive && call->native_pin,
          "native and CPU invocation pins survive Reset entry and return");
    check(env.native.addref_calls == adds, "Reset nested child Releases do not run reference probe");
    check(env.ctx->reset_generation == 1 && !env.ctx->reset_active && env.ctx->scene_thread == 0,
          "Reset generation/thread state remains revoked after result");
    check(!env.ctx->composition_scene_owner, "Reset revokes prior emission scene admission");
    check(env.ctx->comparison_notice.hides==1&&env.ctx->bloom_effective_frame==UINT64_MAX,
          "Reset hides comparison notice and discards effective bloom frame");
    check((extended ? env.native.reset_ex_calls.load() : env.native.reset_calls.load()) == 1,
          "correct Reset vtable slot called once");
    compositor_cleanup(nullptr, storage, nullptr, success ? 0 : 1);
    check(env.native.destroyed == 0 && devices.count(&env.device) == 1,
          "cleanup releases only explicit pin while application owner remains");
}

static void composition_busy_reset(AliasModel model, bool extended) {
    ++scenarios;
    Environment env(model);
    alignas(CompositorInvocation) unsigned char storage[sizeof(CompositorInvocation)];
    auto* call = reinterpret_cast<CompositorInvocation*>(storage);
    construct_invocation(env, call);
    env.ctx->motion_output.composition_busy_ = true;
    env.ctx->composition_scene_owner = true;
    D3DPRESENT_PARAMETERS parameters{};
    D3DDISPLAYMODEEX mode{};
    const HRESULT result = extended ? reset_ex(&env.device, &parameters, &mode)
                                    : reset(&env.device, &parameters);
    check(result == D3DERR_INVALIDCALL, "active emission rejects reentrant Reset/ResetEx");
    check(env.native.reset_calls == 0 && env.native.reset_ex_calls == 0,
          "rejected emission Reset never reaches either native slot");
    check(env.ctx->motion_output.resets == 0 && env.ctx->bloom.resets == 0,
          "rejected Reset preserves active injected resources");
    check(env.ctx->reset_generation == 0 && !env.ctx->reset_active && !call->revoked,
          "rejected Reset leaves the current invocation generation intact");
    check(env.ctx->composition_scene_owner, "rejected Reset leaves current emission admission intact");
    env.ctx->motion_output.composition_busy_ = false;
    compositor_cleanup(nullptr, storage, nullptr, 0);
}

enum class Mismatch { None, Frame, Thread, Generation, Owner, Glow };
static void post_case(Mismatch mismatch) {
    ++scenarios;
    Environment env(AliasModel::NativeObject);
    alignas(CompositorInvocation) unsigned char storage[sizeof(CompositorInvocation)];
    auto* call = reinterpret_cast<CompositorInvocation*>(storage);
    construct_invocation(env, call, false);
    switch (mismatch) {
    case Mismatch::Frame: ++env.ctx->frame; break;
    case Mismatch::Thread: ++call->input.boundary.thread; break;
    case Mismatch::Generation: ++env.ctx->reset_generation; break;
    case Mismatch::Owner: ++compositor_owner::current.renderer; break;
    case Mismatch::Glow: glow_enabled = false; break;
    case Mismatch::None: break;
    }
    compositor_post(nullptr, storage, nullptr);
    check(env.ctx->bloom.commits == (mismatch == Mismatch::None ? 1u : 0u),
          "post commit requires frame/thread/reset/owner identity");
    compositor_cleanup(nullptr, storage, nullptr, 0);
}

enum class PreRefusal { InvalidCaller, UnreadableOwner, MissingDevice, WrongThread, ResetActive };
static void pre_refusal(PreRefusal refusal) {
    ++scenarios;
    Environment env(AliasModel::NativeObject);
    alignas(CompositorInvocation) unsigned char storage[sizeof(CompositorInvocation)];
    X3mCompositorFrame frame{scene_hook::pc, 4};
    IDirect3DDevice9 missing{&env.native};
    switch (refusal) {
    case PreRefusal::InvalidCaller: ++frame.caller_pc; break;
    case PreRefusal::UnreadableOwner: compositor_owner::result = compositor_owner::Result::Failed; break;
    case PreRefusal::MissingDevice:
        compositor_owner::current.device = reinterpret_cast<std::uintptr_t>(&missing);
        compositor_owner::current.manager_device = reinterpret_cast<std::uintptr_t>(&missing);
        break;
    case PreRefusal::WrongThread: ++env.ctx->scene_thread; break;
    case PreRefusal::ResetActive: env.ctx->reset_active = true; break;
    }
    const unsigned adds = env.native.addref_calls;
    compositor_pre(&frame, storage, nullptr);
    auto* call = reinterpret_cast<CompositorInvocation*>(storage);
    check(env.ctx->motion_output.scene_end_hooks == 0,
          "early pre refusal does not broadcast scene-end work");
    check(!call->native_pin && !call->owner && env.ctx->compositor == nullptr,
          "early pre refusal acquires no invocation ownership");
    check(env.native.addref_calls == adds, "early pre refusal takes no native pin");
    compositor_cleanup(nullptr, storage, nullptr, 0);
}

static void pre_glow_off() {
    ++scenarios;
    Environment env(AliasModel::NativeObject);
    alignas(CompositorInvocation) unsigned char storage[sizeof(CompositorInvocation)];
    X3mCompositorFrame frame{scene_hook::pc, 4};
    const unsigned adds = env.native.addref_calls;
    glow_enabled = false;
    compositor_pre(&frame, storage, nullptr);
    auto* call = reinterpret_cast<CompositorInvocation*>(storage);
    check(env.ctx->motion_output.scene_end_hooks == 1
              && env.ctx->motion_output.scene_end_callbacks == 0,
          "safe-owner glow-off path performs one ordinary scene-end hook");
    check(call->native_pin && call->owner == env.ctx && env.ctx->compositor == call,
          "glow-off refusal retains registered cleanup ownership");
    check(env.native.addref_calls == adds + 1, "glow-off path takes exactly one native pin");
    check(!call->ready && !call->input.scene,
          "glow-off path declines replacement after ordinary scene-end work");
    compositor_cleanup(nullptr, storage, nullptr, 0);
}

static void nested_invocation() {
    ++scenarios;
    Environment env(AliasModel::OwnershipAlias);
    alignas(CompositorInvocation) unsigned char outer_storage[sizeof(CompositorInvocation)];
    alignas(CompositorInvocation) unsigned char nested_storage[sizeof(CompositorInvocation)];
    auto* outer = reinterpret_cast<CompositorInvocation*>(outer_storage);
    construct_invocation(env, outer);
    const unsigned releases = env.motion(0)->releases;
    X3mCompositorFrame frame{scene_hook::pc, 4};
    compositor_pre(&frame, nested_storage, nullptr);
    check(outer->revoked && !outer->ready, "nested invocation revokes outer ticket");
    check(env.motion(0)->releases == releases && env.ctx->compositor == outer,
          "nested admission does not destroy outer aliases inside original");
    compositor_cleanup(nullptr, nested_storage, nullptr, 0);
    compositor_cleanup(nullptr, outer_storage, nullptr, 0);
}

} // namespace x3m

ULONG IDirect3DDevice9::Release(){return x3m::release_device(this);}

int main() {
    using namespace x3m;
    for (auto model : {AliasModel::NativeObject, AliasModel::OwnershipAlias}) {
        nonterminal_get_device(model);
        final_during_invocation(model, false);
        final_during_invocation(model, true);
        nested_busy_release(model);
        for(unsigned drop_at:{0u,1u,2u})notice_pin_lifetime(model,drop_at);
        for (bool extended : {false, true}) for (bool success : {false, true})
            reset_case(model, extended, success);
        for (bool extended : {false, true}) composition_busy_reset(model, extended);
    }
    for (auto mismatch : {Mismatch::None, Mismatch::Frame, Mismatch::Thread,
                          Mismatch::Generation, Mismatch::Owner, Mismatch::Glow}) post_case(mismatch);
    for (auto refusal : {PreRefusal::InvalidCaller, PreRefusal::UnreadableOwner,
                         PreRefusal::MissingDevice, PreRefusal::WrongThread,
                         PreRefusal::ResetActive}) pre_refusal(refusal);
    pre_glow_off();
    nested_invocation();
    notice_readiness_text();
    std::printf("capture_bloom_lifetime scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
