#pragma once
#include "muzzle_reshade_core.h"
#include <d3d9.h>

// The muzzle re-shade's device sequence around one substituted draw (docs/architecture/muzzle-flash-look.md): the one
// place it is written, used by MotionOutput (motion_output_muzzle_reshade_inc.h: the plain path under the game's ONE /
// ONE and the hull-emission route's path, whose blend states the route itself applies and restores) and driven as is
// by the native fixture (weapon_effects_fixture.cpp muzzle_reshade_cases). Ops supplies two documented
// IDirect3DDevice9 writes:
//   constants(const float*)             SetPixelShaderConstantF(constant_register, v, constant_count)
//   program(IDirect3DPixelShader9*)     SetPixelShader(p)
// begin: our c192..c195 -> our program; all or nothing (a failed step undoes the earlier one). end: the game's program
// -> the application's c192..c195. No allocation, no branch on the frame.
namespace x3m::weapon_effects::muzzle_reshade {
struct Applied {
    bool constants = false, program = false;
    bool rollback_failed = false; // begin failed and could not undo a step it had applied
};
template <class Ops>
HRESULT begin(Ops& ops, IDirect3DPixelShader9* ours, const float constants[16], const float app[16], Applied& a) noexcept {
    a = Applied{};
    HRESULT hr = ops.constants(constants);
    a.constants = SUCCEEDED(hr);
    if (SUCCEEDED(hr)) {
        hr = ops.program(ours);
        a.program = SUCCEEDED(hr);
    }
    if (FAILED(hr)) {
        const bool back = !a.constants || SUCCEEDED(ops.constants(app));
        a = Applied{};
        a.rollback_failed = !back;
    }
    return hr;
}
template <class Ops> HRESULT end(Ops& ops, Applied& a, IDirect3DPixelShader9* game, const float app[16]) noexcept {
    HRESULT first = S_OK;
    if (a.program) first = ops.program(game);
    if (a.constants) {
        const HRESULT hr = ops.constants(app);
        if (SUCCEEDED(first) && FAILED(hr)) first = hr;
    }
    a = Applied{};
    return first;
}
// An Ops from two callables (lambdas).
template <class C, class P> struct Calls {
    C constants;
    P program;
};
template <class C, class P> Calls<C, P> calls(C c, P p) {
    return Calls<C, P>{c, p};
}
} // namespace x3m::weapon_effects::muzzle_reshade
