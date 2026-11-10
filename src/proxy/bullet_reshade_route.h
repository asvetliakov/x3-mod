#pragma once
#include "bullet_reshade_core.h"
#include <d3d9.h>

// The bullet re-shade's device sequence around one substituted draw (docs/reverse-engineering/fl/bullet-draw-batch.md
// section 6): the one place it is written, used by MotionOutput (motion_output_bullet_reshade_inc.h: the plain path
// under the game's ONE / INVSRCCOLOR and the additive route's path under HDR) and driven as is by the native fixture
// (weapon_effects_fixture.cpp reshade_cases). Ops supplies three documented IDirect3DDevice9 writes:
//   destblend(DWORD)                    SetRenderState(D3DRS_DESTBLEND, v)
//   block(const float*)                 SetPixelShaderConstantF(block_register, v, block_count): c29..c31
//   program(IDirect3DPixelShader9*)     SetPixelShader(p)
// begin: [DESTBLEND ONE when the draw is converted] -> c29..c31 (the body's layout constants, then c31 with gain and
// core times the route's emission gain when converted) -> our program; all or nothing (a failed step undoes the earlier
// ones). end: the game's program -> the application's c29..c31 -> [DESTBLEND INVSRCCOLOR, the value the route admits].
// No allocation, no branch on the frame.
namespace x3m::weapon_effects::reshade {
struct Plan {
    IDirect3DPixelShader9* program = nullptr; // ours (the flag variant when the route's far flag targets are bound)
    const float* constants = nullptr;         // the tuned c31 (constants())
    float gain = 1.f;                         // the additive route's emission gain (1 unconverted)
    float scale = 0.f;                        // the bound texture's luminance normaliser (0: constants[3], the IRE's)
    const float* layout = nullptr;            // the drawn body's c29..c30 (layout_constants; nullptr: identity_layout)
    bool convert = false;                     // the additive route takes the draw: DESTBLEND ONE
};
struct Applied {
    bool destblend = false, block = false, program = false;
    bool rollback_failed = false; // begin failed and could not undo a step it had applied
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
    HRESULT hr = S_OK;
    if (p.convert) {
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
    a = Applied{};
    return first;
}
// An Ops from three callables (lambdas).
template <class D, class C, class P> struct Calls {
    D destblend;
    C block;
    P program;
};
template <class D, class C, class P> Calls<D, C, P> calls(D d, C c, P p) {
    return Calls<D, C, P>{d, c, p};
}
} // namespace x3m::weapon_effects::reshade
