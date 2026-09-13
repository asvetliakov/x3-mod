// Host-only control-flow qualification, not GPU/state/Windows-ABI evidence.
// The paired unittest extracts the unmodified production write_back into a
// temporary *_inc.h; D3D work below has explicitly scripted return values.
#include <array>
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
template<class T> void drop(T*& value) noexcept { if (T* held = value) { value = nullptr; held->Release(); } }
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

// Exact object bytes (except the exchanged target pointer on success) witness
// that meter slots/pending flags, host arrays, exposure, dimensions and programs
// remain unchanged. No representation is written back into the class.
using PassBytes = std::array<unsigned char, sizeof(HdrPass)>;
PassBytes pass_bytes(const HdrPass& pass) {
    PassBytes result{}; std::memcpy(result.data(), &pass, sizeof pass); return result;
}
struct ExchangeFixture {
    IDirect3DDevice9 device, other_device;
    IDirect3DTexture9 old_texture, candidate_texture;
    IDirect3DSurface9 old_target{&old_texture}, candidate{&candidate_texture}, other_level;
    IDirect3DSurface9 chain, ring[2], readback[2];
    IDirect3DPixelShader9 identity, tonemap, meter0, reduce;
    IDirect3DVertexShader9 quad;
    IDirect3DVertexDeclaration9 declaration;
    void* native[1]{}; // exchange must not dispatch any GPU method
    HdrPass pass;
    ExchangeFixture() {
        old_target.device = candidate.device = &device;
        old_texture.device = candidate_texture.device = &device;
        old_texture.level = &old_target; candidate_texture.level = &candidate;
        pass.device_ = &device; pass.native_ = native; pass.caps_.enabled = true;
        pass.caps_.tonemap = pass.caps_.meter = true;
        pass.target_ = &old_target; pass.width_ = pass.last_width_ = 17;
        pass.height_ = pass.last_height_ = 11;
        pass.shader_ = &identity; pass.tonemap_shader_ = &tonemap;
        pass.meter_level0_shader_ = &meter0; pass.meter_reduce_shader_ = &reduce;
        pass.quad_vs_ = &quad; pass.quad_declaration_ = &declaration;
        pass.chain_[0] = &chain; pass.chain_width_[0] = 5; pass.chain_height_[0] = 3;
        pass.chain_count_ = 1; pass.tile_width_ = 2; pass.tile_height_ = 1;
        pass.chain_texel_bytes_ = 8; pass.chain_slot_ = 1;
        for (unsigned i = 0; i < 2; ++i) {
            pass.chain_ring_[i] = &ring[i]; pass.chain_readback_[i] = &readback[i];
            pass.chain_pending_[i] = true;
        }
        pass.tile_mean_ = std::make_unique<float[]>(2);
        pass.tile_mean_[0] = 0.25f; pass.tile_mean_[1] = 0.75f;
        pass.tile_capacity_ = 2; pass.latch_ticks_ = 123456;
        MeterStatistics meter{}; meter.tiles = meter.lit = 1; meter.neutral = false;
        meter.lit_fraction = meter.lit_weight = 1.f; meter.lit_median_log = meter.avg_log_l = -2.f;
        pass.exposure_.step(meter, 0.125f);
    }
    bool non_target_bytes_equal(const PassBytes& before) {
        auto after = pass_bytes(pass);
        const auto offset = reinterpret_cast<const unsigned char*>(&pass.target_)
            - reinterpret_cast<const unsigned char*>(&pass);
        std::memcpy(after.data() + offset, before.data() + offset, sizeof(pass.target_));
        return after == before;
    }
    void balanced_queries(unsigned candidate_refs = 1) {
        check(device.refs == 1 && other_device.refs == 1, "exchange balances every temporary device query");
        check(old_texture.refs == 1 && candidate_texture.refs == 1, "exchange retains no texture interface");
        check(old_target.refs == 1 && candidate.refs == candidate_refs && other_level.refs == 1,
            "exchange adds/releases no owning surface reference");
    }
};
enum class ExchangeCase { Good, Null, Same, Detached, Disabled, NoTarget, Width, Height,
    Format, Pool, Usage, ExtraUsage, Msaa, Quality, Type, DescFail, OwnerFail,
    OwnerFailedOutput, WrongDevice, NullOwner, ContainerFail, ContainerFailedOutput,
    NullContainer, Mips, TextureDescFail, TextureDescMismatch, TextureOwnerFail,
    TextureOwnerFailedOutput, TextureWrongDevice, TextureNullOwner, LevelFail,
    LevelFailedOutput, NullLevel, WrongLevel };
