// The muzzle flash re-shade (included by motion_output.cpp inside namespace x3m; FL only; X3M_WEAPON_EFFECTS=reshade;
// docs/architecture/muzzle-flash-look.md, docs/reverse-engineering/fl/muzzle-flash-draw.md). The engine draws its own
// flash (a CUT scene's classic body, e.g. scene 04251, body 445: 50 textured quads, ONE / ONE, Z test, no Z write;
// the seven bodies of muzzle_reshade_core.h) through the material submit, inside
// the FL object-trace scope (object_trace_fl.cpp, seam 0x0e7142 -> 0x0e0f30). A draw whose bound vertex shader is the
// standard_lighting DEFAULT one (shadow_.vs_muzzle, the ABI our program reads), whose blend is ADD ONE / ONE and whose
// scope node's body id (node +0x144, one 4-byte validated read) is in muzzle_reshade_core.h's list is drawn with our
// ps_3_0 program (src/effects/weapon_muzzle_reshade_ps.hlsl) and c192..c195 through the one device sequence of
// muzzle_reshade_route.h. c192 and c194.xy are the draw's: the camera minus the node's world origin, the lit radius at
// the node's world scale, and gain and core over the fan's overlap at the view ray's angle to the flash axis, all from
// that VS's own g_mWorld (c28..c30) and g_mViewInverse (c34..c36) rows as the application wrote them (the
// SetVertexShaderConstantF shadow), so the origin is in the space of the VS's TEXCOORD1. The draw stays under the
// game's ONE / ONE: before_draw runs the begin before the hull-emission route and skips that route when the begin
// succeeded (its saturating clamp would cap the core below scene white); a refused begin leaves the draw to it. The
// pass is off after a creation or restore failure (no match). The game's program comes back from the shadowed binding,
// c192..c195 from the SetPixelShaderConstantF hook's shadow (read at resync; zero when never written). Documented
// IDirect3DDevice9 calls only, through the native slots; no allocation per draw. Every 60th frame (a 32-bit count) and
// capture frames the sequence alone, before and after the draw, is QPC-timed for the muzzle_reshade_frame row.
namespace {
constexpr DWORD muzzle_reshade_program[] = {
#include "../renderer/weapon_muzzle_reshade_pixel_program_inc.h"
};
static_assert(sizeof muzzle_reshade_program / sizeof muzzle_reshade_program[0] > 2 &&
                  muzzle_reshade_program[0] == 0xffff0300u,
              "the embedded muzzle re-shade program is ps_3_0");
} // namespace
// Each body's c193..c195 from the tuning (c192 is per draw). Called at configure, creation and a tuning reload.
void MotionOutput::muzzle_reshade_refresh_constants() noexcept {
    namespace mz = weapon_effects::muzzle_reshade;
    const auto& tune = reshade_tune_;
    for (unsigned b = 0; b < mz::body_count; ++b) mz::constants(mz::bodies[b], tune, muzzle_reshade_constants_[b]);
}
// Created once per device at the registration of the standard_lighting DEFAULT vertex shader (or its first bind); a
// refusal logs one row and leaves the flashes vanilla for the device's life.
void MotionOutput::muzzle_reshade_create() noexcept {
    if (muzzle_reshade_ps_ || muzzle_reshade_create_failed_) return;
    IDirect3DPixelShader9* ps = nullptr;
    const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, muzzle_reshade_program, &ps);
    if (SUCCEEDED(hr) && ps)
        muzzle_reshade_ps_ = ps;
    else {
        release(ps);
        muzzle_reshade_create_failed_ = true;
    }
    muzzle_reshade_refresh_constants();
    const auto& tune = reshade_tune_;
    namespace pf = weapon_effects::projectile_family;
    log("muzzle_reshade_program device=%llu frame=%llu created=%u result=%08lx words=%u bodies=%u scope=%u size=%.3g "
        "core=%.3g gain=%.3g smooth=%.3g",
        id_, frame_, unsigned(muzzle_reshade_ps_ != nullptr), hr,
        unsigned(sizeof muzzle_reshade_program / sizeof muzzle_reshade_program[0]),
        weapon_effects::muzzle_reshade::body_count, unsigned(object_trace::scope_available()),
        double(pf::clamp_key(tune.mz_size, pf::key_mz_size)), double(pf::clamp_key(tune.mz_core, pf::key_mz_core)),
        double(pf::clamp_key(tune.mz_gain, pf::key_mz_gain)), double(pf::clamp_key(tune.mz_smooth, pf::key_mz_smooth)));
}
// The draw is a muzzle body's: ADD ONE / ONE bound, the VS rows draw_constants reads (c28..c30, c34..c36) known, the
// scope's node carries a listed body id. Called only while the standard_lighting DEFAULT vertex shader is bound
// (shadow_.vs_muzzle) and the program is ready. Blend-off draws end at the ALPHABLENDENABLE shadow; the scope lookup
// (TLS, no memory read) and the one 4-byte node read run for additive draws only.
bool MotionOutput::muzzle_reshade_match(MotionRoute& route) noexcept {
    namespace mz = weapon_effects::muzzle_reshade;
    if (!state_known(3) || !shadow_.states[3]) return false;
    if (!blend_known(0) || !blend_known(1) || !blend_known(2) || shadow_.composition_blend[0] != D3DBLEND_ONE ||
        shadow_.composition_blend[1] != D3DBLEND_ONE || shadow_.composition_blend[2] != D3DBLENDOP_ADD)
        return false;
    if ((shadow_.vs_c28_known & mz::vs_rows_read) != mz::vs_rows_read) return false;
    std::uintptr_t descriptor = 0, node = 0;
    if (!object_trace::scope_node(&descriptor, &node) || !node) return false;
    std::uint32_t id = 0;
    if (!engine_memory::read(node + mz::node_body_offset, &id, sizeof id)) return false;
    const unsigned b = mz::body_index(id);
    if (b >= mz::body_count) return false;
    route.muzzle_body = std::uint8_t(b);
    ++muzzle_reshade_matched_;
    return true;
}
bool MotionOutput::muzzle_reshade_ready() const noexcept {
    return muzzle_reshade_ps_ && !muzzle_reshade_restore_failed_;
}
// The sequence's begin for a matched draw (before_draw, before the hull-emission route): the body's block with the
// draw's c192 and c194.xy (from the shadowed VS rows). false: nothing applied (an unusable world scale; a failed step
// undone; an undo that failed stops the re-shade for the device with one row), and the draw goes on to the hull route.
bool MotionOutput::begin_muzzle_reshade(MotionRoute& route) noexcept {
    namespace mz = weapon_effects::muzzle_reshade;
    std::memcpy(muzzle_reshade_draw_, muzzle_reshade_constants_[route.muzzle_body], sizeof muzzle_reshade_draw_);
    if (!mz::draw_constants(mz::bodies[route.muzzle_body], shadow_.vs_c28, muzzle_reshade_draw_)) {
        ++muzzle_reshade_failures_;
        return false;
    }
    const bool timed = capture_ || muzzle_reshade_frames_ % 60u == 0u;
    LARGE_INTEGER a{}, b{};
    if (timed) QueryPerformanceCounter(&a);
    auto ops = mz::calls(
        [this](const float* v) {
            return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, mz::constant_register, v, mz::constant_count);
        },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    mz::Applied applied;
    const HRESULT hr = mz::begin(ops, muzzle_reshade_ps_, muzzle_reshade_draw_, shadow_.ps_c192, applied);
    if (timed) {
        QueryPerformanceCounter(&b);
        muzzle_reshade_ticks_ += std::uint64_t(b.QuadPart - a.QuadPart);
    }
    if (FAILED(hr)) {
        ++muzzle_reshade_failures_;
        if (applied.rollback_failed) {
            ++counters_.restore_failures;
            if (!muzzle_reshade_restore_failed_)
                log("muzzle_reshade_restore_failed device=%llu frame=%llu step=begin result=%08lx (the re-shade stops for "
                    "this device)",
                    id_, frame_, hr);
            muzzle_reshade_restore_failed_ = true;
        }
        return false;
    }
    route.muzzle_reshade = true;
    route.muzzle_timed = timed;
    return true;
}
// after_draw, first: the sequence's end (the game's program and the application's c192..c195; timed with the begin)
// and the draw's counts. A failed restore stops the re-shade for the device.
void MotionOutput::finish_muzzle_reshade(MotionRoute& route) noexcept {
    namespace mz = weapon_effects::muzzle_reshade;
    LARGE_INTEGER a{}, b{};
    if (route.muzzle_timed) QueryPerformanceCounter(&a);
    auto ops = mz::calls(
        [this](const float* v) {
            return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, mz::constant_register, v, mz::constant_count);
        },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    mz::Applied applied;
    applied.program = applied.constants = true;
    const HRESULT hr = mz::end(ops, applied, shadow_.ps, shadow_.ps_c192);
    if (route.muzzle_timed) {
        QueryPerformanceCounter(&b);
        muzzle_reshade_ticks_ += std::uint64_t(b.QuadPart - a.QuadPart);
        ++muzzle_reshade_timed_draws_;
    }
    ++muzzle_reshade_draws_;
    if (route.muzzle_hull) ++muzzle_reshade_hull_;
    route.muzzle_reshade = route.muzzle_timed = route.muzzle_hull = false;
    if (FAILED(hr)) {
        ++muzzle_reshade_failures_;
        ++counters_.restore_failures;
        if (!muzzle_reshade_restore_failed_)
            log("muzzle_reshade_restore_failed device=%llu frame=%llu step=end result=%08lx (the re-shade stops for this "
                "device)",
                id_, frame_, hr);
        muzzle_reshade_restore_failed_ = true;
        invalidate_render_states();
    }
}
// The dev tuning poll (one per frame for both re-shades, capture.cpp's present while either runs; X3M_DEV_TUNING
// builds) and the constants it feeds.
void MotionOutput::reshade_poll_tune() noexcept {
#if X3M_DEV_TUNING
    if (weapon_effects::projectile_tune::poll(&reshade_tune_)) {
        weapon_effects::reshade::constants(reshade_tune_,
                                           bullet_reshade_constants_);
        bullet_layout_valid_ = false; // rs_head / rs_width: the body's c29..c30 again at its next draw
        muzzle_reshade_refresh_constants();
        hull_reshade_refresh_constants();
    }
#endif
}
void MotionOutput::muzzle_reshade_take_frame(unsigned* draws, unsigned* matched, unsigned* hull, unsigned* failures,
                                             unsigned* timed, float* us) noexcept {
    ++muzzle_reshade_frames_;
    *draws = muzzle_reshade_draws_;
    *matched = muzzle_reshade_matched_;
    *hull = muzzle_reshade_hull_;
    *failures = muzzle_reshade_failures_;
    *timed = muzzle_reshade_timed_draws_;
    const std::uint64_t frequency = engine_qpc_frequency();
    *us = frequency ? float(double(muzzle_reshade_ticks_) * 1e6 / double(frequency)) : 0.f;
    muzzle_reshade_draws_ = muzzle_reshade_matched_ = muzzle_reshade_hull_ = muzzle_reshade_failures_ = 0;
    muzzle_reshade_timed_draws_ = 0;
    muzzle_reshade_ticks_ = 0;
}
