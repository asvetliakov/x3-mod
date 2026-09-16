#include "bloom_pass.h"
#include "quad_vertex_program.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace x3m::renderer {
namespace {
// Public IDirect3DDevice9 method order, matching the existing native pass ABI.
enum Slot : unsigned {
    GetDirect3D = 6, GetDisplayMode = 8, GetCreationParameters = 9, CreateTexture = 23,
    StretchRect = 34, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, GetRenderState = 58,
    GetTexture = 64, SetTexture = 65, GetSamplerState = 68, SetSamplerState = 69,
    SetScissorRect = 75, GetScissorRect = 76, SetSoftwareVertexProcessing = 77,
    GetSoftwareVertexProcessing = 78, SetNPatchMode = 79, GetNPatchMode = 80, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88,
    SetFVF = 89, GetFVF = 90, CreateVertexShader = 91, SetVertexShader = 92,
    GetVertexShader = 93, SetStreamSource = 100, GetStreamSource = 101,
    SetStreamSourceFreq = 102, GetStreamSourceFreq = 103, CreatePixelShader = 106,
    SetPixelShader = 107, GetPixelShader = 108,
    SetPixelShaderConstantF = 109, GetPixelShaderConstantF = 110
};
using D = IDirect3DDevice9*;
using GetD3D = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetMode = HRESULT(WINAPI*)(D, UINT, D3DDISPLAYMODE*);
using GetCreation = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTex = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using Stretch = HRESULT(WINAPI*)(D, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
using SetRT = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRT = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDS = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDS = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SetVP = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetVP = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRS = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using GetRS = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD*);
using GetTex = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9**);
using SetTex = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using GetSampler = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD*);
using SetSampler = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetRect = HRESULT(WINAPI*)(D, const RECT*);
using GetRect = HRESULT(WINAPI*)(D, RECT*);
using SetSW = HRESULT(WINAPI*)(D, BOOL);
using GetSW = BOOL(WINAPI*)(D);
using SetNPatch = HRESULT(WINAPI*)(D, float);
using GetNPatch = float(WINAPI*)(D);
using DrawUP = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreateDecl = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDecl = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDecl = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvf = HRESULT(WINAPI*)(D, DWORD);
using GetFvf = HRESULT(WINAPI*)(D, DWORD*);
using CreateVS = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVS = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using GetVS = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using SetStream = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStream = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using SetFreq = HRESULT(WINAPI*)(D, UINT, UINT);
using GetFreq = HRESULT(WINAPI*)(D, UINT, UINT*);
using CreatePS = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPS = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using GetPS = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
using SetConstants = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using GetConstants = HRESULT(WINAPI*)(D, UINT, float*, UINT);

template<class T> void drop(T*& p) noexcept { T* held = p; p = nullptr; if (held) held->Release(); }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
void first_failure(HRESULT& first, HRESULT hr) noexcept { if (SUCCEEDED(first) && FAILED(hr)) first = hr; }
constexpr D3DRENDERSTATETYPE states[] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
    D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_CULLMODE, D3DRS_FILLMODE, D3DRS_COLORWRITEENABLE,
    D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_CLIPPLANEENABLE, D3DRS_DITHERENABLE, D3DRS_WRAP0, D3DRS_MULTISAMPLEMASK, D3DRS_CLIPPING,
    D3DRS_ENABLEADAPTIVETESSELLATION
};
constexpr DWORD values[] = {
    FALSE, FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, D3DFILL_SOLID, 15,
    FALSE, FALSE, FALSE, FALSE, 0, FALSE, 0, 0xffffffffu, FALSE, FALSE
};
constexpr unsigned state_count = sizeof(states) / sizeof(states[0]);
static_assert(state_count == sizeof(values) / sizeof(values[0]));
constexpr D3DSAMPLERSTATETYPE samplers[] = {
    D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU,
    D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS
};
constexpr DWORD sampler_values[] = {
    D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, D3DTADDRESS_CLAMP,
    D3DTADDRESS_CLAMP, FALSE, 0, 0
};
constexpr unsigned sampler_count = sizeof(samplers) / sizeof(samplers[0]);
static_assert(sampler_count == sizeof(sampler_values) / sizeof(sampler_values[0]));
constexpr unsigned constant_count = x3::temporal::kBloomFirstRegister + x3::temporal::kBloomRegisterCount;

bool descriptor_equal(const D3DSURFACE_DESC& a, const D3DSURFACE_DESC& b) noexcept {
    return a.Format == b.Format && a.Type == b.Type && a.Usage == b.Usage && a.Pool == b.Pool
        && a.MultiSampleType == b.MultiSampleType && a.MultiSampleQuality == b.MultiSampleQuality
        && a.Width == b.Width && a.Height == b.Height;
}
bool main_descriptor(const D3DSURFACE_DESC& d) noexcept {
    return d.Format == D3DFMT_A8R8G8B8 && d.Type == D3DRTYPE_SURFACE
        && (d.Usage & D3DUSAGE_RENDERTARGET) && d.Pool == D3DPOOL_DEFAULT
        && d.MultiSampleType == D3DMULTISAMPLE_NONE && !d.MultiSampleQuality
        && x3::temporal::valid_bloom_size({d.Width, d.Height});
}
bool bytecode_valid(BloomBytecode b, DWORD version) noexcept {
    // Authored bundle contract only, not arbitrary shader admission. Frame the
    // entire bounded program so native CreateShader cannot walk past the input.
    if (!b.words || b.count < 2 || b.count > 8192 || b.words[0] != version || b.words[b.count - 1] != 0x0000ffffu) return false;
    std::size_t i = 1;
    while (i < b.count - 1) {
        const DWORD token = b.words[i];
        if (token == 0x0000ffffu) return false;
        const unsigned n = (token & 0xffffu) == 0xfffeu ? (token >> 16) & 0x7fffu : (token >> 24) & 15u;
        if (n > b.count - i - 2) return false;
        i += n + 1;
    }
    return i == b.count - 1;
}
void geometry(x3::temporal::BloomConstants& c, UINT sw, UINT sh, UINT dw, UINT dh) noexcept {
    c.source[0] = float(sw); c.source[1] = float(sh);
    c.source[2] = 1.f / sw; c.source[3] = 1.f / sh;
    c.destination[0] = float(dw); c.destination[1] = float(dh);
    c.destination[2] = 1.f / dw; c.destination[3] = 1.f / dh;
}
} // namespace

