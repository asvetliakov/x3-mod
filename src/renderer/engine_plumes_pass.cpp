#include "engine_plumes_pass.h"
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
// Only our authored shaders are embedded (provenance: verification/results/engine-plume-{vertex,pixel}-program.json).
constexpr std::uint32_t vs_words[] = {
#include "engine_plume_vertex_program_inc.h"
};
constexpr std::uint32_t ps_words[] = {
#include "engine_plume_pixel_program_inc.h"
};
// The vs_3_0 twin of ps3_program_slots: the same instruction costs under the vertex version token.
std::uint32_t vs3_program_slots() noexcept {
    std::uint32_t copy[std::size(vs_words)];
    std::memcpy(copy, vs_words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, std::size(copy));
}
// The vertex layout of engine_plumes::Vertex (72 bytes).
constexpr D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          {0, 28, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
                                          {0, 44, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
                                          {0, 60, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                          {0, 64, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1},
                                          {0, 68, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 2},
                                          D3DDECL_END()};
static_assert(offsetof(engine_plumes::Vertex, local) == 12 && offsetof(engine_plumes::Vertex, shape) == 28 &&
                  offsetof(engine_plumes::Vertex, intensity) == 44 && offsetof(engine_plumes::Vertex, tint) == 60 &&
                  offsetof(engine_plumes::Vertex, params) == 64 && offsetof(engine_plumes::Vertex, fog) == 68,
              "the declaration follows the vertex");
bool finite(float v) noexcept {
    return v == v && v <= 3.4e38f && v >= -3.4e38f;
}
} // namespace

EnginePlumesPass::~EnginePlumesPass() {
    detach();
}
void EnginePlumesPass::release_objects() noexcept {
    drop(vb_);
    drop(ib_);
    drop(vs_);
    drop(ps_);
    drop(declaration_);
}
void EnginePlumesPass::detach() noexcept {
    PreserveCpuState guard;
    release_objects();
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
    reset_pending_ = false;
}
void EnginePlumesPass::before_reset() noexcept {
    PreserveCpuState guard;
    release_objects();
    reset_pending_ = device_ != nullptr;
}
void EnginePlumesPass::after_reset(HRESULT hr) noexcept {
    if (SUCCEEDED(hr)) reset_pending_ = false;
}
unsigned EnginePlumesPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(vs_), static_cast<const void*>(ps_), static_cast<const void*>(declaration_),
                          static_cast<const void*>(vb_), static_cast<const void*>(ib_)})
        n += p != nullptr;
    return n;
}
HRESULT EnginePlumesPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format) noexcept {
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
    // No refusal on the reported slot cap (docs/architecture/platform-portability.md, "Shader slot budget"): the pixel
    // program exceeds 512 since the end-on disc (after flight C); creation is the capability test (program_create below).
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) || !(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) ||
        !(caps.DestBlendCaps & D3DPBLENDCAPS_ONE))
        return refuse("blend_caps");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE)) return refuse("cull_none");
    if (caps.MaxVertexShaderConst < 2 || caps.MaxStreams < 1 || caps.MaxVertexIndex < engine_plumes::max_vertices - 1u ||
        caps.MaxPrimitiveCount < 4u * engine_plumes::max_nozzles)
        return refuse("limits");
    // FP16 post-pixel-shader blending on the scene format (the motes' query): ONE/ONE onto A16B16G16R16F.
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
// The programs, the declaration, the dynamic vertex buffer and the static index buffer; on failure nothing is held.
HRESULT EnginePlumesPass::create_objects(bool* programs) noexcept {
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
                engine_plumes::write_indices(static_cast<std::uint16_t*>(mapping), engine_plumes::max_nozzles);
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
HRESULT EnginePlumesPass::ensure_resources() noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return E_FAIL; // waiting for a Reset: not a device code (the resolve goes on)
    if (resources_ready()) return S_OK;
    PreserveCpuState guard;
    return create_objects(nullptr);
}
HRESULT EnginePlumesPass::run(const EnginePlumesFrame& f, EnginePlumesReport* output) noexcept {
    PreserveCpuState guard;
    EnginePlumesReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        if (output) *output = r;
        return hr;
    };
    auto refuse = [&](EnginePlumesStep step, HRESULT hr) {
        r.failed = step;
        if (lost(hr)) reset_pending_ = true;
        return finish(hr);
    };
    if (!device_ || !caps_.enabled) return refuse(EnginePlumesStep::Validate, E_INVALIDARG);
    if (reset_pending_) return refuse(EnginePlumesStep::Validate, E_FAIL);
    if (!f.width || !f.height || !f.lane || (f.record_count && !f.records) || !finite(f.view.m00) || !(f.view.m00 > 0.f) ||
        !finite(f.view.m11) || !(f.view.m11 > 0.f) || !finite(f.m20) || !finite(f.m21) || !finite(f.m22) ||
        !finite(f.m32) || !(f.view.near_z > 0.f) || !(f.view.height > 0.f) || !finite(f.seconds) ||
        !finite(f.phase))
        return refuse(EnginePlumesStep::Validate, E_INVALIDARG);
    for (float v : f.view.rows)
        if (!finite(v)) return refuse(EnginePlumesStep::Validate, E_INVALIDARG);
    if (!f.record_count) return finish(S_FALSE);
    HRESULT hr = ensure_resources();
    if (FAILED(hr)) return refuse(EnginePlumesStep::Resources, hr);
    D d = device_;
    // Built straight into the DISCARD-locked buffer: the lock covers every record's eight vertices; only the nozzles
    // that pass the screen rules are drawn.
    const unsigned capacity = f.record_count < engine_plumes::max_nozzles ? f.record_count : engine_plumes::max_nozzles;
    void* mapping = nullptr;
    hr = vb_->Lock(0, capacity * engine_plumes::vertices_per_nozzle * UINT(sizeof(engine_plumes::Vertex)), &mapping,
                   D3DLOCK_DISCARD);
    if (FAILED(hr) || !mapping) {
        if (SUCCEEDED(hr)) {
            vb_->Unlock();
            hr = E_FAIL;
        }
        return refuse(EnginePlumesStep::Lock, hr);
    }
    const engine_plumes::Look& look = f.look ? *f.look : engine_plumes::default_look;
    // The look's tables (the end-on disc's samples, the side view's crest): the proxy caches them at load; a frame
    // without them (the fixture) computes them once here for the build and the constants.
    engine_plumes::LookTables computed;
    const engine_plumes::LookTables* tables = f.tables;
    if (!tables) {
        engine_plumes::look_tables(look, &computed);
        tables = &computed;
    }
    const unsigned nozzles = engine_plumes::build(f.records, f.record_count, f.body, f.view, f.preset, f.seconds,
                                                  static_cast<engine_plumes::Vertex*>(mapping), capacity, &r.stats,
                                                  f.filter.camera && f.filter.scene ? &f.filter : nullptr, &look, tables,
                                                  f.radii);
    hr = vb_->Unlock();
    if (FAILED(hr)) return refuse(EnginePlumesStep::Lock, hr);
    if (!nozzles) return finish(S_FALSE); // nothing drawable: no render state touched
    auto step = [&](EnginePlumesStep at, HRESULT value) {
        if (FAILED(value) && SUCCEEDED(hr)) {
            hr = value;
            r.failed = at;
        }
        return SUCCEEDED(hr);
    };
    const float projection[4] = {f.view.m00, f.view.m11, f.m20, f.m21};
    const float limits[4] = {f.view.near_z, 0.f, 0.f, 0.f};
    // c0 sizes and the flow phase, c1 the lane's form, c2 the lane terms and the clock, c3..c16 the look
    // (engine_plumes_core.h Look, the end-on disc's samples in c8..c15, the mouth ramp in c16): one call for the
    // seventeen registers.
    float pixel[12 + engine_plumes::pixel_constant_floats] = {1.f / float(f.width), 1.f / float(f.height), f.phase, 0.f, f.lane_four_channel ? 1.f : 0.f, f.m22, f.m32, 0.f,
                       engine_plumes::soft_core, engine_plumes::soft_halo, engine_plumes::halo_reach, f.seconds};
    engine_plumes::pixel_constants(look, *tables, pixel + 12);
    step(EnginePlumesStep::State, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 0, projection, 1));
    step(EnginePlumesStep::State, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 1, limits, 1));
    step(EnginePlumesStep::State, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, pixel, 17));
    step(EnginePlumesStep::State, call<SetTextureFn>(SetTexture)(d, 0, f.lane));
    for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_POINT)},
                   std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_NONE)}, std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)},
                   std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_SRGBTEXTURE, DWORD(FALSE)}})
        step(EnginePlumesStep::State, call<SetSamplerFn>(SetSamplerState)(d, 0, s.first, s.second));
    for (auto s : {std::pair{D3DRS_CLIPPING, DWORD(TRUE)}, std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)},
                   std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)},
                   std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                   std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_SEPARATEALPHABLENDENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}})
        step(EnginePlumesStep::State, call<SetRsFn>(SetRenderState)(d, s.first, s.second));
    step(EnginePlumesStep::Draw, call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration_));
    step(EnginePlumesStep::Draw, call<SetStreamFn>(SetStreamSource)(d, 0, vb_, 0, sizeof(engine_plumes::Vertex)));
    step(EnginePlumesStep::Draw, call<SetIndicesFn>(SetIndices)(d, ib_));
    step(EnginePlumesStep::Draw, call<SetVsFn>(SetVertexShader)(d, vs_));
    step(EnginePlumesStep::Draw, call<SetPsFn>(SetPixelShader)(d, ps_));
    if (SUCCEEDED(hr)) {
        HRESULT draw = call<DrawIndexedFn>(DrawIndexedPrimitive)(d, D3DPT_TRIANGLELIST, 0, 0,
                                                                 nozzles * engine_plumes::vertices_per_nozzle, 0, nozzles * 4u);
        if (faults_ & 6u) {
            faults_ &= ~2u;
            draw = E_FAIL;
        }
        if (step(EnginePlumesStep::Draw, draw)) r.drew = true;
    }
    if (FAILED(hr) && lost(hr)) reset_pending_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
