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
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, CreateVertexBuffer = 26, CreateIndexBuffer = 27,
    StretchRect = 34, SetRenderState = 57, GetRenderState = 58, GetTexture = 64, SetTexture = 65, GetSamplerState = 68,
    SetSamplerState = 69, DrawIndexedPrimitive = 82,
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
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using StretchRectFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*,
                                       D3DTEXTUREFILTERTYPE);
using GetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9**);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using GetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
// Only our authored shaders are embedded (provenance: verification/results/sky-stars-{vertex,pixel}-program.json; native
// validator: verification/results/sky-stars/native/result.json).
constexpr std::uint32_t vs_words[] = {
#include "sky_stars_vertex_program_inc.h"
};
constexpr std::uint32_t ps_words[] = {
#include "sky_stars_pixel_program_inc.h"
};
// Decision C2: the deferred batch's twins (SKY_STARS_DIM; sky-stars-dim-{vertex,pixel}-program.json).
constexpr std::uint32_t dim_vs_words[] = {
#include "sky_stars_dim_vertex_program_inc.h"
};
constexpr std::uint32_t dim_ps_words[] = {
#include "sky_stars_dim_pixel_program_inc.h"
};
template <std::size_t N> std::uint32_t vs3_program_slots(const std::uint32_t (&words)[N]) noexcept {
    std::uint32_t copy[N];
    std::memcpy(copy, words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, N);
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
// The deferred batch appends ZFUNC LESSEQUAL and both depth biases at 0 (the far-plane Z test must not be shifted by
// the application's bias).
constexpr unsigned deferred_extra_states = 3;
static_assert(touched_count + deferred_extra_states <= 32, "one bit per touched state");
// Decision C2: the deferred batch's stage-0 sampler states (the dim copy: clamp, no mip, no sRGB decode; the filter is
// the dim_linear capability's), saved and put back with the texture.
constexpr D3DSAMPLERSTATETYPE dim_samplers[] = {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER,
                                                D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE};
constexpr unsigned dim_sampler_count = unsigned(std::size(dim_samplers));
} // namespace