struct BloomPass::SavedState {
    IDirect3DSurface9* rt[4]{};
    IDirect3DSurface9* depth = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    IDirect3DBaseTexture9* texture[2]{};
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD fvf = 0, rs[state_count]{}, sampler[2][sampler_count]{};
    UINT offset = 0, stride = 0, frequency = 1, target_count = 1;
    BOOL software = FALSE;
    float npatch = 0.f;
    float constants[constant_count][4]{};
    ~SavedState() {
        for (auto& p : rt) drop(p);
        for (auto& p : texture) drop(p);
        drop(depth); drop(declaration); drop(vs); drop(ps); drop(stream);
    }
};

// Surface-only persistent ownership matches HdrPass's device-reference model.
// Texture interfaces obtained through the documented container API live only
// inside a pass. The array is bounded; acquiring it never allocates host memory.
struct BloomPass::TextureViews {
    BloomPass& pass;
    IDirect3DTexture9* down[x3::temporal::kBloomMaxLevels]{};
    IDirect3DTexture9* up[x3::temporal::kBloomMaxLevels]{};
    IDirect3DTexture9* stage = nullptr;
    IDirect3DTexture9* candidate = nullptr;
    explicit TextureViews(BloomPass& p) noexcept : pass(p) {}
    HRESULT acquire(const Image& image, IDirect3DTexture9*& out) noexcept {
        HRESULT hr = image.surface->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&out));
        if (out) ++pass.transient_views_;
        return SUCCEEDED(hr) && !out ? E_NOINTERFACE : hr;
    }
    ~TextureViews() {
        const bool was_releasing = pass.releasing_; pass.releasing_ = true;
        auto release = [&](IDirect3DTexture9*& p) { if (p) --pass.transient_views_; drop(p); };
        for (auto& p : down) release(p);
        for (auto& p : up) release(p);
        release(stage); release(candidate);
        pass.releasing_ = was_releasing;
    }
};

BloomPass::~BloomPass() { shutdown(); }
void BloomPass::revoke() noexcept { pending_ = {}; if (++serial_ == 0) ++serial_; }
void BloomPass::release(Resources& r) noexcept {
    auto clear = [](Image& i) { drop(i.surface); i = {}; };
    for (auto& i : r.down) clear(i);
    for (auto& i : r.up) clear(i);
    clear(r.stage); clear(r.candidate); clear(r.recovery);
    r = {};
}
std::uint64_t BloomPass::bytes(const Resources& r) noexcept {
    if (!r.layout.count) return 0;
    const auto last = r.layout.level[r.layout.count - 1];
    return (2 * r.layout.pixels - std::uint64_t(last.width) * last.height) * 8
        + std::uint64_t(r.width) * r.height * (r.sharpen ? 16 : 8);
}
std::uint64_t BloomPass::resource_bytes() const noexcept { return bytes(resources_); }
unsigned BloomPass::references() const noexcept {
    unsigned n = (vertex_ ? 1u : 0u) + (declaration_ ? 1u : 0u)
        + (down_ ? 1u : 0u) + (up_ ? 1u : 0u) + (candidate_ ? 1u : 0u)
        + (sharpen_ ? 1u : 0u) + (copy_ ? 1u : 0u);
    for (auto* p : extract_) n += p ? 1u : 0u;
    for (const auto& i : resources_.down) n += i.surface ? 1u : 0u;
    for (const auto& i : resources_.up) n += i.surface ? 1u : 0u;
    return n + (resources_.stage.surface ? 1u : 0u)
        + (resources_.candidate.surface ? 1u : 0u) + (resources_.recovery.surface ? 1u : 0u);
}
void BloomPass::before_reset() noexcept {
    revoke(); if (++epoch_ == 0) ++epoch_;
    const bool was_releasing = releasing_; releasing_ = true;
    release(resources_); releasing_ = was_releasing;
}
void BloomPass::shutdown() noexcept {
    const bool was_releasing = releasing_; releasing_ = true;
    before_reset();
    drop(vertex_); drop(declaration_);
    for (auto& p : extract_) drop(p);
    drop(down_); drop(up_); drop(candidate_); drop(sharpen_); drop(copy_);
    device_ = nullptr; native_ = nullptr; caps_ = {}; disabled_ = false;
    device_caps_ = {}; peak_bytes_ = 0;
    releasing_ = was_releasing;
}

