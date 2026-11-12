// The bullet re-shade (included by motion_output.cpp inside namespace x3m; FL only; X3M_WEAPON_EFFECTS=reshade;
// docs/reverse-engineering/fl/bullet-draw-batch.md section 6). The batch call 0x097af8 is claimed with a scope stub
// that forwards every submit and raises x3m_bullet_scope around it (weapon_effects_hook.cpp). A draw inside that scope
// whose bound pixel shader is one of the game's INSTANCE_BULLETS programs (shadow_.ps_bullet: recognised by the proxy's
// hash of the bytes the game passed to CreatePixelShader, bullet_reshade_core.h) is drawn with our ps_2_0 program
// (src/effects/weapon_bullet_reshade_ps.hlsl) and c31 = (gain, edge, core, normaliser) with c29..c30 the drawn body's
// UV layout (the geometric size: x3m_bullet_body, the batch's body id the stub published, through the table's
// bullet_bodies, bullet_reshade_layout), through the one device sequence of bullet_reshade_route.h (c31.w the bound
// texture's normaliser, bullet_reshade_scale):
// - the additive route (screen_emission_additive under HDR, prepare_screen_additive) takes the bullet draws: the
//   re-shade takes precedence over the route's own program, keeps the route's conversion (DESTBLEND ONE, applied by
//   the sequence) with the route's emission gain folded into c31, its alpha states, footprint and single-copy drop,
//   and with the route's far-flag targets bound draws our flag variant (weapon_bullet_reshade_flag_ps.hlsl, the
//   route's flag law); finish_bullet_reshade puts back the game's program, the application's c31 and DESTBLEND
//   INVSRCCOLOR, finish_screen_additive the rest of the route;
// - otherwise (the route off, refused, or no HDR) under the game's ONE / INVSRCCOLOR (prepare_bullet_reshade, last in
//   before_draw).
// The lone path (bullet-draw-batch.md section 7): a bullet body the batch does not take is drawn through the material
// submit with its effects.fx material (the effects DEFAULT or INSTANCE VS, shadow_.vs_lone), inside the FL object-trace
// scope. A draw with that VS bound, ADD ONE / INVSRCCOLOR or ONE / ONE, whose scope node's body id (node +0x144, one
// 4-byte validated read) is in the body map (weapon_effects::bullet_body_layout: the live TBullets rows' body ids) is
// re-shaded with the lone pair (weapon_bullet_reshade_lone{,_flag}_ps.hlsl: the same law, the fade in COLOR0.x) and
// that body's layout, through the same sequence: ONE / INVSRCCOLOR goes through the additive route exactly as a batch
// draw (prepare_screen_additive with route.bullet_lone: conversion, gain, alpha law and far flag; no footprint, no
// single-copy drop), and plain under the game's blend when the route is off or refuses it; ONE / ONE plain. Counted
// apart from the batch's draws (lone=, lone_flagged=).
// The game's program comes back from the shadowed binding, c29..c31 from the SetPixelShaderConstantF hook's shadow
// (read at resync; zero when never written). Documented IDirect3DDevice9 calls only, through the native slots (the shadows keep
// the application's values); no allocation per draw. On every 60th frame (a 32-bit count) and capture frames the
// sequence alone, before and after the draw, is QPC-timed for the bullet_reshade_frame row.
namespace {
constexpr DWORD bullet_reshade_program[] = {
#include "../renderer/weapon_bullet_reshade_pixel_program_inc.h"
};
constexpr DWORD bullet_reshade_flag_program[] = {
#include "../renderer/weapon_bullet_reshade_flag_pixel_program_inc.h"
};
constexpr DWORD bullet_reshade_lone_program[] = {
#include "../renderer/weapon_bullet_reshade_lone_pixel_program_inc.h"
};
constexpr DWORD bullet_reshade_lone_flag_program[] = {
#include "../renderer/weapon_bullet_reshade_lone_flag_pixel_program_inc.h"
};
static_assert(sizeof bullet_reshade_program / sizeof bullet_reshade_program[0] > 2 &&
                  bullet_reshade_program[0] == 0xffff0200u && bullet_reshade_flag_program[0] == 0xffff0200u &&
                  bullet_reshade_lone_program[0] == 0xffff0200u && bullet_reshade_lone_flag_program[0] == 0xffff0200u,
              "the embedded re-shade programs are ps_2_0");
static_assert(weapon_effects::reshade::layout_floats == 8, "MotionOutput::bullet_layout_ holds c29..c30");
} // namespace
// Created once per device at the first game bullet program's or lone-path VS's registration (register_pixel_shader,
// register_vertex_shader); a refusal of the plain program logs one row and leaves the bullet draws vanilla for the
// device's life (the flag variant missing only refuses the far flag for re-shaded draws, bolt_flag reason 1; the lone
// pair likewise for the lone path).
void MotionOutput::bullet_reshade_create() noexcept {
    if (bullet_reshade_ps_ || bullet_reshade_create_failed_) return;
    IDirect3DPixelShader9 *ps = nullptr, *flag = nullptr, *lone = nullptr, *lone_flag = nullptr;
    const HRESULT hr = native<CreatePsFn>(CreatePixelShader)(device_, bullet_reshade_program, &ps);
    const HRESULT flag_hr = native<CreatePsFn>(CreatePixelShader)(device_, bullet_reshade_flag_program, &flag);
    const HRESULT lone_hr = native<CreatePsFn>(CreatePixelShader)(device_, bullet_reshade_lone_program, &lone);
    const HRESULT lone_flag_hr = native<CreatePsFn>(CreatePixelShader)(device_, bullet_reshade_lone_flag_program, &lone_flag);
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
    if (SUCCEEDED(lone_hr) && lone && bullet_reshade_ps_)
        bullet_reshade_lone_ps_ = lone;
    else
        release(lone);
    if (SUCCEEDED(lone_flag_hr) && lone_flag && bullet_reshade_lone_ps_)
        bullet_reshade_lone_flag_ps_ = lone_flag;
    else
        release(lone_flag);
    weapon_effects::reshade::constants(projectile_tune_[weapon_effects::projectile_family::fighter_tracer],
                                       bullet_reshade_constants_);
    bullet_layout_valid_ = false;
    log("bullet_reshade_program device=%llu frame=%llu created=%u result=%08lx flag_created=%u flag_result=%08lx "
        "lone_created=%u lone_result=%08lx lone_flag_created=%u lone_flag_result=%08lx words=%u "
        "flag_words=%u gain=%.3g edge=%.3g core=%.3g additive_gain=%.3g texture_keys=%u textures_mapped=%u",
        id_, frame_, unsigned(bullet_reshade_ps_ != nullptr), hr, unsigned(bullet_reshade_flag_ps_ != nullptr), flag_hr,
        unsigned(bullet_reshade_lone_ps_ != nullptr), lone_hr, unsigned(bullet_reshade_lone_flag_ps_ != nullptr),
        lone_flag_hr,
        unsigned(sizeof bullet_reshade_program / sizeof bullet_reshade_program[0]),
        unsigned(sizeof bullet_reshade_flag_program / sizeof bullet_reshade_flag_program[0]),
        double(bullet_reshade_constants_[0]), double(bullet_reshade_constants_[1]), double(bullet_reshade_constants_[2]),
        double(screen_additive_gain_), weapon_effects::bullet_texture_count(), bullet_textures_.count);
}
// The per-texture normaliser. The D3DX observer stores a created texture whose bytes are a tables.bullet_textures entry
// (capture.cpp texture_created: a byte-count compare, the hashes only on a count match); its final release drops it.
bool MotionOutput::bullet_texture_insert(IDirect3DBaseTexture9* texture, std::uint64_t identity, unsigned key) noexcept {
    if (!bullet_reshade_requested_ || !texture || !identity || key > 255u) return false;
    if (texture == bullet_texture_bound_) bullet_texture_bound_ = nullptr; // resolved again at its next draw
    return bullet_textures_.insert(texture, identity, 0, std::uint8_t(key));
}
void MotionOutput::bullet_texture_forget(const void* texture) noexcept {
    if (texture == bullet_texture_bound_) bullet_texture_bound_ = nullptr;
    if (bullet_textures_.count) bullet_textures_.erase(texture);
}
// c31.w for a re-shaded draw: the bound stage-0 texture's entry when the pointer is known and its identity still matches
// (a reused address of a freed texture is dropped), else the IRE's 2.75 with one bullet_reshade_texture row per texture
// (at most 16). One pointer compare per draw while the texture stays bound; the table probe and the identity read
// (GetPrivateData) run when the pointer changes and once a frame.
void MotionOutput::bullet_reshade_scale(IDirect3DBaseTexture9* texture, float* out) noexcept {
    if (texture && texture == bullet_texture_bound_) {
        *out = bullet_texture_scale_;
        return;
    }
    float scale = weapon_effects::reshade::luminance_scale;
    int key = -1;
    if (texture && bullet_textures_.count)
        if (auto* entry = bullet_textures_.find(texture)) {
            const std::uint64_t identity = resource_id(texture);
            if (identity && identity == entry->identity) {
                key = entry->slot;
                scale = weapon_effects::reshade::texture_scale(weapon_effects::bullet_texture_p99_q(entry->slot));
            } else
                bullet_textures_.erase(texture);
        }
    if (key < 0) {
        ++bullet_texture_unknown_draws_;
        bool seen = false;
        for (unsigned i = 0; i < bullet_texture_unknown_count_ && !seen; ++i) seen = bullet_texture_unknown_[i] == texture;
        if (!seen && bullet_texture_unknown_count_ < 16u) {
            bullet_texture_unknown_[bullet_texture_unknown_count_++] = texture;
            log("bullet_reshade_texture device=%llu frame=%llu texture=%p entry=none scale=%.4g (no tables.bullet_textures "
                "entry matched at creation: the IRE normaliser) textures_known=%u",
                id_, frame_, static_cast<void*>(texture), double(scale), bullet_textures_.count);
        }
    }
    bullet_texture_bound_ = texture;
    bullet_texture_scale_ = scale;
    *out = scale;
}
// c29..c30 for a body id: its tables.bullet_bodies layout and the keys rs_head / rs_width (bullet_reshade_core.h
// layout_constants), or the identity (f = 1) for a body without one, logged once per body. Cached until the id changes
// or the tuning reloads; a miss while the engine's TBullets is not readable yet is not cached.
void MotionOutput::bullet_reshade_layout(std::uint32_t body) noexcept {
    bool final = true;
    const std::int32_t* words = weapon_effects::bullet_body_layout(body, &final);
    weapon_effects::reshade::layout_constants(words, projectile_tune_[weapon_effects::projectile_family::fighter_tracer],
                                              bullet_layout_);
    bullet_layout_body_ = body;
    bullet_layout_valid_ = final;
    if (words || !final) return;
    bool seen = false;
    for (unsigned i = 0; i < bullet_layout_unknown_count_ && !seen; ++i) seen = bullet_layout_unknown_[i] == body;
    if (!seen && bullet_layout_unknown_count_ < 16u) {
        bullet_layout_unknown_[bullet_layout_unknown_count_++] = body;
        log("bullet_reshade_layout device=%llu frame=%llu body=%lu entry=none (no tables.bullet_bodies layout mapped to "
            "this body: the accepted look, no geometric size)",
            id_, frame_, static_cast<unsigned long>(body));
    }
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
        [this](const float* v) {
            return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, rs::block_register, v, rs::block_count);
        },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    // The batch's body id from the scope stub; a lone draw's from its scope node (lone_bullet_match).
    const std::uint32_t body = route.bullet_lone ? route.bullet_body : std::uint32_t(x3m_bullet_body);
    if (!bullet_layout_valid_ || body != bullet_layout_body_) bullet_reshade_layout(body);
    rs::Plan plan;
    plan.layout = bullet_layout_;
    plan.program = route.bullet_lone ? (flag ? bullet_reshade_lone_flag_ps_ : bullet_reshade_lone_ps_)
                                     : (flag ? bullet_reshade_flag_ps_ : bullet_reshade_ps_);
    plan.constants = bullet_reshade_constants_;
    plan.gain = screen_additive_gain_;
    bullet_reshade_scale(samplers_[0].texture, &plan.scale);
    plan.convert = convert;
    rs::Applied applied;
    const HRESULT hr = rs::begin(ops, plan, shadow_.ps_c29, applied);
    if (timed) {
        QueryPerformanceCounter(&b);
        (route.bullet_lone ? bullet_reshade_lone_ticks_ : bullet_reshade_ticks_) += std::uint64_t(b.QuadPart - a.QuadPart);
    }
    if (FAILED(hr)) {
        ++(route.bullet_lone ? bullet_reshade_lone_failures_ : bullet_reshade_failures_);
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
// The lone path's recognition (bullet-draw-batch.md section 7), for a draw with the effects DEFAULT / INSTANCE VS bound
// (shadow_.vs_lone) that no earlier preparation took: the lone program ready, the blend shadow ADD ONE / INVSRCCOLOR or
// ONE / ONE, then the scope node (TLS, no memory read), its body id (one 4-byte validated read) and its entry in the
// body map (one compare per mapped body; built once at the first lookup that can read TBullets). Every other draw of
// that VS (engine glows, the gate, flares, other effects) ends at the blend shadow or at the body lookup and goes on to
// the routes it would have taken.
bool MotionOutput::lone_bullet_match(MotionRoute& route) noexcept {
    namespace rs = weapon_effects::reshade;
    if (!bullet_reshade_lone_ps_ || bullet_reshade_restore_failed_) return false;
    if (!state_known(3) || !blend_known(0) || !blend_known(1) || !blend_known(2)) return false;
    const unsigned blend = rs::lone_blend(shadow_.states[3] != 0, shadow_.composition_blend[0],
                                          shadow_.composition_blend[1], shadow_.composition_blend[2]);
    if (blend == rs::lone_none) return false;
    std::uintptr_t descriptor = 0, node = 0;
    if (!object_trace::scope_node(&descriptor, &node) || !node) return false;
    std::uint32_t id = 0;
    if (!engine_memory::read(node + rs::node_body_offset, &id, sizeof id)) return false;
    bool final = true;
    if (!weapon_effects::bullet_body_layout(id, &final)) return false;
    route.bullet_lone = true;
    route.bullet_lone_blend = std::uint8_t(blend);
    route.bullet_body = id;
    return true;
}
// The lone draw's re-shade: ONE / INVSRCCOLOR through the additive route (the batch's conversion, gain, alpha law and
// far flag, prepare_screen_additive with route.bullet_lone) when the route is requested; when it is off or refuses the
// draw (no HDR, a state or scene guard), or for ONE / ONE, plain under the game's blend (gain 1, no flag), as a refused
// batch draw. A failed begin leaves the draw to the routes after this one, vanilla.
void MotionOutput::prepare_lone_bullet(const MotionDrawCall& call, MotionRoute& route) noexcept {
    if (route.bullet_lone_blend == weapon_effects::reshade::lone_screen && screen_additive_requested_) {
        ++screen_additive_window_.pair_draws; // offered to the route: its window's admitted + refused stay within it
        const unsigned failures = screen_additive_failures_;
        prepare_screen_additive(call, route);
        const unsigned failed = screen_additive_failures_ - failures;
        screen_additive_window_.apply_failures += failed;
        if (route.screen_additive || route.bullet_reshade || failed) {
            if (!route.bullet_reshade) route.bullet_lone = false;
            return;
        }
    }
    begin_bullet_reshade(route, false, false);
    if (!route.bullet_reshade) route.bullet_lone = false;
}
// The sequence's end alone (the program, c31 and, converted, DESTBLEND back; timed with the begin); the route's flags
// cleared. Used by finish_bullet_reshade and by the additive route's alpha rollback.
HRESULT MotionOutput::end_bullet_reshade(MotionRoute& route) noexcept {
    namespace rs = weapon_effects::reshade;
    LARGE_INTEGER a{}, b{};
    if (route.bullet_reshade_timed) QueryPerformanceCounter(&a);
    auto ops = rs::calls(
        [this](DWORD v) { return direct_call<SetRenderStateFn>(SetRenderState, D3DRS_DESTBLEND, v); },
        [this](const float* v) {
            return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, rs::block_register, v, rs::block_count);
        },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    rs::Applied applied;
    applied.program = applied.block = true;
    applied.destblend = route.bullet_reshade_convert;
    const HRESULT hr = rs::end(ops, applied, shadow_.ps, shadow_.ps_c29);
    const bool lone = route.bullet_lone; // the lone path's own counts and time (lone_failures=, lone_us_per_draw=)
    if (route.bullet_reshade_timed) {
        QueryPerformanceCounter(&b);
        (lone ? bullet_reshade_lone_ticks_ : bullet_reshade_ticks_) += std::uint64_t(b.QuadPart - a.QuadPart);
        ++(lone ? bullet_reshade_lone_timed_ : bullet_reshade_timed_draws_);
    }
    route.bullet_reshade = route.bullet_reshade_timed = route.bullet_reshade_convert = route.bullet_reshade_flag = false;
    route.bullet_lone = false;
    if (FAILED(hr)) {
        ++(lone ? bullet_reshade_lone_failures_ : bullet_reshade_failures_);
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
    if (route.bullet_lone) {
        ++bullet_reshade_lone_;
        if (route.bullet_reshade_flag) ++bullet_reshade_lone_flagged_;
    } else {
        ++bullet_reshade_draws_;
        if (route.bullet_reshade_convert) ++bullet_reshade_additive_;
        if (route.bullet_reshade_flag) ++bullet_reshade_flagged_;
    }
    end_bullet_reshade(route);
}
void MotionOutput::bullet_reshade_take_frame(unsigned* draws, unsigned* failures, unsigned* outside, unsigned* timed,
                                             float* us, unsigned* additive, unsigned* flagged, unsigned* lone,
                                             unsigned* lone_flagged, unsigned* lone_failures, unsigned* lone_timed,
                                             float* lone_us) noexcept {
    ++bullet_reshade_frames_;
    bullet_texture_bound_ = nullptr; // the bound texture's entry and identity are read again once a frame
    *draws = bullet_reshade_draws_;
    *failures = bullet_reshade_failures_;
    *outside = bullet_reshade_outside_;
    *timed = bullet_reshade_timed_draws_;
    *additive = bullet_reshade_additive_;
    *flagged = bullet_reshade_flagged_;
    *lone = bullet_reshade_lone_;
    *lone_flagged = bullet_reshade_lone_flagged_;
    *lone_failures = bullet_reshade_lone_failures_;
    *lone_timed = bullet_reshade_lone_timed_;
    const std::uint64_t frequency = engine_qpc_frequency();
    *us = frequency ? float(double(bullet_reshade_ticks_) * 1e6 / double(frequency)) : 0.f;
    *lone_us = frequency ? float(double(bullet_reshade_lone_ticks_) * 1e6 / double(frequency)) : 0.f;
    bullet_reshade_draws_ = bullet_reshade_failures_ = bullet_reshade_outside_ = bullet_reshade_timed_draws_ = 0;
    bullet_reshade_additive_ = bullet_reshade_flagged_ = bullet_reshade_lone_ = bullet_reshade_lone_flagged_ = 0;
    bullet_reshade_ticks_ = 0;
    bullet_reshade_lone_failures_ = bullet_reshade_lone_timed_ = 0;
    bullet_reshade_lone_ticks_ = 0;
}
