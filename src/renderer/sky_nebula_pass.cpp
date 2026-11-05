#include "sky_nebula_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
#include "../proxy/sky_nebula_core.h"
#include <cstring>
#include <iterator>
namespace x3m::renderer {
namespace {
template <class T> void drop(T*& value) noexcept {
    if (value) {
        value->Release();
        value = nullptr;
    }
}
// clang-format off
enum Slot : unsigned {
    CreateVertexShader = 91, SetVertexShader = 92, GetVertexShader = 93, SetVertexShaderConstantF = 94,
    GetVertexShaderConstantF = 95, CreatePixelShader = 106, SetPixelShader = 107, GetPixelShader = 108,
    SetPixelShaderConstantF = 109, GetPixelShaderConstantF = 110
};
// clang-format on
using D = IDirect3DDevice9*;
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
using SetConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using GetConstantsFn = HRESULT(WINAPI*)(D, UINT, float*, UINT);
// Only our authored shaders are embedded (provenance: verification/results/sky-nebula-{vertex,pixel}-program.json;
// native validator: verification/results/sky-nebula/native/result.json).
constexpr std::uint32_t vs_words[] = {
#include "sky_nebula_vertex_program_inc.h"
};
constexpr std::uint32_t ps_words[] = {
#include "sky_nebula_pixel_program_inc.h"
};
std::uint32_t vs3_program_slots() noexcept {
    std::uint32_t copy[std::size(vs_words)];
    std::memcpy(copy, vs_words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, std::size(copy));
}
constexpr UINT vs_constant = sky_nebula::core::vs_constant_jitter;
constexpr UINT ps_first = sky_nebula::core::ps_constant_first, ps_count = sky_nebula::core::ps_constant_count;
} // namespace

SkyNebulaPass::~SkyNebulaPass() {
    detach();
}
void SkyNebulaPass::release_objects() noexcept {
    drop(vs_);
    drop(ps_);
}
void SkyNebulaPass::detach() noexcept {
    PreserveCpuState guard;
    release_objects();
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
}
HRESULT SkyNebulaPass::attach(D d, void* const* native, const D3DCAPS9& caps) noexcept {
    PreserveCpuState guard;
    detach();
    auto refuse = [&](const char* reason, HRESULT hr = D3DERR_NOTAVAILABLE) {
        release_objects();
        device_ = nullptr;
        vtable_ = nullptr;
        caps_.enabled = false;
        caps_.reason = reason;
        return hr;
    };
    if (!d || !native) return refuse("device_native_table", E_INVALIDARG);
    device_ = d;
    vtable_ = native;
    if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || caps.VertexShaderVersion < D3DVS_VERSION(3, 0))
        return refuse("shader_model3");
    caps_.vs_slots = vs3_program_slots();
    caps_.ps_slots = ps3_program_slots(ps_words, std::size(ps_words));
    if (!caps_.vs_slots || !caps_.ps_slots) return refuse("compiled_program");
    // begin() reads every binding it changes before the first write: a device whose getters fail is refused here,
    // before any part draw is swapped.
    {
        float constants[8];
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        const bool ok = SUCCEEDED(call<GetVsFn>(GetVertexShader)(d, &vs)) &&
                        SUCCEEDED(call<GetPsFn>(GetPixelShader)(d, &ps)) &&
                        SUCCEEDED(call<GetConstantsFn>(GetVertexShaderConstantF)(d, vs_constant, constants, 1)) &&
                        SUCCEEDED(call<GetConstantsFn>(GetPixelShaderConstantF)(d, ps_first, constants, ps_count));
        drop(vs);
        drop(ps);
        if (!ok) return refuse("getters");
    }
    HRESULT hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(vs_words), &vs_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(ps_words), &ps_);
    if (SUCCEEDED(hr) && !(vs_ && ps_)) hr = E_FAIL;
    if (FAILED(hr)) return refuse("create", hr);
    caps_.enabled = true;
    caps_.reason = "ok";
    return S_OK;
}
HRESULT SkyNebulaPass::begin(const float jitter[4], const float pixel[8], SkyNebulaSaved* saved,
                             SkyNebulaReport* output) noexcept {
    PreserveCpuState guard;
    SkyNebulaReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        if (output) *output = r;
        return hr;
    };
    if (!saved) return finish(E_INVALIDARG);
    *saved = {};
    if (!ready()) return finish(E_FAIL);
    if (!jitter || !pixel) return finish(E_INVALIDARG);
    D d = device_;
    // ---- Save: every read before the first write; a failed read changes nothing.
    HRESULT hr = call<GetVsFn>(GetVertexShader)(d, &saved->vs);
    if (SUCCEEDED(hr)) hr = call<GetPsFn>(GetPixelShader)(d, &saved->ps);
    if (SUCCEEDED(hr)) hr = call<GetConstantsFn>(GetVertexShaderConstantF)(d, vs_constant, saved->vs_c255, 1);
    if (SUCCEEDED(hr)) hr = call<GetConstantsFn>(GetPixelShaderConstantF)(d, ps_first, saved->ps_c200, ps_count);
    if (FAILED(hr)) {
        drop(saved->vs);
        drop(saved->ps);
        *saved = {};
        return finish(hr);
    }
    // ---- Set; every attempted write is recorded for the restore.
    saved->written |= 1u;
    hr = call<SetConstantsFn>(SetVertexShaderConstantF)(d, vs_constant, jitter, 1);
    if (SUCCEEDED(hr)) {
        saved->written |= 2u;
        hr = call<SetConstantsFn>(SetPixelShaderConstantF)(d, ps_first, pixel, ps_count);
    }
    if (SUCCEEDED(hr)) {
        saved->written |= 4u;
        hr = call<SetVsFn>(SetVertexShader)(d, vs_);
    }
    if (SUCCEEDED(hr)) {
        saved->written |= 8u;
        hr = call<SetPsFn>(SetPixelShader)(d, ps_);
        if (faults_ & 1u) {
            faults_ &= ~1u;
            hr = E_FAIL;
        }
    }
    if (FAILED(hr)) {
        r.restore = restore(saved);
        return finish(hr);
    }
    return finish(S_OK);
}
// Reverse order of begin(): PS, VS, PS constants, VS constant; the first failure is returned, every restoration is still
// attempted; the saved references are released and *saved cleared.
HRESULT SkyNebulaPass::restore(SkyNebulaSaved* s) noexcept {
    HRESULT result = S_OK;
    auto put = [&](HRESULT value) {
        if (faults_ & 2u) {
            faults_ &= ~2u;
            value = E_FAIL;
        }
        if (FAILED(value) && SUCCEEDED(result)) result = value;
    };
    D d = device_;
    if (d && vtable_) {
        if (s->written & 8u) put(call<SetPsFn>(SetPixelShader)(d, s->ps));
        if (s->written & 4u) put(call<SetVsFn>(SetVertexShader)(d, s->vs));
        if (s->written & 2u) put(call<SetConstantsFn>(SetPixelShaderConstantF)(d, ps_first, s->ps_c200, ps_count));
        if (s->written & 1u) put(call<SetConstantsFn>(SetVertexShaderConstantF)(d, vs_constant, s->vs_c255, 1));
    } else if (s->written)
        result = E_FAIL;
    drop(s->vs);
    drop(s->ps);
    *s = {};
    return result;
}
HRESULT SkyNebulaPass::end(SkyNebulaSaved* saved, unsigned* calls) noexcept {
    PreserveCpuState guard;
    if (!saved) return E_INVALIDARG;
    const unsigned initial = calls_;
    const HRESULT hr = restore(saved);
    if (calls) *calls = calls_ - initial;
    return hr;
}
} // namespace x3m::renderer