HRESULT BloomPass::attach(IDirect3DDevice9* device, void* const* native, const D3DCAPS9& dc,
                         const BloomPrograms& bundle) noexcept {
    shutdown();
    if (!device || !native) return E_INVALIDARG;
    device_ = device; native_ = native; device_caps_ = dc;
    auto fail = [&](HRESULT hr, const char* reason) {
        const auto formats = caps_.formats, programs = caps_.programs;
        shutdown(); caps_.reason = reason; caps_.formats = formats; caps_.programs = programs; return hr;
    };
    if (dc.PixelShaderVersion < D3DPS_VERSION(3, 0) || dc.VertexShaderVersion < D3DVS_VERSION(3, 0)
        || !dc.NumSimultaneousRTs || dc.NumSimultaneousRTs > 4 || !dc.MaxStreams
        || !(dc.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE)
        || !(dc.DevCaps2 & D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES)
        || !(dc.TextureAddressCaps & D3DPTADDRESSCAPS_CLAMP)
        || !(dc.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR)
        || !(dc.TextureFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR)) return fail(D3DERR_NOTAVAILABLE, "caps");
    D3DDEVICE_CREATION_PARAMETERS creation{};
    D3DDISPLAYMODE mode{};
    HRESULT hr = call<GetCreation>(GetCreationParameters)(device_, &creation);
    if (FAILED(hr)) return fail(hr, "creation");
    if (creation.BehaviorFlags & (D3DCREATE_PUREDEVICE | D3DCREATE_SOFTWARE_VERTEXPROCESSING)) return fail(D3DERR_NOTAVAILABLE, "vertex_processing");
    caps_.mixed_vertex_processing = (creation.BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING) != 0;
    if (!(creation.BehaviorFlags & (D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_HARDWARE_VERTEXPROCESSING))) return fail(D3DERR_NOTAVAILABLE, "vertex_processing");
    hr = call<GetMode>(GetDisplayMode)(device_, 0, &mode);
    if (FAILED(hr)) return fail(hr, "display_mode");
    IDirect3D9* factory = nullptr;
    hr = call<GetD3D>(GetDirect3D)(device_, &factory);
    if (FAILED(hr) || !factory) { drop(factory); return fail(FAILED(hr) ? hr : E_FAIL, "factory"); }
    caps_.formats = S_OK;
    for (D3DFORMAT format : {D3DFMT_A16B16G16R16F, D3DFMT_A8R8G8B8}) {
        first_failure(caps_.formats, factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, format));
        first_failure(caps_.formats, factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
            mode.Format, 0, D3DRTYPE_TEXTURE, format));
    }
    first_failure(caps_.formats, factory->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
        mode.Format, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F));
    drop(factory);
    if (FAILED(caps_.formats)) return fail(caps_.formats, "format");
    if (!bytecode_valid(bundle.vertex, D3DVS_VERSION(3, 0))) return fail(E_INVALIDARG, "vertex_bytecode");
    const BloomBytecode ps[] = {bundle.extract[0], bundle.extract[1], bundle.extract[2],
        bundle.extract[3], bundle.extract[4], bundle.extract[5], bundle.down, bundle.up,
        bundle.candidate, bundle.sharpen, bundle.copy};
    for (const auto& b : ps) if (!bytecode_valid(b, D3DPS_VERSION(3, 0))) return fail(E_INVALIDARG, "pixel_bytecode");
    caps_.programs = call<CreateVS>(CreateVertexShader)(device_, bundle.vertex.words, &vertex_);
    if (SUCCEEDED(caps_.programs) && !vertex_) caps_.programs = E_FAIL;
    if (SUCCEEDED(caps_.programs)) caps_.programs = call<CreateDecl>(CreateVertexDeclaration)(device_, quad_declaration, &declaration_);
    if (SUCCEEDED(caps_.programs) && !declaration_) caps_.programs = E_FAIL;
    IDirect3DPixelShader9** outputs[] = {&extract_[0], &extract_[1], &extract_[2], &extract_[3],
        &extract_[4], &extract_[5], &down_, &up_, &candidate_, &sharpen_, &copy_};
    for (unsigned i = 0; i < sizeof(ps) / sizeof(ps[0]) && SUCCEEDED(caps_.programs); ++i) {
        caps_.programs = call<CreatePS>(CreatePixelShader)(device_, ps[i].words, outputs[i]);
        if (SUCCEEDED(caps_.programs) && !*outputs[i]) caps_.programs = E_FAIL;
    }
    if (FAILED(caps_.programs)) return fail(caps_.programs, "programs");
    caps_.enabled = true; caps_.reason = "ok";
    return S_OK;
}

