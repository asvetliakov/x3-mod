#include "shadow_replay_pass.h"
#include <array>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp), as in AmbientOcclusionPass.
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, CreateDepthStencilSurface = 29,
    SetRenderTarget = 37, GetRenderTarget = 38, SetDepthStencilSurface = 39, GetDepthStencilSurface = 40,
    BeginScene = 41, EndScene = 42, Clear = 43, SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59,
    SetScissorRect = 75, GetScissorRect = 76, DrawPrimitive = 81, DrawIndexedPrimitive = 82,
    SetVertexDeclaration = 87, CreateVertexShader = 91, SetVertexShader = 92, SetVertexShaderConstantF = 94,
    SetStreamSource = 100, SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreateDepthFn = HRESULT(WINAPI*)(D, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
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
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetVsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
// D3D9 token encoding authored here from the documented operation contract
// (as rigid_replay_program.cpp); not a captured program.
// vs_3_0: pos = (v0.xyz, 1); o0 = light rows c0-c3 . pos; o1.x = o0.z (sun depth).
constexpr DWORD vertex_words[] = {
    0xfffe0300u,
    0x05000051u, 0xa00f0008u, 0x3f800000u, 0u, 0u, 0u,  // def c8, 1, 0, 0, 0
    0x0200001fu, 0x80000000u, 0x900f0000u,               // dcl_position v0
    0x0200001fu, 0x80000000u, 0xe00f0000u,               // dcl_position o0
    0x0200001fu, 0x80000005u, 0xe00f0001u,               // dcl_texcoord o1
    0x04000004u, 0x800f0000u, 0x90240000u, 0xa0400008u, 0xa0150008u, // mad r0, v0.xyzx, c8.xxxy, c8.yyyx
    0x03000009u, 0xe0010000u, 0x80e40000u, 0xa0e40000u,  // dp4 o0.x, r0, c0
    0x03000009u, 0xe0020000u, 0x80e40000u, 0xa0e40001u,  // dp4 o0.y, r0, c1
    0x03000009u, 0xe0040000u, 0x80e40000u, 0xa0e40002u,  // dp4 o0.z, r0, c2
    0x03000009u, 0xe0080000u, 0x80e40000u, 0xa0e40003u,  // dp4 o0.w, r0, c3
    0x03000009u, 0xe0010001u, 0x80e40000u, 0xa0e40002u,  // dp4 o1.x, r0, c2
    0x0000ffffu};
// ps_3_0: oC0 = v0.x (the interpolated sun-space depth).
constexpr DWORD pixel_words[] = {
    0xffff0300u,
    0x0200001fu, 0x80000005u, 0x900f0000u,               // dcl_texcoord v0
    0x02000001u, 0x800f0800u, 0x90000000u,               // mov oC0, v0.x
    0x0000ffffu};
} // namespace
// Everything the transaction touches beyond the state block: the target and
// depth bindings, viewport and scissor (SetRenderTarget resets the latter two).
struct ShadowReplayPass::SavedState {
    const ShadowReplayPass& pass;
    IDirect3DStateBlock9* block;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count;
    SavedState(const ShadowReplayPass& p, IDirect3DStateBlock9* b, UINT n) : pass(p), block(b), count(n > 4 ? 4 : n) {}
    ~SavedState() { for (auto& t : targets) drop(t); drop(depth); }
    HRESULT capture() noexcept {
        HRESULT hr = block->Capture();
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
        auto attempt = [&](HRESULT hr) { if (lost(hr) || (FAILED(hr) && SUCCEEDED(first))) first = hr; return !lost(hr); };
        D d = pass.device_;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr))) return first;
        for (UINT i = 1; i < count; ++i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, nullptr))) return first;
        for (UINT i = 0; i < count; ++i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, targets[i]))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, depth))) return first;
        if (!attempt(block->Apply())) return first;
        if (!attempt(pass.call<SetViewportFn>(SetViewport)(d, &viewport))) return first;
        attempt(pass.call<SetScissorFn>(SetScissorRect)(d, &scissor));
        return first;
    }
};
ShadowReplayPass::~ShadowReplayPass() { detach(); }
unsigned ShadowReplayPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(block_), static_cast<const void*>(vs_), static_cast<const void*>(ps_),
                          static_cast<const void*>(map_), static_cast<const void*>(map_surface_), static_cast<const void*>(depth_)})
        n += p != nullptr;
    return n;
}
void ShadowReplayPass::release_targets() noexcept { drop(map_surface_); drop(map_); drop(depth_); }
void ShadowReplayPass::detach() noexcept {
    release_targets(); drop(block_); drop(vs_); drop(ps_);
    device_ = nullptr; vtable_ = nullptr; size_ = render_targets_ = 0; reset_pending_ = false; caps_ = {}; view_rows_valid_ = false;
}
void ShadowReplayPass::before_reset() noexcept { release_targets(); drop(block_); view_rows_valid_ = false; reset_pending_ = device_ != nullptr; }
void ShadowReplayPass::after_reset(HRESULT result) noexcept { if (SUCCEEDED(result)) reset_pending_ = false; }
HRESULT ShadowReplayPass::attach(IDirect3DDevice9* d, void* const* native, const D3DCAPS9& caps, D3DFORMAT adapter_format, unsigned size) noexcept {
    detach();
    if (!d || size < 64 || size > 4096) { caps_.reason = "arguments"; return E_INVALIDARG; }
    device_ = d; vtable_ = native; size_ = size; render_targets_ = caps.NumSimultaneousRTs ? caps.NumSimultaneousRTs : 1;
    if ((caps.VertexShaderVersion & 0xffffu) < 0x0300u || (caps.PixelShaderVersion & 0xffffu) < 0x0300u) {
        caps_.reason = "shader_model"; device_ = nullptr; vtable_ = nullptr; return D3DERR_NOTAVAILABLE;
    }
    if (caps.MaxTextureWidth < size || caps.MaxTextureHeight < size) { caps_.reason = "size"; device_ = nullptr; vtable_ = nullptr; return D3DERR_NOTAVAILABLE; }
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr)) {
        const UINT adapter = creation.AdapterOrdinal; const D3DDEVTYPE type = creation.DeviceType;
        // R32F render target, else an always-available 32-bit colour target
        // with colour writes off (depth-only fallback: the depth attachment
        // alone holds the result and nothing can read it back).
        if (SUCCEEDED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F))) {
            caps_.map_format = D3DFMT_R32F; caps_.readable = true;
        } else if (SUCCEEDED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8))) {
            caps_.map_format = D3DFMT_X8R8G8B8; caps_.readable = false;
        }
        for (D3DFORMAT depth : {D3DFMT_D24X8, D3DFMT_D16}) {
            if (caps_.map_format == D3DFMT_UNKNOWN) break;
            if (SUCCEEDED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, depth)) &&
                SUCCEEDED(api->CheckDepthStencilMatch(adapter, type, adapter_format, caps_.map_format, depth))) { caps_.depth_format = depth; break; }
        }
        if (caps_.map_format == D3DFMT_UNKNOWN || caps_.depth_format == D3DFMT_UNKNOWN) hr = D3DERR_NOTAVAILABLE;
    }
    drop(api);
    caps_.formats = hr;
    if (FAILED(hr)) { caps_.reason = hr == D3DERR_NOTAVAILABLE ? "formats" : "format_query"; device_ = nullptr; vtable_ = nullptr; return hr; }
    hr = call<CreateVsFn>(CreateVertexShader)(d, vertex_words, &vs_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, pixel_words, &ps_);
    caps_.programs = hr;
    if (FAILED(hr)) { const ShadowReplayCaps kept = caps_; detach(); caps_ = kept; caps_.reason = "programs"; return hr; }
    caps_.enabled = true; caps_.reason = "";
    return S_OK;
}
HRESULT ShadowReplayPass::prepare() noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return D3DERR_DEVICENOTRESET;
    if (map_surface_ && depth_) return S_OK;
    release_targets();
    HRESULT hr = call<CreateTextureFn>(CreateTexture)(device_, size_, size_, 1, D3DUSAGE_RENDERTARGET, caps_.map_format, D3DPOOL_DEFAULT, &map_, nullptr);
    if (SUCCEEDED(hr)) hr = map_->GetSurfaceLevel(0, &map_surface_);
    if (SUCCEEDED(hr)) hr = call<CreateDepthFn>(CreateDepthStencilSurface)(device_, size_, size_, caps_.depth_format, D3DMULTISAMPLE_NONE, 0, TRUE, &depth_, nullptr);
    if (FAILED(hr)) { release_targets(); return hr; }
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
#define STEP(call) do { const HRESULT hresult = (call); if (FAILED(hresult)) return hresult; } while (false)
    // The application's depth surface may be smaller than the map: unbind it
    // before the map is RT0, then attach the map's own depth.
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr));
    for (UINT i = 1; i < render_targets_ && i < 4; ++i) STEP(call<SetRtFn>(SetRenderTarget)(d, i, nullptr));
    STEP(call<SetRtFn>(SetRenderTarget)(d, 0, map_surface_));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, depth_));
    const D3DVIEWPORT9 viewport{0, 0, size_, size_, 0.f, 1.f};
    STEP(call<SetViewportFn>(SetViewport)(d, &viewport));
    for (auto state : {D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_STENCILENABLE,
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
    return S_OK;
}
HRESULT ShadowReplayPass::execute(const ShadowReplayDraw* draws, unsigned count, bool caller_scene_open, bool caller_stateblock_recording,
                                  ShadowReplayResult* out) noexcept {
    if (!out) return E_INVALIDARG;
    *out = {};
    auto fail = [&](ShadowReplayStage stage, HRESULT hr) { out->failed = stage; out->operation = hr; return hr; };
    if (!device_ || !caps_.enabled || !draws || !count || caller_stateblock_recording) return fail(ShadowReplayStage::Validate, E_INVALIDARG);
    if (reset_pending_) return fail(ShadowReplayStage::Validate, D3DERR_DEVICENOTRESET);
    for (unsigned i = 0; i < count; ++i) {
        const auto& r = draws[i];
        if (!r.vertex_buffer || !r.declaration || !r.stride || !r.primitives || (r.indexed && !r.index_buffer)) return fail(ShadowReplayStage::Validate, E_INVALIDARG);
    }
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
    auto step = [&](ShadowReplayStage s, HRESULT value) { stage = s; hr = value; return SUCCEEDED(hr); };
    if (!caller_scene_open) own_scene = step(ShadowReplayStage::Scene, call<SceneFn>(BeginScene)(d));
    if (SUCCEEDED(hr)) step(ShadowReplayStage::Bind, bind());
    // Far depth everywhere (1.0 in the R32F .r and in the depth attachment).
    if (SUCCEEDED(hr)) step(ShadowReplayStage::Clear, call<ClearFn>(Clear)(d, 0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xffffffffu, 1.f, 0));
    for (unsigned i = 0; i < count && SUCCEEDED(hr); ++i) {
        const auto& r = draws[i];
        if (!step(ShadowReplayStage::Draw, call<SetDeclarationFn>(SetVertexDeclaration)(d, r.declaration))) break;
        if (!step(ShadowReplayStage::Draw, call<SetStreamFn>(SetStreamSource)(d, 0, r.vertex_buffer, r.stream_offset, r.stride))) break;
        if (!step(ShadowReplayStage::Draw, call<SetIndicesFn>(SetIndices)(d, r.indexed ? r.index_buffer : nullptr))) break;
        if (!step(ShadowReplayStage::Draw, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, r.cull_mode))) break;
        if (!step(ShadowReplayStage::Draw, call<SetVsConstantsFn>(SetVertexShaderConstantF)(d, 0, r.light_rows, 4))) break;
        const HRESULT drawn = r.indexed
            ? call<DrawIndexedFn>(DrawIndexedPrimitive)(d, r.topology, r.base_vertex, r.min_vertex, r.vertex_count, r.first, r.primitives)
            : call<DrawFn>(DrawPrimitive)(d, r.topology, r.first, r.primitives);
        if (!step(ShadowReplayStage::Draw, drawn)) break;
        ++out->drawn;
    }
    if (own_scene && !lost(hr)) { const HRESULT end = call<SceneFn>(EndScene)(d); if (SUCCEEDED(hr) || lost(end)) { if (FAILED(end)) stage = ShadowReplayStage::EndScene; hr = end; } }
    out->operation = hr;
    out->restore = lost(hr) ? hr : saved.restore();
    if (FAILED(hr) || FAILED(out->restore)) {
        out->failed = FAILED(hr) ? stage : ShadowReplayStage::Restore;
        return FAILED(out->restore) ? out->restore : hr;
    }
    return S_OK;
}
} // namespace x3m::renderer
