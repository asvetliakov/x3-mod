// Five real MotionOutput function bodies and its real header, with scripted
// renderer/D3D dependencies. GPU/state and SEH lifetime tests remain separate.
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include "../../src/renderer/exposure.h"
#include "../../src/temporal/agx.h"
#include "../../src/temporal/sharpen.h"
#define private public
#include "../../src/proxy/motion_output.h"
#undef private

using namespace x3m;
namespace x3m::renderer { class TemporalPass {}; }
namespace x3m::telemetry {
enum class Metric { HdrMeter, HdrWriteback, HdrWritebackDraw, HdrWritebackStretch, HdrBind };
}
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", name); }
}
enum class Case {
    Normal, Disabled, HdrDisabled, OutsideScene, Duplicate, NoOpenScene, Query,
    Recording, Blocked, PriorUnwind, PriorRestore, Releasing, TaaBusy, Msaa,
    NoJitter, NotFilled, TaaInitialize, TaaTarget, TaaContainer, TaaFailure,
    TaaRestore, TaaNoTexture, PriorAttempt, PriorOtherAttempt, HdrOff,
    HdrSuspended, NoHdr, NoMain, NoTarget, RebindOnly, AgxFailure, AgxRestore,
    Identity, InvalidSnapshot, SnapshotDimensions, MainDimensions, MainIdentity,
    SnapshotSource, ContainerFailure, ContainerNull, ContainerFailureOutput,
    MeterFailure, UnsharpenedRetry
};
struct Observed {
    Case scenario = Case::Normal;
    MotionOutput* motion = nullptr;
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* main = nullptr;
    IDirect3DTexture9* resolved = nullptr;
    IDirect3DTexture9* original = nullptr;
    IDirect3DTexture9* retained_scene = nullptr;
    IDirect3DSurface9* retained_main = nullptr;
    renderer::HdrDisplaySnapshot display;
    renderer::HdrDisplaySnapshot consumed;
    std::vector<unsigned> calls;
    unsigned resolves = 0, writes = 0, snapshots = 0, callbacks = 0, describes = 0;
} *active = nullptr;

HRESULT get_rt(IDirect3DDevice9*, DWORD, IDirect3DSurface9** out) {
    active->calls.push_back(1);
    *out = active->scenario == Case::TaaTarget ? active->main : active->target;
    if (*out) (*out)->AddRef();
    return *out ? S_OK : E_FAIL;
}
void callback(void* context, const MotionHdrScene& ready) noexcept {
    check(context == active, "caller context"); ++active->callbacks;
    const auto& motion = *active->motion;
    check(ready.device == motion.device_ && ready.device_id == 37 && ready.frame == 43 && ready.generation == 5,
        "exact owner/frame/generation metadata");
    check(ready.main == active->main && motion.hdr_main_ == ready.main && motion.hdr_state_ == MotionOutput::HdrState::Active,
        "callback before main/redirect cleanup");
    check(ready.display.valid && ready.scene == (ready.display.resolved ? active->resolved : active->original),
        "scene matches consumed resolved flag");
    check(ready.main->refs == 2 && ready.scene->refs >= 1, "borrowed references live through callback");
    check(!std::memcmp(&ready.display.agx, &active->consumed.agx, sizeof(ready.display.agx))
        && ready.display.sharpen == active->consumed.sharpen
        && !std::memcmp(&ready.display.sharpen_constants, &active->consumed.sharpen_constants, sizeof(ready.display.sharpen_constants)),
        "exact display payload forwarded");
    active->display = ready.display; // caller-owned copy; no reference escapes
    ready.scene->AddRef(); active->retained_scene = ready.scene;
    ready.main->AddRef(); active->retained_main = ready.main;
}
}