HRESULT BloomPass::create(Image& out, UINT width, UINT height, D3DFORMAT format) noexcept {
    if (fault(BloomFault::Allocate)) return E_OUTOFMEMORY;
    IDirect3DTexture9* texture = nullptr;
    HRESULT hr = call<CreateTex>(CreateTexture)(device_, width, height, 1, D3DUSAGE_RENDERTARGET,
        format, D3DPOOL_DEFAULT, &texture, nullptr);
    if (SUCCEEDED(hr) && !texture) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = texture->GetSurfaceLevel(0, &out.surface);
    const bool was_releasing = releasing_; releasing_ = true;
    drop(texture); releasing_ = was_releasing;
    if (SUCCEEDED(hr) && !out.surface) hr = E_FAIL;
    if (SUCCEEDED(hr)) { out.width = width; out.height = height; }
    return hr;
}
HRESULT BloomPass::ensure(UINT width, UINT height, unsigned levels, bool sharpen) noexcept {
    x3::temporal::BloomLayout layout{};
    if (!x3::temporal::prepare_bloom_layout(layout, {width, height}, levels)
        || width > device_caps_.MaxTextureWidth || height > device_caps_.MaxTextureHeight) return E_INVALIDARG;
    auto admissible = [&](UINT w, UINT h) {
        if ((device_caps_.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) && w != h) return false;
        if ((device_caps_.TextureCaps & D3DPTEXTURECAPS_POW2)
            && !(device_caps_.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL)
            && ((w & (w - 1)) || (h & (h - 1)))) return false;
        const UINT aspect = device_caps_.MaxTextureAspectRatio;
        return !aspect || (std::uint64_t(w) <= std::uint64_t(h) * aspect
            && std::uint64_t(h) <= std::uint64_t(w) * aspect);
    };
    if (!admissible(width, height)) return D3DERR_NOTAVAILABLE;
    for (unsigned i = 0; i < layout.count; ++i)
        if (!admissible(layout.level[i].width, layout.level[i].height)) return D3DERR_NOTAVAILABLE;
    if (resources_.width == width && resources_.height == height
        && resources_.layout.count == layout.count && resources_.sharpen == sharpen) return S_OK;
    Resources next{};
    next.width = width; next.height = height; next.layout = layout; next.sharpen = sharpen;
    peak_bytes_ = std::max(peak_bytes_, bytes(resources_) + bytes(next));
    HRESULT hr = S_OK;
    for (unsigned i = 0; i < layout.count && SUCCEEDED(hr); ++i) {
        hr = create(next.down[i], layout.level[i].width, layout.level[i].height, D3DFMT_A16B16G16R16F);
        if (i + 1 < layout.count && SUCCEEDED(hr))
            hr = create(next.up[i], layout.level[i].width, layout.level[i].height, D3DFMT_A16B16G16R16F);
    }
    if (SUCCEEDED(hr)) hr = create(next.candidate, width, height, D3DFMT_A8R8G8B8);
    if (SUCCEEDED(hr)) hr = create(next.recovery, width, height, D3DFMT_A8R8G8B8);
    if (SUCCEEDED(hr) && sharpen) hr = create(next.stage, width, height, D3DFMT_A16B16G16R16F);
    if (SUCCEEDED(hr)) std::swap(resources_, next);
    const bool was_releasing = releasing_; releasing_ = true;
    release(next); // all new temporaries on failure; prior complete set on success
    releasing_ = was_releasing;
    return hr;
}

HRESULT BloomPass::save(SavedState& s) noexcept {
    if (fault(BloomFault::Save)) return E_FAIL;
    s.target_count = device_caps_.NumSimultaneousRTs;
    HRESULT hr;
    for (unsigned i = 0; i < s.target_count; ++i) {
        hr = call<GetRT>(GetRenderTarget)(device_, i, &s.rt[i]);
        if (FAILED(hr) && !(i && hr == D3DERR_NOTFOUND && !s.rt[i])) return hr;
        if (!i && !s.rt[0]) return E_FAIL;
    }
    hr = call<GetDS>(GetDepthStencilSurface)(device_, &s.depth);
    if (FAILED(hr) && !(hr == D3DERR_NOTFOUND && !s.depth)) return hr;
    if (FAILED(hr = call<GetVP>(GetViewport)(device_, &s.viewport))) return hr;
    if (FAILED(hr = call<GetRect>(GetScissorRect)(device_, &s.scissor))) return hr;
    if (FAILED(hr = call<GetFvf>(GetFVF)(device_, &s.fvf))) return hr;
    if (FAILED(hr = call<GetDecl>(GetVertexDeclaration)(device_, &s.declaration))) return hr;
    if (FAILED(hr = call<GetVS>(GetVertexShader)(device_, &s.vs))) return hr;
    if (FAILED(hr = call<GetPS>(GetPixelShader)(device_, &s.ps))) return hr;
    if (FAILED(hr = call<GetStream>(GetStreamSource)(device_, 0, &s.stream, &s.offset, &s.stride))) return hr;
    if (FAILED(hr = call<GetFreq>(GetStreamSourceFreq)(device_, 0, &s.frequency))) return hr;
    for (unsigned stage = 0; stage < 2; ++stage) {
        if (FAILED(hr = call<GetTex>(GetTexture)(device_, stage, &s.texture[stage]))) return hr;
        for (unsigned i = 0; i < sampler_count; ++i)
            if (FAILED(hr = call<GetSampler>(GetSamplerState)(device_, stage, samplers[i], &s.sampler[stage][i]))) return hr;
    }
    for (unsigned i = 0; i < state_count; ++i)
        if (FAILED(hr = call<GetRS>(GetRenderState)(device_, states[i], &s.rs[i]))) return hr;
    if (FAILED(hr = call<GetConstants>(GetPixelShaderConstantF)(device_, 0, s.constants[0], constant_count))) return hr;
    s.software = call<GetSW>(GetSoftwareVertexProcessing)(device_);
    s.npatch = call<GetNPatch>(GetNPatchMode)(device_);
    if (!std::isfinite(s.npatch)) return E_FAIL;
    // Pure-HW devices cannot legally toggle this state. An unexpected true
    // mode is refused without mutations instead of being silently normalized.
    if (s.software && !caps_.mixed_vertex_processing) return D3DERR_NOTAVAILABLE;
    return S_OK;
}

