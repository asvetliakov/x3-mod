// Host-only control-flow qualification, not GPU/state/Windows-ABI evidence.
// The paired unittest extracts the unmodified production write_back into a
// temporary *_inc.h; D3D work below has explicitly scripted return values.
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "../../src/renderer/exposure.h"
#include "../../src/temporal/agx.h"
#include "../../src/temporal/sharpen.h"
#define X3M_MOTION_OUTPUT_FIXTURE
#define private public
#include "../../src/renderer/hdr_pass.h"
#undef private

using namespace x3m::renderer;
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", name); }
}
enum class Case {
    Normal, SharpenFallback, IdentityFallback, Restore, Draw, Lost, Stretch,
    Rebind, Bind, NoMain, NoFinal, NoTarget, NoShader, NoContainer, NullContainer,
    BeginScene, EndScene, Meter, Identity, SharpenDisabled, TonemapDisabled,
    Manual, NoRing, MutatedConfig, BothFallback
};
struct Observed {
    std::vector<unsigned> calls;
    unsigned meters = 0, draws = 0, early_valid = 0;
    x3::temporal::AgxConstants constants{};
    x3::temporal::SharpenConstants sharpen{};
    IDirect3DTexture9* sampled = nullptr;
    HdrDisplaySnapshot* output = nullptr;
    Case scenario = Case::Normal;
    void record(unsigned id) {
        calls.push_back(id);
        if (output && output->valid) ++early_valid;
    }
} *active = nullptr;
HRESULT begin_scene(IDirect3DDevice9*) {
    active->record(1); return active->scenario == Case::BeginScene ? E_FAIL : S_OK;
}
HRESULT end_scene(IDirect3DDevice9*) {
    active->record(2); return active->scenario == Case::EndScene ? E_FAIL : S_OK;
}
HRESULT stretch(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE) {
    active->record(3); return S_OK;
}
bool same(const HdrWriteback& a, const HdrWriteback& b) {
    return a.source == b.source && a.unwind == b.unwind && !std::strcmp(a.unwind_reason, b.unwind_reason)
        && a.draw == b.draw && a.restore == b.restore && a.stretch == b.stretch && a.bind == b.bind
        && a.ticks_draw == b.ticks_draw && a.ticks_stretch == b.ticks_stretch && a.ticks_bind == b.ticks_bind
        && a.tonemap == b.tonemap && a.fallback == b.fallback && a.tonemap_draw == b.tonemap_draw
        && a.meter == b.meter && a.ticks_meter == b.ticks_meter && a.sharpened == b.sharpened
        && a.sharpen_fallback == b.sharpen_fallback;
}
bool cleared(const HdrDisplaySnapshot& s) {
    const HdrDisplaySnapshot defaults;
    return !s.valid && !s.resolved && !s.width && !s.height && s.sharpen == 0.f
        && s.decode == defaults.decode && !std::memcmp(&s.agx, &defaults.agx, sizeof(s.agx))
        && !std::memcmp(&s.sharpen_constants, &defaults.sharpen_constants, sizeof(s.sharpen_constants));
}
}

