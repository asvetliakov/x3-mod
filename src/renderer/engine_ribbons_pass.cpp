#include "engine_ribbons_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
#include <cstddef>
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
    GetDirect3D = 6, GetCreationParameters = 9, CreateVertexBuffer = 26, CreateIndexBuffer = 27, SetRenderState = 57,
    SetTexture = 65, SetSamplerState = 69, DrawIndexedPrimitive = 82, CreateVertexDeclaration = 86,
    SetVertexDeclaration = 87, CreateVertexShader = 91, SetVertexShader = 92, SetVertexShaderConstantF = 94,
    SetStreamSource = 100, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateVbFn = HRESULT(WINAPI*)(D, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
using CreateIbFn = HRESULT(WINAPI*)(D, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using DrawIndexedFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetVsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored shaders are embedded (provenance: verification/results/engine-ribbon-{vertex,pixel}-program.json).
constexpr std::uint32_t vs_words[] = {
#include "engine_ribbon_vertex_program_inc.h"
};
constexpr std::uint32_t ps_words[] = {
#include "engine_ribbon_pixel_program_inc.h"
};
std::uint32_t vs3_program_slots() noexcept {
    std::uint32_t copy[std::size(vs_words)];
    std::memcpy(copy, vs_words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, std::size(copy));
}
// The vertex layout of engine_ribbons::Vertex (48 bytes).
constexpr D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          {0, 28, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
                                          {0, 44, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                          D3DDECL_END()};
static_assert(offsetof(engine_ribbons::Vertex, strip) == 12 && offsetof(engine_ribbons::Vertex, shape) == 28 &&
                  offsetof(engine_ribbons::Vertex, tint) == 44,
              "the declaration follows the vertex");
bool finite(float v) noexcept {
    return v == v && v <= 3.4e38f && v >= -3.4e38f;
}
} // namespace

EngineRibbonsPass::~EngineRibbonsPass() {
    detach();
}
void EngineRibbonsPass::release_objects() noexcept {
    drop(vb_);
    drop(ib_);
    drop(vs_);
    drop(ps_);
    drop(declaration_);
}
void EngineRibbonsPass::detach() noexcept {
    PreserveCpuState guard;
    release_objects();
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
    reset_pending_ = cut_pending_ = false;
    pool_.clear();
}
void EngineRibbonsPass::before_reset() noexcept {
    PreserveCpuState guard;
    release_objects();
    reset_pending_ = device_ != nullptr;
    if (pool_.live) ++pool_.reset_clears;
    pool_.clear();
}
void EngineRibbonsPass::after_reset(HRESULT hr) noexcept {
    if (SUCCEEDED(hr)) reset_pending_ = false;
}
unsigned EngineRibbonsPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(vs_), static_cast<const void*>(ps_), static_cast<const void*>(declaration_),
                          static_cast<const void*>(vb_), static_cast<const void*>(ib_)})
        n += p != nullptr;
    return n;
}
HRESULT EngineRibbonsPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format) noexcept {
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
    if (caps_.ps_slots > caps.MaxPixelShader30InstructionSlots || caps_.vs_slots > caps.MaxVertexShader30InstructionSlots)
        return refuse("compiled_slots");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) || !(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) ||
        !(caps.DestBlendCaps & D3DPBLENDCAPS_ONE))
        return refuse("blend_caps");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE)) return refuse("cull_none");
    if (caps.MaxVertexShaderConst < 2 || caps.MaxStreams < 1 || caps.MaxVertexIndex < engine_ribbons::max_vertices ||
        caps.MaxPrimitiveCount < engine_ribbons::triangles_per_ribbon * engine_ribbons::max_ribbons)
        return refuse("limits");
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
    bool programs = false;
    hr = create_objects(&programs);
    if (FAILED(hr)) return refuse(programs ? "buffers" : "program_create", hr);
    caps_.enabled = true;
    caps_.reason = "ok";
    return S_OK;
}
HRESULT EngineRibbonsPass::create_objects(bool* programs) noexcept {
    D d = device_;
    HRESULT hr = S_OK;
    if (!vs_) hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(vs_words), &vs_);
    if (SUCCEEDED(hr) && !ps_) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(ps_words), &ps_);
    if (SUCCEEDED(hr) && !declaration_) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, elements, &declaration_);
    if (SUCCEEDED(hr) && !(vs_ && ps_ && declaration_)) hr = E_FAIL;
    if (programs) *programs = SUCCEEDED(hr);
    if (SUCCEEDED(hr) && !vb_) {
        hr = call<CreateVbFn>(CreateVertexBuffer)(d, vertex_bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                                  &vb_, nullptr);
        if (SUCCEEDED(hr) && !vb_) hr = E_FAIL;
    }
    if (SUCCEEDED(hr) && !ib_) {
        hr = call<CreateIbFn>(CreateIndexBuffer)(d, index_bytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib_,
                                                 nullptr);
        if (SUCCEEDED(hr) && !ib_) hr = E_FAIL;
        void* mapping = nullptr;
        if (SUCCEEDED(hr)) {
            hr = ib_->Lock(0, 0, &mapping, 0);
            if (SUCCEEDED(hr) && mapping) {
                engine_ribbons::write_indices(static_cast<std::uint16_t*>(mapping), engine_ribbons::max_ribbons);
                hr = ib_->Unlock();
            } else if (SUCCEEDED(hr)) {
                ib_->Unlock();
                hr = E_FAIL;
            }
        }
    }
    if (FAILED(hr)) release_objects();
    return hr;
}
HRESULT EngineRibbonsPass::ensure_resources() noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return D3DERR_DEVICENOTRESET;
    if (resources_ready()) return S_OK;
    PreserveCpuState guard;
    return create_objects(nullptr);
}
HRESULT EngineRibbonsPass::run(const EngineRibbonsFrame& frame, EngineRibbonsReport* output) noexcept {
    PreserveCpuState guard;
    EngineRibbonsReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        if (output) *output = r;
        return hr;
    };
    auto refuse = [&](EngineRibbonsStep step, HRESULT hr) {
        r.failed = step;
        if (lost(hr)) reset_pending_ = true;
        return finish(hr);
    };
    if (!device_ || !caps_.enabled || !frame.base) return refuse(EngineRibbonsStep::Validate, E_INVALIDARG);
    const EnginePlumesFrame& f = *frame.base;
    if (!f.width || !f.height || !f.lane || (f.record_count && !f.records) || !finite(f.view.m00) || !(f.view.m00 > 0.f) ||
        !finite(f.view.m11) || !(f.view.m11 > 0.f) || !finite(f.m20) || !finite(f.m21) || !finite(f.m22) ||
        !finite(f.m32) || !(f.view.near_z > 0.f) || !(f.view.height > 0.f) || !(frame.seconds == frame.seconds))
        return refuse(EngineRibbonsStep::Validate, E_INVALIDARG);
    for (float v : f.view.rows)
        if (!finite(v)) return refuse(EngineRibbonsStep::Validate, E_INVALIDARG);
    // The pool follows the records on every stage frame, drawn or not (Reset pending included: CPU only).
    float scale = 1.f;
    engine_plumes::preset_scale(f.preset, &scale);
    engine_ribbons::update(pool_, f.records, f.record_count, frame.seconds, frame.cut || cut_pending_, frame.load_epoch, f.body,
                           scale, &r.update);
    cut_pending_ = false;
    r.updated = true;
    if (reset_pending_) return refuse(EngineRibbonsStep::Validate, D3DERR_DEVICENOTRESET);
    if (!pool_.live) return finish(S_FALSE);
    HRESULT hr = ensure_resources();
    if (FAILED(hr)) return refuse(EngineRibbonsStep::Resources, hr);
    D d = device_;
    const unsigned capacity = pool_.live < engine_ribbons::max_ribbons ? pool_.live : engine_ribbons::max_ribbons;
    void* mapping = nullptr;
    hr = vb_->Lock(0, capacity * engine_ribbons::vertices_per_ribbon * UINT(sizeof(engine_ribbons::Vertex)), &mapping,
                   D3DLOCK_DISCARD);
    if (FAILED(hr) || !mapping) {
        if (SUCCEEDED(hr)) {
            vb_->Unlock();
            hr = E_FAIL;
        }
        return refuse(EngineRibbonsStep::Lock, hr);
    }
    const unsigned ribbons = engine_ribbons::build(pool_, f.view, f.preset, frame.seconds,
                                                   static_cast<engine_ribbons::Vertex*>(mapping), capacity, &r.stats);
    hr = vb_->Unlock();
    if (FAILED(hr)) return refuse(EngineRibbonsStep::Lock, hr);
    if (!ribbons) return finish(S_FALSE); // nothing drawable: no render state touched
    auto step = [&](EngineRibbonsStep at, HRESULT value) {
        if (FAILED(value) && SUCCEEDED(hr)) {
            hr = value;
            r.failed = at;
        }
        return SUCCEEDED(hr);
    };
    const float projection[4] = {f.view.m00, f.view.m11, f.m20, f.m21};
    const float limits[4] = {f.view.near_z, 0.f, 0.f, 0.f};
    const float sizes[4] = {1.f / float(f.width), 1.f / float(f.height), 0.f, 0.f};
    const float lane_form[4] = {f.lane_four_channel ? 1.f : 0.f, f.m22, f.m32, 0.f};
    const float look[4] = {engine_ribbons::soft, 0.f, 0.f, 0.f};
    step(EngineRibbonsStep::State, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 0, projection, 1));
    step(EngineRibbonsStep::State, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 1, limits, 1));
    step(EngineRibbonsStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, sizes, 1));
    step(EngineRibbonsStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 1, lane_form, 1));
    step(EngineRibbonsStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 2, look, 1));
    step(EngineRibbonsStep::State, call<SetTextureFn>(SetTexture)(d, 0, f.lane));
    for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_POINT)},
                   std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_NONE)}, std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)},
                   std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_SRGBTEXTURE, DWORD(FALSE)}})
        step(EngineRibbonsStep::State, call<SetSamplerFn>(SetSamplerState)(d, 0, s.first, s.second));
    for (auto s : {std::pair{D3DRS_CLIPPING, DWORD(TRUE)}, std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)},
                   std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)},
                   std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                   std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_SEPARATEALPHABLENDENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}})
        step(EngineRibbonsStep::State, call<SetRsFn>(SetRenderState)(d, s.first, s.second));
    step(EngineRibbonsStep::Draw, call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration_));
    step(EngineRibbonsStep::Draw, call<SetStreamFn>(SetStreamSource)(d, 0, vb_, 0, sizeof(engine_ribbons::Vertex)));
    step(EngineRibbonsStep::Draw, call<SetIndicesFn>(SetIndices)(d, ib_));
    step(EngineRibbonsStep::Draw, call<SetVsFn>(SetVertexShader)(d, vs_));
    step(EngineRibbonsStep::Draw, call<SetPsFn>(SetPixelShader)(d, ps_));
    if (SUCCEEDED(hr)) {
        HRESULT draw = call<DrawIndexedFn>(DrawIndexedPrimitive)(d, D3DPT_TRIANGLELIST, 0, 0,
                                                                 ribbons * engine_ribbons::vertices_per_ribbon, 0,
                                                                 ribbons * engine_ribbons::triangles_per_ribbon);
        if (faults_ & 2u) {
            faults_ &= ~2u;
            draw = E_FAIL;
        }
        if (step(EngineRibbonsStep::Draw, draw)) r.drew = true;
    }
    if (FAILED(hr) && lost(hr)) reset_pending_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
