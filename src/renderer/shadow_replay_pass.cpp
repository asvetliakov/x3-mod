#include "shadow_replay_pass.h"
#include <array>
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
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp).
// clang-format off
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, CreateDepthStencilSurface = 29,
    SetRenderTarget = 37, GetRenderTarget = 38, SetDepthStencilSurface = 39, GetDepthStencilSurface = 40,
    BeginScene = 41, EndScene = 42, Clear = 43, SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59,
    SetTexture = 65, SetSamplerState = 69,
    SetScissorRect = 75, GetScissorRect = 76, DrawPrimitive = 81, DrawIndexedPrimitive = 82,
    SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90, CreateVertexShader = 91, SetVertexShader = 92, SetVertexShaderConstantF = 94,
    SetStreamSource = 100, SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstantF = 109
};
// clang-format on
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreateDepthFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**,
                                       HANDLE*);
using SetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using ClearFn = HRESULT(WINAPI*)(D, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetViewportFn = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using CreateBlockFn = HRESULT(WINAPI*)(D, D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexedFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetVsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// D3D9 token encoding authored here from the documented operation contract
// (as rigid_replay_program.cpp); not a captured program.
// vs_3_0: pos = (v0.xyz, 1); o0 = light rows c0-c3 . pos with o0.z = max(z, 0);
// o1.x = z (the unclamped sun depth). Pancaking (directional-shadows.md, "Run
// 38 A (run111) diagnosis", cause 4): a caster nearer the light than the map's
// near plane is flattened onto it instead of being clipped away, so it still
// shadows; the pixel program stores max(depth, 0), exact wherever the caster
// lies inside the range.
// clang-format off
constexpr DWORD vertex_words[] = {
    0xfffe0300u,
    0x05000051u, 0xa00f0008u, 0x3f800000u, 0u, 0u, 0u,  // def c8, 1, 0, 0, 0
    0x0200001fu, 0x80000000u, 0x900f0000u,               // dcl_position v0
    0x0200001fu, 0x80000000u, 0xe00f0000u,               // dcl_position o0
    0x0200001fu, 0x80000005u, 0xe00f0001u,               // dcl_texcoord o1
    0x04000004u, 0x800f0000u, 0x90240000u, 0xa0400008u, 0xa0150008u, // mad r0, v0.xyzx, c8.xxxy, c8.yyyx
    0x03000009u, 0xe0010000u, 0x80e40000u, 0xa0e40000u,  // dp4 o0.x, r0, c0
    0x03000009u, 0xe0020000u, 0x80e40000u, 0xa0e40001u,  // dp4 o0.y, r0, c1
    0x03000009u, 0x80040001u, 0x80e40000u, 0xa0e40002u,  // dp4 r1.z, r0, c2
    0x03000009u, 0xe0080000u, 0x80e40000u, 0xa0e40003u,  // dp4 o0.w, r0, c3
    0x02000001u, 0xe0010001u, 0x80aa0001u,               // mov o1.x, r1.z
    0x0300000bu, 0x80040001u, 0x80aa0001u, 0xa0550008u,  // max r1.z, r1.z, c8.y
    0x02000001u, 0xe0040000u, 0x80aa0001u,               // mov o0.z, r1.z
    0x0000ffffu};
// ps_3_0: oC0 = max(v0.x, 0) (the interpolated sun-space depth, pancaked).
constexpr DWORD pixel_words[] = {
    0xffff0300u,
    0x05000051u, 0xa00f0000u, 0u, 0u, 0u, 0u,            // def c0, 0, 0, 0, 0
    0x0200001fu, 0x80000005u, 0x900f0000u,               // dcl_texcoord v0
    0x0300000bu, 0x800f0000u, 0x90000000u, 0xa0000000u,  // max r0, v0.x, c0.x
    0x02000001u, 0x800f0800u, 0x80e40000u,               // mov oC0, r0
    0x0000ffffu};
// clang-format on
// Alpha-tested casters (X3M_SHADOW_ALPHA_CASTERS; docs/architecture/
// shadow-replay-gates.md, "Alpha-tested casters"), authored the same way.
// vs_3_0: the depth-only program above plus the declaration's TEXCOORD0 in v1
// and its .xy out in o2 (TEXCOORD1); o1 carries only the depth's .x.
// clang-format off
constexpr DWORD alpha_vertex_words[] = {
    0xfffe0300u,
    0x05000051u, 0xa00f0008u, 0x3f800000u, 0u, 0u, 0u,  // def c8, 1, 0, 0, 0
    0x0200001fu, 0x80000000u, 0x900f0000u,               // dcl_position v0
    0x0200001fu, 0x80000005u, 0x900f0001u,               // dcl_texcoord v1
    0x0200001fu, 0x80000000u, 0xe00f0000u,               // dcl_position o0
    0x0200001fu, 0x80000005u, 0xe0010001u,               // dcl_texcoord o1.x
    0x0200001fu, 0x80010005u, 0xe0030002u,               // dcl_texcoord1 o2.xy
    0x04000004u, 0x800f0000u, 0x90240000u, 0xa0400008u, 0xa0150008u, // mad r0, v0.xyzx, c8.xxxy, c8.yyyx
    0x03000009u, 0xe0010000u, 0x80e40000u, 0xa0e40000u,  // dp4 o0.x, r0, c0
    0x03000009u, 0xe0020000u, 0x80e40000u, 0xa0e40001u,  // dp4 o0.y, r0, c1
    0x03000009u, 0x80040001u, 0x80e40000u, 0xa0e40002u,  // dp4 r1.z, r0, c2
    0x03000009u, 0xe0080000u, 0x80e40000u, 0xa0e40003u,  // dp4 o0.w, r0, c3
    0x02000001u, 0xe0010001u, 0x80aa0001u,               // mov o1.x, r1.z
    0x0300000bu, 0x80040001u, 0x80aa0001u, 0xa0550008u,  // max r1.z, r1.z, c8.y
    0x02000001u, 0xe0040000u, 0x80aa0001u,               // mov o0.z, r1.z
    0x02000001u, 0xe0030002u, 0x90e40001u,               // mov o2.xy, v1
    0x0000ffffu};
// clang-format on
// ps_3_0: r0 = s0 at v1.xy; texkill (r0.a - c0.x) replicated (the alpha test
// GREATEREQUAL c0.x: clip(tex.a - threshold)); oC0 = max(v0.x, 0) as above.
// clang-format off
constexpr DWORD alpha_pixel_words[] = {
    0xffff0300u,
    0x05000051u, 0xa00f0001u, 0u, 0u, 0u, 0u,            // def c1, 0, 0, 0, 0
    0x0200001fu, 0x80000005u, 0x90010000u,               // dcl_texcoord v0.x
    0x0200001fu, 0x80010005u, 0x90030001u,               // dcl_texcoord1 v1.xy
    0x0200001fu, 0x90000000u, 0xa00f0800u,               // dcl_2d s0
    0x03000042u, 0x800f0000u, 0x90e40001u, 0xa0e40800u,  // texld r0, v1, s0
    0x03000002u, 0x800f0000u, 0x80ff0000u, 0xa1000000u,  // add r0, r0.w, -c0.x
    0x01000041u, 0x800f0000u,                            // texkill r0
    0x0300000bu, 0x800f0000u, 0x90000000u, 0xa0000001u,  // max r0, v0.x, c1.x
    0x02000001u, 0x800f0800u, 0x80e40000u,               // mov oC0, r0
    0x0000ffffu};
// clang-format on
} // namespace
// Everything the transaction touches beyond the state block: the target and
// depth bindings, viewport and scissor (SetRenderTarget resets the latter two),
// and the vertex input mode: a D3DSBT_ALL block does not put back "no
// declaration" (a caller in FVF mode, or with nothing bound yet), so the FVF or
// the declaration is re-set explicitly, as SunShadowApplyPass does.
struct ShadowReplayPass::SavedState {
    const ShadowReplayPass& pass;
    IDirect3DStateBlock9* block;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    DWORD fvf = 0;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count;
    SavedState(const ShadowReplayPass& p, IDirect3DStateBlock9* b, UINT n)
        : pass(p)
        , block(b)
        , count(n > 4 ? 4 : n) {}
    ~SavedState() {
        for (auto& t : targets) drop(t);
        drop(depth);
        drop(declaration);
    }
    HRESULT capture() noexcept {
        HRESULT hr = block->Capture();
        if (FAILED(hr)) return hr;
        hr = pass.call<GetDeclarationFn>(GetVertexDeclaration)(pass.device_, &declaration);
        if (FAILED(hr)) return hr;
        hr = pass.call<GetFvfFn>(GetFVF)(pass.device_, &fvf);
        if (FAILED(hr)) return hr;
        for (UINT i = 0; i < count; ++i) {
            hr = pass.call<GetRtFn>(GetRenderTarget)(pass.device_, i, &targets[i]);
            if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
        }
        hr = pass.call<GetDepthFn>(GetDepthStencilSurface)(pass.device_, &depth);
        if (FAILED(hr) && hr != D3DERR_NOTFOUND) return hr;
        hr = pass.call<GetViewportFn>(GetViewport)(pass.device_, &viewport);
        if (FAILED(hr)) return hr;
        return pass.call<GetScissorFn>(GetScissorRect)(pass.device_, &scissor);
    }
    HRESULT restore() noexcept {
        HRESULT first = S_OK;
        auto attempt = [&](HRESULT hr) {
            if (lost(hr) || (FAILED(hr) && SUCCEEDED(first))) first = hr;
            return !lost(hr);
        };
        D d = pass.device_;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr))) return first;
        for (UINT i = 1; i < count; ++i)
            if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, nullptr))) return first;
        for (UINT i = 0; i < count; ++i)
            if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, targets[i]))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, depth))) return first;
        if (!attempt(block->Apply())) return first;
        // The declaration object the caller had bound goes back as that object;
        // only a caller without one is restored through its FVF (or to none).
        if (declaration) {
            if (!attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration))) return first;
        } else if (fvf) {
            if (!attempt(pass.call<SetFvfFn>(SetFVF)(d, fvf))) return first;
        } else if (!attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, nullptr)))
            return first;
        if (!attempt(pass.call<SetViewportFn>(SetViewport)(d, &viewport))) return first;
        attempt(pass.call<SetScissorFn>(SetScissorRect)(d, &scissor));
        return first;
    }
};
ShadowReplayPass::~ShadowReplayPass() {
    detach();
}
unsigned ShadowReplayPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p :
         {static_cast<const void*>(block_), static_cast<const void*>(vs_), static_cast<const void*>(ps_),
          static_cast<const void*>(depth_), static_cast<const void*>(vs_alpha_), static_cast<const void*>(ps_alpha_)})
        n += p != nullptr;
    for (unsigned i = 0; i < shadow_replay_maps_max; ++i) n += (maps_[i] != nullptr) + (map_surfaces_[i] != nullptr);
    return n;
}
void ShadowReplayPass::release_targets() noexcept {
    for (unsigned i = 0; i < shadow_replay_maps_max; ++i) {
        drop(map_surfaces_[i]);
        drop(maps_[i]);
    }
    drop(depth_);
    invalidate_retained(); // a map that is gone retains nothing
}
void ShadowReplayPass::detach() noexcept {
    release_targets();
    drop(block_);
    drop(vs_);
    drop(ps_);
    drop(vs_alpha_);
    drop(ps_alpha_);
    alpha_bound_ = alpha_sampler_ = false;
    alpha_texture_ = nullptr;
    alpha_threshold_ = -1.f;
    device_ = nullptr;
    vtable_ = nullptr;
    count_ = depth_size_ = render_targets_ = 0;
    reset_pending_ = false;
    caps_ = {};
    for (unsigned& size : sizes_) size = 0;
}
void ShadowReplayPass::before_reset() noexcept {
    release_targets();
    drop(block_);
    reset_pending_ = device_ != nullptr;
}
void ShadowReplayPass::after_reset(HRESULT result) noexcept {
    if (SUCCEEDED(result)) reset_pending_ = false;
}
HRESULT ShadowReplayPass::attach_cascades(IDirect3DDevice9* d, void* const* native, const D3DCAPS9& caps,
                                          D3DFORMAT adapter_format, const unsigned* sizes, unsigned count) noexcept {
    detach();
    if (!d || !sizes || count < 1 || count > shadow_replay_maps_max) {
        caps_.reason = "arguments";
        return E_INVALIDARG;
    }
    for (unsigned i = 0; i < count; ++i)
        if (sizes[i] < 64 || sizes[i] > 4096) {
            caps_.reason = "arguments";
            return E_INVALIDARG;
        }
    device_ = d;
    vtable_ = native;
    render_targets_ = caps.NumSimultaneousRTs ? caps.NumSimultaneousRTs : 1;
    auto refuse = [&](const char* reason, HRESULT hr) {
        caps_.reason = reason;
        device_ = nullptr;
        vtable_ = nullptr;
        count_ = depth_size_ = 0;
        for (unsigned& kept : sizes_) kept = 0;
        return hr;
    };
    if ((caps.VertexShaderVersion & 0xffffu) < 0x0300u || (caps.PixelShaderVersion & 0xffffu) < 0x0300u)
        return refuse("shader_model", D3DERR_NOTAVAILABLE);
    // MaxTextureWidth/Height: a map is halved until it fits (its texel
    // doubles; the owner reads size(i) back).
    unsigned halved = 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned size = sizes[i];
        const auto fits = [&](unsigned v) { return caps.MaxTextureWidth >= v && caps.MaxTextureHeight >= v; };
        if (!fits(size)) {
            ++halved;
            while (size >= 64 && !fits(size)) size /= 2;
        }
        if (size < 64 || !fits(size)) return refuse("size", D3DERR_NOTAVAILABLE);
        sizes_[i] = size;
        if (size > depth_size_) depth_size_ = size;
    }
    count_ = count;
    caps_.halved = halved;
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr)) {
        const UINT adapter = creation.AdapterOrdinal;
        const D3DDEVTYPE type = creation.DeviceType;
        // R32F render target, else an always-available 32-bit colour target
        // with colour writes off (depth-only fallback: the depth attachment
        // alone holds the result and nothing can read it back).
        if (SUCCEEDED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
                                             D3DFMT_R32F))) {
            caps_.map_format = D3DFMT_R32F;
            caps_.readable = true;
        } else if (SUCCEEDED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_RENDERTARGET,
                                                    D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8))) {
            caps_.map_format = D3DFMT_X8R8G8B8;
            caps_.readable = false;
        }
        for (D3DFORMAT depth : {D3DFMT_D24X8, D3DFMT_D16}) {
            if (caps_.map_format == D3DFMT_UNKNOWN) break;
            if (SUCCEEDED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE,
                                                 depth)) &&
                SUCCEEDED(api->CheckDepthStencilMatch(adapter, type, adapter_format, caps_.map_format, depth))) {
                caps_.depth_format = depth;
                break;
            }
        }
        if (caps_.map_format == D3DFMT_UNKNOWN || caps_.depth_format == D3DFMT_UNKNOWN) hr = D3DERR_NOTAVAILABLE;
    }
    drop(api);
    caps_.formats = hr;
    if (FAILED(hr)) return refuse(hr == D3DERR_NOTAVAILABLE ? "formats" : "format_query", hr);
    hr = call<CreateVsFn>(CreateVertexShader)(d, vertex_words, &vs_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, pixel_words, &ps_);
    caps_.programs = hr;
    if (FAILED(hr)) {
        const ShadowReplayCaps kept = caps_;
        detach();
        caps_ = kept;
        caps_.reason = "programs";
        return hr;
    }
    // The alpha-tested caster pair, only when asked for. A refused program
    // (no smaller fallback set: platform-portability.md, "Shader slot budget")
    // leaves the pass enabled for the depth-only casters with caps_.alpha false.
    if (alpha_requested_) {
        HRESULT alpha = call<CreateVsFn>(CreateVertexShader)(d, alpha_vertex_words, &vs_alpha_);
        if (SUCCEEDED(alpha)) alpha = call<CreatePsFn>(CreatePixelShader)(d, alpha_pixel_words, &ps_alpha_);
        caps_.alpha_programs = alpha;
        caps_.alpha = SUCCEEDED(alpha) && vs_alpha_ && ps_alpha_;
        if (!caps_.alpha) {
            drop(vs_alpha_);
            drop(ps_alpha_);
        }
    }
    caps_.enabled = true;
    caps_.reason = "";
    return S_OK;
}
HRESULT ShadowReplayPass::prepare() noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return D3DERR_DEVICENOTRESET;
    bool ready = depth_ != nullptr;
    for (unsigned i = 0; i < count_; ++i) ready = ready && map_surfaces_[i];
    if (ready) return S_OK;
    release_targets();
    HRESULT hr = S_OK;
    for (unsigned i = 0; i < count_ && SUCCEEDED(hr); ++i) {
        hr = call<CreateTextureFn>(CreateTexture)(device_, sizes_[i], sizes_[i], 1, D3DUSAGE_RENDERTARGET,
                                                  caps_.map_format, D3DPOOL_DEFAULT, &maps_[i], nullptr);
        if (SUCCEEDED(hr)) hr = maps_[i]->GetSurfaceLevel(0, &map_surfaces_[i]);
    }
    // One attachment of the largest map's size serves every map.
    if (SUCCEEDED(hr))
        hr = call<CreateDepthFn>(CreateDepthStencilSurface)(device_, depth_size_, depth_size_, caps_.depth_format,
                                                            D3DMULTISAMPLE_NONE, 0, TRUE, &depth_, nullptr);
    if (FAILED(hr)) {
        release_targets();
        return hr;
    }
    ++allocations_;
    return S_OK;
}
HRESULT ShadowReplayPass::ensure_block() noexcept {
    if (block_) return S_OK;
    const HRESULT hr = call<CreateBlockFn>(CreateStateBlock)(device_, D3DSBT_ALL, &block_);
    if (FAILED(hr)) drop(block_);
    return hr;
}
// The map bound with its depth attachment and the depth-only draw state.
HRESULT ShadowReplayPass::bind() noexcept {
    D d = device_;
#define STEP(call)                                                                                                     \
    do {                                                                                                               \
        const HRESULT hresult = (call);                                                                                \
        if (FAILED(hresult)) return hresult;                                                                           \
    } while (false)
    // The application's depth surface may be smaller than the map: unbind it
    // before the map is RT0, then attach the map's own depth.
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr));
    for (UINT i = 1; i < render_targets_ && i < 4; ++i) STEP(call<SetRtFn>(SetRenderTarget)(d, i, nullptr));
    STEP(call<SetRtFn>(SetRenderTarget)(d, 0, map_surfaces_[0]));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, depth_));
    const D3DVIEWPORT9 viewport{0, 0, sizes_[0], sizes_[0], 0.f, 1.f};
    STEP(call<SetViewportFn>(SetViewport)(d, &viewport));
    for (auto state :
         {D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_STENCILENABLE,
          D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_LIGHTING,
          D3DRS_INDEXEDVERTEXBLENDENABLE, D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE, D3DRS_MULTISAMPLEANTIALIAS})
        STEP(call<SetRsFn>(SetRenderState)(d, state, FALSE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_ZENABLE, D3DZB_TRUE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_ZWRITEENABLE, TRUE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_ZFUNC, D3DCMP_LESSEQUAL));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_CLIPPING, TRUE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_VERTEXBLEND, D3DVBF_DISABLE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_FILLMODE, D3DFILL_SOLID));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE, caps_.readable ? 15u : 0u));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_MULTISAMPLEMASK, 0xffffffffu));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_DEPTHBIAS, 0));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_SLOPESCALEDEPTHBIAS, 0));
    STEP(call<SetVsFn>(SetVertexShader)(d, vs_));
    STEP(call<SetPsFn>(SetPixelShader)(d, ps_));
    STEP(call<SetFreqFn>(SetStreamSourceFreq)(d, 0, 1));
