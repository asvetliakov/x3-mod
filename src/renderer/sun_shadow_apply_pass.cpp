#include "sun_shadow_apply_pass.h"
#include "ambient_occlusion_caps.h"
#include "quad_vertex_program.h"
#include <cmath>
#include <iterator>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp), as in AmbientOcclusionPass.
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, GetVertexDeclaration = 88, SetFVF = 89, GetFVF = 90,
    CreateVertexShader = 91, SetVertexShader = 92,
    SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using SetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9*);
using GetRtFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DSurface9**);
using SetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9*);
using GetDepthFn = HRESULT(WINAPI*)(D, IDirect3DSurface9**);
using SceneFn = HRESULT(WINAPI*)(D);
using SetViewportFn = HRESULT(WINAPI*)(D, const D3DVIEWPORT9*);
using GetViewportFn = HRESULT(WINAPI*)(D, D3DVIEWPORT9*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using CreateBlockFn = HRESULT(WINAPI*)(D, D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetStageFn = HRESULT(WINAPI*)(D, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetScissorFn = HRESULT(WINAPI*)(D, const RECT*);
using GetScissorFn = HRESULT(WINAPI*)(D, RECT*);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using SetFvfFn = HRESULT(WINAPI*)(D, DWORD);
using GetFvfFn = HRESULT(WINAPI*)(D, DWORD*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored program is embedded (tools/shaders/generate_rigid_motion_pixel.py).
constexpr DWORD apply_words[] = {
#include "sun_shadow_apply_program_inc.h"
};
constexpr DWORD cascade_apply_words[] = {
#include "sun_shadow_cascade_apply_program_inc.h"
};
// The eight kernel rotations (pi / 8 steps; the 3x3 kernel is symmetric under pi).
constexpr float kernel_cos[8] = {1.f, .92387953f, .70710678f, .38268343f, 0.f, -.38268343f, -.70710678f, -.92387953f};
constexpr float kernel_sin[8] = {0.f, .38268343f, .70710678f, .92387953f, 1.f, .92387953f, .70710678f, .38268343f};
template<class Resource> HRESULT same_device(IDirect3DDevice9* device, Resource* resource) noexcept {
    IDirect3DDevice9* owner = nullptr;
    HRESULT hr = resource->GetDevice(&owner);
    if (FAILED(hr)) return hr;
    const bool same = owner == device;
    drop(owner);
    return same ? S_OK : E_INVALIDARG;
}
// RT2 of the lane: G32R32F (view depth from .r by the z/w law) or
// A32B32G32R32F (.b carries the interpolated clip w; the receiver-depth
// option, docs/architecture/shadow-receiver-depth.md). Gated per frame from
// the bound texture's format; the program's flag constant follows it.
bool depth_share_format(D3DFORMAT format, bool& linear) noexcept {
    linear = format == D3DFMT_A32B32G32R32F;
    return linear || format == D3DFMT_G32R32F;
}
bool finite_params(const SunShadowApplyParams& p) noexcept {
    for (float v : {p.m00, p.m11, p.m20, p.m21, p.m22, p.m32, p.exponent, p.bias_constant, p.bias_max, p.planar_step})
        if (!std::isfinite(v)) return false;
    for (float v : p.rows) if (!std::isfinite(v)) return false;
    return p.m00 > 0.f && p.m11 > 0.f && p.m22 > 1.f && p.m32 < 0.f && std::isfinite(1.f / std::fabs(p.m32)) &&
           p.exponent > 0.f && p.exponent <= 1.f && p.bias_constant >= 0.f && p.bias_max >= 0.f && p.planar_step > 0.f;
}
} // namespace
// Everything the quad touches beyond the state block: the target and depth
// bindings, viewport and scissor (SetRenderTarget resets the latter two), and
// the vertex input mode: a D3DSBT_ALL block records the declaration but a
// caller with no declaration bound (FVF mode, or nothing yet) is not restored
// by Apply alone, so the FVF or the declaration is re-set explicitly.
struct SunShadowApplyPass::SavedState {
    const SunShadowApplyPass& pass;
    IDirect3DStateBlock9* block;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    DWORD fvf = 0;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count, samplers;
    SavedState(const SunShadowApplyPass& p, IDirect3DStateBlock9* b, UINT n, UINT s = 2) : pass(p), block(b), count(n > 4 ? 4 : n), samplers(s) {}
    ~SavedState() { for (auto& t : targets) drop(t); drop(depth); drop(declaration); }
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
        auto attempt = [&](HRESULT hr) { if (lost(hr) || (FAILED(hr) && SUCCEEDED(first))) first = hr; return !lost(hr); };
        D d = pass.device_;
        for (UINT i = 0; i < samplers; ++i) if (!attempt(pass.call<SetTextureFn>(SetTexture)(d, i, nullptr))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr))) return first;
        for (UINT i = 1; i < count; ++i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, nullptr))) return first;
        for (UINT i = 0; i < count; ++i) if (!attempt(pass.call<SetRtFn>(SetRenderTarget)(d, i, targets[i]))) return first;
        if (!attempt(pass.call<SetDepthFn>(SetDepthStencilSurface)(d, depth))) return first;
        if (!attempt(block->Apply())) return first;
        // The declaration object the caller had bound goes back as that object;
        // only a caller without one is restored through its FVF (or to none).
        if (declaration) { if (!attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, declaration))) return first; }
        else if (fvf) { if (!attempt(pass.call<SetFvfFn>(SetFVF)(d, fvf))) return first; }
        else if (!attempt(pass.call<SetDeclarationFn>(SetVertexDeclaration)(d, nullptr))) return first;
        if (!attempt(pass.call<SetViewportFn>(SetViewport)(d, &viewport))) return first;
        attempt(pass.call<SetScissorFn>(SetScissorRect)(d, &scissor));
        return first;
    }
};
SunShadowApplyPass::~SunShadowApplyPass() { detach(); }
unsigned SunShadowApplyPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(block_), static_cast<const void*>(apply_), static_cast<const void*>(quad_vs_),
                          static_cast<const void*>(quad_declaration_), static_cast<const void*>(cascade_apply_)})
        n += p != nullptr;
    return n;
}
void SunShadowApplyPass::detach() noexcept {
    drop(block_); drop(apply_); drop(cascade_apply_); drop(quad_vs_); drop(quad_declaration_);
    device_ = nullptr; vtable_ = nullptr; render_targets_ = streams_ = 0; reset_pending_ = false;
    caps_ = {}; target_format_ = D3DFMT_UNKNOWN;
}
void SunShadowApplyPass::before_reset() noexcept { drop(block_); reset_pending_ = device_ != nullptr; }
void SunShadowApplyPass::after_reset(HRESULT result) noexcept { if (SUCCEEDED(result)) reset_pending_ = false; }
HRESULT SunShadowApplyPass::attach(IDirect3DDevice9* d, void* const* native, const D3DCAPS9& caps, D3DFORMAT adapter_format,
                                   D3DFORMAT target_format, bool cascades) noexcept {
    detach();
    if (!d) { caps_.reason = "device"; return E_INVALIDARG; }
    device_ = d; vtable_ = native;
    auto refuse = [&](const char* reason, HRESULT hr) { caps_.reason = reason; device_ = nullptr; vtable_ = nullptr; return hr; };
    if ((caps.VertexShaderVersion & 0xffffu) < 0x0300u || (caps.PixelShaderVersion & 0xffffu) < 0x0300u) return refuse("shader_model", D3DERR_NOTAVAILABLE);
    caps_.program_slots = ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(apply_words), std::size(apply_words));
    if (caps_.program_slots == 0 || caps.MaxPixelShader30InstructionSlots < caps_.program_slots) return refuse("ps_slots", D3DERR_NOTAVAILABLE);
    if (cascades) {
        caps_.cascade_slots = ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(cascade_apply_words), std::size(cascade_apply_words));
        if (caps_.cascade_slots == 0 || caps.MaxPixelShader30InstructionSlots < caps_.cascade_slots) return refuse("cascade_ps_slots", D3DERR_NOTAVAILABLE);
    }
    if (!(caps.SrcBlendCaps & D3DPBLENDCAPS_ZERO) || !(caps.DestBlendCaps & D3DPBLENDCAPS_SRCCOLOR)) return refuse("blend_caps", D3DERR_NOTAVAILABLE);
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    const char* gate = nullptr;
    if (SUCCEEDED(hr)) {
        const UINT adapter = creation.AdapterOrdinal; const D3DDEVTYPE type = creation.DeviceType;
        if (FAILED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_G32R32F))) gate = "g32r32f";
        else if (FAILED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F))) gate = "r32f";
        else if (FAILED(api->CheckDeviceFormat(adapter, type, adapter_format, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
                                               D3DRTYPE_TEXTURE, target_format))) gate = "target_blending";
    }
    drop(api);
    caps_.formats = hr;
    if (FAILED(hr)) return refuse("format_query", hr);
    if (gate) { caps_.formats = D3DERR_NOTAVAILABLE; return refuse(gate, D3DERR_NOTAVAILABLE); }
    render_targets_ = caps.NumSimultaneousRTs ? caps.NumSimultaneousRTs : 1; streams_ = caps.MaxStreams; target_format_ = target_format;
    hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(quad_vertex_program()), &quad_vs_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, quad_declaration, &quad_declaration_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, apply_words, &apply_);
    if (SUCCEEDED(hr) && cascades) hr = call<CreatePsFn>(CreatePixelShader)(d, cascade_apply_words, &cascade_apply_);
    caps_.programs = hr;
    if (FAILED(hr)) { const SunShadowApplyCaps kept = caps_; detach(); caps_ = kept; caps_.reason = "programs"; return hr; }
    caps_.enabled = true; caps_.reason = ""; caps_.cascades = cascades;
    return S_OK;
}
HRESULT SunShadowApplyPass::ensure_block() noexcept {
    if (block_) return S_OK;
    const HRESULT hr = call<CreateBlockFn>(CreateStateBlock)(device_, D3DSBT_ALL, &block_);
    if (FAILED(hr)) drop(block_);
    return hr;
}
// The quad's whole device state: the target alone (depth and the other
// targets unbound, RT0 bound before its full viewport: a viewport must fit
// the bound target), the quad program pair, the multiply blend, point/clamp
// samplers 0-1 (0-5 for the cascade program).
HRESULT SunShadowApplyPass::normalize(IDirect3DSurface9* target, UINT w, UINT h, IDirect3DPixelShader9* program, UINT samplers) noexcept {
    D d = device_;
#define STEP(call) do { const HRESULT hresult = (call); if (FAILED(hresult)) return hresult; } while (false)
    for (UINT i = 0; i < 16; ++i) STEP(call<SetTextureFn>(SetTexture)(d, i, nullptr));
    for (UINT i = 0; i < 4; ++i) STEP(call<SetTextureFn>(SetTexture)(d, D3DVERTEXTEXTURESAMPLER0 + i, nullptr));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr));
    for (UINT i = 1; i < render_targets_ && i < 4; ++i) STEP(call<SetRtFn>(SetRenderTarget)(d, i, nullptr));
    STEP(call<SetRtFn>(SetRenderTarget)(d, 0, target));
    STEP(call<SetDeclarationFn>(SetVertexDeclaration)(d, quad_declaration_));
    STEP(call<SetVsFn>(SetVertexShader)(d, quad_vs_));
    STEP(call<SetPsFn>(SetPixelShader)(d, program));
    STEP(call<SetIndicesFn>(SetIndices)(d, nullptr));
    for (UINT i = 0; i < streams_; ++i) STEP(call<SetFreqFn>(SetStreamSourceFreq)(d, i, 1));
    for (auto state : {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE,
                       D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_FOGENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE,
                       D3DRS_CLIPPLANEENABLE, D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_INDEXEDVERTEXBLENDENABLE,
                       D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE, D3DRS_ANTIALIASEDLINEENABLE})
        STEP(call<SetRsFn>(SetRenderState)(d, state, FALSE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_ALPHABLENDENABLE, TRUE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, D3DBLEND_ZERO));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_BLENDOP, D3DBLENDOP_ADD));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_VERTEXBLEND, D3DVBF_DISABLE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_WRAP0, 0));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_FILLMODE, D3DFILL_SOLID));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, D3DCULL_NONE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE, 15));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_MULTISAMPLEMASK, 0xffffffff));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXCOORDINDEX, 0));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE));
    for (UINT i = 0; i < samplers; ++i) {
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MINFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_SRGBTEXTURE, FALSE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAXMIPLEVEL, 0));
    }
    const D3DVIEWPORT9 viewport{0, 0, w, h, 0.f, 1.f};
    STEP(call<SetViewportFn>(SetViewport)(d, &viewport));