namespace x3m::renderer {
HdrPass::~HdrPass() = default;
HdrWriteback HdrPass::write_back(IDirect3DSurface9* main, IDirect3DSurface9* final_rt, bool,
                               bool write, bool, IDirect3DTexture9* source, HdrDisplaySnapshot* display) noexcept {
    active->calls.push_back(2); ++active->writes;
    check(main == active->main && final_rt == main, "normal writeback uses matching main/final target");
    if (display) { ++active->snapshots; *display = {}; }
    HdrWriteback result;
    if (!write) return result;
    result.source = HdrWritebackSource::Shader;
    result.draw = result.restore = result.tonemap_draw = S_OK;
    result.tonemap = true; result.meter = S_OK;
    if (active->scenario == Case::AgxFailure || !target_) {
        result.unwind = true; result.draw = E_FAIL;
    } else if (active->scenario == Case::AgxRestore) {
        result.unwind = true; result.restore = E_FAIL;
    } else if (active->scenario == Case::Identity) {
        result.tonemap = false; result.fallback = true; result.unwind = true;
    } else if (active->scenario == Case::MeterFailure) result.meter = E_FAIL;
    if (display && !result.unwind) {
        *display = active->consumed;
        display->resolved = source != nullptr;
        if (active->scenario == Case::InvalidSnapshot) display->valid = false;
        if (active->scenario == Case::SnapshotDimensions) ++display->width;
        if (active->scenario == Case::SnapshotSource) display->resolved = !display->resolved;
    }
    return result;
}
}

namespace x3m {
namespace {
constexpr unsigned failure_log_limit = 16;
enum { GetRenderTarget = 38 };
using GetRenderTargetFn = decltype(&get_rt);
template<class T> void release(T*& p) noexcept { if (T* old = p) { p = nullptr; old->Release(); } }
bool same(const renderer::Surface& a, const renderer::Surface& b) noexcept {
    return a.known && b.known && a.identity && a.identity == b.identity && a.container == b.container
        && a.width == b.width && a.height == b.height && a.format == b.format && a.msaa == b.msaa;
}
}
void log(const char*, ...) { active->calls.push_back(3); }
renderer::Surface describe_surface(IDirect3DSurface9* surface) noexcept {
    ++active->describes;
    return {true, surface->identity, 0, surface->width, surface->height, unsigned(D3DFMT_A8R8G8B8), 0};
}
const char* hdr_source_name(unsigned) { return "fixture"; }
MotionOutput::MotionOutput() noexcept = default;
MotionOutput::~MotionOutput() = default;
void MotionOutput::restore_bindings() noexcept { active->calls.push_back(5); }
void MotionOutput::finish_cut_detector() noexcept { active->calls.push_back(6); cut_finished_ = true; }
void MotionOutput::invalidate_taa(TaaInvalidateSite) noexcept { active->calls.push_back(7); }
bool MotionOutput::ensure_taa() noexcept { active->calls.push_back(8); return active->scenario != Case::TaaInitialize; }
std::uint64_t MotionOutput::stamp() const noexcept { return 0; }
void MotionOutput::record(unsigned, std::uint64_t, bool, std::uint64_t) noexcept {}
void MotionOutput::invalidate_render_states() noexcept { active->calls.push_back(9); }
// Inert doubles for composition identity/export bookkeeping the fixture does
// not script; the handoff paths under test only reach them on teardown.
void MotionOutput::composition_export() noexcept {}
void MotionOutput::release_composition_identity() noexcept {}
// The composition pass itself is never constructed here; these satisfy the
// unique_ptr<LinearEmissionPass> member and the busy query on that path.
renderer::LinearEmissionPass::~LinearEmissionPass() = default;
renderer::ShadowReplayPass::~ShadowReplayPass() = default; // the depth-replay pass is never constructed here; only the member destructor is needed
renderer::SunShadowApplyPass::~SunShadowApplyPass() = default; // likewise: only the unique_ptr member destructor is needed
renderer::FogPass::~FogPass() = default; // likewise
renderer::SunOcclusionPass::~SunOcclusionPass() = default; // likewise (partial sun occlusion, 855fc1bc)
// The scene-end apply quad is gated on sun_apply_requested_, which no scenario
// here sets; inert so it cannot perturb the recorded call sequences.
void MotionOutput::run_sun_shadow_apply() noexcept {}
void MotionOutput::run_volumetric_fog() noexcept {} // the hook's call is behind fog_requested_ (default false): never reached here
bool renderer::LinearEmissionPass::reference_accounting_busy() const noexcept { return false; }
bool renderer::LinearEmissionPass::coverage_valid() const noexcept { return true; }
// Shadow-replay candidate publication is a separate per-frame diagnostic with
// its own fixture; scene_end_hook only has to reach it.
void MotionOutput::publish_shadow_replay_candidates() noexcept {}
HRESULT MotionOutput::readback_surface(IDirect3DSurface9*, D3DFORMAT, unsigned, const wchar_t*,
                                     const wchar_t*, const char*, const char*, UINT, UINT) noexcept {
    active->calls.push_back(10); return S_OK;
}
HRESULT MotionOutput::resolve(IDirect3DSurface9*, IDirect3DTexture9* scene) noexcept {
    active->calls.push_back(11); ++active->resolves;
    auto& t = counters_.taa;
    t.hdr = scene != nullptr;
    t.result = active->scenario == Case::TaaFailure ? E_FAIL : S_OK;
    t.restore = active->scenario == Case::TaaRestore ? E_FAIL : S_OK;
    t.resolved = SUCCEEDED(t.result) && SUCCEEDED(t.restore);
    hdr_resolved_ = t.resolved && active->scenario != Case::TaaNoTexture ? active->resolved : nullptr;
    return t.resolved ? S_OK : E_FAIL;
}
#include "motion_hdr_scene_under_test_inc.h"
}