HRESULT BloomPass::restore(const SavedState& s, bool inject_partial_failure) noexcept {
    // Always attempt every restoration, retaining the first failure. Recovery
    // reuses this SAME snapshot, even if a preceding restoration partly worked.
    HRESULT hr = S_OK;
    for (unsigned stage = 0; stage < 2; ++stage)
        first_failure(hr, call<SetTex>(SetTexture)(device_, stage, nullptr));
    for (unsigned i = 1; i < s.target_count; ++i)
        first_failure(hr, call<SetRT>(SetRenderTarget)(device_, i, nullptr));
    first_failure(hr, call<SetDS>(SetDepthStencilSurface)(device_, nullptr));
    first_failure(hr, call<SetRT>(SetRenderTarget)(device_, 0, s.rt[0]));
    for (unsigned i = 1; i < s.target_count; ++i)
        first_failure(hr, call<SetRT>(SetRenderTarget)(device_, i, s.rt[i]));
    first_failure(hr, call<SetDS>(SetDepthStencilSurface)(device_, s.depth));
    first_failure(hr, call<SetVP>(SetViewport)(device_, &s.viewport));
    first_failure(hr, call<SetRect>(SetScissorRect)(device_, &s.scissor));
    if (caps_.mixed_vertex_processing)
        first_failure(hr, call<SetSW>(SetSoftwareVertexProcessing)(device_, s.software));
    first_failure(hr, call<SetNPatch>(SetNPatchMode)(device_, s.npatch));
    if (s.fvf) first_failure(hr, call<SetFvf>(SetFVF)(device_, s.fvf));
    else first_failure(hr, call<SetDecl>(SetVertexDeclaration)(device_, s.declaration));
    first_failure(hr, call<SetVS>(SetVertexShader)(device_, s.vs));
    first_failure(hr, call<SetPS>(SetPixelShader)(device_, s.ps));
    first_failure(hr, call<SetStream>(SetStreamSource)(device_, 0, s.stream, s.offset, s.stride));
    first_failure(hr, call<SetFreq>(SetStreamSourceFreq)(device_, 0, s.frequency));
    first_failure(hr, call<SetConstants>(SetPixelShaderConstantF)(device_, 0, s.constants[0], constant_count));
    for (unsigned stage = 0; stage < 2; ++stage) {
        first_failure(hr, call<SetTex>(SetTexture)(device_, stage, s.texture[stage]));
        for (unsigned i = 0; i < sampler_count; ++i)
            first_failure(hr, call<SetSampler>(SetSamplerState)(device_, stage, samplers[i], s.sampler[stage][i]));
    }
    for (unsigned i = 0; i < state_count; ++i) {
        // Fixture fault leaves one actually touched state unrestored while
        // attempting the rest. Retry must repair it from the original snapshot.
        if (inject_partial_failure && states[i] == D3DRS_COLORWRITEENABLE) first_failure(hr, E_FAIL);
        else first_failure(hr, call<SetRS>(SetRenderState)(device_, states[i], s.rs[i]));
    }
    return hr;
}

HRESULT BloomPass::setup(const SavedState& s, DWORD mask) noexcept {
    if (fault(BloomFault::Setup)) return E_FAIL;
    HRESULT hr = S_OK;
    for (unsigned stage = 0; stage < 2; ++stage)
        first_failure(hr, call<SetTex>(SetTexture)(device_, stage, nullptr));
    for (unsigned i = 1; i < s.target_count; ++i)
        first_failure(hr, call<SetRT>(SetRenderTarget)(device_, i, nullptr));
    first_failure(hr, call<SetDS>(SetDepthStencilSurface)(device_, nullptr));
    if (caps_.mixed_vertex_processing && s.software)
        first_failure(hr, call<SetSW>(SetSoftwareVertexProcessing)(device_, FALSE));
    first_failure(hr, call<SetDecl>(SetVertexDeclaration)(device_, declaration_));
    first_failure(hr, call<SetVS>(SetVertexShader)(device_, vertex_));
    first_failure(hr, call<SetNPatch>(SetNPatchMode)(device_, 0.f));
    first_failure(hr, call<SetFreq>(SetStreamSourceFreq)(device_, 0, 1));
    for (unsigned i = 0; i < state_count; ++i)
        first_failure(hr, call<SetRS>(SetRenderState)(device_, states[i], states[i] == D3DRS_COLORWRITEENABLE ? mask : values[i]));
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned i = 0; i < sampler_count; ++i) {
            const bool linear = stage == 1 && (samplers[i] == D3DSAMP_MINFILTER || samplers[i] == D3DSAMP_MAGFILTER);
            first_failure(hr, call<SetSampler>(SetSamplerState)(device_, stage, samplers[i], linear ? DWORD(D3DTEXF_LINEAR) : sampler_values[i]));
        }
    return hr;
}

HRESULT BloomPass::draw(const Image& output, IDirect3DPixelShader9* shader,
                        IDirect3DTexture9* s0, IDirect3DTexture9* s1,
                        const x3::temporal::BloomConstants* constants, bool* issued) noexcept {
    if (issued) *issued = false;
    // Pipeline state is bracketed once per public operation, not per level.
    // Unbind both reads before changing targets to avoid previous-level alias.
    HRESULT hr = call<SetTex>(SetTexture)(device_, 0, nullptr);
    first_failure(hr, call<SetTex>(SetTexture)(device_, 1, nullptr));
    first_failure(hr, call<SetRT>(SetRenderTarget)(device_, 0, output.surface));
    const D3DVIEWPORT9 viewport{0, 0, output.width, output.height, 0.f, 1.f};
    first_failure(hr, call<SetVP>(SetViewport)(device_, &viewport));
    first_failure(hr, call<SetPS>(SetPixelShader)(device_, shader));
    if (constants) first_failure(hr, call<SetConstants>(SetPixelShaderConstantF)(device_,
        x3::temporal::kBloomFirstRegister, constants->source, x3::temporal::kBloomRegisterCount));
    first_failure(hr, call<SetTex>(SetTexture)(device_, 0, s0));
    first_failure(hr, call<SetTex>(SetTexture)(device_, 1, s1));
    if (SUCCEEDED(hr)) {
        QuadVertex vertices[4]; quad_vertices(output.width, output.height, vertices);
        if (issued) *issued = true;
        hr = call<DrawUP>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(vertices[0]));
    }
    return hr;
}

