#pragma once
#include "bullet_reshade_core.h"
#include <d3d9.h>

// The bullet re-shade's device sequence around one substituted draw (docs/reverse-engineering/fl/bullet-draw-batch.md
// sections 6 and 7): the one place it is written, used by MotionOutput (motion_output_bullet_reshade_inc.h: the plain
// path under the game's ONE / INVSRCCOLOR and the additive route's path under HDR) and driven as is by the native fixture
// (weapon_effects_fixture.cpp reshade_cases, lone_reshade_cases). Ops supplies four documented IDirect3DDevice9 writes:
//   state(D3DRENDERSTATETYPE, DWORD)    SetRenderState(s, v): the lone path's normalised states only
//   destblend(DWORD)                    SetRenderState(D3DRS_DESTBLEND, v)
//   block(const float*)                 SetPixelShaderConstantF(block_register, v, block_count): c29..c31
//   program(IDirect3DPixelShader9*)     SetPixelShader(p)
// begin: [a lone draw's normalised states, lone_states: lone_route_bits on the route, lone_plain_bits off it] ->
// [DESTBLEND ONE when the draw is converted] -> c29..c31 (the body's layout constants, then c31 with gain and core times
// the route's emission gain when converted) -> our program; all or nothing (a failed step undoes the earlier ones). end:
// the game's program -> the application's c29..c31 -> [DESTBLEND INVSRCCOLOR, the value the route admits] -> [the
// application's values of the normalised states, in reverse]. flag_bind / flag_unbind: the far flag's targets around
// the draw (the route's and the lone path's outside it). No allocation, no branch on the frame.
namespace x3m::weapon_effects::reshade {
// The additive route's state admission (MotionOutput::prepare_screen_additive, screen-emission-region.md "Additive
// option") as a mask of the bits in which a draw's render states differ from the exact native screen state, and the
// lone path's normalisation (bullet-draw-batch.md section 7). The first lone_state_count bits name the four states the
// route requires and a lone draw's sequence may write (lone_states / lone_values): Z write off, colour mask 15, sRGB
// write off, separate alpha off. On the route a lone draw takes all four (lone_route_bits: the exact screen state the
// route admits for every other draw); off the route only Z write (lone_plain_bits: a bolt writes no depth and must not
// enter the TAA depth lane; the far flag's MRT write needs none of the others, since RT2.g is written under
// COLORWRITEENABLE2 = GREEN, which the flag binding sets, RT0's mask and the separate alpha factors do not reach RT2.g,
// and the game's alpha test discards the flag exactly where it discards the bolt). Alpha test is never written: the
// route admits any, and our programs output the texel's alpha, so the game's test cuts as in vanilla. state_blend
// (blend off, or not ONE / INVSRCCOLOR / ADD) is the lone path's own recognition (lone_blend) and the route's
// conversion, never normalised; state_alphatest is the refusal breakdown's only (MotionOutput's window row).
enum StateBit : unsigned {
    state_zwrite = 1u << 0,
    state_mask = 1u << 1,
    state_srgb = 1u << 2,
    state_sepalpha = 1u << 3,
    state_blend = 1u << 4,
    state_alphatest = 1u << 5,
};
constexpr unsigned lone_state_count = 4;
constexpr unsigned lone_state_bits = (1u << lone_state_count) - 1u;
constexpr unsigned lone_route_bits = state_zwrite | state_mask | state_srgb | state_sepalpha;
constexpr unsigned lone_plain_bits = state_zwrite;
constexpr unsigned route_state_bits = lone_route_bits | state_blend;
static_assert(lone_route_bits == lone_state_bits, "the route requires every normalisable state");
constexpr D3DRENDERSTATETYPE lone_states[lone_state_count] = {D3DRS_ZWRITEENABLE, D3DRS_COLORWRITEENABLE,
                                                              D3DRS_SRGBWRITEENABLE, D3DRS_SEPARATEALPHABLENDENABLE};
constexpr DWORD lone_values[lone_state_count] = {FALSE, 15u, FALSE, FALSE};
// A draw's render states as the route reads them (the application's values: MotionOutput's shadow); lone[] in
// lone_states order.
struct ScreenState {
    DWORD lone[lone_state_count] = {};
    DWORD blend = 0, src = 0, dst = 0, op = 0; // ALPHABLENDENABLE, SRCBLEND, DESTBLEND, BLENDOP
};
inline unsigned state_differs(const ScreenState& s) noexcept {
    unsigned bits = 0;
    for (unsigned i = 0; i < lone_state_count; ++i)
        if (s.lone[i] != lone_values[i]) bits |= 1u << i;
    if (!s.blend || s.src != D3DBLEND_ONE || s.dst != D3DBLEND_INVSRCCOLOR || s.op != D3DBLENDOP_ADD) bits |= state_blend;
    return bits;
}
// The route's state verdict: 0 admits; otherwise the bits that refuse (reason `state`). A lone bullet draw is admitted
// on the states its sequence's begin writes (lone_route_bits; all or nothing: a failed begin leaves the draw native,
// counted as an apply failure); every other draw on the exact native screen state.
inline unsigned route_state_refuses(const ScreenState& s, bool lone) noexcept {
    return state_differs(s) & route_state_bits & (lone ? ~lone_route_bits : ~0u);
}
// The far flag for a lone draw the additive route does not take (not offered: ONE / ONE; or refused): written with
// the lone flag program under the game's own blend only when that blend adds, ONE / ONE. D3D9 blends every
// simultaneous render target with the one SRCBLEND / DESTBLEND / BLENDOP (there is no per-target blend state; the
// D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING cap the four-lane RT2 needs blends each target with those same factors), and
// RT2.g holds the depth lane `base` under the flag (bolts-through-taa.md: the composite's test is g > 1). Under ONE /
// ONE the lane becomes base + s, as under the route's DESTBLEND ONE; under ONE / INVSRCCOLOR it would become
// s + base (1 - s) = base + s (1 - base), which for far depth (base -> 1, the station the flag exists for) stays at 1 and
// fails the test (the native fixture's `lone_flag_invsrccolor_witness`). Converting the draw to ONE is the route's job
// (prepare_screen_additive); outside it a ONE / INVSRCCOLOR lone draw stays unflagged (unflagged_screen_unrouted).
constexpr bool lone_native_flag(unsigned blend) noexcept {
    return blend == lone_additive;
}
// Why a re-shaded or offered lone draw ended without the far flag (bullet_reshade_frame lone_refused_why=):
// off: the far composite is not requested; scene: outside the bound HDR scene (no open scene, a query or recording
// block, MSAA, no HDR target); no_depth: before the scene's first depth writer; no_variant .. bind: the flag targets'
// reasons (MotionOutput::refuse_bolt_flag 1..5, same order); screen_unrouted: ONE / INVSRCCOLOR the route did not
// take (lone_native_flag); failed: the substitution failed (the draw stayed vanilla).
enum LoneUnflagged : unsigned {
    unflagged_none = 0,
    unflagged_off,
    unflagged_scene,
    unflagged_no_depth,
    unflagged_no_variant,
    unflagged_no_target,
    unflagged_r32f_lane,
    unflagged_write_masks,
    unflagged_bind,
    unflagged_screen_unrouted,
    unflagged_failed,
    unflagged_count
};
constexpr const char* unflagged_names[unflagged_count] = {"none",        "off",         "scene",    "no_depth",
                                                          "no_variant",  "no_target",   "r32f_lane", "write_masks",
                                                          "bind",        "screen_unrouted", "failed"};
struct Plan {
    IDirect3DPixelShader9* program = nullptr; // ours (the flag variant when the route's far flag targets are bound)
    const float* constants = nullptr;         // the tuned c31 (constants())
    float gain = 1.f;                         // the additive route's emission gain (1 unconverted)
    float scale = 0.f;                        // the bound texture's luminance normaliser (0: constants[3], the IRE's)
    const float* layout = nullptr;            // the drawn body's c29..c30 (layout_constants; nullptr: identity_layout)
    bool convert = false;                     // the additive route takes the draw: DESTBLEND ONE
    unsigned states = 0;                      // lone_states bits to write (state_differs & the path's bits; 0: batch)
    const DWORD* state_app = nullptr;         // the application's values of lone_states (required when states != 0)
};
struct Applied {
    bool destblend = false, block = false, program = false;
    bool rollback_failed = false;           // begin failed and could not undo a step it had applied
    unsigned states = 0;                    // lone_states bits written (end puts state_app back)
    DWORD state_app[lone_state_count] = {}; // their application values
};
// c29..c31 for a plan: the layout's eight floats, then c31 = (gain x G, edge, core x G, the texture's normaliser).
inline void plan_constants(const Plan& p, float out[4 * block_count]) noexcept {
    const float* l = p.layout ? p.layout : identity_layout;
    for (unsigned i = 0; i < layout_floats; ++i) out[i] = l[i];
    const float g = p.convert ? p.gain : 1.f;
    out[8] = p.constants[0] * g;
    out[9] = p.constants[1];
    out[10] = p.constants[2] * g;
    out[11] = p.scale > 0.f ? p.scale : p.constants[3];
}
// app_block: the application's c29..c31 (12 floats), put back by a failed begin and by end.
template <class Ops> HRESULT begin(Ops& ops, const Plan& p, const float app_block[12], Applied& a) noexcept {
    a = Applied{};
    float c[4 * block_count];
    plan_constants(p, c);
    HRESULT hr = (p.states & lone_state_bits) && !p.state_app ? D3DERR_INVALIDCALL : S_OK;
    for (unsigned i = 0; i < lone_state_count && SUCCEEDED(hr); ++i)
        if (p.states & (1u << i)) {
            a.state_app[i] = p.state_app[i];
            hr = ops.state(lone_states[i], lone_values[i]);
            if (SUCCEEDED(hr)) a.states |= 1u << i;
        }
    if (SUCCEEDED(hr) && p.convert) {
        hr = ops.destblend(D3DBLEND_ONE);
        a.destblend = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) {
        hr = ops.block(c);
        a.block = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) {
        hr = ops.program(p.program);
        a.program = SUCCEEDED(hr);
    }
    if (FAILED(hr)) {
        bool back = true;
        if (a.block) back = SUCCEEDED(ops.block(app_block)) && back;
        if (a.destblend) back = SUCCEEDED(ops.destblend(D3DBLEND_INVSRCCOLOR)) && back;
        for (unsigned i = lone_state_count; i--;)
            if (a.states & (1u << i)) back = SUCCEEDED(ops.state(lone_states[i], a.state_app[i])) && back;
        a = Applied{};
        a.rollback_failed = !back;
    }
    return hr;
}
template <class Ops>
HRESULT end(Ops& ops, Applied& a, IDirect3DPixelShader9* game, const float app_block[12]) noexcept {
    HRESULT first = S_OK;
    const auto step = [&](HRESULT hr) {
        if (SUCCEEDED(first) && FAILED(hr)) first = hr;
    };
    if (a.program) step(ops.program(game));
    if (a.block) step(ops.block(app_block));
    if (a.destblend) step(ops.destblend(D3DBLEND_INVSRCCOLOR));
    for (unsigned i = lone_state_count; i--;)
        if (a.states & (1u << i)) step(ops.state(lone_states[i], a.state_app[i]));
    a = Applied{};
    return first;
}
// An Ops from four callables (lambdas): state, destblend, block, program.
template <class S, class D, class C, class P> struct Calls {
    S state;
    D destblend;
    C block;
    P program;
};
template <class S, class D, class C, class P> Calls<S, D, C, P> calls(S s, D d, C c, P p) {
    return Calls<S, D, C, P>{s, d, c, p};
}
// The far flag's targets around one draw (bolts-through-taa.md B'): the application's COLORWRITEENABLE1 / 2 saved, RT1
// bound with COLORWRITEENABLE1 = 0 (bound only so the MRT set has no gap; a ps_2_0 oC gap is not legal), RT2 bound with
// COLORWRITEENABLE2 = GREEN; on a failed step the applied ones are undone (*back: the undo's result). flag_unbind:
// reverse order, only the steps applied, the saved masks put back; the first failure returned. The production route
// (MotionOutput::bind_bolt_flag_targets / unbind_bolt_flag_targets) and the fixture run these. F carries the route's
// fields (MotionRoute: bolt_flag, bolt_flag_rt1 / _write1 / _rt2 / _write2, bolt_flag_saved_write1 / 2). Ops:
//   mask_read(D3DRENDERSTATETYPE, DWORD*)   the application's value (MotionOutput: the shadow)
//   mask(D3DRENDERSTATETYPE, DWORD)         SetRenderState
//   target(DWORD index, bool bind)          SetRenderTarget(index, bind ? the flag's surface for index : nullptr)
template <class Ops, class F> HRESULT flag_unbind(Ops& ops, F& f) noexcept {
    HRESULT first = S_OK;
    const auto step = [&](HRESULT hr) {
        if (SUCCEEDED(first) && FAILED(hr)) first = hr;
    };
    if (f.bolt_flag_write2) step(ops.mask(D3DRS_COLORWRITEENABLE2, f.bolt_flag_saved_write2));
    if (f.bolt_flag_rt2) step(ops.target(2, false));
    if (f.bolt_flag_write1) step(ops.mask(D3DRS_COLORWRITEENABLE1, f.bolt_flag_saved_write1));
    if (f.bolt_flag_rt1) step(ops.target(1, false));
    f.bolt_flag = f.bolt_flag_rt1 = f.bolt_flag_write1 = f.bolt_flag_rt2 = f.bolt_flag_write2 = false;
    return first;
}
template <class Ops, class F> HRESULT flag_bind(Ops& ops, F& f, HRESULT* back) noexcept {
    *back = S_OK;
    HRESULT hr = ops.mask_read(D3DRS_COLORWRITEENABLE1, &f.bolt_flag_saved_write1);
    if (SUCCEEDED(hr)) hr = ops.mask_read(D3DRS_COLORWRITEENABLE2, &f.bolt_flag_saved_write2);
    if (SUCCEEDED(hr)) {
        f.bolt_flag_rt1 = true;
        hr = ops.target(1, true);
    }
    if (SUCCEEDED(hr)) {
        f.bolt_flag_write1 = true;
        hr = ops.mask(D3DRS_COLORWRITEENABLE1, 0);
    }
    if (SUCCEEDED(hr)) {
        f.bolt_flag_rt2 = true;
        hr = ops.target(2, true);
    }
    if (SUCCEEDED(hr)) {
        f.bolt_flag_write2 = true;
        hr = ops.mask(D3DRS_COLORWRITEENABLE2, D3DCOLORWRITEENABLE_GREEN);
    }
    if (SUCCEEDED(hr)) {
        f.bolt_flag = true;
        return S_OK;
    }
    *back = flag_unbind(ops, f);
    return hr;
}
// FlagOps from three callables (lambdas): mask_read, mask, target.
template <class R, class M, class T> struct FlagCalls {
    R mask_read;
    M mask;
    T target;
};
template <class R, class M, class T> FlagCalls<R, M, T> flag_calls(R r, M m, T t) {
    return FlagCalls<R, M, T>{r, m, t};
}
} // namespace x3m::weapon_effects::reshade