namespace x3m::renderer {
namespace {
enum { StretchRect = 34, BeginScene = 41, EndScene = 42 };
using SceneFn = HRESULT(*)(IDirect3DDevice9*);
using StretchFn = decltype(&stretch);
bool lost(HRESULT hr) { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
template<class T> void drop(T*& value) { if (value) { value->Release(); value = nullptr; } }
}
HdrPass::~HdrPass() = default; // fake resources are stack objects
std::uint64_t HdrPass::stamp(bool) const noexcept { return 0; }
HRESULT HdrPass::bind(IDirect3DSurface9*, std::uint64_t*) noexcept {
    active->record(4); return fault(HdrFault::Bind) ? E_FAIL : S_OK;
}
HRESULT HdrPass::copy_draw(IDirect3DSurface9*, IDirect3DTexture9* texture, IDirect3DSurface9*,
                         UINT, UINT, IDirect3DSurface9*, HRESULT* restoration,
                         HRESULT injected_draw, bool injected_restore, Program* program) noexcept {
    active->record(5); ++active->draws; active->sampled = texture;
    if (program && program->constants)
        std::memcpy(&active->constants, program->constants, sizeof(active->constants));
    if (program && program->sharpen)
        std::memcpy(&active->sharpen, program->sharpen, sizeof(active->sharpen));
    // Count requested meter submissions. The real copy_draw owns the GPU
    // chain; this seam only proves write_back does not request it twice.
    if (program && program->meter) {
        ++active->meters;
        program->meter_result = fault(HdrFault::Meter) ? E_FAIL : S_OK;
    }
    if (active->scenario == Case::MutatedConfig) {
        config_.sharpen = .1f;
        config_.decode = x3::temporal::AgxDecode::none;
    }
    *restoration = injected_restore ? E_FAIL : S_OK;
    if (active->scenario == Case::BothFallback && active->draws == 2) return E_FAIL;
    return injected_draw;
}
#include "hdr_writeback_under_test_inc.h"
}

struct Run {
    Observed observed;
    HdrWriteback result;
};
Run run(Case scenario, x3::temporal::AgxDecode decode, bool resolved, bool sharp, bool output, bool scene_open) {
    Run run; active = &run.observed; active->scenario = scenario;
    HdrDisplaySnapshot snapshot;
    snapshot.valid = true; snapshot.resolved = true; snapshot.width = 999; snapshot.height = 777;
    snapshot.sharpen = .9f; snapshot.agx.exposure[0] = 999.f; snapshot.sharpen_constants.values[0] = 999.f;
    active->output = output ? &snapshot : nullptr;
    IDirect3DDevice9 device;
    IDirect3DTexture9 unresolved_texture, resolved_texture;
    IDirect3DSurface9 target{&unresolved_texture}, main, final_rt;
    IDirect3DPixelShader9 identity, tonemap, sharpen, tonemap_sharpen;
    void* native[43]{};
    native[34] = reinterpret_cast<void*>(&stretch);
    native[41] = reinterpret_cast<void*>(&begin_scene);
    native[42] = reinterpret_cast<void*>(&end_scene);
    HdrPass pass;
    pass.device_ = &device; pass.native_ = native; pass.target_ = &target;
    pass.shader_ = &identity; pass.tonemap_shader_ = &tonemap;
    pass.sharpen_shader_ = &sharpen; pass.tonemap_sharpen_shader_ = &tonemap_sharpen;
    pass.caps_.tonemap = pass.caps_.sharpen = pass.caps_.meter = true;
    pass.caps_.stretch_conversion = S_OK;
    pass.config_.sharpen = sharp ? .75f : 0.f;
    // Deliberately differ from the consumed AgX block: a snapshot must not
    // rebuild it from current config/exposure or silently change decode.
    pass.config_.decode = x3::temporal::AgxDecode::none;
    pass.exposure_.configure({}, ExposureMode::Manual, 1.25f);
    check(x3::temporal::prepare(pass.agx_, .78125f, 17.5f, decode, x3::temporal::AgxLook::golden), "prepare test constants");
    pass.chain_ring_[0] = &target;
    pass.width_ = 17; pass.height_ = 11;
    const auto exact = pass.agx_;
    const float ev = pass.exposure_.ev();
    const unsigned steps = pass.exposure_.steps();
    bool write = true;
    IDirect3DSurface9* main_arg = &main;
    IDirect3DSurface9* final_arg = &final_rt;
    switch (scenario) {
    case Case::SharpenFallback: case Case::IdentityFallback: case Case::BothFallback:
        pass.set_fault(HdrFault::TonemapDraw, 1); break;
    case Case::Restore: pass.set_fault(HdrFault::Restore, 1); break;
    case Case::Draw: pass.set_fault(HdrFault::Draw, 1); break;
    case Case::Lost: pass.set_fault(HdrFault::Lost, 1); break;
    case Case::Stretch: pass.set_fault(HdrFault::Stretch, 1); break;
    case Case::Rebind: write = false; break;
    case Case::Bind: write = false; pass.set_fault(HdrFault::Bind, 1); break;
    case Case::NoMain: main_arg = nullptr; break;
    case Case::NoFinal: final_arg = nullptr; break;
    case Case::NoTarget: pass.target_ = nullptr; break;
    case Case::NoShader: pass.shader_ = nullptr; break;
    case Case::NoContainer: target.container_hr = E_FAIL; break;
    case Case::NullContainer: target.texture = nullptr; break;
    case Case::Meter: pass.set_fault(HdrFault::Meter, 1); break;
    case Case::Identity: pass.caps_.tonemap = false; break;
    case Case::SharpenDisabled: pass.sharpen_failures_ = 3; break;
    case Case::TonemapDisabled: pass.tonemap_failures_ = 3; break;
    case Case::Manual: pass.config_.exposure = ExposureMode::Manual; break;
    case Case::NoRing: pass.chain_ring_[0] = nullptr; break;
    default: break;
    }
    IDirect3DTexture9* source = resolved ? &resolved_texture : nullptr;
    if (output) run.result = pass.write_back(main_arg, final_arg, scene_open, write, false, source, &snapshot);
    else run.result = pass.write_back(main_arg, final_arg, scene_open, write, false, source); // default-null ABI
    check(pass.exposure_.ev() == ev && pass.exposure_.steps() == steps, "no exposure recalculation/adaptation");
    check(pass.chain_slot_ == 0 && !pass.chain_pending_[0] && !pass.chain_pending_[1], "no extra ring manipulation");
    check(!std::memcmp(&pass.agx_, &exact, sizeof(exact)), "consumed constants unchanged");
    check(active->meters <= 1 && active->early_valid == 0, "one meter request and publish after all callbacks");
    check(unresolved_texture.refs == 1 && resolved_texture.refs == 1, "temporary texture references balanced");
    if (output) {
        const bool valid = scenario == Case::Normal || scenario == Case::SharpenFallback || scenario == Case::Meter
            || scenario == Case::SharpenDisabled || scenario == Case::Manual || scenario == Case::NoRing || scenario == Case::MutatedConfig;
        check(snapshot.valid == valid, "scenario validity");
        if (!valid) check(cleared(snapshot), "invalid snapshot clears every field");
        else {
            check(!std::memcmp(&snapshot.agx, &active->constants, sizeof(exact))
                && !std::memcmp(&snapshot.agx, &exact, sizeof(exact)), "bit-exact draw constants");
            check(snapshot.decode == decode, "decode describes consumed constants");
            check(snapshot.resolved == resolved && snapshot.width == 17 && snapshot.height == 11,
                "effective source and dimensions");
            check(active->sampled == (resolved ? &resolved_texture : &unresolved_texture), "matching sampled texture");
            const bool sharpened = sharp && resolved && scenario != Case::SharpenFallback && scenario != Case::SharpenDisabled;
            check(snapshot.sharpen == (sharpened ? .75f : 0.f), "effective original sharpen strength");
            if (sharpened) check(!std::memcmp(&snapshot.sharpen_constants, &active->sharpen, sizeof(active->sharpen)), "exact c23");
            else check(snapshot.sharpen_constants.values[0] == 1.f && snapshot.sharpen_constants.values[1] == 0.f,
                "unsharpened c23 unused/default");
        }
    }
    if (scenario == Case::SharpenFallback)
        check(run.result.sharpen_fallback && run.result.tonemap && active->draws == 2 && active->meters == 1,
            "unsharpened AgX retry submits meter once");
    if (scenario == Case::IdentityFallback)
        check(run.result.fallback && !run.result.tonemap && active->draws == 2 && active->meters == 1,
            "identity retry submits meter once");
    if (scenario == Case::BothFallback)
        check(run.result.sharpen_fallback && run.result.fallback && !run.result.tonemap
            && active->draws == 3 && active->meters == 1, "both fallbacks still submit meter once");
    if (scenario == Case::Manual || scenario == Case::NoRing) check(active->meters == 0, "manual/missing-ring skips meter");
    if (scenario == Case::Meter) check(run.result.meter == E_FAIL && run.result.draw == S_OK, "meter failure does not spoil display snapshot");
    active->output = nullptr; active->sampled = nullptr; // no stack pointer escapes the fixture
    return run;
}

int main() {
    unsigned scenarios = 0;
    auto paired = [&](Case scenario, x3::temporal::AgxDecode decode, bool resolved, bool sharp, bool scene_open = false) {
        ++scenarios;
        const Run on = run(scenario, decode, resolved, sharp, true, scene_open);
        const Run off = run(scenario, decode, resolved, sharp, false, scene_open);
        check(same(on.result, off.result) && on.observed.calls == off.observed.calls
            && on.observed.meters == off.observed.meters && on.observed.draws == off.observed.draws,
            "optional output preserves default-null results and device work");
    };
    for (auto decode : {x3::temporal::AgxDecode::gamma22, x3::temporal::AgxDecode::srgb, x3::temporal::AgxDecode::none}) {
        for (bool resolved : {false, true}) for (bool sharp : {false, true}) for (bool scene_open : {false, true})
            paired(Case::Normal, decode, resolved, sharp, scene_open);
        paired(Case::SharpenFallback, decode, true, true);
        paired(Case::IdentityFallback, decode, false, false);
    }
    for (unsigned i = unsigned(Case::Restore); i <= unsigned(Case::BothFallback); ++i) {
        const auto scenario = Case(i);
        const bool resolved = scenario != Case::NoContainer && scenario != Case::NullContainer;
        paired(scenario, x3::temporal::AgxDecode::srgb, resolved, true);
    }
    std::printf("hdr_display_snapshot scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