SkyStarsPass::~SkyStarsPass() {
    detach();
}
void SkyStarsPass::release_dim() noexcept {
    drop(dim_surface_);
    drop(dim_texture_);
    dim_width_ = dim_height_ = 0;
}
void SkyStarsPass::before_reset() noexcept {
    PreserveCpuState guard;
    lost_ = false;
    dim_unavailable_ = false;
    release_dim(); // D3DPOOL_DEFAULT: released before Reset, created again at the next deferred batch
}
void SkyStarsPass::release_objects() noexcept {
    drop(vs_);
    drop(ps_);
    drop(declaration_);
    drop(vb_);
    drop(ib_);
    drop(dim_vs_);
    drop(dim_ps_);
    release_dim();
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
    const void* objects[] = {vs_, ps_, declaration_, vb_, ib_, dim_vs_, dim_ps_, dim_texture_, dim_surface_};
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
    caps_.vs_slots = vs3_program_slots(vs_words);
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
    // Decision C2, the dim side: never a refusal of the pass; without it the caller keeps the in-place batch (k = 0).
    {
        auto dim_off = [&](const char* reason) {
            drop(dim_vs_);
            drop(dim_ps_);
            caps_.dim = false;
            caps_.dim_reason = reason;
        };
        caps_.dim_vs_slots = vs3_program_slots(dim_vs_words);
        caps_.dim_ps_slots = ps3_program_slots(dim_ps_words, std::size(dim_ps_words));
        IDirect3D9* api3 = nullptr;
        HRESULT filter = call<GetD3DFn>(GetDirect3D)(d, &api3);
        if (SUCCEEDED(filter) && api3) {
            filter = api3->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
                                             D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE,
                                             D3DFMT_A16B16G16R16F);
            drop(api3);
        } else
            filter = E_FAIL;
        caps_.dim_linear = filter == D3D_OK;
        caps_.copy_filter = (caps.StretchRectFilterCaps & D3DPTFILTERCAPS_MINFLINEAR)  ? unsigned(D3DTEXF_LINEAR)
                            : (caps.StretchRectFilterCaps & D3DPTFILTERCAPS_MINFPOINT) ? unsigned(D3DTEXF_POINT)
                                                                                       : 0u;
        if (!caps_.dim_vs_slots || !caps_.dim_ps_slots) dim_off("compiled_program");
        else if (!(caps.DevCaps2 & D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES)) dim_off("stretch_from_textures");
        else if (!caps_.copy_filter) dim_off("stretch_filter");
        else {
            HRESULT made = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(dim_vs_words), &dim_vs_);
            if (SUCCEEDED(made))
                made = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(dim_ps_words), &dim_ps_);
            if (FAILED(made) || !dim_vs_ || !dim_ps_) dim_off("create");
            else {
                caps_.dim = true;
                caps_.dim_reason = "ok";
            }
        }
    }
    return S_OK;
}
HRESULT SkyStarsPass::prepare_dim(unsigned width, unsigned height) noexcept {
    if (!ready() || !caps_.dim || dim_unavailable_) return E_FAIL;
    const unsigned w = sky_stars::core::dim_size(width), h = sky_stars::core::dim_size(height);
    if (!w || !h) return E_INVALIDARG;
    if (dim_texture_ && dim_surface_ && dim_width_ == w && dim_height_ == h) return S_OK;
    PreserveCpuState guard;
    release_dim();
    HRESULT hr = call<CreateTextureFn>(CreateTexture)(device_, w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                                                      D3DPOOL_DEFAULT, &dim_texture_, nullptr);
    if (SUCCEEDED(hr) && !dim_texture_) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = dim_texture_->GetSurfaceLevel(0, &dim_surface_);
    if (SUCCEEDED(hr) && !dim_surface_) hr = E_FAIL;
    if (FAILED(hr)) {
        release_dim();
        if (lost(hr))
            lost_ = true;
        else
            dim_unavailable_ = true; // no retry per phase end: the in-place batch until the next Reset
        return hr;
    }
    dim_width_ = w;
    dim_height_ = h;
    return S_OK;
}
HRESULT SkyStarsPass::copy_dim(IDirect3DSurface9* source) noexcept {
    if (!ready() || !caps_.dim || !dim_surface_ || !source) return E_FAIL;
    PreserveCpuState guard;
    const HRESULT hr = call<StretchRectFn>(StretchRect)(device_, source, nullptr, dim_surface_, nullptr,
                                                        D3DTEXTUREFILTERTYPE(caps_.copy_filter));
    if (lost(hr)) lost_ = true;
    return hr;
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
    return draw_batch(constants, twinkle, nullptr, output);
}
HRESULT SkyStarsPass::draw_deferred(const float constants[16], const float twinkle[4], const float dim[4],
                                    SkyStarsReport* output) noexcept {
    if (!dim) {
        if (output) *output = SkyStarsReport{E_INVALIDARG, S_OK, SkyStarsStep::Build, 0, built_stars_, false, false};
        return E_INVALIDARG;
    }
    return draw_batch(constants, twinkle, dim, output);
}
// One batch. dim == nullptr: the in-place batch (Implementation A, the call sequence of every earlier build); else the
// deferred batch (Decision C2): c250-c255, the Z test (ZENABLE TRUE, ZFUNC LESSEQUAL appended to the touched states),
// the dim twins and the dim copy on stage 0 with its six sampler states.
HRESULT SkyStarsPass::draw_batch(const float constants[16], const float twinkle[4], const float* dim,
                                 SkyStarsReport* output) noexcept {
    PreserveCpuState guard;
    const bool deferred = dim != nullptr;
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
    if (deferred && (!caps_.dim || !dim_vs_ || !dim_ps_ || !dim_texture_)) {
        r.failed = SkyStarsStep::Resources;
        return finish(E_FAIL);
    }
    D d = device_;
    const UINT first = deferred ? constants_first - 1u : constants_first;
    const UINT count = deferred ? constants_count + 1u : constants_count;
    Touched states[touched_count + deferred_extra_states];
    unsigned state_count = touched_count;
    for (unsigned i = 0; i < touched_count; ++i) {
        states[i] = touched[i];
        if (deferred && states[i].state == D3DRS_ZENABLE) states[i].value = D3DZB_TRUE;
    }
    if (deferred) {
        states[state_count++] = {D3DRS_ZFUNC, D3DCMP_LESSEQUAL};
        states[state_count++] = {D3DRS_DEPTHBIAS, 0};           // float 0.0
        states[state_count++] = {D3DRS_SLOPESCALEDEPTHBIAS, 0}; // float 0.0
    }
    const DWORD dim_filter = caps_.dim_linear ? DWORD(D3DTEXF_LINEAR) : DWORD(D3DTEXF_POINT);
    const DWORD sampler_values[dim_sampler_count] = {D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, dim_filter, dim_filter,
                                                     D3DTEXF_NONE, FALSE};
    // ---- Save: every read before the first write; a failed read changes nothing.
    IDirect3DVertexShader9* saved_vs = nullptr;
    IDirect3DPixelShader9* saved_ps = nullptr;
    IDirect3DVertexDeclaration9* saved_declaration = nullptr;
    IDirect3DVertexBuffer9* saved_stream = nullptr;
    IDirect3DIndexBuffer9* saved_indices = nullptr;
    UINT saved_offset = 0, saved_stride = 0, saved_frequency = 1;
    DWORD saved_fvf = 0, saved_states[touched_count + deferred_extra_states]{}, saved_samplers[dim_sampler_count]{};
    IDirect3DBaseTexture9* saved_texture = nullptr;
    float saved_constants[(constants_count + 1) * 4]{}, batch_constants[(constants_count + 1) * 4];
    {
        float* out = batch_constants;
        if (deferred) {
            std::memcpy(out, dim, 4 * sizeof(float));
            out += 4;
        }
        std::memcpy(out, twinkle, 4 * sizeof(float));
        std::memcpy(out + 4, constants, 16 * sizeof(float));
    }
    auto release_saved = [&] {
        drop(saved_vs);
        drop(saved_ps);
        drop(saved_declaration);
        drop(saved_stream);
        drop(saved_indices);
        drop(saved_texture);
    };
    HRESULT hr = call<GetVsFn>(GetVertexShader)(d, &saved_vs);
    if (SUCCEEDED(hr)) hr = call<GetPsFn>(GetPixelShader)(d, &saved_ps);
    if (SUCCEEDED(hr)) hr = call<GetFvfFn>(GetFVF)(d, &saved_fvf);
    if (SUCCEEDED(hr)) hr = call<GetDeclarationFn>(GetVertexDeclaration)(d, &saved_declaration);
    if (SUCCEEDED(hr)) hr = call<GetStreamFn>(GetStreamSource)(d, 0, &saved_stream, &saved_offset, &saved_stride);
    if (SUCCEEDED(hr)) hr = call<GetFreqFn>(GetStreamSourceFreq)(d, 0, &saved_frequency);
    if (SUCCEEDED(hr)) hr = call<GetIndicesFn>(GetIndices)(d, &saved_indices);
    if (SUCCEEDED(hr)) hr = call<GetConstantsFn>(GetVertexShaderConstantF)(d, first, saved_constants, count);
    for (unsigned i = 0; i < state_count && SUCCEEDED(hr); ++i)
        hr = call<GetRsFn>(GetRenderState)(d, states[i].state, &saved_states[i]);
    if (deferred && SUCCEEDED(hr)) hr = call<GetTextureFn>(GetTexture)(d, 0, &saved_texture);
    for (unsigned i = 0; deferred && i < dim_sampler_count && SUCCEEDED(hr); ++i)
        hr = call<GetSamplerFn>(GetSamplerState)(d, 0, dim_samplers[i], &saved_samplers[i]);
    if (FAILED(hr)) {
        release_saved();
        r.failed = SkyStarsStep::Save;
        if (lost(hr)) lost_ = true;
        return finish(hr);
    }
    // ---- Set and draw; every attempted write is recorded for the restore.
    std::uint32_t written = 0, samplers_written = 0;
    bool texture_set = false;
    bool constants_set = false, declaration_set = false, vs_set = false, ps_set = false, stream_set = false,
         frequency_set = false, indices_set = false;
    auto step = [&](SkyStarsStep at, HRESULT value) {
        if (FAILED(value) && SUCCEEDED(hr)) {
            hr = value;
            r.failed = at;
        }
        return SUCCEEDED(hr);
    };
    for (unsigned i = 0; i < state_count && SUCCEEDED(hr); ++i) {
        if (saved_states[i] == states[i].value) continue;
        written |= 1u << i;
        step(SkyStarsStep::State, call<SetRsFn>(SetRenderState)(d, states[i].state, states[i].value));
    }
    if (SUCCEEDED(hr)) {
        constants_set = true;
        step(SkyStarsStep::State, call<SetConstantsFn>(SetVertexShaderConstantF)(d, first, batch_constants, count));
    }
    if (SUCCEEDED(hr)) {
        declaration_set = true;
        step(SkyStarsStep::State, call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration_));
    }
    if (SUCCEEDED(hr)) {
        vs_set = true;
        step(SkyStarsStep::State, call<SetVsFn>(SetVertexShader)(d, deferred ? dim_vs_ : vs_));
    }
    if (SUCCEEDED(hr)) {
        ps_set = true;
        step(SkyStarsStep::State, call<SetPsFn>(SetPixelShader)(d, deferred ? dim_ps_ : ps_));
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
    if (deferred && SUCCEEDED(hr)) {
        texture_set = true;
        step(SkyStarsStep::State, call<SetTextureFn>(SetTexture)(d, 0, dim_texture_));
    }
    for (unsigned i = 0; deferred && i < dim_sampler_count && SUCCEEDED(hr); ++i) {
        if (saved_samplers[i] == sampler_values[i]) continue;
        samplers_written |= 1u << i;
        step(SkyStarsStep::State, call<SetSamplerFn>(SetSamplerState)(d, 0, dim_samplers[i], sampler_values[i]));
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
    for (unsigned i = dim_sampler_count; i-- > 0;)
        if (samplers_written & (1u << i)) put(call<SetSamplerFn>(SetSamplerState)(d, 0, dim_samplers[i], saved_samplers[i]));
    if (texture_set) put(call<SetTextureFn>(SetTexture)(d, 0, saved_texture));
    for (unsigned i = state_count; i-- > 0;)
        if (written & (1u << i)) {
            HRESULT v = call<SetRsFn>(SetRenderState)(d, states[i].state, saved_states[i]);
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
    if (constants_set) put(call<SetConstantsFn>(SetVertexShaderConstantF)(d, first, saved_constants, count));
    release_saved();
    r.restore = restore;
    if (lost(hr) || lost(restore)) lost_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
