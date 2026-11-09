// The bullet re-shade (included by motion_output.cpp inside namespace x3m; FL only; X3M_WEAPON_EFFECTS=reshade;
// docs/reverse-engineering/fl/bullet-draw-batch.md section 6). The batch call 0x097af8 is claimed with a scope stub
// that forwards every submit and raises x3m_bullet_scope around it (weapon_effects_hook.cpp). A draw inside that scope
// whose bound pixel shader is one of the game's INSTANCE_BULLETS programs (shadow_.ps_bullet: recognised by the proxy's
// hash of the bytes the game passed to CreatePixelShader, bullet_reshade_core.h) is drawn with our ps_2_0 program
// (src/effects/weapon_bullet_reshade_ps.hlsl) and c31 = (gain, edge, core, normaliser), through the one device
// sequence of bullet_reshade_route.h:
// - the additive route (screen_emission_additive under HDR, prepare_screen_additive) takes the bullet draws: the
//   re-shade takes precedence over the route's own program, keeps the route's conversion (DESTBLEND ONE, applied by
//   the sequence) with the route's emission gain folded into c31, its alpha states, footprint and single-copy drop,
//   and with the route's far-flag targets bound draws our flag variant (weapon_bullet_reshade_flag_ps.hlsl, the
//   route's flag law); finish_bullet_reshade puts back the game's program, the application's c31 and DESTBLEND
//   INVSRCCOLOR, finish_screen_additive the rest of the route;
// - otherwise (the route off, refused, or no HDR) under the game's ONE / INVSRCCOLOR (prepare_bullet_reshade, last in
//   before_draw).
// The game's program comes back from the shadowed binding, c31 from the SetPixelShaderConstantF hook's shadow (read at
// resync; zero when never written). Documented IDirect3DDevice9 calls only, through the native slots (the shadows keep
// the application's values); no allocation per draw. On every 60th frame (a 32-bit count) and capture frames the
// sequence alone, before and after the draw, is QPC-timed for the bullet_reshade_frame row.
namespace {
constexpr DWORD bullet_reshade_program[] = {
#include "../renderer/weapon_bullet_reshade_pixel_program_inc.h"
};
constexpr DWORD bullet_reshade_flag_program[] = {
#include "../renderer/weapon_bullet_reshade_flag_pixel_program_inc.h"
};
static_assert(sizeof bullet_reshade_program / sizeof bullet_reshade_program[0] > 2 &&
                  bullet_reshade_program[0] == 0xffff0200u && bullet_reshade_flag_program[0] == 0xffff0200u,
              "the embedded re-shade programs are ps_2_0");
} // namespace
// Created once per device at the first game bullet program's registration (register_pixel_shader); a refusal of the
// plain program logs one row and leaves the bullet draws vanilla for the device's life (the flag variant missing only
// refuses the far flag for re-shaded draws, bolt_flag reason 1).
void MotionOutput::bullet_reshade_create() noexcept {
    if (bullet_reshade_ps_ || bullet_reshade_create_failed_) return;
    IDirect3DPixelShader9 *ps = nullptr, *flag = nullptr;
    const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, bullet_reshade_program, &ps);
    const HRESULT flag_hr = native<CreatePsFn>(CreatePixelShader)(device_, bullet_reshade_flag_program, &flag);
    if (SUCCEEDED(hr) && ps)
        bullet_reshade_ps_ = ps;
    else {
        release(ps);
        bullet_reshade_create_failed_ = true;
    }
    if (SUCCEEDED(flag_hr) && flag && bullet_reshade_ps_)
        bullet_reshade_flag_ps_ = flag;
    else
        release(flag);
    weapon_effects::reshade::constants(projectile_tune_[weapon_effects::projectile_family::fighter_tracer],
                                       bullet_reshade_constants_);
    log("bullet_reshade_program device=%llu frame=%llu created=%u result=%08lx flag_created=%u flag_result=%08lx words=%u "
        "flag_words=%u gain=%.3g edge=%.3g core=%.3g additive_gain=%.3g",
        id_, frame_, unsigned(bullet_reshade_ps_ != nullptr), hr, unsigned(bullet_reshade_flag_ps_ != nullptr), flag_hr,
        unsigned(sizeof bullet_reshade_program / sizeof bullet_reshade_program[0]),
        unsigned(sizeof bullet_reshade_flag_program / sizeof bullet_reshade_flag_program[0]),
        double(bullet_reshade_constants_[0]), double(bullet_reshade_constants_[1]), double(bullet_reshade_constants_[2]),
        double(screen_additive_gain_));
}
// The draw is a re-shade candidate: a game bullet program bound inside the batch submit, our program ready.
bool MotionOutput::bullet_reshade_ready() const noexcept {
    return shadow_.ps_bullet && x3m_bullet_scope && bullet_reshade_ps_ && !bullet_reshade_restore_failed_;
}
// The sequence's begin for this draw (convert: the additive route's DESTBLEND ONE and gain; flag: its far-flag targets
// are bound, so the flag variant). false: nothing applied (a failed step undone; an undo that failed stops the
// re-shade for the device with one row).
bool MotionOutput::begin_bullet_reshade(MotionRoute& route, bool convert, bool flag) noexcept {
    namespace rs = weapon_effects::reshade;
    const bool timed = capture_ || bullet_reshade_frames_ % 60u == 0u;
    LARGE_INTEGER a{}, b{};
    if (timed) QueryPerformanceCounter(&a);
    auto ops = rs::calls(
        [this](DWORD v) { return direct_call<SetRenderStateFn>(SetRenderState, D3DRS_DESTBLEND, v); },
        [this](const float* v) { return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, rs::constant_register, v, 1u); },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    rs::Plan plan;
    plan.program = flag ? bullet_reshade_flag_ps_ : bullet_reshade_ps_;
    plan.constants = bullet_reshade_constants_;
    plan.gain = screen_additive_gain_;
    plan.convert = convert;
    rs::Applied applied;
    const HRESULT hr = rs::begin(ops, plan, shadow_.ps_c31, applied);
    if (timed) {
        QueryPerformanceCounter(&b);
        bullet_reshade_ticks_ += std::uint64_t(b.QuadPart - a.QuadPart);
    }
    if (FAILED(hr)) {
        ++bullet_reshade_failures_;
        if (applied.rollback_failed) {
            ++counters_.restore_failures;
            if (!bullet_reshade_restore_failed_)
                log("bullet_reshade_restore_failed device=%llu frame=%llu step=begin result=%08lx (the re-shade stops for "
                    "this device)",
                    id_, frame_, hr);
            bullet_reshade_restore_failed_ = true;
            invalidate_render_states();
        }
        return false;
    }
    route.bullet_reshade = true;
    route.bullet_reshade_timed = timed;
    route.bullet_reshade_convert = convert;
    route.bullet_reshade_flag = flag;
    return true;
}
// before_draw, last of the preparations, for a submitted draw no route took while a bullet program is bound.
void MotionOutput::prepare_bullet_reshade(MotionRoute& route) noexcept {
    if (!x3m_bullet_scope) {
        ++bullet_reshade_outside_; // a bullet program outside the batch submit (the traversal path): left vanilla
        return;
    }
    if (!bullet_reshade_ps_ || bullet_reshade_restore_failed_) {
        ++bullet_reshade_failures_;
        return;
    }
    begin_bullet_reshade(route, false, false);
}
// The sequence's end alone (the program, c31 and, converted, DESTBLEND back; timed with the begin); the route's flags
// cleared. Used by finish_bullet_reshade and by the additive route's alpha rollback.
HRESULT MotionOutput::end_bullet_reshade(MotionRoute& route) noexcept {
    namespace rs = weapon_effects::reshade;
    LARGE_INTEGER a{}, b{};
    if (route.bullet_reshade_timed) QueryPerformanceCounter(&a);
    auto ops = rs::calls(
        [this](DWORD v) { return direct_call<SetRenderStateFn>(SetRenderState, D3DRS_DESTBLEND, v); },
        [this](const float* v) { return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, rs::constant_register, v, 1u); },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    rs::Applied applied;
    applied.program = applied.c31 = true;
    applied.destblend = route.bullet_reshade_convert;
    const HRESULT hr = rs::end(ops, applied, shadow_.ps, shadow_.ps_c31);
    if (route.bullet_reshade_timed) {
        QueryPerformanceCounter(&b);
        bullet_reshade_ticks_ += std::uint64_t(b.QuadPart - a.QuadPart);
        ++bullet_reshade_timed_draws_;
    }
    route.bullet_reshade = route.bullet_reshade_timed = route.bullet_reshade_convert = route.bullet_reshade_flag = false;
    if (FAILED(hr)) {
        ++bullet_reshade_failures_;
        ++counters_.restore_failures;
        if (!bullet_reshade_restore_failed_)
            log("bullet_reshade_restore_failed device=%llu frame=%llu step=end result=%08lx (the re-shade stops for this "
                "device)",
                id_, frame_, hr);
        bullet_reshade_restore_failed_ = true;
        invalidate_render_states();
    }
    return hr;
}
// after_draw, first: the sequence's end and the draw's counts. A failed restore stops the re-shade for the device.
void MotionOutput::finish_bullet_reshade(MotionRoute& route) noexcept {
    ++bullet_reshade_draws_;
    if (route.bullet_reshade_convert) ++bullet_reshade_additive_;
    if (route.bullet_reshade_flag) ++bullet_reshade_flagged_;
    end_bullet_reshade(route);
}
void MotionOutput::bullet_reshade_take_frame(unsigned* draws, unsigned* failures, unsigned* outside, unsigned* timed,
                                             float* us, unsigned* additive, unsigned* flagged) noexcept {
#if X3M_DEV_TUNING
    if (weapon_effects::projectile_tune::poll(&projectile_tune_[weapon_effects::projectile_family::fighter_tracer]))
        weapon_effects::reshade::constants(projectile_tune_[weapon_effects::projectile_family::fighter_tracer],
                                           bullet_reshade_constants_);
#endif
    ++bullet_reshade_frames_;
    *draws = bullet_reshade_draws_;
    *failures = bullet_reshade_failures_;
    *outside = bullet_reshade_outside_;
    *timed = bullet_reshade_timed_draws_;
    *additive = bullet_reshade_additive_;
    *flagged = bullet_reshade_flagged_;
    const std::uint64_t frequency = engine_qpc_frequency();
    *us = frequency ? float(double(bullet_reshade_ticks_) * 1e6 / double(frequency)) : 0.f;
    bullet_reshade_draws_ = bullet_reshade_failures_ = bullet_reshade_outside_ = bullet_reshade_timed_draws_ = 0;
    bullet_reshade_additive_ = bullet_reshade_flagged_ = 0;
    bullet_reshade_ticks_ = 0;
}
