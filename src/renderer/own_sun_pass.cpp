#include "own_sun_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <utility>
namespace x3m::renderer {
namespace {
template <class T> void drop(T*& value) noexcept {
    if (value) {
        value->Release();
        value = nullptr;
    }
}
bool lost(HRESULT hr) noexcept {
    return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET;
}
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, SetRenderState = 57, SetTexture = 65, SetSamplerState = 69,
    DrawPrimitiveUP = 83, CreateVertexDeclaration = 86, SetVertexDeclaration = 87, CreateVertexShader = 91,
    SetVertexShader = 92, CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored shaders are embedded (provenance: verification/results/own-sun-{vertex,pixel}-program.json; native
// validator: verification/results/own-sun-pass/native/result.json).
constexpr std::uint32_t vs_words[] = {
#include "own_sun_vertex_program_inc.h"
};
constexpr std::uint32_t ps_words[] = {
#include "own_sun_pixel_program_inc.h"
};
// The vs_3_0 twin of ps3_program_slots: the same instruction costs under the vertex version token.
std::uint32_t vs3_program_slots() noexcept {
    std::uint32_t copy[std::size(vs_words)];
    std::memcpy(copy, vs_words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, std::size(copy));
}
// own_sun::core::Vertex: clip position, disc coordinate.
constexpr D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
static_assert(sizeof(own_sun::core::Vertex) == 24, "the declaration follows the vertex");
bool finite(float v) noexcept {
    return v == v && v <= 3.4e38f && v >= -3.4e38f;
}
} // namespace

OwnSunPass::~OwnSunPass() {
    detach();
}
void OwnSunPass::release_objects() noexcept {
    drop(vs_);
    drop(ps_);
    drop(declaration_);
}
void OwnSunPass::detach() noexcept {
    PreserveCpuState guard;
    release_objects();
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
    reset_pending_ = false;
}
void OwnSunPass::before_reset() noexcept {
    PreserveCpuState guard;
    release_objects();
    reset_pending_ = device_ != nullptr;
}
void OwnSunPass::after_reset(HRESULT hr) noexcept {
    if (SUCCEEDED(hr)) reset_pending_ = false;
}
unsigned OwnSunPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(vs_), static_cast<const void*>(ps_), static_cast<const void*>(declaration_)})
        n += p != nullptr;
    return n;
}
HRESULT OwnSunPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format) noexcept {
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
    // No refusal on the reported slot cap (platform-portability.md, "Shader slot budget"): creation is the test.
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) || !(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) ||
        !(caps.DestBlendCaps & D3DPBLENDCAPS_ONE))
        return refuse("blend_caps");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE)) return refuse("cull_none");
    if (caps.MaxPrimitiveCount < 2u || caps.MaxStreams < 1u) return refuse("limits");
    // FP16 post-pixel-shader blending on the scene format (the plumes' and motes' query): ONE/ONE onto A16B16G16R16F.
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr))
        hr = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
                                    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE,
                                    D3DFMT_A16B16G16R16F);
    drop(api);
    caps_.fp16_blending = hr;
    if (faults_ & 1u) {
        caps_.fp16_blending = D3DERR_NOTAVAILABLE;
        hr = D3DERR_NOTAVAILABLE;
    }
    if (hr != D3D_OK) return refuse("fp16_blending", D3DERR_NOTAVAILABLE);
    hr = create_objects();
    if (FAILED(hr)) return refuse("program_create", hr);
    caps_.enabled = true;
    caps_.reason = "ok";
    return S_OK;
}
// The programs and the declaration; on failure nothing is held.
HRESULT OwnSunPass::create_objects() noexcept {
    D d = device_;
    HRESULT hr = S_OK;
    if (!vs_) hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(vs_words), &vs_);
    if (SUCCEEDED(hr) && !ps_) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(ps_words), &ps_);
    if (SUCCEEDED(hr) && !declaration_) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, elements, &declaration_);
    if (SUCCEEDED(hr) && !(vs_ && ps_ && declaration_)) hr = E_FAIL;
    if (FAILED(hr)) release_objects();
    return hr;
}
HRESULT OwnSunPass::ensure_resources() noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return E_FAIL; // waiting for a Reset: not a device code (the resolve goes on)
    if (resources_ready()) return S_OK;
    PreserveCpuState guard;
    return create_objects();
}
HRESULT OwnSunPass::run(const OwnSunFrame& f, OwnSunReport* output) noexcept {
    PreserveCpuState guard;
    OwnSunReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        if (output) *output = r;
        return hr;
    };
    auto refuse = [&](OwnSunStep step, HRESULT hr) {
        r.failed = step;
        if (lost(hr)) reset_pending_ = true;
        return finish(hr);
    };
    if (!device_ || !caps_.enabled) return refuse(OwnSunStep::Validate, E_INVALIDARG);
    if (reset_pending_) return refuse(OwnSunStep::Validate, E_FAIL);
    if (!f.width || !f.height || !f.lane) return refuse(OwnSunStep::Validate, E_INVALIDARG);
    if (f.disc.skip != own_sun::core::Skip::None || f.radiance.black) return finish(S_FALSE); // nothing to draw
    if (!finite(f.disc.x) || !finite(f.disc.y) || !finite(f.disc.half_x) || !finite(f.disc.half_y) ||
        !(f.disc.half_x > 0.f) || !(f.disc.half_y > 0.f))
        return refuse(OwnSunStep::Validate, E_INVALIDARG);
    for (unsigned i = 0; i < 3; ++i)
        if (!finite(f.radiance.disc[i]) || !finite(f.radiance.corona[i])) return refuse(OwnSunStep::Validate, E_INVALIDARG);
    HRESULT hr = ensure_resources();
    if (FAILED(hr)) return refuse(OwnSunStep::Resources, hr);
    D d = device_;
    auto step = [&](OwnSunStep at, HRESULT value) {
        if (FAILED(value) && SUCCEEDED(hr)) {
            hr = value;
            r.failed = at;
        }
        return SUCCEEDED(hr);
    };
    own_sun::core::Vertex vertices[4];
    own_sun::core::quad(f.disc, vertices);
    float pixel[own_sun::core::pixel_registers * 4];
    own_sun::core::pixel_constants(f.width, f.height, f.radiance, pixel);
    step(OwnSunStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, pixel, own_sun::core::pixel_registers));
    step(OwnSunStep::State, call<SetTextureFn>(SetTexture)(d, 0, f.lane));
    for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_POINT)},
                   std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_NONE)}, std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)},
                   std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_SRGBTEXTURE, DWORD(FALSE)}})
        step(OwnSunStep::State, call<SetSamplerFn>(SetSamplerState)(d, 0, s.first, s.second));
    for (auto s : {std::pair{D3DRS_CLIPPING, DWORD(TRUE)}, std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)},
                   std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)},
                   std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                   std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_SEPARATEALPHABLENDENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}})
        step(OwnSunStep::State, call<SetRsFn>(SetRenderState)(d, s.first, s.second));
    step(OwnSunStep::Draw, call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration_));
    step(OwnSunStep::Draw, call<SetVsFn>(SetVertexShader)(d, vs_));
    step(OwnSunStep::Draw, call<SetPsFn>(SetPixelShader)(d, ps_));
    if (SUCCEEDED(hr)) {
        HRESULT draw = call<DrawUpFn>(DrawPrimitiveUP)(d, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(own_sun::core::Vertex));
        if (faults_ & 6u) {
            faults_ &= ~2u;
            draw = E_FAIL;
        }
        if (step(OwnSunStep::Draw, draw)) r.drew = true;
    }
    if (FAILED(hr) && lost(hr)) reset_pending_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
