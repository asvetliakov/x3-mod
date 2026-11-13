// The hull hit re-shade (included by motion_output.cpp inside namespace x3m; X3M_WEAPON_EFFECTS=reshade; edition-
// independent code over an edition table: FL's 24 bodies, none on AP; docs/architecture/hull-hit-look.md, the facts in
// docs/reverse-engineering/fl/hull-hit-draw.md). The engine draws its own impact effect at a hit the shield stage did
// not take (the weapon's Effects row: each element a small body at the contact point, riding the hit object) through
// the material submit, inside the object-trace scope, with the effects / effects2s DEFAULT pair (VS d5e1c75351ed3f04)
// or the standard_lighting(2s) DEFAULT pair (VS 494fe349b8bc12ec). A draw with one of those VS bound that no earlier
// route substituted (the lone classic and lone effects bullet routes, the muzzle route: one claimant), under ADD ONE /
// ONE or ONE / INVSRCCOLOR, whose scope node's body id (one 4-byte validated read) is a listed body of that pair and
// blend, not in the bullet body map nor the muzzle list (hull_reshade_core.h match), is drawn with our program for the
// pair (ps_2_0 weapon_hull_reshade_effects_ps.hlsl, ps_3_0 weapon_hull_reshade_classic_ps.hlsl) and the body's two
// registers (c30..c31 inside the lone bullet block, c192..c193 inside the muzzle block) through the muzzle's device
// sequence (muzzle_reshade_route.h begin / end: constants then program, all or nothing; end: the game's program from
// the shadowed binding, the application's registers from the SetPixelShaderConstantF shadows). No state is written in
// the first step and no far flag: the game's blend, Z and alpha states draw it. A substituted draw skips the
// hull-emission route (its saturating clamp would cap the core below scene white); a refused begin leaves the draw to
// it. A pair's creation failure leaves that pair vanilla for the device (one row); a failed restore stops both.
// Documented IDirect3DDevice9 calls only, through the native slots; no allocation per draw. Every 60th frame and
// capture frames the sequence alone is QPC-timed for the hull_reshade_frame row.
namespace {
constexpr DWORD hull_reshade_effects_program[] = {
#include "../renderer/weapon_hull_reshade_effects_pixel_program_inc.h"
};
constexpr DWORD hull_reshade_classic_program[] = {
#include "../renderer/weapon_hull_reshade_classic_pixel_program_inc.h"
};
static_assert(hull_reshade_effects_program[0] == 0xffff0200u && hull_reshade_classic_program[0] == 0xffff0300u,
              "the effects pair's program is ps_2_0 (its VS is vs_2_0), the classic pair's ps_3_0 (its VS is vs_3_0)");
} // namespace
// Each body's block from the tuning. Called at configure, creation and a tuning reload.
void MotionOutput::hull_reshade_refresh_constants() noexcept {
    namespace hh = weapon_effects::hull_reshade;
    for (unsigned b = 0; b < hh::table_count; ++b) hh::constants(hh::bodies[b], reshade_tune_, hull_reshade_constants_[b]);
}
// Created once per device at the registration of the pair's vertex shader (or its first bind); a refusal logs one row and
// leaves that pair's impact draws vanilla for the device's life.
void MotionOutput::hull_reshade_create(unsigned pair) noexcept {
    namespace hh = weapon_effects::hull_reshade;
    namespace pf = weapon_effects::projectile_family;
    if (pair > 1 || !hull_reshade_requested_ || hull_reshade_ps_[pair] || hull_reshade_create_failed_[pair]) return;
    const auto& t = reshade_tune_;
    // The effects pair reads the bullet re-shade's vs_lone gate and its c29..c31 shadow: without that re-shade (its
    // batch scope stub not armed) the pair stays vanilla for the device, said once.
    if (pair == hh::pair_effects && !bullet_reshade_requested_) {
        hull_reshade_create_failed_[pair] = true;
        log("hull_reshade_program device=%llu frame=%llu pair=effects created=0 reason=bullet_reshade_unarmed bodies=%u",
            id_, frame_, hh::body_count());
        return;
    }
    const DWORD* code = pair == hh::pair_classic ? hull_reshade_classic_program : hull_reshade_effects_program;
    const unsigned words = pair == hh::pair_classic
                               ? unsigned(sizeof hull_reshade_classic_program / sizeof hull_reshade_classic_program[0])
                               : unsigned(sizeof hull_reshade_effects_program / sizeof hull_reshade_effects_program[0]);
    IDirect3DPixelShader9* ps = nullptr;
    const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, code, &ps);
    if (SUCCEEDED(hr) && ps)
        hull_reshade_ps_[pair] = ps;
    else {
        release(ps);
        hull_reshade_create_failed_[pair] = true;
    }
    hull_reshade_refresh_constants();
    log("hull_reshade_program device=%llu frame=%llu pair=%s created=%u reason=%s result=%08lx words=%u bodies=%u "
        "scope=%u "
        "gain=%.3g edge=%.3g core=%.3g cool=%.3g",
        id_, frame_, pair == hh::pair_classic ? "classic" : "effects", unsigned(hull_reshade_ps_[pair] != nullptr),
        hull_reshade_ps_[pair] ? "ok" : "create_failed", hr,
        words, hh::body_count(), unsigned(object_trace::scope_available()),
        double(pf::clamp_key(t.hh_gain, pf::key_hh_gain)), double(pf::clamp_key(t.hh_edge, pf::key_hh_edge)),
        double(pf::clamp_key(t.hh_core, pf::key_hh_core)), double(pf::clamp_key(t.hh_cool, pf::key_hh_cool)));
}
// The draw is an impact body's (hull_reshade_core.h match, the fixture's decision too). Called only while one of the two
// VS is bound (shadow_.vs_lone / vs_muzzle) for a draw no earlier route substituted. Blend-off and non-additive draws end
// at the blend shadow; the scope lookup (TLS, no memory read) and the one 4-byte node read run for the candidates only.
bool MotionOutput::hull_reshade_match(MotionRoute& route) noexcept {
    namespace hh = weapon_effects::hull_reshade;
    if (hull_reshade_restore_failed_ || !state_known(3) || !blend_known(0) || !blend_known(1) || !blend_known(2))
        return false;
    const hh::Match m = hh::match(
        shadow_.vs_hash, shadow_.states[3] != 0, shadow_.composition_blend[0], shadow_.composition_blend[1],
        shadow_.composition_blend[2], shadow_.ps_hash, shadow_.vs_lone && hull_reshade_ps_[hh::pair_effects],
        shadow_.vs_muzzle && hull_reshade_ps_[hh::pair_classic],
        [](std::uint32_t* id) {
            std::uintptr_t descriptor = 0, node = 0;
            return object_trace::scope_node(&descriptor, &node) && node &&
                   engine_memory::read(node + weapon_effects::core::layout::node_body, id, sizeof *id);
        },
        [](std::uint32_t id) {
            bool final = true;
            return weapon_effects::bullet_body_layout(id, &final) != nullptr;
        });
    if (m.candidate) ++hull_reshade_frame_.candidates;
    if (m.body >= hh::table_count) return false;
    ++hull_reshade_frame_.matched;
    route.hull_body = std::uint8_t(m.body);
    route.hull_pair = m.pair;
    return true;
}
// The sequence's begin for a matched draw (before_draw, before the hull-emission route). false: nothing applied (a failed
// step undone; an undo that failed stops the re-shade for the device with one row), and the draw goes on to the hull
// route.
bool MotionOutput::begin_hull_reshade(MotionRoute& route) noexcept {
    namespace hh = weapon_effects::hull_reshade;
    namespace mz = weapon_effects::muzzle_reshade;
    const bool classic = route.hull_pair == hh::pair_classic;
    const UINT reg = classic ? hh::classic_register : hh::effects_register;
    const float* app = classic ? shadow_.ps_c192 + hh::classic_shadow_offset : shadow_.ps_c29 + hh::effects_shadow_offset;
    const bool timed = capture_ || hull_reshade_frames_ % 60u == 0u;
    LARGE_INTEGER a{}, b{};
    if (timed) QueryPerformanceCounter(&a);
    auto ops = mz::calls(
        [this, reg](const float* v) {
            return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, reg, v, hh::constant_count);
        },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    mz::Applied applied;
    const HRESULT hr = mz::begin(ops, hull_reshade_ps_[route.hull_pair], hull_reshade_constants_[route.hull_body], app,
                                 applied);
    if (timed) {
        QueryPerformanceCounter(&b);
        hull_reshade_ticks_ += std::uint64_t(b.QuadPart - a.QuadPart);
    }
    if (FAILED(hr)) {
        ++hull_reshade_frame_.failures;
        if (applied.rollback_failed) {
            ++counters_.restore_failures;
            if (!hull_reshade_restore_failed_)
                log("hull_reshade_restore_failed device=%llu frame=%llu step=begin result=%08lx (the re-shade stops for "
                    "this device)",
                    id_, frame_, hr);
            hull_reshade_restore_failed_ = true;
        }
        return false;
    }
    route.hull_reshade = true;
    route.hull_timed = timed;
    return true;
}
// after_draw, beside the other re-shades' ends: the game's program and the application's two registers back (timed with
// the begin), then the draw's counts. A failed restore stops the re-shade for the device.
void MotionOutput::finish_hull_reshade(MotionRoute& route) noexcept {
    namespace hh = weapon_effects::hull_reshade;
    namespace mz = weapon_effects::muzzle_reshade;
    const bool classic = route.hull_pair == hh::pair_classic;
    const UINT reg = classic ? hh::classic_register : hh::effects_register;
    const float* app = classic ? shadow_.ps_c192 + hh::classic_shadow_offset : shadow_.ps_c29 + hh::effects_shadow_offset;
    LARGE_INTEGER a{}, b{};
    if (route.hull_timed) QueryPerformanceCounter(&a);
    auto ops = mz::calls(
        [this, reg](const float* v) {
            return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, reg, v, hh::constant_count);
        },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    mz::Applied applied;
    applied.program = applied.constants = true;
    const HRESULT hr = mz::end(ops, applied, shadow_.ps, app);
    if (route.hull_timed) {
        QueryPerformanceCounter(&b);
        hull_reshade_ticks_ += std::uint64_t(b.QuadPart - a.QuadPart);
        ++hull_reshade_frame_.timed;
    }
    const unsigned body = route.hull_body < hh::table_count ? route.hull_body : 0u;
    ++(classic ? hull_reshade_frame_.classic : hull_reshade_frame_.effects);
    if (hh::bodies[body].shared) ++hull_reshade_frame_.shared;
    DWORD zwrite = 0;
    if (SUCCEEDED(render_state(D3DRS_ZWRITEENABLE, &zwrite)) && zwrite) ++hull_reshade_frame_.zwrite_on;
    const unsigned lane = classic ? 1u : 0u;
    if ((shadow_.vs_alpha_known & (1u << lane)) && shadow_.vs_alpha[lane] < 1.f) ++hull_reshade_frame_.cool_active;
    if (hull_reshade_body_draws_[body] < 0xffffu) ++hull_reshade_body_draws_[body];
    hull_reshade_body_blend_[body] = std::uint8_t(shadow_.composition_blend[1]);
    route.hull_reshade = route.hull_timed = false;
    if (FAILED(hr)) {
        ++hull_reshade_frame_.failures;
        ++counters_.restore_failures;
        if (!hull_reshade_restore_failed_)
            log("hull_reshade_restore_failed device=%llu frame=%llu step=end result=%08lx (the re-shade stops for this "
                "device)",
                id_, frame_, hr);
        hull_reshade_restore_failed_ = true;
        invalidate_render_states();
    }
}
// The frame's counts for the period row (reset here), and one hull_reshade_body row for each of the first 16 bodies
// drawn on this device, at the end of the first frame that drew it: its pair, the draw's DESTBLEND (2 ONE, 4
// INVSRCCOLOR) and that frame's draws.
void MotionOutput::hull_reshade_take_frame(weapon_effects::hull_reshade::Frame* out) noexcept {
    namespace hh = weapon_effects::hull_reshade;
    ++hull_reshade_frames_;
    *out = hull_reshade_frame_;
    const std::uint64_t frequency = engine_qpc_frequency();
    out->us = frequency ? float(double(hull_reshade_ticks_) * 1e6 / double(frequency)) : 0.f;
    hull_reshade_frame_ = hh::Frame{};
    hull_reshade_ticks_ = 0;
    if (!out->effects && !out->classic) return;
    for (unsigned b = 0; b < hh::table_count; ++b) {
        const unsigned draws = hull_reshade_body_draws_[b];
        hull_reshade_body_draws_[b] = 0;
        if (!draws || (hull_reshade_body_logged_ & (1u << b)) || hull_reshade_body_rows_ >= 16u) continue;
        hull_reshade_body_logged_ |= 1u << b;
        ++hull_reshade_body_rows_;
        log("hull_reshade_body device=%llu frame=%llu body=%lu pair=%s blend=%u shared=%u draws=%u", id_, frame_,
            static_cast<unsigned long>(hh::bodies[b].id),
            hh::bodies[b].pair == hh::pair_classic ? "classic" : "effects", unsigned(hull_reshade_body_blend_[b]),
            unsigned(hh::bodies[b].shared), draws);
    }
}
