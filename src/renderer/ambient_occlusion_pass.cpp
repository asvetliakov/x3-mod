#include "ambient_occlusion_pass.h"
#include "quad_vertex_program.h"
#include <cmath>
#include <iterator>

namespace x3m::renderer {
namespace {
template<class T> void drop(T*& value) noexcept { if (value) { value->Release(); value = nullptr; } }
bool lost(HRESULT hr) noexcept { return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET; }
// IDirect3DDevice9 vtable slots (verification/probe/abi_check.cpp), as in TemporalPass.
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, CreateTexture = 23, SetRenderTarget = 37, GetRenderTarget = 38,
    SetDepthStencilSurface = 39, GetDepthStencilSurface = 40, BeginScene = 41, EndScene = 42,
    SetViewport = 47, GetViewport = 48, SetRenderState = 57, CreateStateBlock = 59, SetTexture = 65,
    SetTextureStageState = 67, SetSamplerState = 69, SetScissorRect = 75, GetScissorRect = 76, DrawPrimitiveUP = 83,
    CreateVertexDeclaration = 86, SetVertexDeclaration = 87, CreateVertexShader = 91, SetVertexShader = 92,
    SetStreamSourceFreq = 102, SetIndices = 104, CreatePixelShader = 106, SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using CreateTextureFn = HRESULT(WINAPI*)(D, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
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
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using SetFreqFn = HRESULT(WINAPI*)(D, UINT, UINT);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetPsConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored programs are embedded (tools/shaders/generate_rigid_motion_pixel.py).
constexpr DWORD linearize_words[] = {
#include "ambient_occlusion_linearize_program_inc.h"
};
constexpr DWORD gtao_words[] = {
#include "ambient_occlusion_gtao_program_inc.h"
};
constexpr DWORD blur_words[] = {
#include "ambient_occlusion_blur_program_inc.h"
};
constexpr DWORD apply_words[] = {
#include "ambient_occlusion_apply_program_inc.h"
};
template<class Resource> HRESULT same_device(IDirect3DDevice9* device, Resource* resource) noexcept {
    IDirect3DDevice9* owner = nullptr;
    HRESULT hr = resource->GetDevice(&owner);
    if (FAILED(hr)) return hr;
    const bool same = owner == device;
    drop(owner);
    return same ? S_OK : E_INVALIDARG;
}
bool finite_params(const AmbientOcclusionParams& p) noexcept {
    for (float v : {p.m00, p.m11, p.m20, p.m21, p.m22, p.m32, p.radius_metres, p.units_per_metre, p.strength, p.falloff, p.max_radius_px, p.depth_tolerance})
        if (!std::isfinite(v)) return false;
    return p.m00 > 0.f && p.m11 > 0.f && p.m22 > 1.f && p.m32 < 0.f && p.radius_metres > 0.f && p.units_per_metre > 0.f && p.strength >= 0.f && p.strength <= 1.f &&
           p.falloff > 0.f && p.falloff < 1.f && p.max_radius_px >= 2.f && p.depth_tolerance > 0.f;
}
} // namespace
// Everything a chain touches beyond the state block: the target and depth
// bindings, viewport and scissor (SetRenderTarget resets the latter two).
struct AmbientOcclusionPass::SavedState {
    const AmbientOcclusionPass& pass;
    IDirect3DStateBlock9* block;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    UINT count;
    SavedState(const AmbientOcclusionPass& p, IDirect3DStateBlock9* b, UINT n) : pass(p), block(b), count(n) {}
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
        for (UINT i = 0; i < 16; ++i) if (!attempt(pass.call<SetTextureFn>(SetTexture)(d, i, nullptr))) return first;
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
AmbientOcclusionPass::~AmbientOcclusionPass() { detach(); }
unsigned AmbientOcclusionPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(block_), static_cast<const void*>(linearize_), static_cast<const void*>(gtao_),
                          static_cast<const void*>(blur_), static_cast<const void*>(apply_), static_cast<const void*>(quad_vs_),
                          static_cast<const void*>(quad_declaration_), static_cast<const void*>(half_depth_),
                          static_cast<const void*>(half_depth_surface_), static_cast<const void*>(ao_[0]), static_cast<const void*>(ao_[1]),
                          static_cast<const void*>(ao_surfaces_[0]), static_cast<const void*>(ao_surfaces_[1])})
        n += p != nullptr;
    return n;
}
void AmbientOcclusionPass::release_targets() noexcept {
    drop(half_depth_surface_); drop(half_depth_);
    for (auto& s : ao_surfaces_) drop(s);
    for (auto& t : ao_) drop(t);
    width_ = height_ = half_width_ = half_height_ = 0;
}
void AmbientOcclusionPass::detach() noexcept {
    release_targets(); drop(block_);
    drop(linearize_); drop(gtao_); drop(blur_); drop(apply_); drop(quad_vs_); drop(quad_declaration_);
    device_ = nullptr; vtable_ = nullptr; render_targets_ = streams_ = 0; reset_pending_ = false;
    caps_ = {}; target_format_ = D3DFMT_UNKNOWN;
}
void AmbientOcclusionPass::before_reset() noexcept { release_targets(); drop(block_); reset_pending_ = device_ != nullptr; }
void AmbientOcclusionPass::after_reset(HRESULT result) noexcept { if (SUCCEEDED(result)) reset_pending_ = false; }
HRESULT AmbientOcclusionPass::attach(IDirect3DDevice9* d, void* const* native, const D3DCAPS9& caps, D3DFORMAT adapter_format,
                                     D3DFORMAT target_format) noexcept {
    detach();
    if (!d) { caps_.reason = "device"; return E_INVALIDARG; }
    device_ = d; vtable_ = native;
    AmbientOcclusionCapabilityInputs in{};
    in.pixel_shader_version = caps.PixelShaderVersion; in.vertex_shader_version = caps.VertexShaderVersion;
    in.ps30_instruction_slots = caps.MaxPixelShader30InstructionSlots;
    in.simultaneous_targets = caps.NumSimultaneousRTs; in.max_streams = caps.MaxStreams;
    for (auto& program : {std::pair{linearize_words, std::size(linearize_words)}, std::pair{gtao_words, std::size(gtao_words)},
                          std::pair{blur_words, std::size(blur_words)}, std::pair{apply_words, std::size(apply_words)}}) {
        const std::uint32_t slots = ambient_occlusion_program_slots(reinterpret_cast<const std::uint32_t*>(program.first), program.second);
        if (slots == 0) { in.largest_program_slots = 0; break; }
        if (slots > in.largest_program_slots) in.largest_program_slots = slots;
    }
    caps_.largest_program_slots = in.largest_program_slots;
    in.src_blend_zero = (caps.SrcBlendCaps & D3DPBLENDCAPS_ZERO) != 0;
    in.dest_blend_srccolor = (caps.DestBlendCaps & D3DPBLENDCAPS_SRCCOLOR) != 0;
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr)) {
        in.r32f_target = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F);
        in.r16f_target = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, adapter_format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R16F);
        in.target_blending = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, adapter_format,
                                                    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, target_format);
    }
    drop(api);
    caps_.formats = hr;
    if (FAILED(hr)) { caps_.reason = "format_query"; device_ = nullptr; vtable_ = nullptr; return hr; }
    if (const char* reason = ambient_occlusion_capability(in)) {
        caps_.reason = reason; caps_.formats = D3DERR_NOTAVAILABLE; device_ = nullptr; vtable_ = nullptr;
        return D3DERR_NOTAVAILABLE;
    }
    render_targets_ = caps.NumSimultaneousRTs; streams_ = caps.MaxStreams; target_format_ = target_format;
    hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(quad_vertex_program()), &quad_vs_);
    if (SUCCEEDED(hr)) hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, quad_declaration, &quad_declaration_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, linearize_words, &linearize_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, gtao_words, &gtao_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, blur_words, &blur_);
    if (SUCCEEDED(hr)) hr = call<CreatePsFn>(CreatePixelShader)(d, apply_words, &apply_);
    caps_.programs = hr;
    if (FAILED(hr)) { const unsigned slots = caps_.largest_program_slots; detach(); caps_.reason = "programs"; caps_.programs = hr; caps_.largest_program_slots = slots; return hr; }
    caps_.enabled = true; caps_.reason = "";
    return S_OK;
}
HRESULT AmbientOcclusionPass::prepare(UINT w, UINT h) noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return D3DERR_DEVICENOTRESET;
    if (!w || !h) return E_INVALIDARG;
    if (width_ == w && height_ == h) return S_OK;
    release_targets();
    const UINT hw = (w + 1) / 2, hh = (h + 1) / 2;
    HRESULT hr = call<CreateTextureFn>(CreateTexture)(device_, hw, hh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &half_depth_, nullptr);
    if (SUCCEEDED(hr)) hr = half_depth_->GetSurfaceLevel(0, &half_depth_surface_);
    for (UINT i = 0; i < 2 && SUCCEEDED(hr); ++i) {
        hr = call<CreateTextureFn>(CreateTexture)(device_, hw, hh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F, D3DPOOL_DEFAULT, &ao_[i], nullptr);
        if (SUCCEEDED(hr)) hr = ao_[i]->GetSurfaceLevel(0, &ao_surfaces_[i]);
    }
    if (FAILED(hr)) { release_targets(); return hr; }
    width_ = w; height_ = h; half_width_ = hw; half_height_ = hh; ++allocations_;
    return S_OK;
}
HRESULT AmbientOcclusionPass::ensure_block() noexcept {
    if (block_) return S_OK;
    const HRESULT hr = call<CreateBlockFn>(CreateStateBlock)(device_, D3DSBT_ALL, &block_);
    if (FAILED(hr)) drop(block_);
    return hr;
}
HRESULT AmbientOcclusionPass::quad(UINT w, UINT h) noexcept {
    QuadVertex vertices[4];
    quad_vertices(w, h, vertices);
    return call<DrawUpFn>(DrawPrimitiveUP)(device_, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(QuadVertex));
}
HRESULT AmbientOcclusionPass::normalize(UINT w, UINT h) noexcept {
    D d = device_;
#define STEP(call) do { const HRESULT hresult = (call); if (FAILED(hresult)) return hresult; } while (false)
    for (UINT i = 0; i < 16; ++i) STEP(call<SetTextureFn>(SetTexture)(d, i, nullptr));
    for (UINT i = 0; i < 4; ++i) STEP(call<SetTextureFn>(SetTexture)(d, D3DVERTEXTEXTURESAMPLER0 + i, nullptr));
    STEP(call<SetDepthFn>(SetDepthStencilSurface)(d, nullptr));
    for (UINT i = 1; i < render_targets_; ++i) STEP(call<SetRtFn>(SetRenderTarget)(d, i, nullptr));
    STEP(call<SetDeclarationFn>(SetVertexDeclaration)(d, quad_declaration_));
    STEP(call<SetVsFn>(SetVertexShader)(d, quad_vs_));
    STEP(call<SetIndicesFn>(SetIndices)(d, nullptr));
    for (UINT i = 0; i < streams_; ++i) STEP(call<SetFreqFn>(SetStreamSourceFreq)(d, i, 1));
    for (auto state : {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE,
                       D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_FOGENABLE,
                       D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE,
                       D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_INDEXEDVERTEXBLENDENABLE,
                       D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE, D3DRS_ANTIALIASEDLINEENABLE})
        STEP(call<SetRsFn>(SetRenderState)(d, state, FALSE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_VERTEXBLEND, D3DVBF_DISABLE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_WRAP0, 0));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_FILLMODE, D3DFILL_SOLID));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, D3DCULL_NONE));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE, 15));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_MULTISAMPLEMASK, 0xffffffff));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, D3DBLEND_ZERO));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR));
    STEP(call<SetRsFn>(SetRenderState)(d, D3DRS_BLENDOP, D3DBLENDOP_ADD));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXCOORDINDEX, 0));
    STEP(call<SetStageFn>(SetTextureStageState)(d, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE));
    for (UINT i = 0; i < 3; ++i) {
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MINFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_SRGBTEXTURE, FALSE));
        STEP(call<SetSamplerFn>(SetSamplerState)(d, i, D3DSAMP_MAXMIPLEVEL, 0));
    }
    D3DVIEWPORT9 viewport{0, 0, w, h, 0, 1};
    STEP(call<SetViewportFn>(SetViewport)(d, &viewport));
