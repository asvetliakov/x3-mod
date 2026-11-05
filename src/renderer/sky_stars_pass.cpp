#include "sky_stars_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
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
bool lost(HRESULT hr) noexcept {
    return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET;
}
// clang-format off
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateVertexBuffer = 26, CreateIndexBuffer = 27,
    SetRenderState = 57, GetRenderState = 58, DrawIndexedPrimitive = 82,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92, GetVertexShader = 93, SetVertexShaderConstantF = 94,
    GetVertexShaderConstantF = 95, SetStreamSource = 100, GetStreamSource = 101, SetStreamSourceFreq = 102,
    GetStreamSourceFreq = 103, SetIndices = 104, GetIndices = 105, CreatePixelShader = 106, SetPixelShader = 107,
    GetPixelShader = 108
};
// clang-format on
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateVbFn = HRESULT(WINAPI*)(D, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
using CreateIbFn = HRESULT(WINAPI*)(D, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using GetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD*);
using DrawIndexedFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using SetConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using GetConstantsFn = HRESULT(WINAPI*)(D, UINT, float*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using GetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using GetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9**);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
// Only our authored shaders are embedded (provenance: verification/results/sky-stars-{vertex,pixel}-program.json; native
// validator: verification/results/sky-stars/native/result.json).
constexpr std::uint32_t vs_words[] = {
#include "sky_stars_vertex_program_inc.h"
};
constexpr std::uint32_t ps_words[] = {
#include "sky_stars_pixel_program_inc.h"
};
std::uint32_t vs3_program_slots() noexcept {
    std::uint32_t copy[std::size(vs_words)];
    std::memcpy(copy, vs_words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, std::size(copy));
}
// sky_stars::core::Vertex: direction, corner px, energy, tint, twinkle lanes.
constexpr D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          {0, 20, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
                                          {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
                                          {0, 28, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1},
                                          D3DDECL_END()};
// The vertex constants the batch owns: c251 (twinkle) and c252-c255 (projection), one Get/Set each.
constexpr UINT constants_first = 251, constants_count = 5;
constexpr UINT vertex_bytes = sky_stars::core::vertex_capacity * sky_stars::core::vertex_stride;
constexpr UINT index_bytes = sky_stars::core::stars_max * sky_stars::core::indices_per_star * 2u;
// The render states the batch sets (value) and restores: ONE/ONE additive RGB, no depth, no stencil, no cull, no
// alpha test, no sRGB write, no fog, no scissor, no user clip planes, clipping on, solid fill, no texture-coordinate
// wrapping on the two interpolated coordinates.
struct Touched {
    D3DRENDERSTATETYPE state;
    DWORD value;
};
constexpr Touched touched[] = {
    {D3DRS_ALPHABLENDENABLE, TRUE},         {D3DRS_SRCBLEND, D3DBLEND_ONE},        {D3DRS_DESTBLEND, D3DBLEND_ONE},
    {D3DRS_BLENDOP, D3DBLENDOP_ADD},        {D3DRS_SEPARATEALPHABLENDENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, 7},
    {D3DRS_ZENABLE, D3DZB_FALSE},           {D3DRS_ZWRITEENABLE, FALSE},           {D3DRS_CULLMODE, D3DCULL_NONE},
    {D3DRS_ALPHATESTENABLE, FALSE},         {D3DRS_SRGBWRITEENABLE, FALSE},        {D3DRS_FOGENABLE, FALSE},
    {D3DRS_STENCILENABLE, FALSE},           {D3DRS_SCISSORTESTENABLE, FALSE},      {D3DRS_CLIPPING, TRUE},
    {D3DRS_FILLMODE, D3DFILL_SOLID},        {D3DRS_CLIPPLANEENABLE, 0},            {D3DRS_WRAP0, 0},
    {D3DRS_WRAP1, 0}};
constexpr unsigned touched_count = unsigned(std::size(touched));
static_assert(touched_count <= 32, "one bit per touched state");
} // namespace

SkyStarsPass::~SkyStarsPass() {
    detach();
}
void SkyStarsPass::release_objects() noexcept {
    drop(vs_);
    drop(ps_);
    drop(declaration_);
    drop(vb_);
    drop(ib_);
    built_seed_ = 0;
    built_key_ = 0;
    built_look_ = {};
    built_stats_ = {};
    built_stars_ = 0;
}
void SkyStarsPass::detach() noexcept {
    PreserveCpuState guard;
    release_objects();
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
    lost_ = false;
}
unsigned SkyStarsPass::references() const noexcept {
    const void* objects[] = {vs_, ps_, declaration_, vb_, ib_};
    unsigned n = 0;
    for (const void* p : objects) n += p != nullptr;
    return n;
}
HRESULT SkyStarsPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format) noexcept {
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
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) || !(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) ||
        !(caps.DestBlendCaps & D3DPBLENDCAPS_ONE))
        return refuse("blend_caps");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE)) return refuse("cull_none");
    if (caps.MaxPrimitiveCount < sky_stars::core::stars_max * 2u ||
        caps.MaxVertexIndex < sky_stars::core::vertex_capacity - 1u || caps.MaxStreams < 1u)
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
    if (hr != D3D_OK) return refuse("fp16_blending");
    // The batch reads every state it changes before the first write (draw()): a device whose getters fail (a pure
    // device) is refused here, before any star body is ever masked.
    {
        DWORD value = 0;
        float constants[4];
        if (FAILED(call<GetRsFn>(GetRenderState)(d, D3DRS_COLORWRITEENABLE, &value)) ||
            FAILED(call<GetConstantsFn>(GetVertexShaderConstantF)(d, constants_first, constants, 1)))
            return refuse("getters");
    }
    hr = create_objects();
    if (FAILED(hr)) return refuse("create", hr);
    caps_.enabled = true;
    caps_.reason = "ok";
    return S_OK;
}
// Programs, declaration and the two MANAGED buffers; the index list is written here once. On failure nothing is held.
HRESULT SkyStarsPass::create_objects() noexcept {
    D d = device_;
    HRESULT hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(vs_words), &vs_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(ps_words), &ps_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, elements, &declaration_);
    if (SUCCEEDED(hr))
        hr = call<CreateVbFn>(CreateVertexBuffer)(d, vertex_bytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL(pool_), &vb_, nullptr);
    if (SUCCEEDED(hr))
        hr = call<CreateIbFn>(CreateIndexBuffer)(d, index_bytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL(pool_),
                                                 &ib_, nullptr);
    if (SUCCEEDED(hr) && !(vs_ && ps_ && declaration_ && vb_ && ib_)) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        void* mapping = nullptr;
        hr = ib_->Lock(0, index_bytes, &mapping, 0);
        if (SUCCEEDED(hr) && !mapping) hr = E_FAIL;
        if (SUCCEEDED(hr)) {
            sky_stars::core::build_indices(static_cast<std::uint16_t*>(mapping), sky_stars::core::stars_max);
            hr = ib_->Unlock();
        }
    }
    if (FAILED(hr)) release_objects();
    return hr;
}
unsigned SkyStarsPass::prepare(std::uint32_t seed, const sky_stars::core::Look& look, std::uint64_t sector_key,
                               bool* rebuilt) noexcept {
    if (rebuilt) *rebuilt = false;
    if (!ready()) return 0;
    if (sky_stars::core::same_build(seed, look, sector_key, built_seed_, built_look_, built_key_) && built_stars_)
        return built_stars_;
    PreserveCpuState guard;
    built_seed_ = seed;
    built_key_ = sector_key;
    built_look_ = look;
    built_stars_ = 0;
    built_stats_ = {};
    const unsigned n =
        sky_stars::core::star_count(sky_stars::core::seed_k(seed), sky_stars::core::sanitized(look).density);
    void* mapping = nullptr;
    HRESULT hr = vb_->Lock(0, n * sky_stars::core::vertices_per_star * sky_stars::core::vertex_stride, &mapping, 0);
    if (FAILED(hr) || !mapping) {
        if (SUCCEEDED(hr)) vb_->Unlock();
        if (lost(hr)) lost_ = true;
        return 0;
    }
    sky_stars::core::BuildStats stats;
    const unsigned built = sky_stars::core::build(seed, look, static_cast<sky_stars::core::Vertex*>(mapping),
                                                  sky_stars::core::stars_max, &stats, sector_key);
    hr = vb_->Unlock();
    if (FAILED(hr)) {
        if (lost(hr)) lost_ = true;
        return 0;
    }
    built_stars_ = built;
    built_stats_ = stats;
    if (rebuilt) *rebuilt = true;
    return built;
}
HRESULT SkyStarsPass::draw(const float constants[16], const float twinkle[4], SkyStarsReport* output) noexcept {
    PreserveCpuState guard;
    SkyStarsReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        r.stars = built_stars_;
        if (output) *output = r;
        return hr;
    };
    if (!ready()) {
        r.failed = SkyStarsStep::Resources;
        return finish(lost_ ? D3DERR_DEVICELOST : E_FAIL);
    }
    if (!built_stars_ || !constants || !twinkle) {
        r.failed = SkyStarsStep::Build;
        return finish(E_INVALIDARG);
    }
    D d = device_;
    // ---- Save: every read before the first write; a failed read changes nothing.
    IDirect3DVertexShader9* saved_vs = nullptr;
    IDirect3DPixelShader9* saved_ps = nullptr;
    IDirect3DVertexDeclaration9* saved_declaration = nullptr;
    IDirect3DVertexBuffer9* saved_stream = nullptr;
    IDirect3DIndexBuffer9* saved_indices = nullptr;
    UINT saved_offset = 0, saved_stride = 0, saved_frequency = 1;
    DWORD saved_fvf = 0, saved_states[touched_count]{};
    float saved_constants[constants_count * 4]{}, batch_constants[constants_count * 4];
    std::memcpy(batch_constants, twinkle, 4 * sizeof(float));
    std::memcpy(batch_constants + 4, constants, 16 * sizeof(float));
    auto release_saved = [&] {
        drop(saved_vs);
        drop(saved_ps);
        drop(saved_declaration);
        drop(saved_stream);
        drop(saved_indices);
    };
    HRESULT hr = call<GetVsFn>(GetVertexShader)(d, &saved_vs);
    if (SUCCEEDED(hr)) hr = call<GetPsFn>(GetPixelShader)(d, &saved_ps);
    if (SUCCEEDED(hr)) hr = call<GetFvfFn>(GetFVF)(d, &saved_fvf);
    if (SUCCEEDED(hr)) hr = call<GetDeclarationFn>(GetVertexDeclaration)(d, &saved_declaration);
    if (SUCCEEDED(hr)) hr = call<GetStreamFn>(GetStreamSource)(d, 0, &saved_stream, &saved_offset, &saved_stride);
    if (SUCCEEDED(hr)) hr = call<GetFreqFn>(GetStreamSourceFreq)(d, 0, &saved_frequency);
    if (SUCCEEDED(hr)) hr = call<GetIndicesFn>(GetIndices)(d, &saved_indices);
    if (SUCCEEDED(hr))
        hr = call<GetConstantsFn>(GetVertexShaderConstantF)(d, constants_first, saved_constants, constants_count);
    for (unsigned i = 0; i < touched_count && SUCCEEDED(hr); ++i)
        hr = call<GetRsFn>(GetRenderState)(d, touched[i].state, &saved_states[i]);
    if (FAILED(hr)) {
        release_saved();
        r.failed = SkyStarsStep::Save;
        if (lost(hr)) lost_ = true;
        return finish(hr);
    }
    // ---- Set and draw; every attempted write is recorded for the restore.
    std::uint32_t written = 0;
    bool constants_set = false, declaration_set = false, vs_set = false, ps_set = false, stream_set = false,
         frequency_set = false, indices_set = false;
    auto step = [&](SkyStarsStep at, HRESULT value) {
        if (FAILED(value) && SUCCEEDED(hr)) {
            hr = value;
            r.failed = at;
        }
        return SUCCEEDED(hr);
    };
    for (unsigned i = 0; i < touched_count && SUCCEEDED(hr); ++i) {
        if (saved_states[i] == touched[i].value) continue;
        written |= 1u << i;
        step(SkyStarsStep::State, call<SetRsFn>(SetRenderState)(d, touched[i].state, touched[i].value));
    }
    if (SUCCEEDED(hr)) {
        constants_set = true;
        step(SkyStarsStep::State, call<SetConstantsFn>(SetVertexShaderConstantF)(d, constants_first, batch_constants,
                                                                                  constants_count));
    }
    if (SUCCEEDED(hr)) {
        declaration_set = true;
        step(SkyStarsStep::State, call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration_));
    }
    if (SUCCEEDED(hr)) {
        vs_set = true;
        step(SkyStarsStep::State, call<SetVsFn>(SetVertexShader)(d, vs_));
    }
    if (SUCCEEDED(hr)) {
        ps_set = true;
        step(SkyStarsStep::State, call<SetPsFn>(SetPixelShader)(d, ps_));
    }
    if (SUCCEEDED(hr)) {
        stream_set = true;
        step(SkyStarsStep::State,
             call<SetStreamFn>(SetStreamSource)(d, 0, vb_, 0, sky_stars::core::vertex_stride));
    }
    if (SUCCEEDED(hr) && saved_frequency != 1) {
        frequency_set = true;
        step(SkyStarsStep::State, call<SetFreqFn>(SetStreamSourceFreq)(d, 0, 1));
    }
    if (SUCCEEDED(hr)) {
        indices_set = true;
        step(SkyStarsStep::State, call<SetIndicesFn>(SetIndices)(d, ib_));
    }
    if (SUCCEEDED(hr)) {
        HRESULT drawn = call<DrawIndexedFn>(DrawIndexedPrimitive)(
            d, D3DPT_TRIANGLELIST, 0, 0, built_stars_ * sky_stars::core::vertices_per_star, 0, built_stars_ * 2u);
        if (faults_ & 1u) {
            faults_ &= ~1u;
            drawn = E_FAIL;
        }
        if (step(SkyStarsStep::Draw, drawn)) r.drew = true;
    }
    // ---- Restore in reverse order; the first failure is reported, every restore is still attempted.
    HRESULT restore = S_OK;
    auto put = [&](HRESULT value) {
        if (FAILED(value) && SUCCEEDED(restore)) restore = value;
    };
    for (unsigned i = touched_count; i-- > 0;)
        if (written & (1u << i)) {
            HRESULT v = call<SetRsFn>(SetRenderState)(d, touched[i].state, saved_states[i]);
            if (faults_ & 2u) {
                faults_ &= ~2u;
                v = E_FAIL;
            }
            put(v);
        }
    if (indices_set) put(call<SetIndicesFn>(SetIndices)(d, saved_indices));
    if (frequency_set) put(call<SetFreqFn>(SetStreamSourceFreq)(d, 0, saved_frequency));
    if (stream_set) put(call<SetStreamFn>(SetStreamSource)(d, 0, saved_stream, saved_offset, saved_stride));
    if (ps_set) put(call<SetPsFn>(SetPixelShader)(d, saved_ps));
    if (vs_set) put(call<SetVsFn>(SetVertexShader)(d, saved_vs));
    if (declaration_set) {
        // FVF and declaration are two views of one binding: restored through whichever the application used.
        if (saved_fvf)
            put(call<SetFvfFn>(SetFVF)(d, saved_fvf));
        else
            put(call<SetDeclarationFn>(SetVertexDeclaration)(d, saved_declaration));
    }
    if (constants_set)
        put(call<SetConstantsFn>(SetVertexShaderConstantF)(d, constants_first, saved_constants, constants_count));
    release_saved();
    r.restore = restore;
    if (lost(hr) || lost(restore)) lost_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