#undef STEP
    return S_OK;
}
HRESULT SunShadowApplyPass::execute(const SunShadowApplyFrame& in, SunShadowApplyResult* out) noexcept {
    if (!out) return E_INVALIDARG;
    *out = {};
    // Missing or invalid inputs skip the quad and touch nothing (S_FALSE).
    auto skip = [&](const char* reason) { out->skipped = true; out->skipped_reason = reason; out->operation = S_FALSE; return S_FALSE; };
    if (!device_ || !caps_.enabled) return skip("detached");
    if (reset_pending_) return skip("reset_pending");
    if (!in.depth_share || !in.map || !in.target || !in.width || !in.height || in.caller_stateblock_recording) return skip("input");
    if (!finite_params(in.params)) return skip("params");
    D3DSURFACE_DESC desc{};
    bool linear_depth = false;
    if (FAILED(in.depth_share->GetLevelDesc(0, &desc)) || desc.Width != in.width || desc.Height != in.height || !depth_share_format(desc.Format, linear_depth) ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE)
        return skip("format");
    if (FAILED(in.map->GetLevelDesc(0, &desc)) || desc.Width != desc.Height || desc.Width < 64 || desc.Format != D3DFMT_R32F ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE)
        return skip("format");
    const UINT map_size = desc.Width;
    if (FAILED(in.target->GetDesc(&desc)) || desc.Width != in.width || desc.Height != in.height || desc.Format != target_format_ ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE || !(desc.Usage & D3DUSAGE_RENDERTARGET))
        return skip("format");
    if (FAILED(same_device(device_, in.depth_share)) || FAILED(same_device(device_, in.map)) || FAILED(same_device(device_, in.target)))
        return skip("device");
    auto fail = [&](SunShadowApplyStage stage, HRESULT hr) { out->failed = stage; out->operation = hr; return hr; };
    HRESULT hr = ensure_block();
    if (FAILED(hr)) return fail(SunShadowApplyStage::Block, hr);
    SavedState saved(*this, block_, render_targets_);
    hr = saved.capture();
    if (FAILED(hr)) return fail(SunShadowApplyStage::Capture, hr);
    D d = device_;
    const SunShadowApplyParams& p = in.params;
    SunShadowApplyStage stage = SunShadowApplyStage::Normalize;
    bool own_scene = false;
    auto step = [&](SunShadowApplyStage s, HRESULT value) { stage = s; hr = value; return SUCCEEDED(hr); };
    hr = normalize(in.target, in.width, in.height, apply_, 2);
    if (SUCCEEDED(hr) && !in.caller_scene_open) own_scene = step(SunShadowApplyStage::Scene, call<SceneFn>(BeginScene)(d));
    // c0..c6 of sun_shadow_apply_ps.hlsl, one upload.
    const unsigned rotation = p.jitter_index & 7u;
    const float block[7][4] = {
        {p.m00, p.m11, p.m20, p.m21},
        {p.m22, p.m32, p.exponent, p.bias_constant},
        {p.rows[0], p.rows[1], p.rows[2], p.rows[3]},
        {p.rows[4], p.rows[5], p.rows[6], p.rows[7]},
        {p.rows[8], p.rows[9], p.rows[10], p.rows[11]},
        {float(map_size), 1.f / float(map_size), kernel_cos[rotation], kernel_sin[rotation]},
        {p.bias_max, p.planar_step, linear_depth ? 1.f : 0.f, 0.f},
    };
    if (SUCCEEDED(hr)) step(SunShadowApplyStage::Constants, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, &block[0][0], 7));
    if (SUCCEEDED(hr)) step(SunShadowApplyStage::Apply, call<SetTextureFn>(SetTexture)(d, 0, in.depth_share));
    if (SUCCEEDED(hr)) step(SunShadowApplyStage::Apply, call<SetTextureFn>(SetTexture)(d, 1, in.map));
    bool applied = false;
    if (SUCCEEDED(hr)) {
        QuadVertex vertices[4];
        quad_vertices(in.width, in.height, vertices);
        applied = step(SunShadowApplyStage::Apply, call<DrawUpFn>(DrawPrimitiveUP)(d, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(QuadVertex)));
    }
    if (own_scene && !lost(hr)) { const HRESULT end = call<SceneFn>(EndScene)(d); if (SUCCEEDED(hr) || lost(end)) { if (FAILED(end)) stage = SunShadowApplyStage::EndScene; hr = end; } }
    out->operation = hr;
    out->restore = lost(hr) ? hr : saved.restore();
    if (FAILED(hr) || FAILED(out->restore)) {
        out->failed = FAILED(hr) ? stage : SunShadowApplyStage::Restore;
        return FAILED(out->restore) ? out->restore : hr;
    }
    out->applied = applied; out->map_size = map_size; out->linear_depth = linear_depth;
    return S_OK;
}
HRESULT SunShadowApplyPass::execute_cascades(const SunShadowCascadeFrame& in, SunShadowApplyResult* out) noexcept {
    if (!out) return E_INVALIDARG;
    *out = {};
    auto skip = [&](const char* reason) { out->skipped = true; out->skipped_reason = reason; out->operation = S_FALSE; return S_FALSE; };
    if (!device_ || !caps_.enabled) return skip("detached");
    if (!caps_.cascades || !cascade_apply_) return skip("cascades");
    if (reset_pending_) return skip("reset_pending");
    if (!in.depth_share || !in.target || !in.width || !in.height || !in.count || in.count > shadow_cascade_max || in.caller_stateblock_recording) return skip("input");
    for (float v : {in.m00, in.m11, in.m20, in.m21, in.m22, in.m32, in.exponent, in.planar_step}) if (!std::isfinite(v)) return skip("params");
    if (!(in.m00 > 0.f) || !(in.m11 > 0.f) || !(in.m22 > 1.f) || !(in.m32 < 0.f) || !std::isfinite(1.f / std::fabs(in.m32)) ||
        !(in.exponent > 0.f) || !(in.exponent <= 1.f) || !(in.planar_step > 0.f))
        return skip("params");
    D3DSURFACE_DESC desc{};
    bool linear_depth = false;
    if (FAILED(in.depth_share->GetLevelDesc(0, &desc)) || desc.Width != in.width || desc.Height != in.height || !depth_share_format(desc.Format, linear_depth) ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE)
        return skip("format");
    if (FAILED(in.target->GetDesc(&desc)) || desc.Width != in.width || desc.Height != in.height || desc.Format != target_format_ ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE || !(desc.Usage & D3DUSAGE_RENDERTARGET))
        return skip("format");
    if (FAILED(same_device(device_, in.depth_share)) || FAILED(same_device(device_, in.target))) return skip("device");
    // c0-c3, the nine rotated kernel offsets (c4-c12, row-major as the
    // single-map program's j, i loops) and five constants per cascade from
    // c13 (c13-c37 for five cascades); an unused cascade keeps zero rows with
    // row 0 w = 2, so it never contains a pixel.
    const unsigned rotation = in.jitter_index & 7u;
    constexpr unsigned cascade_base = 13, constants = cascade_base + 5 * shadow_cascade_max;
    float block[constants][4] = {
        {in.m00, in.m11, in.m20, in.m21},
        {in.m22, in.m32, in.exponent, in.planar_step},
        {0.f, 0.f, 0.f, 0.f},
        {shadow_cascade_select_margin, shadow_cascade_select_margin - shadow_cascade_blend_band, 1.f / shadow_cascade_blend_band, linear_depth ? 1.f : 0.f},
    };
    for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) {
        float* tap = block[4 + (j + 1) * 3 + (i + 1)];
        tap[0] = kernel_cos[rotation] * float(i) - kernel_sin[rotation] * float(j); tap[1] = kernel_sin[rotation] * float(i) + kernel_cos[rotation] * float(j);
    }
    unsigned bound = 0;
    for (unsigned c = 0; c < shadow_cascade_max; ++c) {
        float (*k)[4] = &block[cascade_base + 5 * c];
        if (c >= in.count) { k[0][3] = 2.f; continue; }
        const auto& cascade = in.cascades[c];
        for (float v : cascade.rows) if (!std::isfinite(v)) return skip("params");
        // The rows stay even for an absent cascade: it still owns its pixels
        // (they are lit), so a nearer cascade's shadows do not reappear from a
        // coarser map.
        for (unsigned i = 0; i < 12; ++i) k[i / 4][i % 4] = cascade.rows[i];
        k[4][1] = c + 1 == in.count ? 1.f : 0.f;
        if (!cascade.valid || !cascade.map) continue;
        if (!std::isfinite(cascade.bias_constant) || !std::isfinite(cascade.bias_max) || cascade.bias_constant < 0.f || cascade.bias_max < 0.f) return skip("params");
        if (FAILED(cascade.map->GetLevelDesc(0, &desc)) || desc.Width != desc.Height || desc.Width < 64 || desc.Format != D3DFMT_R32F ||
            desc.MultiSampleType != D3DMULTISAMPLE_NONE)
            return skip("format");
        if (FAILED(same_device(device_, cascade.map))) return skip("device");
        k[3][0] = float(desc.Width); k[3][1] = 1.f / float(desc.Width); k[3][2] = cascade.bias_constant; k[3][3] = cascade.bias_max;
        k[4][0] = 1.f;
        ++bound; if (!out->map_size) out->map_size = desc.Width;
    }
    if (!bound) return skip("absent");
    auto fail = [&](SunShadowApplyStage stage, HRESULT hr) { out->failed = stage; out->operation = hr; return hr; };
    HRESULT hr = ensure_block();
    if (FAILED(hr)) return fail(SunShadowApplyStage::Block, hr);
    const UINT samplers = 1 + shadow_cascade_max;
    SavedState saved(*this, block_, render_targets_, samplers);
    hr = saved.capture();
    if (FAILED(hr)) return fail(SunShadowApplyStage::Capture, hr);
    D d = device_;
    SunShadowApplyStage stage = SunShadowApplyStage::Normalize;
    bool own_scene = false;
    auto step = [&](SunShadowApplyStage s, HRESULT value) { stage = s; hr = value; return SUCCEEDED(hr); };
    hr = normalize(in.target, in.width, in.height, cascade_apply_, samplers);
    if (SUCCEEDED(hr) && !in.caller_scene_open) own_scene = step(SunShadowApplyStage::Scene, call<SceneFn>(BeginScene)(d));
    if (SUCCEEDED(hr)) step(SunShadowApplyStage::Constants, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, &block[0][0], constants));
    if (SUCCEEDED(hr)) step(SunShadowApplyStage::Apply, call<SetTextureFn>(SetTexture)(d, 0, in.depth_share));
    for (unsigned c = 0; c < in.count && SUCCEEDED(hr); ++c)
        if (in.cascades[c].valid && in.cascades[c].map) step(SunShadowApplyStage::Apply, call<SetTextureFn>(SetTexture)(d, 1 + c, in.cascades[c].map));
    bool applied = false;
    if (SUCCEEDED(hr)) {
        QuadVertex vertices[4];
        quad_vertices(in.width, in.height, vertices);
        applied = step(SunShadowApplyStage::Apply, call<DrawUpFn>(DrawPrimitiveUP)(d, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(QuadVertex)));
    }
    if (own_scene && !lost(hr)) { const HRESULT end = call<SceneFn>(EndScene)(d); if (SUCCEEDED(hr) || lost(end)) { if (FAILED(end)) stage = SunShadowApplyStage::EndScene; hr = end; } }
    out->operation = hr;
    out->restore = lost(hr) ? hr : saved.restore();
    if (FAILED(hr) || FAILED(out->restore)) {
        out->failed = FAILED(hr) ? stage : SunShadowApplyStage::Restore;
        return FAILED(out->restore) ? out->restore : hr;
    }
    out->applied = applied; out->cascades_bound = bound; out->linear_depth = linear_depth;
    return S_OK;
}
} // namespace x3m::renderer