#undef STEP
    return S_OK;
}
// One quad of the chain: unbind the samplers (the previous pass's output may
// become this target), bind the target and its full viewport, the program and
// the inputs, draw.
HRESULT AmbientOcclusionPass::bind_and_draw(IDirect3DSurface9* target, UINT w, UINT h, IDirect3DPixelShader9* program,
                                            IDirect3DBaseTexture9* s0, IDirect3DBaseTexture9* s1, IDirect3DBaseTexture9* s2) noexcept {
    D d = device_;
    HRESULT hr = S_OK;
    auto step = [&](HRESULT value) { hr = value; return SUCCEEDED(hr); };
    for (UINT i = 0; i < 3 && SUCCEEDED(hr); ++i) step(call<SetTextureFn>(SetTexture)(d, i, nullptr));
    if (SUCCEEDED(hr)) step(call<SetRtFn>(SetRenderTarget)(d, 0, target));
    D3DVIEWPORT9 viewport{0, 0, w, h, 0, 1};
    if (SUCCEEDED(hr)) step(call<SetViewportFn>(SetViewport)(d, &viewport));
    if (SUCCEEDED(hr)) step(call<SetPsFn>(SetPixelShader)(d, program));
    if (SUCCEEDED(hr)) step(call<SetTextureFn>(SetTexture)(d, 0, s0));
    if (SUCCEEDED(hr) && s1) step(call<SetTextureFn>(SetTexture)(d, 1, s1));
    if (SUCCEEDED(hr) && s2) step(call<SetTextureFn>(SetTexture)(d, 2, s2));
    if (SUCCEEDED(hr)) hr = quad(w, h);
    return hr;
}
HRESULT AmbientOcclusionPass::execute(const AmbientOcclusionFrame& in, AmbientOcclusionResult* out) noexcept {
    if (!out) return E_INVALIDARG;
    *out = {};
    auto fail = [&](AmbientOcclusionStage stage, HRESULT hr) { out->failed = stage; out->operation = hr; return hr; };
    if (!device_ || !caps_.enabled || !in.depth || !in.width || !in.height || in.caller_stateblock_recording || !in.caller_queries_idle ||
        !finite_params(in.params))
        return fail(AmbientOcclusionStage::Validate, E_INVALIDARG);
    if (reset_pending_) return fail(AmbientOcclusionStage::Validate, D3DERR_DEVICENOTRESET);
    if (in.depth == half_depth_ || in.depth == ao_[0] || in.depth == ao_[1]) return fail(AmbientOcclusionStage::Validate, E_INVALIDARG);
    D3DSURFACE_DESC desc{};
    HRESULT hr = in.depth->GetLevelDesc(0, &desc);
    if (SUCCEEDED(hr) && (desc.Width != in.width || desc.Height != in.height || desc.Format != D3DFMT_R32F || desc.MultiSampleType != D3DMULTISAMPLE_NONE))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) hr = same_device(device_, in.depth);
    if (SUCCEEDED(hr) && in.target) {
        if (in.target == half_depth_surface_ || in.target == ao_surfaces_[0] || in.target == ao_surfaces_[1]) hr = E_INVALIDARG;
        else hr = in.target->GetDesc(&desc);
        if (SUCCEEDED(hr) && (desc.Width != in.width || desc.Height != in.height || desc.Format != target_format_ ||
                              desc.MultiSampleType != D3DMULTISAMPLE_NONE || !(desc.Usage & D3DUSAGE_RENDERTARGET)))
            hr = E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = same_device(device_, in.target);
    }
    if (FAILED(hr)) return fail(AmbientOcclusionStage::Validate, hr);
    hr = prepare(in.width, in.height);
    if (FAILED(hr)) return fail(AmbientOcclusionStage::Targets, hr);
    hr = ensure_block();
    if (FAILED(hr)) return fail(AmbientOcclusionStage::Block, hr);
    SavedState saved(*this, block_, render_targets_);
    hr = saved.capture();
    if (FAILED(hr)) return fail(AmbientOcclusionStage::Capture, hr);
    const UINT w = in.width, h = in.height, hw = half_width_, hh = half_height_;
    const AmbientOcclusionParams& p = in.params;
    D d = device_;
    AmbientOcclusionStage stage = AmbientOcclusionStage::Normalize;
    hr = normalize(hw, hh);
    bool own_scene = false;
    // Every chain below is guarded by SUCCEEDED(hr) so no device call follows a failure.
    auto step = [&](AmbientOcclusionStage s, HRESULT value) { stage = s; hr = value; return SUCCEEDED(hr); };
    if (SUCCEEDED(hr) && !in.caller_scene_open) own_scene = step(AmbientOcclusionStage::Scene, call<SceneFn>(BeginScene)(d));
    const float half_size[4] = {1.f / float(hw), 1.f / float(hh), float(hw), float(hh)};
    // Linearize: c0 = (m22, m32), c1 = (w, h, hw, hh).
    const float projection[4] = {p.m22, p.m32, 0.f, 0.f};
    const float sizes[4] = {float(w), float(h), float(hw), float(hh)};
    if (SUCCEEDED(hr) && step(AmbientOcclusionStage::Linearize, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, projection, 1)) &&
        step(AmbientOcclusionStage::Linearize, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 1, sizes, 1)))
        step(AmbientOcclusionStage::Linearize, bind_and_draw(half_depth_surface_, hw, hh, linearize_, in.depth, nullptr, nullptr));
    // GTAO: c0 = half size, c1 = (m00, m11, m20, m21), c2 = radius terms, c3 = (rotation, px per view unit at z = 1).
    const float terms[4] = {p.m00, p.m11, p.m20, p.m21};
    const float radius_units = p.radius_metres * p.units_per_metre;
    const float falloff_range = p.falloff * radius_units, falloff_from = radius_units - falloff_range;
    const float radius[4] = {radius_units, -1.f / falloff_range, falloff_from / falloff_range + 1.f, p.max_radius_px};
    const float noise[4] = {float(p.jitter_index % 16u), p.m11 * float(hh) * .5f, 0.f, 0.f};
    if (SUCCEEDED(hr) && step(AmbientOcclusionStage::Gtao, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 0, half_size, 1)) &&
        step(AmbientOcclusionStage::Gtao, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 1, terms, 1)) &&
        step(AmbientOcclusionStage::Gtao, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 2, radius, 1)) &&
        step(AmbientOcclusionStage::Gtao, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 3, noise, 1)))
        step(AmbientOcclusionStage::Gtao, bind_and_draw(ao_surfaces_[0], hw, hh, gtao_, half_depth_, nullptr, nullptr));
    // Two blurs: c1 = (dx, dy, tau). ao0 -> ao1 -> ao0.
    const float horizontal[4] = {1.f, 0.f, p.depth_tolerance, 0.f}, vertical[4] = {0.f, 1.f, p.depth_tolerance, 0.f};
    bool blur = true;