struct Run {
    std::vector<unsigned> calls;
    MotionFrameCounters counters;
    unsigned writes, resolves, callbacks, containers, describes;
};
Run run(Case scenario, bool taa, bool supplied) {
    Observed observed; active = &observed; observed.scenario = scenario;
    MotionOutput motion; observed.motion = &motion;
    IDirect3DDevice9 device;
    IDirect3DTexture9 original, resolved;
    IDirect3DSurface9 target, main, depth, motion_target;
    target.texture = &original; target.identity = 2;
    main.refs = 2; // fixture's external ref + MotionOutput's retained main
    observed.target = &target; observed.main = &main;
    observed.original = &original; observed.resolved = &resolved;
    void* native[39]{}; native[38] = reinterpret_cast<void*>(&get_rt);
    motion.device_ = &device; motion.native_ = native;
    motion.id_ = 37; motion.frame_ = 43; motion.generation_ = 5;
    motion.enabled_ = motion.hdr_enabled_ = motion.scene_open_ = true;
    motion.taa_enabled_ = taa; motion.jitter_active_ = true;
    motion.counters_.filled = true; motion.target_surface_ = &motion_target; motion.depth_surface_ = &depth;
    motion.hdr_ = std::make_unique<renderer::HdrPass>(); motion.hdr_->target_ = &target;
    motion.hdr_->width_ = 17; motion.hdr_->height_ = 11;
    motion.hdr_main_ = &main; motion.hdr_state_ = MotionOutput::HdrState::Active; motion.hdr_dirty_ = true;
    motion.selector_.state_ = renderer::BoundaryState::Scene;
    motion.main_ = {true, 1, 0, 17, 11, unsigned(D3DFMT_A8R8G8B8), 0};
    observed.consumed.valid = true; observed.consumed.width = 17; observed.consumed.height = 11;
    observed.consumed.sharpen = taa && scenario != Case::UnsharpenedRetry ? .75f : 0.f;
    x3::temporal::prepare(observed.consumed.agx, .78125f, 19.f, x3::temporal::AgxDecode::srgb, x3::temporal::AgxLook::golden);
    observed.consumed.decode = x3::temporal::AgxDecode::srgb;
    if (observed.consumed.sharpen) x3::temporal::prepare_sharpen(observed.consumed.sharpen_constants, .75f, 17, 11);
    switch (scenario) {
    case Case::Disabled: motion.enabled_ = false; break;
    case Case::HdrDisabled: motion.hdr_enabled_ = false; break;
    case Case::OutsideScene: motion.selector_.state_ = renderer::BoundaryState::Background; break;
    case Case::Duplicate: motion.counters_.hook_scene_end = true; break;
    case Case::NoOpenScene: motion.scene_open_ = false; break;
    case Case::Query: motion.active_queries_ = 1; break;
    case Case::Recording: motion.shadow_.recording = true; break;
    case Case::Blocked: motion.hdr_blocked_ = true; break;
    case Case::PriorUnwind: motion.counters_.hdr.unwind = true; break;
    case Case::PriorRestore: motion.counters_.restore_failures = 1; break;
    case Case::Releasing: motion.releasing_ = true; break;
    case Case::TaaBusy: motion.taa_busy_ = true; break;
    case Case::Msaa: motion.main_msaa_ = true; break;
    case Case::NoJitter: motion.jitter_active_ = false; break;
    case Case::NotFilled: motion.counters_.filled = false; break;
    case Case::TaaContainer: target.container_hr = E_FAIL; break;
    case Case::PriorAttempt: case Case::PriorOtherAttempt:
        motion.counters_.taa.attempted = motion.counters_.taa.resolved = motion.counters_.taa.hdr = true;
        motion.counters_.taa.source = unsigned(scenario == Case::PriorAttempt ? SceneEndSource::Hook : SceneEndSource::StretchRect);
        motion.hdr_resolved_ = &resolved; break;
    case Case::HdrOff: motion.hdr_state_ = MotionOutput::HdrState::Off; motion.hdr_main_ = nullptr; main.refs = 1; break;
    case Case::HdrSuspended: motion.hdr_state_ = MotionOutput::HdrState::Suspended; break;
    case Case::NoHdr: motion.hdr_.reset(); break;
    case Case::NoMain: motion.hdr_main_ = nullptr; main.refs = 1; break;
    case Case::NoTarget: motion.hdr_->target_ = nullptr; break;
    case Case::RebindOnly: motion.hdr_dirty_ = false; break;
    case Case::MainDimensions: ++motion.main_.width; break;
    case Case::MainIdentity: ++main.identity; break;
    case Case::ContainerFailure: target.container_hr = E_FAIL; break;
    case Case::ContainerNull: target.texture = nullptr; break;
    case Case::ContainerFailureOutput: target.container_hr = E_FAIL; target.failure_output = true; break;
    default: break;
    }
    check(motion.reference_accounting_busy() == (scenario == Case::Releasing || scenario == Case::TaaBusy), "reference accounting accessor");
    if (supplied) motion.scene_end_hook(&callback, &observed);
    else motion.scene_end_hook(); // unchanged public call compiles and executes
    const bool accepted = scenario == Case::Normal || scenario == Case::MeterFailure || scenario == Case::UnsharpenedRetry;
    check(observed.callbacks == (supplied && accepted ? 1u : 0u), "handoff acceptance");
    check(observed.writes <= 1 && observed.resolves <= 1, "no extra writeback or temporal resolve");
    if (!supplied) check(observed.snapshots == 0, "null callback does not request a snapshot");
    if (observed.callbacks) {
        check(motion.hdr_state_ == MotionOutput::HdrState::Off && !motion.hdr_main_ && !motion.hdr_resolved_, "redirect cleaned after callback");
        check(observed.retained_scene->refs == 2 && observed.retained_main->refs == 2,
            "caller-owned pins outlive all temporary references and redirect cleanup");
        check(observed.display.valid && observed.display.width == 17 && observed.display.height == 11,
            "copied snapshot outlives callback stack");
        check(motion.bloom_boundary_available(), "post-original CPU admission permits ended redirect");
        observed.retained_scene->Release(); observed.retained_main->Release();
    }
    for (auto unavailable : {Case::Disabled, Case::HdrDisabled, Case::NoOpenScene, Case::Query, Case::Recording,
            Case::Blocked, Case::PriorUnwind, Case::PriorRestore, Case::Releasing, Case::TaaBusy,
            Case::AgxFailure, Case::AgxRestore, Case::Identity})
        if (scenario == unavailable) check(!motion.bloom_boundary_available(), "CPU admission rechecks current failure/activity state");
    if (scenario == Case::ContainerFailureOutput) check(original.refs == 1, "failed GetContainer non-null output released");
    const unsigned writes = observed.writes, resolves = observed.resolves, callbacks = observed.callbacks;
    if (supplied) motion.scene_end_hook(&callback, &observed); else motion.scene_end_hook();
    check(observed.writes == writes && observed.resolves == resolves && observed.callbacks == callbacks,
        "repeated boundary cannot retry resolve/writeback/handoff");
    check(original.refs == 1 && resolved.refs == 1 && target.refs == 1, "all ordinary-path temporary scene refs balanced");
    const Run result{observed.calls, motion.counters_, observed.writes, observed.resolves,
        observed.callbacks, target.containers, observed.describes};
    // Release any pre-existing test-only retained main on early rejection.
    if (motion.hdr_main_) { motion.hdr_main_->Release(); motion.hdr_main_ = nullptr; }
    check(main.refs == 1, "main reference balanced after cleanup");
    return result;
}