bool BloomPass::owned(IDirect3DSurface9* t) const noexcept {
    if (!t) return false;
    if (t == resources_.candidate.surface || t == resources_.recovery.surface || t == resources_.stage.surface) return true;
    for (unsigned i = 0; i < resources_.layout.count; ++i)
        if (t == resources_.down[i].surface || t == resources_.up[i].surface) return true;
    return false;
}

HRESULT BloomPass::validate_inputs(const BloomPrepare& p) const noexcept {
    const auto& b = p.boundary;
    if (!enabled() || !p.scene || !b.admitted || !b.main || owned(b.main)
        || b.thread != GetCurrentThreadId() || !main_descriptor(b.main_desc)
        || !x3::temporal::valid_bloom_params(p.filter) || !x3::temporal::valid_sharpen(p.sharpen)) return E_INVALIDARG;
    if (p.exact_sharpen && p.sharpen > 0) {
        const auto& c = p.sharpen_constants.values;
        for (float v : c) if (!std::isfinite(v)) return E_INVALIDARG;
        // Preserve the supplied gain exactly; only validate its supported
        // range and that its texel steps describe this same image.
        if (c[0] < 0.25f || c[0] > 1.f || c[3] != 0.f
            || c[1] != 1.f / float(b.main_desc.Width)
            || c[2] != 1.f / float(b.main_desc.Height)) return E_INVALIDARG;
    }
    const auto mode = p.decode;
    if (mode != x3::temporal::AgxDecode::gamma22 && mode != x3::temporal::AgxDecode::srgb
        && mode != x3::temporal::AgxDecode::none) return E_INVALIDARG;
    // Preserve the exact supplied AgX block but reject nonfinite inputs and a
    // decode mismatch between extraction specialization and base tonemapping.
    float floats[sizeof(p.agx) / sizeof(float)];
    std::memcpy(floats, &p.agx, sizeof(p.agx));
    for (unsigned i = 0; i < sizeof(p.agx) / sizeof(float); ++i) if (!std::isfinite(floats[i])) return E_INVALIDARG;
    x3::temporal::AgxConstants expected{};
    x3::temporal::set_decode(expected, mode);
    for (unsigned i = 0; i < 4; ++i) if (p.agx.decode[i] != expected.decode[i]) return E_INVALIDARG;
    if (p.agx.exposure[0] <= 0 || p.agx.exposure[0] > x3::temporal::kAgxClampOff
        || p.agx.exposure[1] <= 0 || p.agx.exposure[1] > x3::temporal::kAgxClampOff) return E_INVALIDARG;
    D3DSURFACE_DESC scene{};
    HRESULT hr = p.scene->GetLevelDesc(0, &scene);
    if (FAILED(hr)) return hr;
    if (scene.Format != D3DFMT_A16B16G16R16F || scene.Pool != D3DPOOL_DEFAULT
        || scene.Width != b.main_desc.Width || scene.Height != b.main_desc.Height
        || scene.MultiSampleType != D3DMULTISAMPLE_NONE || scene.MultiSampleQuality) return E_INVALIDARG;
    IDirect3DSurface9* scene_surface = nullptr;
    hr = p.scene->GetSurfaceLevel(0, &scene_surface);
    const bool alias = !scene_surface || owned(scene_surface) || scene_surface == b.main;
    drop(scene_surface);
    if (FAILED(hr) || alias) return FAILED(hr) ? hr : E_INVALIDARG;
    // Native resource ownership checked once per prepare, never per pyramid
    // draw. Exact device interface mismatch refuses; no backend-private data.
    IDirect3DDevice9* owner = nullptr;
    hr = p.scene->GetDevice(&owner);
    bool same = SUCCEEDED(hr) && owner == device_; drop(owner);
    if (!same) return FAILED(hr) ? hr : E_INVALIDARG;
    hr = b.main->GetDevice(&owner); same = SUCCEEDED(hr) && owner == device_; drop(owner);
    if (!same) return FAILED(hr) ? hr : E_INVALIDARG;
    if (b.depth) {
        hr = b.depth->GetDevice(&owner); same = SUCCEEDED(hr) && owner == device_; drop(owner);
        if (!same) return FAILED(hr) ? hr : E_INVALIDARG;
    }
    return S_OK;
}

HRESULT BloomPass::validate_boundary(const BloomBoundary& b, const SavedState& s, bool full_viewport) const noexcept {
    if (!b.admitted || b.thread != GetCurrentThreadId() || !b.main || s.rt[0] != b.main
        || s.depth != b.depth || !main_descriptor(b.main_desc)) return E_INVALIDARG;
    D3DSURFACE_DESC actual{};
    HRESULT hr = b.main->GetDesc(&actual);
    if (FAILED(hr)) return hr;
    if (!descriptor_equal(actual, b.main_desc)) return E_INVALIDARG;
    if (b.depth) {
        hr = b.depth->GetDesc(&actual);
        if (FAILED(hr)) return hr;
        if (!descriptor_equal(actual, b.depth_desc) || !(actual.Usage & D3DUSAGE_DEPTHSTENCIL)
            || actual.Pool != D3DPOOL_DEFAULT || actual.MultiSampleType != D3DMULTISAMPLE_NONE
            || actual.MultiSampleQuality || actual.Width < b.main_desc.Width || actual.Height < b.main_desc.Height) return E_INVALIDARG;
    }
    if (full_viewport && (s.viewport.X || s.viewport.Y || s.viewport.Width != b.main_desc.Width
        || s.viewport.Height != b.main_desc.Height)) return E_INVALIDARG;
    return S_OK;
}