#ifdef X3M_AMBIENT_OCCLUSION_FIXTURE
    blur = !fixture_skip_blur_;
#endif
    if (blur && SUCCEEDED(hr) && step(AmbientOcclusionStage::BlurH, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 1, horizontal, 1)))
        step(AmbientOcclusionStage::BlurH, bind_and_draw(ao_surfaces_[1], hw, hh, blur_, ao_[0], half_depth_, nullptr));
    if (blur && SUCCEEDED(hr) && step(AmbientOcclusionStage::BlurV, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 1, vertical, 1)))
        step(AmbientOcclusionStage::BlurV, bind_and_draw(ao_surfaces_[0], hw, hh, blur_, ao_[1], half_depth_, nullptr));
    // Apply: the multiply into the owning target under ZERO/SRCCOLOR (the
    // factors were set by normalize; only the enable toggles here).
    bool applied = false;
    if (SUCCEEDED(hr) && in.target) {
        const float full_size[4] = {float(w), float(h), 1.f / float(w), 1.f / float(h)};
        const float apply_terms[4] = {p.m22, p.m32, p.depth_tolerance, p.strength};
        if (step(AmbientOcclusionStage::Apply, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 1, full_size, 1)) &&
            step(AmbientOcclusionStage::Apply, call<SetPsConstantsFn>(SetPixelShaderConstantF)(d, 2, apply_terms, 1)) &&
            step(AmbientOcclusionStage::Apply, call<SetRsFn>(SetRenderState)(d, D3DRS_ALPHABLENDENABLE, TRUE)))
            applied = step(AmbientOcclusionStage::Apply, bind_and_draw(in.target, w, h, apply_, ao_[0], half_depth_, in.depth));
    }
    if (own_scene && !lost(hr)) { const HRESULT end = call<SceneFn>(EndScene)(d); if (SUCCEEDED(hr) || lost(end)) { if (FAILED(end)) stage = AmbientOcclusionStage::EndScene; hr = end; } }
    out->operation = hr;
    out->restore = lost(hr) ? hr : saved.restore();
    if (FAILED(hr) || FAILED(out->restore)) {
        out->failed = FAILED(hr) ? stage : AmbientOcclusionStage::Restore;
        return FAILED(out->restore) ? out->restore : hr;
    }
    out->applied = applied; out->term = ao_[0]; out->half_width = hw; out->half_height = hh;
    return S_OK;
}
} // namespace x3m::renderer