int main() {
    unsigned scenarios = 0;
    auto paired = [&](Case scenario, bool taa) {
        ++scenarios;
        const auto on = run(scenario, taa, true);
        const auto off = run(scenario, taa, false);
        check(on.calls == off.calls && on.writes == off.writes && on.resolves == off.resolves,
            "default-null writeback/resolve/call sequence parity");
        check(on.counters.hdr.writebacks == off.counters.hdr.writebacks && on.counters.taa.attempted == off.counters.taa.attempted
            && on.counters.taa.resolved == off.counters.taa.resolved && on.counters.hdr.end == off.counters.hdr.end
            && on.counters.hdr.unwind == off.counters.hdr.unwind && on.counters.restore_failures == off.counters.restore_failures,
            "default-null policy/telemetry parity");
        check(off.containers <= (taa ? 1u : 0u), "null callback adds no GetContainer");
        check(on.containers <= 1u, "optional handoff takes at most one unresolved container");
        check(off.describes == 0 || scenario == Case::HdrOff || scenario == Case::HdrSuspended
            || scenario == Case::NoHdr || scenario == Case::NoMain || scenario == Case::NoTarget,
            "null callback adds no identity lookup");
    };
    for (bool taa : {false, true}) {
        for (auto scenario : {Case::Normal, Case::Disabled, Case::HdrDisabled, Case::OutsideScene, Case::Duplicate,
                Case::NoOpenScene, Case::Query, Case::Recording, Case::Blocked, Case::PriorUnwind, Case::PriorRestore,
                Case::Releasing, Case::TaaBusy, Case::Msaa, Case::HdrOff, Case::HdrSuspended, Case::NoHdr, Case::NoMain,
                Case::NoTarget, Case::AgxFailure, Case::AgxRestore, Case::Identity, Case::InvalidSnapshot,
                Case::SnapshotDimensions, Case::MainDimensions, Case::MainIdentity, Case::SnapshotSource,
                Case::MeterFailure, Case::UnsharpenedRetry}) paired(scenario, taa);
    }
    for (auto scenario : {Case::NoJitter, Case::NotFilled, Case::TaaInitialize, Case::TaaTarget, Case::TaaContainer,
            Case::TaaFailure, Case::TaaRestore, Case::TaaNoTexture, Case::PriorAttempt, Case::PriorOtherAttempt}) paired(scenario, true);
    for (auto scenario : {Case::RebindOnly, Case::ContainerFailure, Case::ContainerNull, Case::ContainerFailureOutput}) paired(scenario, false);
    std::printf("motion_hdr_scene scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