BloomPreparation BloomPass::prepare(const BloomPrepare& p) noexcept {
    revoke();
    BloomPreparation result{};
    result.operation = validate_inputs(p);
    if (FAILED(result.operation)) { if (lost(result.operation)) disabled_ = true; return result; }
    const UINT width = p.boundary.main_desc.Width, height = p.boundary.main_desc.Height;
    result.allocation = ensure(width, height, p.filter.levels, p.sharpen > 0);
    if (FAILED(result.allocation)) { result.reason = "allocation"; if (lost(result.allocation)) disabled_ = true; return result; }
    SavedState saved;
    result.saved = save(saved);
    if (FAILED(result.saved)) { result.reason = "save"; if (lost(result.saved)) disabled_ = true; return result; }
    result.operation = validate_boundary(p.boundary, saved, true);
    if (FAILED(result.operation)) { result.reason = "boundary"; if (lost(result.operation)) disabled_ = true; return result; }
    TextureViews views(*this);
    for (unsigned i = 0; i < resources_.layout.count && SUCCEEDED(result.operation); ++i) {
        result.operation = views.acquire(resources_.down[i], views.down[i]);
        if (i + 1 < resources_.layout.count && SUCCEEDED(result.operation))
            result.operation = views.acquire(resources_.up[i], views.up[i]);
    }
    if (p.sharpen > 0 && SUCCEEDED(result.operation)) result.operation = views.acquire(resources_.stage, views.stage);
    if (FAILED(result.operation)) { result.reason = "texture_view"; if (lost(result.operation)) disabled_ = true; return result; }
    result.operation = setup(saved, 15);
    x3::temporal::BloomConstants c{};
    // One validated common block; per-pass work updates dimensions only.
    c.filter[0] = p.filter.threshold; c.filter[1] = p.filter.knee;
    c.filter[2] = p.filter.scatter; c.filter[3] = p.filter.strength;
    c.radiance[0] = p.agx.exposure[0];
    // Bloom-only decoded-space source ceiling (bloom.h); the AgX display block
    // uploaded below keeps the unmodified firefly clamp.
    c.radiance[1] = p.agx.exposure[1] < p.filter.source_clamp ? p.agx.exposure[1] : p.filter.source_clamp;
    for (unsigned i = 0; i < 4; ++i) c.decode[i] = p.agx.decode[i];
    c.radiance[3] = p.filter.authored_glow_gain;
    if (p.filter.authored_glow_gain > 0.f) c.decode[3] = p.filter.highlight_gain;
    x3::temporal::BloomExtractShader selected{};
    if (!x3::temporal::select_bloom_extract(selected, {width, height}, p.decode))
        first_failure(result.operation, E_INVALIDARG);
    IDirect3DTexture9* source = p.scene;
    UINT sw = width, sh = height;
    for (unsigned i = 0; i < resources_.layout.count && SUCCEEDED(result.operation); ++i) {
        const auto& output = resources_.down[i];
        geometry(c, sw, sh, output.width, output.height);
        result.operation = fault(BloomFault::PrepareDraw) ? E_FAIL
            : draw(output, i == 0 ? extract_[static_cast<unsigned>(selected)] : down_, source, nullptr, &c);
        source = views.down[i]; sw = output.width; sh = output.height;
    }
    // Coarsest U aliases D; every other U is a distinct texture. The layout
    // supplied the exact ceil-half relationships, including thin/odd images.
    for (unsigned i = resources_.layout.count - 1; i > 0 && SUCCEEDED(result.operation); --i) {
        const auto& output = resources_.up[i - 1];
        geometry(c, sw, sh, output.width, output.height);
        result.operation = fault(BloomFault::PrepareDraw) ? E_FAIL
            : draw(output, up_, views.down[i - 1], source, &c);
        source = views.up[i - 1]; sw = output.width; sh = output.height;
    }
    if (SUCCEEDED(result.operation)) {
        geometry(c, sw, sh, width, height);
        result.operation = call<SetConstants>(SetPixelShaderConstantF)(device_, x3::temporal::kAgxFirstRegister,
            p.agx.exposure, x3::temporal::kAgxRegisterCount);
        if (SUCCEEDED(result.operation)) result.operation = fault(BloomFault::PrepareDraw) ? E_FAIL
            : draw(p.sharpen > 0 ? resources_.stage : resources_.candidate, candidate_, p.scene, source, &c);
    }
    if (SUCCEEDED(result.operation) && p.sharpen > 0) {
        x3::temporal::SharpenConstants sharp{};
        if (p.exact_sharpen) sharp = p.sharpen_constants;
        else if (!x3::temporal::prepare_sharpen(sharp, p.sharpen, width, height)) result.operation = E_INVALIDARG;
        if (SUCCEEDED(result.operation)) result.operation = call<SetConstants>(SetPixelShaderConstantF)(device_, x3::temporal::kSharpenRegister, sharp.values, 1);
        if (SUCCEEDED(result.operation)) result.operation = fault(BloomFault::PrepareDraw) ? E_FAIL
            : draw(resources_.candidate, sharpen_, views.stage, nullptr);
    }
    result.restore = restore(saved, fault(BloomFault::PrepareRestore));
    result.state_preserved = SUCCEEDED(result.restore);
    if (!result.state_preserved || lost(result.operation)) disabled_ = true;
    if (FAILED(result.operation) || !result.state_preserved) {
        result.reason = result.state_preserved ? "prepare" : "restore";
        return result;
    }
    pending_ = {this, epoch_, serial_, p.boundary.frame, p.boundary.reset, p.boundary.thread,
                resources_.candidate.surface, p.boundary.main, p.boundary.depth};
    result.candidate = pending_; result.ready = true; result.reason = "ok";
    return result;
}