unsigned exchange_cases() {
    unsigned scenarios = 0;
    for (unsigned i = 0; i <= unsigned(ExchangeCase::WrongLevel); ++i) {
        ++scenarios; ExchangeFixture f;
        auto which = ExchangeCase(i);
        IDirect3DSurface9* candidate = &f.candidate;
        switch (which) {
        case ExchangeCase::Good: break;
        case ExchangeCase::Null: candidate = nullptr; break;
        case ExchangeCase::Same: candidate = &f.old_target; break;
        case ExchangeCase::Detached: f.pass.device_ = nullptr; break;
        case ExchangeCase::Disabled: f.pass.caps_.enabled = false; break;
        case ExchangeCase::NoTarget: f.pass.target_ = nullptr; break;
        case ExchangeCase::Width: ++f.candidate.desc.Width; break;
        case ExchangeCase::Height: ++f.candidate.desc.Height; break;
        case ExchangeCase::Format: f.candidate.desc.Format = D3DFMT_A8R8G8B8; break;
        case ExchangeCase::Pool: f.candidate.desc.Pool = D3DPOOL_SYSTEMMEM; break;
        case ExchangeCase::Usage: f.candidate.desc.Usage = 0; break;
        case ExchangeCase::ExtraUsage: f.candidate.desc.Usage |= D3DUSAGE_DYNAMIC; break;
        case ExchangeCase::Msaa: f.candidate.desc.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES; break;
        case ExchangeCase::Quality: f.candidate.desc.MultiSampleQuality = 1; break;
        case ExchangeCase::Type: f.candidate.desc.Type = D3DRTYPE_TEXTURE; break;
        case ExchangeCase::DescFail: f.candidate.desc_hr = E_FAIL; break;
        case ExchangeCase::OwnerFail: f.candidate.device_hr = E_FAIL; break;
        case ExchangeCase::OwnerFailedOutput: f.candidate.device_hr = E_FAIL; f.candidate.output_on_failure = true; break;
        case ExchangeCase::WrongDevice: f.candidate.device = &f.other_device; break;
        case ExchangeCase::NullOwner: f.candidate.device = nullptr; break;
        case ExchangeCase::ContainerFail: f.candidate.container_hr = E_NOINTERFACE; break;
        case ExchangeCase::ContainerFailedOutput: f.candidate.container_hr = E_FAIL; f.candidate.output_on_failure = true; break;
        case ExchangeCase::NullContainer: f.candidate.texture = nullptr; break;
        case ExchangeCase::Mips: f.candidate_texture.levels = 2; break;
        case ExchangeCase::TextureDescFail: f.candidate_texture.desc_hr = E_FAIL; break;
        case ExchangeCase::TextureDescMismatch: ++f.candidate_texture.desc.Width; break;
        case ExchangeCase::TextureOwnerFail: f.candidate_texture.device_hr = E_FAIL; break;
        case ExchangeCase::TextureOwnerFailedOutput: f.candidate_texture.device_hr = E_FAIL; f.candidate_texture.output_on_failure = true; break;
        case ExchangeCase::TextureWrongDevice: f.candidate_texture.device = &f.other_device; break;
        case ExchangeCase::TextureNullOwner: f.candidate_texture.device = nullptr; break;
        case ExchangeCase::LevelFail: f.candidate_texture.level_hr = E_FAIL; break;
        case ExchangeCase::LevelFailedOutput: f.candidate_texture.level_hr = E_FAIL; f.candidate_texture.output_on_failure = true; break;
        case ExchangeCase::NullLevel: f.candidate_texture.level = nullptr; break;
        case ExchangeCase::WrongLevel: f.candidate_texture.level = &f.other_level; break;
        }
        auto* old_candidate = candidate; auto* old_target = f.pass.target();
        const auto before = pass_bytes(f.pass); const unsigned references = f.pass.references();
        const HRESULT result = f.pass.exchange_target(candidate);
        check((which == ExchangeCase::Good) ? result == S_OK : FAILED(result), "exchange verdict");
        if (which == ExchangeCase::Good) {
            check(f.pass.target() == old_candidate && candidate == old_target, "exchange transfers both owned references");
            check(f.non_target_bytes_equal(before), "exchange preserves all non-target owner/meter/exposure bytes");
        } else check(f.pass.target() == old_target && candidate == old_candidate && pass_bytes(f.pass) == before,
            "rejected exchange changes no owner or candidate state");
        check(f.pass.references() == references, "exchange preserves persistent reference accounting");
        f.balanced_queries();
    }
    // COM aliases may use distinct interface pointers. Reject a second owned
    // reference to the current target, but accept canonical device/level aliases.
    for (unsigned alias_case = 0; alias_case < 4; ++alias_case) {
        ++scenarios; ExchangeFixture f;
        IDirect3DSurface9* candidate = &f.candidate;
        if (alias_case == 0) {
            f.candidate.identity = &f.old_target;
            f.old_target.AddRef(); // caller really owns the second alias reference
        } else if (alias_case == 1) {
            f.other_device.identity = &f.device;
            f.candidate.device = f.candidate_texture.device = &f.other_device;
        } else if (alias_case == 2) {
            f.other_level.identity = &f.candidate;
            f.candidate_texture.level = &f.other_level;
        } else {
            f.other_level.identity = &f.candidate;
            f.other_level.texture = &f.candidate_texture;
            f.other_level.device = &f.device;
            candidate = &f.other_level;
        }
        auto* supplied = candidate;
        const auto before = pass_bytes(f.pass);
        const HRESULT result = f.pass.exchange_target(candidate);
        if (alias_case == 0) {
            check(result == E_INVALIDARG && candidate == supplied && pass_bytes(f.pass) == before,
                "canonical current-target alias cannot bypass distinctness");
            check(f.old_target.refs == 2, "identity refusal preserves both alias owning references");
            candidate->Release(); // remove only the caller's alias ref
        } else check(result == S_OK && f.pass.target() == supplied && candidate == &f.old_target
                && f.non_target_bytes_equal(before), "canonical device and surface aliases exchange successfully");
        f.balanced_queries();
    }
    // Exercise both sides of all four identity comparisons, including late
    // failure after container/device/level references have already been acquired.
    for (unsigned position = 0; position < 8; ++position) for (unsigned fault = 0; fault < 3; ++fault) {
        ++scenarios; ExchangeFixture f;
        IUnknown* object = nullptr; unsigned occurrence = 1;
        if (position == 0) object = &f.candidate;
        else if (position == 1) object = &f.old_target;
        else if (position < 6) { object = &f.device; occurrence = position - 1; }
        else { object = &f.candidate; occurrence = position - 4; }
        object->qi_fault_at = occurrence;
        if (fault < 2) object->qi_hr = E_FAIL;
        object->qi_output_on_failure = fault == 1;
        object->qi_null = fault == 2;
        IDirect3DSurface9* candidate = &f.candidate;
        const auto before = pass_bytes(f.pass);
        const HRESULT result = f.pass.exchange_target(candidate);
        check(result == (fault == 2 ? E_NOINTERFACE : E_FAIL), "identity query failure remains distinct from inequality");
        check(candidate == &f.candidate && pass_bytes(f.pass) == before, "failed identity query never mutates ownership or meter state");
        f.balanced_queries();
    }
    // Rotate the same two owning references repeatedly, then exercise actual
    // class Reset/shutdown cleanup. The old surface remains the caller's owner.
    ++scenarios; ExchangeFixture f;
    IDirect3DSurface9* candidate = &f.candidate;
    const auto before = pass_bytes(f.pass);
    for (unsigned i = 0; i < 32; ++i) check(f.pass.exchange_target(candidate) == S_OK, "repeated pool rotation");
    check(pass_bytes(f.pass) == before, "even rotations return exact owner state");
    f.balanced_queries();
    check(f.pass.exchange_target(candidate) == S_OK, "final candidate adoption");
    const auto steps = f.pass.exposure().steps(); const auto ev = f.pass.exposure().ev();
    candidate->Release(); candidate = nullptr; // caller retires its old target before native Reset
    f.pass.before_reset();
    check(!f.pass.target() && !f.pass.width() && !f.pass.height(), "Reset retires exchanged target");
    check(f.candidate.refs == 0 && f.old_target.refs == 0 && f.chain.refs == 0
        && !f.ring[0].refs && !f.ring[1].refs && !f.readback[0].refs && !f.readback[1].refs,
        "Reset releases all owned DEFAULT resources without a duplicate ref");
    check(f.pass.exposure().steps() == steps && f.pass.exposure().ev() == ev,
        "existing Reset exposure policy remains unchanged");
    f.pass.shutdown();
    check(!f.pass.device_ && !f.pass.native_ && !f.pass.enabled() && f.pass.references() == 0
        && f.identity.refs == 0 && f.tonemap.refs == 0 && f.meter0.refs == 0 && f.reduce.refs == 0
        && f.quad.refs == 0 && f.declaration.refs == 0 && f.pass.exposure().steps() == 0,
        "shutdown retires remaining programs and exposure state");
    return scenarios;
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
    const auto exchanges = exchange_cases();
    std::printf("hdr_display_snapshot scenarios=%u exchange_scenarios=%u checks=%u failures=%u\n", scenarios, exchanges, checks, failures);
    return failures ? 1 : 0;
}