#undef STEP
    alpha_bound_ = alpha_sampler_ = false;
    alpha_texture_ = nullptr;
    alpha_threshold_ = -1.f; // this transaction's alpha state starts unbound
    return S_OK;
}
// An alpha-tested issue's program pair, sampler 0 (written once per
// transaction: WRAP as the engine's material samplers, trilinear, no bias or
// LOD clamp, no sRGB decode; the alpha is never decoded anyway), its texture
// and threshold, each only when it changes; an opaque issue after one binds
// the depth-only pair back. The block restores every one of these.
HRESULT ShadowReplayPass::bind_alpha(const ShadowReplayDraw& r, unsigned& made) noexcept {
    D d = device_;
    HRESULT hr = S_OK;
    if (!r.alpha_texture) {
        if (!alpha_bound_) return S_OK;
        hr = call<SetVsFn>(SetVertexShader)(d, vs_);
        ++made;
        if (SUCCEEDED(hr)) {
            hr = call<SetPsFn>(SetPixelShader)(d, ps_);
            ++made;
        }
        if (SUCCEEDED(hr)) alpha_bound_ = false;
        return hr;
    }
    if (!alpha_bound_) {
        hr = call<SetVsFn>(SetVertexShader)(d, vs_alpha_);
        ++made;
        if (SUCCEEDED(hr)) {
            hr = call<SetPsFn>(SetPixelShader)(d, ps_alpha_);
            ++made;
        }
        if (FAILED(hr)) return hr;
        alpha_bound_ = true;
    }
    if (!alpha_sampler_) {
        const std::pair<D3DSAMPLERSTATETYPE, DWORD> states[] = {{D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP},
                                                                {D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP},
                                                                {D3DSAMP_MAGFILTER, D3DTEXF_LINEAR},
                                                                {D3DSAMP_MINFILTER, D3DTEXF_LINEAR},
                                                                {D3DSAMP_MIPFILTER, D3DTEXF_LINEAR},
                                                                {D3DSAMP_MIPMAPLODBIAS, 0},
                                                                {D3DSAMP_MAXMIPLEVEL, 0},
                                                                {D3DSAMP_SRGBTEXTURE, FALSE}};
        for (const auto& state : states) {
            hr = call<SetSamplerFn>(SetSamplerState)(d, 0, state.first, state.second);
            ++made;
            if (FAILED(hr)) return hr;
        }
        alpha_sampler_ = true;
    }
    if (r.alpha_texture != alpha_texture_) {
        hr = call<SetTextureFn>(SetTexture)(d, 0, r.alpha_texture);
        ++made;
        if (FAILED(hr)) return hr;
        alpha_texture_ = r.alpha_texture;
    }
    if (r.alpha_threshold != alpha_threshold_) {
        const float threshold[4] = {r.alpha_threshold, r.alpha_threshold, r.alpha_threshold, r.alpha_threshold};
        hr = call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, threshold, 1);
        ++made;
        if (FAILED(hr)) return hr;
        alpha_threshold_ = r.alpha_threshold;
    }
    return S_OK;
}
// One draw: the application's own geometry bindings and cull mode (inverted
// for a back-face map: shadow_replay_cull_mode), the light rows (c0-c3, or
// c0-c2 of a cascade issue over the transaction's constant c3).
HRESULT ShadowReplayPass::issue(const ShadowReplayDraw& r, const float* rows, unsigned vectors, bool invert_cull,
                                unsigned* state_calls) noexcept {
    D d = device_;
    unsigned made = 0; // native state calls made (each only after the previous succeeded)
    HRESULT hr = bind_alpha(r, made);
    if (SUCCEEDED(hr)) {
        hr = call<SetDeclarationFn>(SetVertexDeclaration)(d, r.declaration);
        ++made;
    }
    if (SUCCEEDED(hr)) {
        hr = call<SetStreamFn>(SetStreamSource)(d, 0, r.vertex_buffer, r.stream_offset, r.stride);
        ++made;
    }
    if (SUCCEEDED(hr)) {
        hr = call<SetIndicesFn>(SetIndices)(d, r.indexed ? r.index_buffer : nullptr);
        ++made;
    }
    if (SUCCEEDED(hr)) {
        hr = call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, shadow_replay_cull_mode(r.cull_mode, invert_cull));
        ++made;
    }
    if (SUCCEEDED(hr)) {
        hr = call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 0, rows, vectors);
        ++made;
    }
    if (state_calls) *state_calls += made;
    if (FAILED(hr)) return hr;
    return r.indexed ? call<DrawIndexedFn>(DrawIndexedPrimitive)(d, r.topology, r.base_vertex, r.min_vertex,
                                                                 r.vertex_count, r.first, r.primitives)
                     : call<DrawFn>(DrawPrimitive)(d, r.topology, r.first, r.primitives);
}
// A cascade's map as RT0 under the shared attachment (which is at least as
// large as every map); SetRenderTarget resets the viewport, set explicitly.
HRESULT ShadowReplayPass::bind_map(unsigned map) noexcept {
    HRESULT hr = call<SetRtFn>(SetRenderTarget)(device_, 0, map_surfaces_[map]);
    const D3DVIEWPORT9 viewport{0, 0, sizes_[map], sizes_[map], 0.f, 1.f};
    if (SUCCEEDED(hr)) hr = call<SetViewportFn>(SetViewport)(device_, &viewport);
    return hr;
}
HRESULT ShadowReplayPass::execute_cascades(const ShadowReplayDraw* draws, unsigned draw_count,
                                           const ShadowReplayMapList* lists, unsigned list_count,
                                           bool caller_scene_open, bool caller_stateblock_recording,
                                           ShadowReplayResult* out) noexcept {
    if (!out) return E_INVALIDARG;
    *out = {};
    auto fail = [&](ShadowReplayStage stage, HRESULT hr) {
        out->failed = stage;
        out->operation = hr;
        return hr;
    };
    if (!device_ || !caps_.enabled || !draws || !draw_count || !lists || !list_count || list_count > count_ ||
        caller_stateblock_recording)
        return fail(ShadowReplayStage::Validate, E_INVALIDARG);
    if (reset_pending_) return fail(ShadowReplayStage::Validate, D3DERR_DEVICENOTRESET);
    unsigned seen = 0;
    for (unsigned l = 0; l < list_count; ++l) {
        const auto& list = lists[l];
        if (list.map >= count_ || (seen & (1u << list.map)) || (list.count && !list.issues))
            return fail(ShadowReplayStage::Validate, E_INVALIDARG);
        seen |= 1u << list.map;
        for (unsigned i = 0; i < list.count; ++i)
            if (list.issues[i].draw >= draw_count) return fail(ShadowReplayStage::Validate, E_INVALIDARG);
    }
    for (unsigned i = 0; i < draw_count; ++i) {
        const auto& r = draws[i];
        if (!r.vertex_buffer || !r.declaration || !r.stride || !r.primitives || (r.indexed && !r.index_buffer) ||
            (r.alpha_texture && !caps_.alpha))
            return fail(ShadowReplayStage::Validate, E_INVALIDARG);
    }
    // From here the listed maps are being rewritten: whatever was retained
    // for them is void until the owner retains again after a full success.
    for (unsigned l = 0; l < list_count; ++l) retained_[lists[l].map].valid = false;
    HRESULT hr = prepare();
    if (FAILED(hr)) return fail(ShadowReplayStage::Targets, hr);
    hr = ensure_block();
    if (FAILED(hr)) return fail(ShadowReplayStage::Block, hr);
    SavedState saved(*this, block_, render_targets_);
    hr = saved.capture();
    if (FAILED(hr)) return fail(ShadowReplayStage::Capture, hr);
    D d = device_;
    ShadowReplayStage stage = ShadowReplayStage::Scene;
    bool own_scene = false;
    auto step = [&](ShadowReplayStage s, HRESULT value) {
        stage = s;
        hr = value;
        return SUCCEEDED(hr);
    };
    if (!caller_scene_open) own_scene = step(ShadowReplayStage::Scene, call<SceneFn>(BeginScene)(d));
    if (SUCCEEDED(hr)) step(ShadowReplayStage::Bind, bind());
    constexpr float w_row[4] = {0.f, 0.f, 0.f, 1.f}; // c3 of every issue: the light projection is orthographic
    if (SUCCEEDED(hr)) step(ShadowReplayStage::Bind, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 3, w_row, 1));
    for (unsigned l = 0; l < list_count && SUCCEEDED(hr); ++l) {
        const auto& list = lists[l];
        out->state_calls_map[list.map] += 2; // bind_map: SetRenderTarget and SetViewport
        if (!step(ShadowReplayStage::Bind, bind_map(list.map))) break;
        ++out->state_calls_map[list.map];
        if (!step(ShadowReplayStage::Clear,
                  call<ClearFn>(Clear)(d, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffffffffu, 1.f, 0)))
            break;
        for (unsigned i = 0; i < list.count; ++i) {
            if (!step(ShadowReplayStage::Draw, issue(draws[list.issues[i].draw], list.issues[i].rows, 3,
                                                     list.invert_cull, &out->state_calls_map[list.map])))
                break;
            ++out->drawn;
            ++out->drawn_map[list.map];
        }
    }
    if (own_scene && !lost(hr)) {
        const HRESULT end = call<SceneFn>(EndScene)(d);
        if (SUCCEEDED(hr) || lost(end)) {
            if (FAILED(end)) stage = ShadowReplayStage::EndScene;
            hr = end;
        }
    }
    out->operation = hr;
    out->restore = lost(hr) ? hr : saved.restore();
    if (FAILED(hr) || FAILED(out->restore)) {
        out->failed = FAILED(hr) ? stage : ShadowReplayStage::Restore;
        return FAILED(out->restore) ? out->restore : hr;
    }
    return S_OK;
}
} // namespace x3m::renderer