bool BloomPass::valid(const BloomCandidate& token) const noexcept {
    return enabled() && pending_.owner == this && token.owner == this
        && token.epoch == epoch_ && token.epoch == pending_.epoch
        && token.serial == serial_ && token.serial == pending_.serial
        && token.frame == pending_.frame && token.reset == pending_.reset
        && token.thread == pending_.thread && token.thread == GetCurrentThreadId()
        && token.surface == resources_.candidate.surface && token.surface == pending_.surface
        && token.main == pending_.main && token.depth == pending_.depth;
}

BloomCommit BloomPass::commit(const BloomCandidate& token, const BloomBoundary& boundary) noexcept {
    BloomCommit result{};
    const bool admitted = valid(token) && boundary.admitted && boundary.frame == token.frame
        && boundary.reset == token.reset && boundary.thread == token.thread
        && boundary.main == token.main && boundary.depth == token.depth;
    revoke(); // no retry/repeated boundary can reuse the same complete candidate
    if (!admitted) return result;
    SavedState saved;
    result.saved = save(saved);
    if (FAILED(result.saved)) {
        result.reason = "save"; result.requalification_required = lost(result.saved);
        if (result.requalification_required) disabled_ = true;
        return result;
    }
    result.operation = validate_boundary(boundary, saved, false);
    if (FAILED(result.operation) || boundary.main_desc.Width != resources_.width
        || boundary.main_desc.Height != resources_.height) {
        result.reason = "boundary"; result.requalification_required = lost(result.operation);
        if (result.requalification_required) disabled_ = true;
        return result;
    }
    TextureViews views(*this);
    result.operation = views.acquire(resources_.candidate, views.candidate);
    if (FAILED(result.operation)) {
        result.reason = "texture_view"; result.requalification_required = lost(result.operation);
        if (result.requalification_required) disabled_ = true;
        return result;
    }
    // Public same-format color RT copy. No state mutation or main write has
    // occurred yet; a failed backup cannot authorize candidate replacement.
    result.backup = fault(BloomFault::Backup) ? E_FAIL
        : call<Stretch>(StretchRect)(device_, boundary.main, nullptr, resources_.recovery.surface, nullptr, D3DTEXF_NONE);
    if (FAILED(result.backup)) {
        result.reason = "backup"; result.requalification_required = lost(result.backup);
        if (result.requalification_required) disabled_ = true;
        return result;
    }
    result.operation = setup(saved, 7);
    if (SUCCEEDED(result.operation)) {
        const Image main{boundary.main, resources_.width, resources_.height};
        // The draw may write before returning failure. The seam intentionally
        // returns failure AFTER the real write, exercising actual rollback.
        result.operation = draw(main, copy_, views.candidate, nullptr, nullptr, &result.write_attempted);
        result.original_preserved = !result.write_attempted;
        if (result.write_attempted && fault(BloomFault::CommitDrawAfterWrite) && SUCCEEDED(result.operation)) result.operation = E_FAIL;
    }
    result.restore = restore(saved, fault(BloomFault::CommitRestore));
    result.state_preserved = SUCCEEDED(result.restore);
    if (SUCCEEDED(result.operation) && result.state_preserved) {
        result.committed = true; result.reason = "ok"; return result;
    }
    // Failed setup has not touched main. Do not introduce a main write merely
    // to recover state: even a rollback StretchRect can partially fail. Retry
    // only the same state snapshot if necessary, leaving Recovery unconsumed.
    if (!result.write_attempted) {
        result.reason = "setup";
        if (!result.state_preserved) {
            result.recovery_restore = restore(saved, fault(BloomFault::RecoveryRestore));
            result.state_preserved = SUCCEEDED(result.recovery_restore);
        }
        result.requalification_required = FAILED(result.restore) || lost(result.operation);
        if (result.requalification_required) disabled_ = true;
        if (!result.state_preserved) result.reason = "unrecovered_state";
        return result;
    }
    // Even successful restore cannot establish the image after a failed draw.
    // A restore failure also invokes rollback, retaining the original snapshot.
    result.reason = FAILED(result.operation) ? "write" : "restore";
    HRESULT unbind = call<SetTex>(SetTexture)(device_, 0, nullptr);
    first_failure(unbind, call<SetTex>(SetTexture)(device_, 1, nullptr));
    const bool recovery_issued = SUCCEEDED(unbind) && !fault(BloomFault::Recovery);
    result.recovery = FAILED(unbind) ? unbind : !recovery_issued ? E_FAIL
        : call<Stretch>(StretchRect)(device_, resources_.recovery.surface, nullptr, boundary.main, nullptr, D3DTEXF_NONE);
    // The candidate draw was attempted; either it or a failed rollback copy
    // may have partially written main. Only successful recovery proves RGBA.
    result.original_preserved = SUCCEEDED(result.recovery);
    result.recovery_restore = restore(saved, fault(BloomFault::RecoveryRestore));
    result.state_preserved = SUCCEEDED(result.recovery_restore);
    result.requalification_required = !result.original_preserved || !result.state_preserved
        || FAILED(result.restore) || lost(result.operation) || lost(result.recovery);
    if (result.requalification_required) disabled_ = true;
    if (!result.original_preserved || !result.state_preserved) result.reason = "unrecovered";
    return result;
}
} // namespace x3m::renderer
