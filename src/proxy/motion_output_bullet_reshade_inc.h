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
// single-copy drop), and plain under the game's blend when the route is off or refuses it; ONE / ONE plain, with the
// far flag written outside the route (lone_native_flag: the route's targets and the lone flag program under the game's
// ONE / ONE; never under ONE / INVSRCCOLOR, bullet_reshade_route.h lone_native_flag gives the D3D9 reason). On every
// path the sequence first writes the lone draw's states where they differ (bullet_reshade_route.h lone_states): on the
// route the four it requires (Z write off, colour mask 15, sRGB write off, separate alpha off), off it Z write only;
// alpha test and Z test stay the game's; the application's values go back after the draw. The route admits a lone draw
// on those four (FL 68 A: the route refused every lone draw for `state`). Counted apart from the batch's draws (lone=, lone_flagged=,
// lone_flagged_native=, lone_refused= with lone_refused_why=, lone_route_state=, lone_zwrite= .. lone_sepalpha=).
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
    weapon_effects::reshade::constants(reshade_tune_,
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
    weapon_effects::reshade::layout_constants(words, reshade_tune_,
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
// The application's values of bullet_reshade_route.h lone_states for this draw: the render-state shadow (one Get each
// for an indexed state the shadow does not hold yet, cached as render_state does); separate alpha from the blend shadow
// only (unknown: false, the lone draw stays vanilla).
bool MotionOutput::lone_state_values(DWORD out[4]) noexcept {
    static_assert(weapon_effects::reshade::lone_state_count == 4, "MotionRoute::bullet_lone_app holds lone_states");
    for (unsigned i = 0; i < 3; ++i)
        if (FAILED(render_state(weapon_effects::reshade::lone_states[i], &out[i]))) return false;
    if (!blend_known(3)) return false;
    out[3] = shadow_.composition_blend[3];
    return true;
}
// The sequence's begin for this draw (convert: the additive route's DESTBLEND ONE and gain; flag: its far-flag targets
// are bound, so the flag variant). A lone draw (route.bullet_lone) first has its states written where they differ
// (bullet_reshade_route.h lone_states): on the route (convert) the four the route requires (Z write off, colour mask
// 15, sRGB write off, separate alpha off), off it Z write only; alpha test and Z test stay as the game set them; the
// application's values are kept in the route for the end. false: nothing applied (a failed step undone; an undo that
// failed stops the re-shade for the device with one row and invalidates the TAA, Z write being among the states).
bool MotionOutput::begin_bullet_reshade(MotionRoute& route, bool convert, bool flag) noexcept {
    namespace rs = weapon_effects::reshade;
    const bool timed = capture_ || bullet_reshade_frames_ % 60u == 0u;
    LARGE_INTEGER a{}, b{};
    if (timed) QueryPerformanceCounter(&a);
    auto ops = rs::calls(
        [this](D3DRENDERSTATETYPE s, DWORD v) { return direct_call<SetRenderStateFn>(SetRenderState, s, v); },
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
    if (route.bullet_lone) {
        if (!lone_state_values(route.bullet_lone_app)) {
            ++bullet_reshade_lone_failures_; // nothing applied: the draw stays vanilla
            return false;
        }
        rs::ScreenState current;
        for (unsigned i = 0; i < rs::lone_state_count; ++i) current.lone[i] = route.bullet_lone_app[i];
        plan.states = rs::state_differs(current) & (convert ? rs::lone_route_bits : rs::lone_plain_bits);
        plan.state_app = route.bullet_lone_app;
    }
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
            invalidate_taa(TaaInvalidateSite::RestoreFailed);
        }
        return false;
    }
    route.bullet_reshade = true;
    route.bullet_reshade_timed = timed;
    route.bullet_reshade_convert = convert;
    route.bullet_reshade_flag = flag;
    route.bullet_lone_states = std::uint8_t(applied.states); // counted at finish_bullet_reshade (the draw drawn)
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
static_assert(MotionOutput::lone_count_slots_ == weapon_effects::lone_count_slots,
              "MotionOutput's lone count array is weapon_effects_patch.h's layout");
static_assert(weapon_effects::lone_count_slots ==
                  weapon_effects::lone_count_refused_why + weapon_effects::reshade::unflagged_count - 1u,
              "bullet_reshade_frame's lone_refused_why= holds every LoneUnflagged reason");
// One lone draw ended without the far flag: counted in total and by its reason (bullet_reshade_route.h LoneUnflagged).
void MotionOutput::lone_unflagged(unsigned reason) noexcept {
    ++bullet_reshade_lone_counts_[weapon_effects::lone_count_refused];
    if (reason > weapon_effects::reshade::unflagged_none && reason < weapon_effects::reshade::unflagged_count)
        ++bullet_reshade_lone_counts_[weapon_effects::lone_count_refused_why + reason - 1u];
}
// The far flag for a lone draw the additive route does not take (bullet_reshade_route.h lone_native_flag): only under
// the game's ONE / ONE, inside the bound HDR scene after its first depth writer, on the route's own preconditions
// (bolt_flag_target_reason) and through its own binding (bind_bolt_flag_targets: RT1 masked off, RT2 GREEN, the masks
// saved; rolled back on a partial failure). Sets route.bolt_flag and route.bullet_lone_native_flag when bound, else
// route.bullet_lone_unflagged.
void MotionOutput::lone_native_flag(MotionRoute& route) noexcept {
    namespace rs = weapon_effects::reshade;
    unsigned why = rs::unflagged_none;
    if (!bolt_far_requested_)
        why = rs::unflagged_off;
    else if (!rs::lone_native_flag(route.bullet_lone_blend))
        why = rs::unflagged_screen_unrouted;
    else if (!scene_open_ || active_queries_ || shadow_.recording || !scene_bound() || main_msaa_ ||
             hdr_state_ != HdrState::Active || !hdr_)
        why = rs::unflagged_scene;
    else if (!selector_.scene_depth_written())
        why = rs::unflagged_no_depth;
    else if (const unsigned target = bolt_flag_target_reason(bullet_reshade_lone_flag_ps_ != nullptr))
        why = rs::unflagged_no_variant + target - 1u;
    else if (FAILED(bind_bolt_flag_targets(route)))
        why = rs::unflagged_bind;
    route.bullet_lone_unflagged = std::uint8_t(why);
    route.bullet_lone_native_flag = route.bolt_flag;
}
// The flag targets of a natively flagged lone draw back (after the sequence's end: reverse order of the bind); a failed
// restore is latched exactly as finish_screen_additive latches it.
void MotionOutput::finish_lone_native_flag(MotionRoute& route) noexcept {
    route.bullet_lone_native_flag = false;
    if (!route.bolt_flag) return;
    const HRESULT back = unbind_bolt_flag_targets(route);
    if (FAILED(back)) {
        if (!motion_state_lost_) {
            motion_state_lost_ = true;
            motion_state_error_ = back;
        }
        ++counters_.restore_failures;
        invalidate_render_states();
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
    }
}
// The lone draw's re-shade: ONE / INVSRCCOLOR through the additive route (the batch's conversion, gain, alpha law and
// far flag, prepare_screen_additive with route.bullet_lone) when the route is requested. When the route is off or
// refuses the draw (no HDR, a scene guard), or for ONE / ONE (never offered), plain under the game's blend (gain 1),
// with the far flag written outside the route where the game's blend allows it (ONE / ONE: lone_native_flag); a draw
// that ends unflagged is counted with its reason. A failed begin leaves the draw to the routes after this one, vanilla.
void MotionOutput::prepare_lone_bullet(const MotionDrawCall& call, MotionRoute& route) noexcept {
    if (route.bullet_lone_blend == weapon_effects::reshade::lone_screen && screen_additive_requested_) {
        ++screen_additive_window_.pair_draws; // offered to the route: its window's admitted + refused stay within it
        const unsigned failures = screen_additive_failures_;
        prepare_screen_additive(call, route);
        const unsigned failed = screen_additive_failures_ - failures;
        screen_additive_window_.apply_failures += failed;
        if (route.screen_additive || route.bullet_reshade || failed) {
            if (!route.bullet_reshade) {
                lone_unflagged(weapon_effects::reshade::unflagged_failed);
                route.bullet_lone = false;
            }
            return;
        }
    }
    lone_native_flag(route);
    begin_bullet_reshade(route, false, route.bullet_lone_native_flag);
    if (route.bullet_reshade) {
        if (route.bullet_lone_native_flag) {
            ++bolt_flag_frame_;
            ++bolt_flag_session_;
        }
        return;
    }
    finish_lone_native_flag(route); // nothing of the sequence applied: the targets back, the draw vanilla
    lone_unflagged(weapon_effects::reshade::unflagged_failed);
    route.bullet_lone = false;
}
// The sequence's end alone (the program, c31 and, converted, DESTBLEND back; timed with the begin); the route's flags
// cleared. Used by finish_bullet_reshade and by the additive route's alpha rollback.
HRESULT MotionOutput::end_bullet_reshade(MotionRoute& route) noexcept {
    namespace rs = weapon_effects::reshade;
    LARGE_INTEGER a{}, b{};
    if (route.bullet_reshade_timed) QueryPerformanceCounter(&a);
    auto ops = rs::calls(
        [this](D3DRENDERSTATETYPE s, DWORD v) { return direct_call<SetRenderStateFn>(SetRenderState, s, v); },
        [this](DWORD v) { return direct_call<SetRenderStateFn>(SetRenderState, D3DRS_DESTBLEND, v); },
        [this](const float* v) {
            return direct_call<SetConstantsFFn>(SetPixelShaderConstantF, rs::block_register, v, rs::block_count);
        },
        [this](IDirect3DPixelShader9* p) { return native<SetPsFn>(SetPixelShader)(device_, p); });
    rs::Applied applied;
    applied.program = applied.block = true;
    applied.destblend = route.bullet_reshade_convert;
    applied.states = route.bullet_lone ? route.bullet_lone_states : 0u; // the lone draw's states, from the route
    for (unsigned i = 0; i < rs::lone_state_count; ++i) applied.state_app[i] = route.bullet_lone_app[i];
    route.bullet_lone_states = 0;
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
        invalidate_taa(TaaInvalidateSite::RestoreFailed); // Z write is among a lone draw's restored states
    }
    return hr;
}
// after_draw, first: the sequence's end and the draw's counts. A failed restore stops the re-shade for the device.
void MotionOutput::finish_bullet_reshade(MotionRoute& route) noexcept {
    if (route.bullet_lone) {
        ++bullet_reshade_lone_;
        if (route.bullet_reshade_flag) ++bullet_reshade_lone_flagged_;
        if (route.bullet_lone_blend == weapon_effects::reshade::lone_additive)
            ++bullet_reshade_lone_counts_[weapon_effects::lone_count_additive];
        for (unsigned i = 0; i < weapon_effects::reshade::lone_state_count; ++i) // the draw was drawn re-shaded
            if (route.bullet_lone_states & (1u << i))
                ++bullet_reshade_lone_counts_[weapon_effects::lone_count_states + i];
        // One row per (body, blend) the lone path re-shades (at most 16 a device): which bodies the lone draws are and
        // which of their states the sequence normalised (bullet_reshade_route.h StateBit), flagged or not.
        const std::uint32_t key = (route.bullet_body << 2) | route.bullet_lone_blend;
        bool seen = false;
        for (unsigned i = 0; i < bullet_lone_seen_count_ && !seen; ++i) seen = bullet_lone_seen_[i] == key;
        if (!seen && bullet_lone_seen_count_ < 16u) {
            bullet_lone_seen_[bullet_lone_seen_count_++] = key;
            log("bullet_reshade_lone_body device=%llu frame=%llu body=%lu blend=%s states=%02x convert=%u flag=%u",
                id_, frame_, static_cast<unsigned long>(route.bullet_body),
                route.bullet_lone_blend == weapon_effects::reshade::lone_additive ? "one_one" : "one_invsrccolor",
                unsigned(route.bullet_lone_states), unsigned(route.bullet_reshade_convert),
                unsigned(route.bullet_reshade_flag));
        }
        if (route.bullet_reshade_flag && route.bullet_lone_native_flag)
            ++bullet_reshade_lone_counts_[weapon_effects::lone_count_flagged_native];
        if (!route.bullet_reshade_flag) lone_unflagged(route.bullet_lone_unflagged);
    } else {
        ++bullet_reshade_draws_;
        if (route.bullet_reshade_convert) ++bullet_reshade_additive_;
        if (route.bullet_reshade_flag) ++bullet_reshade_flagged_;
    }
    const bool native_flag = route.bullet_lone_native_flag;
    end_bullet_reshade(route);
    if (native_flag) finish_lone_native_flag(route); // the flag targets after the program, c29..c31 and the states
}
void MotionOutput::bullet_reshade_take_frame(unsigned* draws, unsigned* failures, unsigned* outside, unsigned* timed,
                                             float* us, unsigned* additive, unsigned* flagged, unsigned* lone,
                                             unsigned* lone_flagged, unsigned* lone_failures, unsigned* lone_timed,
                                             float* lone_us, unsigned lone_counts[lone_count_slots_]) noexcept {
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
    for (unsigned i = 0; i < weapon_effects::lone_count_slots; ++i) {
        lone_counts[i] = bullet_reshade_lone_counts_[i];
        bullet_reshade_lone_counts_[i] = 0;
    }
    const std::uint64_t frequency = engine_qpc_frequency();
    *us = frequency ? float(double(bullet_reshade_ticks_) * 1e6 / double(frequency)) : 0.f;
    *lone_us = frequency ? float(double(bullet_reshade_lone_ticks_) * 1e6 / double(frequency)) : 0.f;
    bullet_reshade_draws_ = bullet_reshade_failures_ = bullet_reshade_outside_ = bullet_reshade_timed_draws_ = 0;
    bullet_reshade_additive_ = bullet_reshade_flagged_ = bullet_reshade_lone_ = bullet_reshade_lone_flagged_ = 0;
    bullet_reshade_ticks_ = 0;
    bullet_reshade_lone_failures_ = bullet_reshade_lone_timed_ = 0;
    bullet_reshade_lone_ticks_ = 0;
}
